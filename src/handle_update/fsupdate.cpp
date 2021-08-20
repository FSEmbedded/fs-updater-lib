#include "fsupdate.h"

#include "updateFirmware.h"
#include "updateApplication.h"
#include "utils.h"
#include "../uboot_interface/allowed_uboot_variable_states.h"

fs::FSUpdate::FSUpdate(const std::shared_ptr<logger::LoggerHandler> &ptr):
    uboot_handler(std::make_shared<UBoot::UBoot>(UBOOT_CONFIG_PATH)),
    logger(ptr),
    update_handler(uboot_handler, logger)
{
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "fsupdate: construct", logger::logLevel::DEBUG));
}

fs::FSUpdate::~FSUpdate()
{
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "fsupdate: deconstruct", logger::logLevel::DEBUG));
}

void fs::FSUpdate::decorator_update_state(std::function<void()> func)
{
    if (this->update_handler.noUpdateProcessing())
    {
        func();
    }
    else if (this->update_handler.failedFirmwareUpdate())
    {
        throw(UpdateInProgress("Failed firmware update is uncommited"));
    }
    else if (this->update_handler.failedApplicationUpdate())
    {
        throw(UpdateInProgress("Failed application update is uncommited"));
    }
    else if(this->update_handler.pendingFirmwareUpdate())
    {
        throw(UpdateInProgress("Pending firmware update is not commited"));
    }
    else if(this->update_handler.pendingApplicationUpdate())
    {
        throw(UpdateInProgress("Pending application update is not commited"));
    }
    else if(this->update_handler.pendingApplicationFirmwareUpdate())
    {
        throw(UpdateInProgress("Pending application & firmware update is not commited"));
    }
    else
    {
        throw(UpdateInProgress("Unknown state of update process"));
    }
}

void fs::FSUpdate::update_firmware(const std::filesystem::path & path_to_firmware)
{
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);

    std::function<void()> update_firmware = [&](){
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
        update.at(2) = '1';

        try
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "update_firmware: start firmware update", logger::logLevel::DEBUG));
            update_fw.install(path_to_firmware);
            this->uboot_handler->addVariable("update_reboot_state", 
                update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
            );
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->flushEnvironment();
        }
        catch(const std::exception& e)
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("update_firmware: firmware exception: ") + std::string(e.what()), logger::logLevel::ERROR));
            this->uboot_handler->addVariable("update_reboot_state", 
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
            );
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->flushEnvironment();
            throw;
        }
    };

    this->decorator_update_state(update_firmware);
}

void fs::FSUpdate::update_application(const std::filesystem::path & path_to_application)
{
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);
    std::function<void()> update_application = [&](){
        
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
        update.at(3) = '1';
    
        try
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "update_application: start application update", logger::logLevel::DEBUG));
            update_app.install(path_to_application);
            this->uboot_handler->addVariable("update_reboot_state", 
                update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE)
            );
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->flushEnvironment();
        }
        catch(const std::exception & e)
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("application exception: ") + std::string(e.what()), logger::logLevel::ERROR));
            this->uboot_handler->addVariable("update_reboot_state", 
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE)
            );
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->flushEnvironment();
            throw;
        }
    };

    this->decorator_update_state(update_application);
}

void fs::FSUpdate::update_firmware_and_application(const std::filesystem::path & path_to_firmware, 
                                                   const std::filesystem::path & path_to_application)
{
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);
    
    std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));

    std::function<void()> update_firmware_and_application = [&](){
        try
        {
            update.at(3) = '1';
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "update_firmware_and_application: start application update", logger::logLevel::DEBUG));
            update_app.install(path_to_application);

            try
            {
                update.at(2) = '1';
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "update_firmware_and_application: start firmware update", logger::logLevel::DEBUG));
                update_fw.install(path_to_firmware);
            }
            catch(const std::exception& e)
            {
                this->uboot_handler->freeVariables();
                this->uboot_handler->addVariable("update_reboot_state", 
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
                );
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("update_firmware_and_application: error during firmware update"), logger::logLevel::ERROR));
                this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
                this->uboot_handler->flushEnvironment();
                throw;
            }
            
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->flushEnvironment();
        }
        catch(const std::exception& e)
        {
            this->uboot_handler->addVariable("update_reboot_state", 
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE)
            );
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("update_firmware_and_application: error during application update") + std::string(e.what()), logger::logLevel::ERROR));
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->flushEnvironment();
            throw;
        }
    };

    this->decorator_update_state(update_firmware_and_application);
}

bool fs::FSUpdate::commit_update()
{
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "commit_update: commit update", logger::logLevel::DEBUG));
    bool retValue = false;
    if (this->update_handler.pendingApplicationUpdate())
    {
        this->update_handler.confirmPendingApplicationUpdate();
        retValue = true;
    }
    else if (this->update_handler.pendingFirmwareUpdate())
    {
        this->update_handler.confirmPendingFirmwareUpdate();
        retValue = true;
    }
    else if (this->update_handler.pendingApplicationFirmwareUpdate())
    {
        this->update_handler.confirmPendingApplicationFirmwareUpdate();
        retValue = true;  
    }
    else if (this->update_handler.failedFirmwareUpdate())
    {
        this->update_handler.confirmFailedFirmwareUpdate();
        retValue = true;
    }
    else if (this->update_handler.failedApplicationUpdate())
    {
        this->update_handler.confirmFailedApplicationeUpdate();
        retValue = true;
    }
    else if (this->update_handler.noUpdateProcessing())
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "commit_update: nothing to commit", logger::logLevel::DEBUG));
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "commit_update: not allowed update state", logger::logLevel::ERROR));
        throw(NotAllowedUpdateState());
    }

    this->uboot_handler->flushEnvironment();
    return retValue;
}

void fs::FSUpdate::automatic_update_application(const std::filesystem::path & path_to_application, 
                                                const unsigned int & dest_version)
{
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);
    std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
    update.at(3) = '1';

    std::function<void()> automatic_update_application = [&](){

        const unsigned int application_version = update_app.getCurrentVersion();
        if (dest_version > application_version)
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_application: destination application version: ") + std::to_string(dest_version), logger::logLevel::DEBUG));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_application: local application version: ") + std::to_string(application_version), logger::logLevel::DEBUG));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_application: start automatic application update", logger::logLevel::DEBUG));
            
            try
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_application: start application update", logger::logLevel::DEBUG));
                update_app.install(path_to_application);
                this->uboot_handler->addVariable("update_reboot_state", 
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE)
                );
                this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
                this->uboot_handler->flushEnvironment();
            }
            catch(const std::exception & e)
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("application exception: ") + std::string(e.what()), logger::logLevel::WARNING));
                this->uboot_handler->addVariable("update_reboot_state", 
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE)
                );
                this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
                this->uboot_handler->flushEnvironment();
                throw;
            }
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_application: installed application is newer than update version", logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_application: installed application version: ") + std::to_string(application_version), logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_application: destination application version: ") + std::to_string(dest_version), logger::logLevel::WARNING));
            throw(fs::ApplicationVersion(dest_version, application_version));
        }
    };

    this->decorator_update_state(automatic_update_application);
}

void fs::FSUpdate::automatic_update_firmware(const std::filesystem::path & path_to_firmware,
                                             const unsigned int & dest_version)
{
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);

    std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
    update.at(2) = '1';

    std::function<void()> automatic_update_application = [&](){

        const unsigned int firmware_version = update_fw.getCurrentVersion();
        if (dest_version > firmware_version)
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware: destination firmware version: ") + std::to_string(dest_version), logger::logLevel::DEBUG));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware: local firmware version: ") + std::to_string(firmware_version), logger::logLevel::DEBUG));
            
            try
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_firmware: start firmware update", logger::logLevel::DEBUG));
                update_fw.install(path_to_firmware);
                this->uboot_handler->addVariable("update_reboot_state", 
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
                );
                this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
                this->uboot_handler->flushEnvironment();
            }
            catch(const std::exception& e)
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware: firmware exception: ") + std::string(e.what()), logger::logLevel::WARNING));
                this->uboot_handler->addVariable("update_reboot_state", 
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
                );
                this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
                this->uboot_handler->flushEnvironment();
                throw;
            }
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_firmware: installed firmware is newer than update version", logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware: installed firmware version: ") + std::to_string(firmware_version), logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware: destination firmware version: ") + std::to_string(dest_version), logger::logLevel::WARNING));
            throw(fs::FirmwareVersion(dest_version, firmware_version));

        }
    };

    this->decorator_update_state(automatic_update_application);
}

void fs::FSUpdate::automatic_update_firmware_and_application(const std::filesystem::path & path_to_firmware,
                                                        const std::filesystem::path & path_to_application,
                                                        const unsigned int & dest_ver_application, 
                                                        const unsigned int & dest_ver_firmware)
{
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);

    std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
    update.at(3) = '1';
    update.at(2) = '1';

    std::function<void()> automatic_update_firmware_and_application = [&](){

        const unsigned int fw_version = update_fw.getCurrentVersion();
        const unsigned int app_version = update_app.getCurrentVersion();

        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware_and_application: destination firmware version: ") + std::to_string(dest_ver_firmware), logger::logLevel::DEBUG));
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware_and_application: local firmware version: ") + std::to_string(fw_version), logger::logLevel::DEBUG));
        
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware_and_application: destination application version: ") + std::to_string(dest_ver_application), logger::logLevel::DEBUG));
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware_and_application: local application version: ") + std::to_string(app_version), logger::logLevel::DEBUG)); 
        
        if ((dest_ver_firmware > fw_version) && (dest_ver_application > app_version))
        {
            // Application update codeblock
            try
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_firmware_and_application: start application update", logger::logLevel::DEBUG));
                update_app.install(path_to_application);
            }
            catch(const std::exception & e)
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("application exception: ") + std::string(e.what()), logger::logLevel::WARNING));
                this->uboot_handler->addVariable("update_reboot_state", 
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE)
                );
                this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
                this->uboot_handler->flushEnvironment();
                throw;
            }
            // Firmware update codeblock
            try
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_firmware_and_application: start firmware update", logger::logLevel::DEBUG));
                update_fw.install(path_to_firmware);
            }
            catch(const std::exception& e)
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware_and_application: firmware exception: ") + std::string(e.what()), logger::logLevel::WARNING));
                this->uboot_handler->addVariable("update_reboot_state", 
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
                );
                this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
                this->uboot_handler->flushEnvironment();
                throw;
            }

            this->uboot_handler->addVariable("update_reboot_state", 
                update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE)
            );
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->flushEnvironment();
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_firmware_and_application: at least one of firmware or application is not new enough", logger::logLevel::WARNING));
            throw(fs::FirmwareApplicationVersion(dest_ver_firmware, fw_version, dest_ver_application, app_version));
        }
    };

    this->decorator_update_state(automatic_update_firmware_and_application);
}

update_definitions::UBootBootstateFlags fs::FSUpdate::get_update_reboot_state()
{
    const uint8_t update_reboot_state = this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables);
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("update_reboot_state: ") + std::to_string(update_reboot_state), logger::logLevel::DEBUG));        
    return update_definitions::to_UBootBootstateFlags(update_reboot_state);
}
