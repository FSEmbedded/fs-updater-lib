#include "updateFirmware.h"
#include "../subprocess/subprocess.h"
#include <iostream>

updater::firmwareUpdate::firmwareUpdate(const std::shared_ptr<UBoot::UBoot> &ptr, const std::shared_ptr<logger::LoggerHandler> &logger):
    updateBase(ptr, logger),
    system_installer(ptr, logger)
{

    this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, "firmwareUpdate: constructor", logger::logLevel::DEBUG));

}

updater::firmwareUpdate::~firmwareUpdate()
{

    this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, "firmwareUpdate: deconstructor", logger::logLevel::DEBUG));

}

void updater::firmwareUpdate::install(const std::string & path_to_bundle)
{
    try
    {
        this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("install: firmware update: ") + path_to_bundle, logger::logLevel::DEBUG));
        this->system_installer.installBundle(path_to_bundle);
    }
    catch(rauc::RaucBaseException & err)
    {
        this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("install: firmware update: ") + std::string(err.what()), logger::logLevel::ERROR));
        throw(FirmwareUpdateInstall(std::string(err.what())));
    }

    /* lets call sync to be sure data write back */
    std::string command = std::string("sync");
    subprocess::Popen handler = subprocess::Popen(command);

    if (handler.successful() == false)
    {
        this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("Command sync: execution fails: ") + handler.output(), logger::logLevel::ERROR));
        throw(FirmwareUpdateInstall(std::string("Sync of firmware update fails.")));
     }
}

void updater::firmwareUpdate::rollback()
{
    try
    {
        this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, "rollback: rollback", logger::logLevel::DEBUG));
        this->system_installer.rollback();
    }
    catch(const rauc::RaucBaseException & err)
    {
        this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("rollback: ") + std::string(err.what()), logger::logLevel::ERROR));
        throw(FirmwareRollback(std::string(err.what())));
    }
    
}

unsigned int updater::firmwareUpdate::getCurrentVersion()
{
    unsigned int current_fw_version;
    std::regex file_content_regex("[0-9]{8}");
    std::string fw_version;

    std::ifstream firmware_version(PATH_TO_FIRMWARE_VERSION_FILE);
    if (firmware_version.good())
    {
        std::getline(firmware_version, fw_version);
    }
    else
    {
        if(firmware_version.eof())
        {
            const std::string error_msg = "End-of-File reached on input operation";
            this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
            throw(GetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE, error_msg));
        }
        else if (firmware_version.fail())
        {
            const std::string error_msg = "Logical error on I/O operation";
            this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
            throw(GetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE, error_msg));
        }
        else if (firmware_version.bad())
        {
            const std::string error_msg = "Read/writing error on I/O operation";
            this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
            throw(GetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE, error_msg));
        }
    }

    if (std::regex_search(fw_version, file_content_regex))
    {
        current_fw_version = std::stoul(fw_version);
    }
    else
    {
        std::string error_msg("Content miss formatting rules: ");
        error_msg += fw_version;
        
        this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
        throw(GetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE, error_msg));
    }

    return current_fw_version;
}

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
        this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, "failedUpdateReboot: booted slot is not A/B", logger::logLevel::ERROR));
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
