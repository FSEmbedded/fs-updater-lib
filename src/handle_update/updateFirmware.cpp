#include "updateFirmware.h"

updater::firmwareUpdate::firmwareUpdate(const std::shared_ptr<UBoot::UBoot> & ptr):
    updateBase(ptr),
    system_installer(ptr)
{

}

updater::firmwareUpdate::~firmwareUpdate()
{

}

void updater::firmwareUpdate::install(const std::filesystem::path & path_to_bundle)
{
    try
    {
        this->system_installer.installBundle(path_to_bundle);
    }
    catch(rauc::RaucBaseException & err)
    {
        throw(ErrorFirmwareUpdateInstall(std::string(err.what())));
    }
}

void updater::firmwareUpdate::rollback()
{
    try
    {
        this->system_installer.rollback();
    }
    catch(const rauc::RaucBaseException & err)
    {
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
            throw(ErrorGetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE,"End-of-File reached on input operation"));
        }
        else if (firmware_version.fail())
        {
            throw(ErrorGetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE,"Logical error on i/o operation"));
        }
        else if (firmware_version.bad())
        {
            throw(ErrorGetFirmwareVersion(PATH_TO_FIRMWARE_VERSION_FILE,"Read/writing error on i/o operation"));
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
        this->system_installer.markUpdateAsSuccessfull();
    }
    catch(const std::exception& e)
    {
        throw(ErrorRaucMarkUpdateSuccessfull(e.what()));
    }
}
