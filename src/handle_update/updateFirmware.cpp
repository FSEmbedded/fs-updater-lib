#include <fus_updater_lib/config.h>
#include "updateFirmware.h"
#include "utils.h"
#include "../uboot_interface/allowed_uboot_variable_states.h"
#include <algorithm>
#include <fstream>

extern "C" {
#include <unistd.h>
}

updater::firmwareUpdate::firmwareUpdate(const std::shared_ptr<UBoot::IUBootEnv> &ptr, const std::shared_ptr<logger::LoggerHandler> &logger):
    updateBase(ptr, logger),
    system_installer(ptr, logger)
{

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, "firmwareUpdate: constructor", logger::logLevel::DEBUG));

}

updater::firmwareUpdate::~firmwareUpdate()
{

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, "firmwareUpdate: deconstructor", logger::logLevel::DEBUG));

}

/*
 * RAUC installation behavior:
 *
 * The install goes over D-Bus (InstallBundle), not over the `rauc install`
 * command line. Either way RAUC writes the U-Boot environment variables that
 * drive the A/B boot mechanism:
 *
 *   - BOOT_ORDER      : Defines the priority/order of boot slots (e.g. "A B")
 *   - BOOT_A_LEFT     : Remaining boot attempts for slot A
 *   - BOOT_B_LEFT     : Remaining boot attempts for slot B
 *
 * BOOT_ORDER_OLD is NOT one of them -- it is this framework's own backup of
 * the previous order, written by the update handling, and it is what the
 * revert after a failed install compares against.
 */
void updater::firmwareUpdate::install(const std::string & path_to_bundle)
{
    try
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, std::string("install: firmware update: ") + path_to_bundle, logger::logLevel::DEBUG));
        this->system_installer.installBundle(path_to_bundle);
        this->system_installer.waitForCompletion(0, progress_cb_);
    }
    catch(rauc::RaucBaseException & err)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, std::string("install: firmware update: ") + std::string(err.what()), logger::logLevel::ERROR));
        throw(FirmwareUpdateInstall(std::string(err.what())));
    }

    /* flush filesystem buffers so the freshly written slot reaches disk */
    ::sync();
}

void updater::firmwareUpdate::rollback()
{
    try
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, "rollback: rollback", logger::logLevel::DEBUG));
        this->system_installer.rollback();
    }
    catch(const rauc::RaucBaseException & err)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, std::string("rollback: ") + std::string(err.what()), logger::logLevel::ERROR));
        throw(FirmwareRollback(std::string(err.what())));
    }

}

void updater::firmwareUpdate::markOtherPartition()
{
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        FIRMWARE_UPDATE, "markOtherPartition: Mark(good,other)",
        logger::logLevel::DEBUG));
    this->system_installer.markOtherPartition();
}
#if UPDATE_VERSION_TYPE_UINT64 == 1

static bool is_8digit_version(const std::string &s)
{
    return s.size() == 8 && std::all_of(s.begin(), s.end(),
        [](unsigned char c){ return c >= '0' && c <= '9'; });
}

version_t updater::firmwareUpdate::getCurrentVersion()
{
    version_t current_fw_version;
    std::string fw_version;

    std::ifstream firmware_version(PATH_TO_FIRMWARE_VERSION_FILE);
    if (firmware_version.good())
    {
        std::getline(firmware_version, fw_version);
    }
    else
    {
        const std::string error_msg = util::describe_stream_error(firmware_version);
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
        throw(GetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE, error_msg));
    }

    if (is_8digit_version(fw_version))
    {
        current_fw_version = std::stoul(fw_version);
    }
    else
    {
        std::string error_msg("Content miss formatting rules: ");
        error_msg += fw_version;
        
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
        throw(GetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE, error_msg));
    }

    return current_fw_version;
}
#elif UPDATE_VERSION_TYPE_STRING == 1
version_t updater::firmwareUpdate::getCurrentVersion()
{
    std::string fw_version;

    std::ifstream firmware_version(PATH_TO_FIRMWARE_VERSION_FILE);
    if (firmware_version.good())
    {
        std::getline(firmware_version, fw_version);
    }
    else
    {
        const std::string error_msg = util::describe_stream_error(firmware_version);
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
        throw(GetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE, error_msg));
    }

    return fw_version;
}
#else
#error "No valid version type defined"
#endif

bool updater::firmwareUpdate::failedUpdateReboot()
{
    const std::string rauc_cmd    = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
    const std::string booted_slot = util::split(rauc_cmd, '=').back();

    if (booted_slot != "A" && booted_slot != "B")
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, "failedUpdateReboot: booted slot is not A/B", logger::logLevel::ERROR));
        throw(WrongVariableContent(booted_slot));
    }

    const std::string updated_slot = (booted_slot == "A") ? "B" : "A";

    const rauc::SlotStatusList slots = this->system_installer.getSlotStatus();
    for (const auto& [name, props] : slots)
    {
        const auto cls_it = props.find("class");
        if (cls_it == props.end() || cls_it->second != "boot") {
            continue;
}
        const auto bootname_it = props.find("bootname");
        if (bootname_it == props.end() || bootname_it->second != updated_slot) {
            continue;
}
        const auto status_it = props.find("boot-status");
        if (status_it == props.end()) {
            break;
}
        return status_it->second == "bad";
    }

    throw(RaucDetection());
}
