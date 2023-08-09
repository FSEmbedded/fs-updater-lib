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
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "decorator_update_state: no update in progress pending", logger::logLevel::DEBUG));
        func();
    }
    else if (this->update_handler.failedFirmwareUpdate())
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "decorator_update_state: failed firmware update pending", logger::logLevel::ERROR));
        throw(UpdateInProgress("Failed firmware update is uncommited"));
    }
    else if (this->update_handler.failedApplicationUpdate())
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "decorator_update_state: failed application update pending", logger::logLevel::ERROR));
        throw(UpdateInProgress("Failed application update is uncommited"));
    }
    else if(this->update_handler.pendingFirmwareUpdate())
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "decorator_update_state: firmware update pending", logger::logLevel::ERROR));
        throw(UpdateInProgress("Pending firmware update is not commited"));
    }
    else if(this->update_handler.pendingApplicationUpdate())
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "decorator_update_state: application update pending", logger::logLevel::ERROR));
        throw(UpdateInProgress("Pending application update is not commited"));
    }
    else if(this->update_handler.pendingApplicationFirmwareUpdate())
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "decorator_update_state: application & firmware update pending", logger::logLevel::ERROR));
        throw(UpdateInProgress("Pending application & firmware update is not commited"));
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "decorator_update_state: Unknown update state: HEAVY PROGRAMMING ERROR", logger::logLevel::ERROR));
        throw(UpdateInProgress("Unknown state of update process"));
    }
}

void fs::FSUpdate::update_firmware(const std::string & path_to_firmware)
{
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);

    std::function<void()> update_firmware = [&](){
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
        update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS)) = '1';

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

void fs::FSUpdate::update_application(const std::string & path_to_application)
{
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);
    std::function<void()> update_application = [&](){

        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
        update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP)) = '1';

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

void fs::FSUpdate::update_firmware_and_application(const std::string & path_to_firmware,
                                                   const std::string & path_to_application)
{
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);

    std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));

    std::function<void()> update_firmware_and_application = [&](){

        try
        {
            update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS)) = '1';
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

        try
        {
            update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP)) = '1';
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "update_firmware_and_application: start application update", logger::logLevel::DEBUG));
            update_app.install(path_to_application);

            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE)
            );
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->flushEnvironment();
        }
        catch(const std::exception& e)
        {
            update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS)) = '0';
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE)
            );
            const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD");
            this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);

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
    else if (this->update_handler.failedRebootFirmwareUpdate())
    {
        this->update_handler.confirmFailedRebootFirmwareUpdate();
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
    else if (this->update_handler.missedFirmwareRebootDuringRollback())
    {
        this->update_handler.confirmMissedRebootDuringFirmwareRollback();
        retValue = true;
    }
    else if (this->update_handler.missedApplicationRebootDuringRollback())
    {
        this->update_handler.confirmMissedRebootDuringApplicationRollback();
        retValue = true;
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "commit_update: not allowed update state", logger::logLevel::ERROR));
        throw(NotAllowedUpdateState());
    }

    this->uboot_handler->flushEnvironment();
    return retValue;
}

void fs::FSUpdate::automatic_update_application(const std::string & path_to_application,
                                                const version_t & dest_version)
{
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);
    std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
    update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP)) = '1';
    std::function<void()> automatic_update_application = [&]()
    {
#ifdef USE_FS_VERSION_COMPARE
        const version_t application_version = update_app.getCurrentVersion();

        if (dest_version > application_version)
#endif
        {
#ifdef USE_FS_VERSION_COMPARE
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_application: destination application version: ") + std::to_string(dest_version), logger::logLevel::DEBUG));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_application: local application version: ") + std::to_string(application_version), logger::logLevel::DEBUG));
#endif
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_application: start automatic application update", logger::logLevel::DEBUG));

            try
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_application: start application update", logger::logLevel::DEBUG));
                update_app.install(path_to_application);
                this->uboot_handler->addVariable("update_reboot_state",
                                                 update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE));
                this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
                this->uboot_handler->flushEnvironment();
            }
            catch (const std::exception &e)
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("application exception: ") + std::string(e.what()), logger::logLevel::ERROR));
                this->uboot_handler->addVariable("update_reboot_state",
                                                 update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE));
                this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
                this->uboot_handler->flushEnvironment();
                throw;
            }
        }
#ifdef USE_FS_VERSION_COMPARE
        else
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_application: installed application is newer than update version", logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_application: installed application version: ") + std::to_string(application_version), logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_application: destination application version: ") + std::to_string(dest_version), logger::logLevel::WARNING));
            throw(fs::ApplicationVersion(dest_version, application_version));
        }
#endif
    };

    this->decorator_update_state(automatic_update_application);
}

void fs::FSUpdate::automatic_update_firmware(const std::string & path_to_firmware,
                                             const version_t & dest_version)
{
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);

    std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
    update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS)) = '1';

    std::function<void()> automatic_update_application = [&]()
    {
#ifdef USE_FS_VERSION_COMPARE
        const version_t firmware_version = update_fw.getCurrentVersion();
        if (dest_version > firmware_version)
#endif
        {
#ifdef USE_FS_VERSION_COMPARE
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware: destination firmware version: ") + std::to_string(dest_version), logger::logLevel::DEBUG));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware: local firmware version: ") + std::to_string(firmware_version), logger::logLevel::DEBUG));
#endif

            try
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_firmware: start firmware update", logger::logLevel::DEBUG));
                update_fw.install(path_to_firmware);
                this->uboot_handler->addVariable("update_reboot_state",
                                                 update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE));
                this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
                this->uboot_handler->flushEnvironment();
            }
            catch (const std::exception &e)
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware: firmware exception: ") + std::string(e.what()), logger::logLevel::ERROR));
                this->uboot_handler->addVariable("update_reboot_state",
                                                 update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE));
                this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
                this->uboot_handler->flushEnvironment();
                throw;
            }
        }
#ifdef USE_FS_VERSION_COMPARE
        else
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_firmware: installed firmware is newer than update version", logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware: installed firmware version: ") + std::to_string(firmware_version), logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware: destination firmware version: ") + std::to_string(dest_version), logger::logLevel::WARNING));
            throw(fs::FirmwareVersion(dest_version, firmware_version));
        }
#endif
    };
    this->decorator_update_state(automatic_update_application);
}

void fs::FSUpdate::automatic_update_firmware_and_application(const std::string & path_to_firmware,
                                                        const std::string & path_to_application,
                                                        const version_t & dest_ver_application,
                                                        const version_t & dest_ver_firmware)
{
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);

    std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
    update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS)) = '1';
    update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP)) = '1';

    std::function<void()> automatic_update_firmware_and_application = [&]()
    {

#ifdef USE_FS_VERSION_COMPARE
        const version_t fw_version = update_fw.getCurrentVersion();
        const version_t app_version = update_app.getCurrentVersion();

        if ((dest_ver_firmware > fw_version) && (dest_ver_application > app_version))
#endif
        {
            // Application update codeblock
            try
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_firmware_and_application: start application update", logger::logLevel::DEBUG));
                update_app.install(path_to_application);
            }
            catch (const std::exception &e)
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("application exception: ") + std::string(e.what()), logger::logLevel::ERROR));
                this->uboot_handler->addVariable("update_reboot_state",
                                                 update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE));
                update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS)) = '0';
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
            catch (const std::exception &e)
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware_and_application: firmware exception: ") + std::string(e.what()), logger::logLevel::ERROR));
                this->uboot_handler->addVariable("update_reboot_state",
                                                 update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE));
                update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP)) = '0';
                this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
                this->uboot_handler->flushEnvironment();
                throw;
            }

            this->uboot_handler->addVariable("update_reboot_state",
                                             update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE));
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->flushEnvironment();
        }
#ifdef USE_FS_VERSION_COMPARE
        else
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "automatic_update_firmware_and_application: at least one of firmware or application is not new enough", logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware_and_application: destination firmware version: ") + std::to_string(dest_ver_firmware), logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware_and_application: local firmware version: ") + std::to_string(fw_version), logger::logLevel::WARNING));

            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware_and_application: destination application version: ") + std::to_string(dest_ver_application), logger::logLevel::WARNING));
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("automatic_update_firmware_and_application: local application version: ") + std::to_string(app_version), logger::logLevel::WARNING));
            throw(fs::FirmwareApplicationVersion(dest_ver_firmware, fw_version, dest_ver_application, app_version));
        }
#endif
    };
    this->decorator_update_state(automatic_update_firmware_and_application);
}

update_definitions::UBootBootstateFlags fs::FSUpdate::get_update_reboot_state()
{
    const uint8_t update_reboot_state = this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables);
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("update_reboot_state: ") + std::to_string(update_reboot_state), logger::logLevel::DEBUG));
    return update_definitions::to_UBootBootstateFlags(update_reboot_state);
}

version_t fs::FSUpdate::get_application_version()
{
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);
    return update_app.getCurrentVersion();
}

version_t fs::FSUpdate::get_firmware_version()
{
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);
    return update_fw.getCurrentVersion();
}

void fs::FSUpdate::rollback_firmware()
{
    try
    {
        if (this->update_handler.pendingFirmwareUpdate())
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("rollback_firmware: Proceed rollback"), logger::logLevel::DEBUG));
            this->update_handler.firmware_rollback();
            this->uboot_handler->flushEnvironment();
        }
        else if(this->update_handler.missedFirmwareRebootDuringRollback())
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("rollback_firmware: Reboot for rollback pending"), logger::logLevel::ERROR));
            throw(RollbackFirmware("Reboot for rollback pending"));
        }
        else
        {
            /* Do rollback from commited firmware state.
             * Change state is a kind of switch back to other commited state.
             * The system will switch to other commited state or
             * fails if next state is not commited.
             */
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("rollback_firmware: commited fw -> start rollback "), logger::logLevel::DEBUG));
            size_t fw_index = FIRMWARE_A_INDEX;
            int next_update_state = 0;
            std::string s("rollback_firmware: ");
            const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
            const std::string current_slot = util::split(rauc_cmd, '=').back();

            std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));

            if (current_slot == "A")
            {
                fw_index = FIRMWARE_B_INDEX;
            }

            next_update_state = update.at (fw_index) - '0';
            s += "try switch to ";
            if (current_slot == "B")
            {
                s += "A";
            }
            else
            {
                s += "B";
            }

            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, s, logger::logLevel::DEBUG));

            s = "rollback_firmware: ";

            if ((next_update_state & STATE_UPDATE_UNCOMMITED) == STATE_UPDATE_UNCOMMITED)
            {
                /* firwmare rollback was executed before and is't possible */
                s += "fails commit FW_";
                s += current_slot;
                s += " requred.";
                this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, s, logger::logLevel::WARNING));
                throw(RollbackFirmware("Firmware rollback is not allowed.", ECANCELED));
            }
            else if ((next_update_state & STATE_UPDATE_BAD) == STATE_UPDATE_BAD)
            {
                s += "fails FW_";
                s += current_slot;
                s += " state is bad.";
                /* firmware rollback is't possible because other state is bad. */
                this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, s, logger::logLevel::WARNING));
                throw(RollbackFirmware("Firmware rollback is not allowed.", EPERM));
            }

            /* switch to other firmware state */
            //this->update_handler.firmware_rollback(true);
            this->uboot_handler->addVariable("BOOT_ORDER", "A B");
            this->uboot_handler->addVariable("BOOT_ORDER_OLD", "B A");
            /*  uncommited update state */
            if (current_slot == "A") {
                this->uboot_handler->addVariable("BOOT_ORDER", "B A");
                this->uboot_handler->addVariable("BOOT_ORDER_OLD", "A B");
                /* B uncommited */
                update.at(FIRMWARE_B_INDEX) = '1';
            } else
            {
                /* A uncommited */
                update.at(FIRMWARE_A_INDEX) = '1';
            }
            /* set new update state */
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            /* to switch reboot should be done */
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING));
            this->uboot_handler->flushEnvironment();
        }
    }
    catch(const updater::RollbackFirmwareUpdate &e)
    {
        std::throw_with_nested(RollbackFirmware(e.what()));
    }
}

void fs::FSUpdate::rollback_application()
{
    try
    {
        updater::applicationUpdate app_update(this->uboot_handler, this->logger);
        if (this->update_handler.pendingApplicationUpdate())
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("rollback_application: Proceed rollback"), logger::logLevel::DEBUG));
            this->update_handler.applicaton_rollback(app_update);
            this->uboot_handler->flushEnvironment();
        }
        else if(this->update_handler.missedApplicationRebootDuringRollback())
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("rollback_application: Reboot for rollback pending"), logger::logLevel::ERROR));
            throw(RollbackApplication("Reboot for rollback pending"));
        }
        else
        {
            /* Do rollback from commited application state.
            *  Change state is a kind of switch back to other commited state.
            *  The system will switch to other commited state or
            *  fails if next state is not commited.
            */
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("rollback_application: commited app -> start rollback "), logger::logLevel::DEBUG));
            /* get currect application state */
            const char current_app = this->uboot_handler->getVariable("application", allowed_application_variables);
            std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
            size_t app_index = APPLICATION_A_INDEX;
            int current_update_state = 0;
            std::string s("rollback_application: ");

            if (current_app == 'A')
            {
                app_index = APPLICATION_B_INDEX;
            }
            /* current state to int */
            current_update_state = update.at(app_index) - '0';
            s += "try switch to ";
            if (current_app == 'B')
            {
               s.push_back('A');
            }
            else
            {
                s.push_back('B');
            }
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, s, logger::logLevel::DEBUG));

            s = "rollback_application: ";

            if ((current_update_state & STATE_UPDATE_UNCOMMITED) == STATE_UPDATE_UNCOMMITED)
            {
                /* application rollback was executed before and is't possible */
                s += "fails commit APP_";
                s.push_back(current_app);
                s += " requred.";
                this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, s, logger::logLevel::WARNING));
                throw(RollbackApplication("Application rollback is not allowed.", ECANCELED));
            }
            else if ( (current_update_state & STATE_UPDATE_BAD) ==  STATE_UPDATE_BAD)
            {
                s += "fails APP_";
                s.push_back(current_app);
                s += " state is bad.";
                 /* application rollback was executed before and is't possible */
                 this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, s, logger::logLevel::WARNING));
                 throw(RollbackApplication("Application rollback is not allowed.", EPERM));
            }

            /* switch to other application */
            app_update.rollback();

            /*  uncommited update state */
            if (current_app == 'A') {
                /* B uncommited */
                update.at(APPLICATION_B_INDEX) = '1';
            } else
            {
                /* A uncommited */
                update.at(APPLICATION_A_INDEX) = '1';
            }
            /* set new update state */
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));

            /* to switch reboot should be done */
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING));
            /* save to bootloader env. block */
            this->uboot_handler->flushEnvironment();
        }
    }
    catch(const updater::RollbackApplicationUpdate& e)
    {
        std::throw_with_nested(RollbackApplication(e.what()));
    }
}

int fs::FSUpdate::set_update_state_bad(const char &state, uint32_t update_id)
{
    int current_state = 0;
    size_t update_index;
    std::string out_string;

    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("application state: set application state bad "), logger::logLevel::DEBUG));
    /* check passing parameter */
    if ((state != 'a' && state != 'A' && state != 'b' && state != 'B') || (update_id >= 2))
        return EINVAL;
    /* get update state */
    std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));

    /* firmware update */
    if (update_id == 0)
    {
        out_string = "firmware state: FW_";
        update_index = FIRMWARE_A_INDEX;
        if (state == 'B' || state == 'b')
        {
            update_index = FIRMWARE_B_INDEX;
        }
    }
    else
    {
        /* application update normal update_id = 1*/
        out_string = "application state: APP_";
        update_index = APPLICATION_A_INDEX;
        if (state == 'B' || state == 'b')
        {
            update_index = APPLICATION_B_INDEX;
        }
    }

    current_state = update.at(update_index) - '0';
    out_string.push_back(state);

    if ((current_state & STATE_UPDATE_BAD) == STATE_UPDATE_BAD)
    {
        out_string += " state is allready bad.";
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, out_string, logger::logLevel::DEBUG));
    }
    else
    {
        current_state += STATE_UPDATE_BAD;
        out_string += " state mark bad.";
        /* mark update state bad */
        update.at(update_index) = '0' + STATE_UPDATE_BAD;
        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
    }

    /* save to bootloader env. block */
    this->uboot_handler->flushEnvironment();
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, out_string, logger::logLevel::DEBUG));

    return 0;
}

bool fs::FSUpdate::is_update_state_bad(const char &state, uint32_t update_id)
{
    bool ret_state = false;
    int current_state = 0;
    size_t update_index;
    std::string out_string;
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("application state: set application state bad "), logger::logLevel::DEBUG));

    /* get update state */
    std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));

    /* firmware update */
    if (update_id == 0)
    {
        out_string = "firmware state: ";
        update_index = FIRMWARE_A_INDEX;
        if (state == 'B' || state == 'b')
        {
            update_index = FIRMWARE_B_INDEX;
        }
    }
    else
    {
        /* application update normal update_id = 1*/
        out_string = "application state: ";
        update_index = APPLICATION_A_INDEX;
        if (state == 'B' || state == 'b')
        {
            update_index = APPLICATION_B_INDEX;
        }
    }

    current_state = update.at(update_index) - '0';

    if ((current_state & STATE_UPDATE_BAD) == STATE_UPDATE_BAD)
    {
        ret_state = true;
        out_string += "BAD";
    }
    else
    {
        out_string += "not BAD";
    }

    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, out_string, logger::logLevel::DEBUG));
    return ret_state;
}

bool fs::FSUpdate::is_reboot_complete(bool firmware)
{
    if (firmware == true)
    {
        /* get missing reboot */
        return !(this->update_handler.firmware_reboot());
    }

    /* check reboot complete state for app rollback or update */
    return this->update_handler.application_reboot();
}

void fs::FSUpdate::update_reboot_state (update_definitions::UBootBootstateFlags flag)
{
    /* to switch reboot should be done */
    this->uboot_handler->addVariable (
        "update_reboot_state", update_definitions::to_string (
                                   flag));
    /* save to bootloader env. block */
    this->uboot_handler->flushEnvironment ();
}
