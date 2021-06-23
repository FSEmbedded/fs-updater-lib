#include "fsupdate.h"

fs::FSUpdate::FSUpdate(const std::shared_ptr<logger::LoggerHandler> &ptr):
    logger(ptr)
{
    this->uboot_handler = std::make_shared<UBoot::UBoot>(PATH_TO_FIRMWARE_VERSION_FILE);
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "construct fsupdate", logger::logLevel::INFO));
}

fs::FSUpdate::~FSUpdate()
{
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "deconstruct fsupdate", logger::logLevel::INFO));
}

bool fs::FSUpdate::update_firmware(const std::filesystem::path & path_to_firmware)
{
    bool retValue = false;
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);

    try
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "start firmware update", logger::logLevel::DEBUG));
        update_fw.install(path_to_firmware);
        this->uboot_handler->addVariable("update_reboot_state", 
            update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
        );
        retValue = true;
    }
    catch(const std::exception& e)
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("firmware exception: ") + std::string(e.what()), logger::logLevel::WARNING));
        this->uboot_handler->addVariable("update_reboot_state", 
            update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
        );
        throw;
    }

    this->uboot_handler->addVariable("update", "0010");
    this->uboot_handler->flushEnvironment();

    return retValue;
}

bool fs::FSUpdate::update_application(const std::filesystem::path & path_to_application)
{
    bool retValue = false;
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);

    try
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "start application update", logger::logLevel::DEBUG));
        update_app.install(path_to_application);
        this->uboot_handler->addVariable("update_reboot_state", 
            update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE)
        );
        retValue = true;
    }
    catch(const std::exception& e)
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("application exception: ") + std::string(e.what()), logger::logLevel::WARNING));
        this->uboot_handler->addVariable("update_reboot_state", 
            update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
        );
        throw;
    }

    this->uboot_handler->addVariable("update", "0001");
    this->uboot_handler->flushEnvironment();

    return retValue;
}

bool fs::FSUpdate::update_firmware_and_application(const std::filesystem::path & path_to_firmware, 
                                                   const std::filesystem::path & path_to_application)
{
    bool retValue = false;
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);
    
    std::vector<char> update = {'0', '0', '0', '0'};
    try
    {
        update[3] = '1';
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "start application update", logger::logLevel::INFO));
        update_app.install(path_to_application);

        try
        {
            update[2] = '1';
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "start firmware update", logger::logLevel::INFO));
            update_fw.install(path_to_firmware);
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE)
            );
            retValue = true;
        }
        catch(const std::exception& e)
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("application exception: ") + std::string(e.what()), logger::logLevel::WARNING));
            this->uboot_handler->addVariable("update_reboot:state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
            );
            throw;
        }
        
    }
    catch(const std::exception& e)
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("application exception: ") + std::string(e.what()), logger::logLevel::WARNING));
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

bool fs::FSUpdate::commit_update()
{
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "commit update", logger::logLevel::INFO));
    updater::Bootstate updateHandler(this->uboot_handler, this->logger);
    return updateHandler.checkPendingUpdate();
}

bool fs::FSUpdate::automatic_update_application(const std::filesystem::path & path_to_application, 
                                                const unsigned int & dest_version)
{
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);
    bool retValue = false;

    const unsigned int application_version = update_app.getCurrentVersion();
    const char application_prev_update = util::to_array(this->uboot_handler->getVariable("update")).at(3);
    const update_definitions::UBootBootstateFlags current_update_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state"));

    const bool update_after_successful_update = (application_prev_update == '0') && (current_update_state == update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING);
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("update after successful update: ") + std::to_string(update_after_successful_update), logger::logLevel::INFO));

    const bool update_after_failed_update = (application_prev_update == '1') && ( 
        (current_update_state == update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE) ||
        (current_update_state == update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE) ||
        (current_update_state == update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED) );

    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("update after failed update: ") + std::to_string(update_after_failed_update), logger::logLevel::INFO));

    if (update_after_successful_update || update_after_failed_update)
    {
        if (dest_version > application_version)
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "start automatic application update", logger::logLevel::INFO));
            retValue = update_application(path_to_application);
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "installed application is newer than update version", logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("installed application version: ") + std::to_string(application_version), logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("destination application version: ") + std::to_string(dest_version), logger::logLevel::WARNING));

            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING)
            );
        }
    }
    else
    {
        updater::Bootstate handleUpdate(this->uboot_handler, this->logger);
        if (handleUpdate.checkPendingUpdate())
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "no update pending", logger::logLevel::INFO));
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING)
            );    
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "failed application update", logger::logLevel::INFO));
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE)
            );
        }
    }

    this->uboot_handler->flushEnvironment();
    return retValue;
}

bool fs::FSUpdate::automatic_update_firmware(const std::filesystem::path & path_to_firmware,
                                             const unsigned int & dest_version)
{
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);
    bool retValue = false;

    const unsigned int firmware_version = update_fw.getCurrentVersion();
    const unsigned char firmware_prev_update = util::to_array(this->uboot_handler->getVariable("update")).at(2);
    const update_definitions::UBootBootstateFlags current_update_state = update_definitions::to_UBootBootstateFlags(
                                                                            this->uboot_handler->getVariable("update_reboot_state")
                                                                         );
    const bool update_after_successful_update = (firmware_prev_update == '0') && (current_update_state == update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING);
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("update after successful firmware update: ") + std::to_string(update_after_successful_update), logger::logLevel::INFO));
    const bool update_after_failed_update = (firmware_prev_update == '1') &&
            ( (current_update_state == update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE) ||
              (current_update_state == update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE) ||
              (current_update_state == update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED)
            );
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("update after failed firmware update: ") + std::to_string(update_after_failed_update), logger::logLevel::INFO));

    if (update_after_failed_update || update_after_successful_update)
    {
        if (dest_version > firmware_version)
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("destination firmware version: ") + std::to_string(dest_version), logger::logLevel::INFO));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("local firmware version: ") + std::to_string(firmware_version), logger::logLevel::INFO));

            if (update_firmware(path_to_firmware))
            {
                retValue = true;
            }
            else
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "failed firmware update", logger::logLevel::WARNING));
                this->uboot_handler->addVariable("update_reboot_state",
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
                );
            }
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "installed firmware is newer than update version", logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("installed firmware version: ") + std::to_string(firmware_version), logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("destination firmware version: ") + std::to_string(dest_version), logger::logLevel::WARNING));
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING)
            );        
        }
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "", logger::logLevel::WARNING));   
        updater::Bootstate handleUpdate(this->uboot_handler, this->logger);
        handleUpdate.checkPendingUpdate();
    }
    
    this->uboot_handler->flushEnvironment();
    return retValue;
}

bool fs::FSUpdate::automatic_update_firmware_and_application(const std::filesystem::path & path_to_firmware,
                                                        const std::filesystem::path & path_to_application,
                                                        const unsigned int & dest_ver_application, 
                                                        const unsigned int & dest_ver_firmware)
{
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);

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
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("update after successful update: ") + std::to_string(update_after_successful_update), logger::logLevel::INFO));

        const bool update_after_failed_update = (((firmware_prev_update == 1) || (application_prev_update == 1)) &&
            ((current_update_state == update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE) ||
            (current_update_state == update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE) ||
            (current_update_state == update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED))
        );

        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("update after failed update: ") + std::to_string(update_after_failed_update), logger::logLevel::INFO));

        if (update_after_successful_update || update_after_failed_update)
        {
            if (update_firmware_and_application(path_to_firmware, path_to_application))
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "incomplete application and firmware update", logger::logLevel::WARNING));
                this->uboot_handler->addVariable("update_reboot_state",
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE)
                );
                retValue = true;
            }
            else
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "incomplete application and firmware update", logger::logLevel::WARNING));           
            }
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "firmware and application update package is not updateable", logger::logLevel::WARNING));           
            updater::Bootstate handleUpdate(this->uboot_handler, this->logger);
            handleUpdate.checkPendingUpdate();
        }

        this->uboot_handler->flushEnvironment();
    }

    return retValue;
}

update_definitions::UBootBootstateFlags fs::FSUpdate::get_update_reboot_state()
{
    const std::string update_reboot_state = this->uboot_handler->getVariable("update_reboot_state");
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("update_reboot_state: ") + update_reboot_state, logger::logLevel::INFO));        
    return update_definitions::to_UBootBootstateFlags(update_reboot_state);
}
