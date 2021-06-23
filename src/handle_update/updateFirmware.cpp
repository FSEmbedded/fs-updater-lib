#include "updateFirmware.h"

updater::firmwareUpdate::firmwareUpdate(const std::shared_ptr<UBoot::UBoot> &ptr, const std::shared_ptr<logger::LoggerHandler> &logger):
    updateBase(ptr),
    system_installer(ptr),
    logger(logger)
{

    this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, "firmware updater constructed", logger::logLevel::DEBUG));

}

updater::firmwareUpdate::~firmwareUpdate()
{

    this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, "firmware updater deconstructed", logger::logLevel::DEBUG));

}

void updater::firmwareUpdate::install(const std::filesystem::path & path_to_bundle)
{
    try
    {
        this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("install: firmware update: ") + path_to_bundle.string(), logger::logLevel::DEBUG));
        this->system_installer.installBundle(path_to_bundle);
    }
    catch(rauc::RaucBaseException & err)
    {
        this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("install: firmware update: ") + std::string(err.what()), logger::logLevel::ERROR));
        throw(ErrorFirmwareUpdateInstall(std::string(err.what())));
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
        throw(ErrorFirmwareRollback(std::string(err.what())));
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
            throw(ErrorGetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE, error_msg));
        }
        else if (firmware_version.fail())
        {
            const std::string error_msg = "Logical error on i/o operation";
            this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
            throw(ErrorGetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE, error_msg));
        }
        else if (firmware_version.bad())
        {
            const std::string error_msg = "Read/writing error on i/o operation";
            this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
            throw(ErrorGetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE, error_msg));
        }
    }

    if (std::regex_match(fw_version, file_content_regex))
    {
        current_fw_version = std::stoul(fw_version);
    }
    else
    {
        std::string error_msg("Content miss formatting rules: ");
        error_msg += fw_version;
        
        this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
        throw(ErrorGetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE, error_msg));
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
        throw(ErrorWrongVariableContent(booted_slot));
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

    throw(ErrorRaucDetection());
}

void updater::firmwareUpdate::markSuccessfull()
{   
    try
    {
        this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, "markSuccessfull", logger::logLevel::DEBUG));
        this->system_installer.markUpdateAsSuccessfull();
    }
    catch(const std::exception& e)
    {
        this->logger->setLogEntry(logger::LogEntry(FIRMWARE_UPDATE, std::string("markSuccessfull: ") + std::string(e.what()), logger::logLevel::ERROR));
        throw(ErrorRaucMarkUpdateSuccessfull(e.what()));
    }
}
