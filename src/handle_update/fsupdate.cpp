#include "fsupdate.h"

update::FSUpdate::FSUpdate()
{
    this->uboot_handler = std::make_shared<UBoot::UBoot>(PATH_TO_FIRMWARE_VERSION_FILE);
}

update::FSUpdate::~FSUpdate()
{

}

bool update::FSUpdate::update_firmware(const std::filesystem::path & path_to_firmware)
{
    bool retValue = false;
    updater::firmwareUpdate update_fw(this->uboot_handler);

    try
    {
        update_fw.install(path_to_firmware);
        this->uboot_handler->addVariable("update_reboot_state", 
            update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
        );
        retValue = true;
    }
    catch(const std::exception& e)
    {
        this->uboot_handler->addVariable("update_reboot_state", 
            update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
        );
        throw;
    }

    this->uboot_handler->addVariable("update", "0010");
    this->uboot_handler->flushEnvironment();

    return retValue;
}

bool update::FSUpdate::update_application(const std::filesystem::path & path_to_application)
{
    bool retValue = false;
    updater::applicationUpdate update_app(this->uboot_handler);

    try
    {
        update_app.install(path_to_application);
        this->uboot_handler->addVariable("update_reboot_state", 
            update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE)
        );
        retValue = true;
    }
    catch(const std::exception& e)
    {
        this->uboot_handler->addVariable("update_reboot_state", 
            update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
        );
        throw;
    }

    this->uboot_handler->addVariable("update", "0001");
    this->uboot_handler->flushEnvironment();

    return retValue;
}

bool update::FSUpdate::update_firmware_and_application(const std::filesystem::path & path_to_firmware, 
                                                             const std::filesystem::path & path_to_application)
{
    bool retValue = false;
    updater::applicationUpdate update_app(this->uboot_handler);
    updater::firmwareUpdate update_fw(this->uboot_handler);
    
    std::vector<char> update = {'0', '0', '0', '0'};
    try
    {
        update[3] = '1';
        update_app.install(path_to_application);

        try
        {
            update[2] = '1';
            update_fw.install(path_to_firmware);
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE)
            );
            retValue = true;
        }
        catch(const std::exception& e)
        {
            this->uboot_handler->addVariable("update_reboot:state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
            );
            throw;
        }
        
    }
    catch(const std::exception& e)
    {
        this->uboot_handler->addVariable("update_reboot_state",
            update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE)
        );
        throw;
    }

    const std::string update_value(update.begin(), update.end());
    this->uboot_handler->addVariable("update", update_value);
    this->uboot_handler->flushEnvironment();

    return retValue;
}

bool update::FSUpdate::commit_update()
{
    updater::Bootstate updateHandler(this->uboot_handler);
    return updateHandler.checkPendingUpdate();
}

bool update::FSUpdate::automatic_update_application(const std::filesystem::path & path_to_application, 
                                        const unsigned int & dest_version)
{
    updater::applicationUpdate update_app(this->uboot_handler);
    bool retValue = false;

    const unsigned int application_version = update_app.getCurrentVersion();
    const char application_prev_update = util::to_array(this->uboot_handler->getVariable("update")).at(3);
    const update_definitions::UBootBootstateFlags current_update_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state"));

    const bool update_after_successful_update = (application_prev_update == '0') && (current_update_state == update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING);
    const bool update_after_failed_update = (application_prev_update == '1') && ( 
        (current_update_state == update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE) ||
        (current_update_state == update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE) ||
        (current_update_state == update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED) );
    
    if (update_after_successful_update || update_after_failed_update)
    {
        if (dest_version > application_version)
        {
            retValue = update_application(path_to_application);
        }
        else
        {
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING)
            );
        }
    }
    else
    {
        updater::Bootstate handleUpdate(this->uboot_handler);
        if (handleUpdate.checkPendingUpdate())
        {
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING)
            );    
        }
        else
        {
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE)
            );
        }
    }

    this->uboot_handler->flushEnvironment();
    return retValue;
}

bool update::FSUpdate::automatic_update_firmware(const std::filesystem::path & path_to_firmware,
                                     const unsigned int & dest_version)
{
    updater::firmwareUpdate update_fw(this->uboot_handler);
    bool retValue = false;

    const unsigned int firmware_version = update_fw.getCurrentVersion();
    const unsigned char firmware_prev_update = util::to_array(this->uboot_handler->getVariable("update")).at(2);
    const update_definitions::UBootBootstateFlags current_update_state = update_definitions::to_UBootBootstateFlags(
                                                                            this->uboot_handler->getVariable("update_reboot_state")
                                                                         );
    const bool update_after_successful_update = (firmware_prev_update == '0') && (current_update_state == update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING);
    const bool update_after_failed_update = (firmware_prev_update == '1') &&
            ( (current_update_state == update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE) ||
              (current_update_state == update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE) ||
              (current_update_state == update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED)
            );
    
    if (update_after_failed_update || update_after_successful_update)
    {
        if (dest_version > firmware_version)
        {
            if (update_firmware(path_to_firmware))
            {
                this->uboot_handler->addVariable("update_reboot_state",
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
                );
                retValue = true;
            }
            else
            {
                this->uboot_handler->addVariable("update_reboot_state",
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
                );
            }
        }
        else
        {
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING)
            );        
        }
    }
    else
    {
        updater::Bootstate handleUpdate(this->uboot_handler);
        handleUpdate.checkPendingUpdate();
    }
    
    this->uboot_handler->flushEnvironment();
    return retValue;
}

bool update::FSUpdate::automatic_update_firmware_and_application(const std::filesystem::path & path_to_firmware,
                                                        const std::filesystem::path & path_to_application,
                                                        const unsigned int & dest_ver_application, 
                                                        const unsigned int & dest_ver_firmware)
{
    updater::firmwareUpdate update_fw(this->uboot_handler);
    updater::applicationUpdate update_app(this->uboot_handler);

    bool retValue = false;

    const unsigned int fw_version = update_fw.getCurrentVersion();
    const unsigned int app_version = update_app.getCurrentVersion();

    if ((dest_ver_firmware > fw_version) && (dest_ver_application > app_version))
    {
        const unsigned char firmware_prev_update = util::to_array(this->uboot_handler->getVariable("update")).at(2);
        const unsigned char application_prev_update = util::to_array(this->uboot_handler->getVariable("update")).at(3);
        const update_definitions::UBootBootstateFlags current_update_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state"));

        const bool update_after_successful_update = ( ((firmware_prev_update == 0) && (application_prev_update == 0)) &&
            (current_update_state == update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING)
        );

        const bool update_after_failed_update = (((firmware_prev_update == 1) || (application_prev_update == 1)) &&
            ((current_update_state == update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE) ||
            (current_update_state == update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE) ||
            (current_update_state == update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED))
        );

        if (update_after_successful_update || update_after_failed_update)
        {
            if (update_firmware_and_application(path_to_firmware, path_to_application))
            {
                this->uboot_handler->addVariable("update_reboot_state",
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE)
                );
                retValue = true;
            }
            else
            {
                this->uboot_handler->addVariable("update_reboot_state",
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
                );            
            }
        }
        else
        {
            updater::Bootstate handleUpdate(this->uboot_handler);
            handleUpdate.checkPendingUpdate();
        }

        this->uboot_handler->flushEnvironment();
    }

    return retValue;
}