#include <fus_updater_lib/config.h>
#include "updateFirmware.h"
#include "utils.h"
#include "../subprocess/subprocess.h"
#include <algorithm>
#include <iostream>

static bool is_8digit_version(const std::string &s)
{
    return s.size() == 8 && std::all_of(s.begin(), s.end(),
        [](unsigned char c){ return c >= '0' && c <= '9'; });
}

updater::firmwareUpdate::firmwareUpdate(const std::shared_ptr<UBoot::UBoot> &ptr, const std::shared_ptr<logger::LoggerHandler> &logger):
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
 * The command `rauc install <bundle>` automatically updates several
 * U-Boot environment variables to control the A/B boot mechanism.
 *
 * In particular, RAUC modifies:
 *
 *   - BOOT_ORDER      : Defines the priority/order of boot slots (e.g. "A B")
 *   - BOOT_A_LEFT     : Remaining boot attempts for slot A
 *   - BOOT_B_LEFT     : Remaining boot attempts for slot B
 *
 * Additionally, depending on the system configuration, RAUC may also update:
 *
 *   - BOOT_ORDER_OLD  : Backup of the previous boot order
 *
 */
void updater::firmwareUpdate::install(const std::string & path_to_bundle)
{
    try
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, std::string("install: firmware update: ") + path_to_bundle, logger::logLevel::DEBUG));
        this->system_installer.installBundle(path_to_bundle);
    }
    catch(rauc::RaucBaseException & err)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, std::string("install: firmware update: ") + std::string(err.what()), logger::logLevel::ERROR));
        throw(FirmwareUpdateInstall(std::string(err.what())));
    }

    /* lets call sync to be sure data write back */
    std::string command = std::string("sync");
    subprocess::Popen handler = subprocess::Popen(command);

    if (handler.successful() == false)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, std::string("Command sync: execution fails: ") + handler.output(), logger::logLevel::ERROR));
        throw(FirmwareUpdateInstall(std::string("Sync of firmware update fails.")));
    }
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
#if UPDATE_VERSION_TYPE_UINT64 == 1
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
    const Json::Value ret_value = this->system_installer.getStatus();
    const std::string booted_slot = ret_value["booted"].asString();
    std::string updated_slot;

    if (booted_slot == "A")
    {
        updated_slot = "B";
    }
    else if (booted_slot == "B")
    {
        updated_slot = "A";
    }
    else
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FIRMWARE_UPDATE, "failedUpdateReboot: booted slot is not A/B", logger::logLevel::ERROR));
        throw(WrongVariableContent(booted_slot));
    }

    for (auto & slot: ret_value["slots"])
    {
        for (auto & entry: slot)
        {
            if (entry["bootname"] == updated_slot)
            {
                if (entry["boot_status"] == "bad")
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
        }
    }

    throw(RaucDetection());
}
