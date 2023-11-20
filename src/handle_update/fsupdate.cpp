#include "fsupdate.h"

#include "updateFirmware.h"
#include "updateApplication.h"
#include "utils.h"
#include "../uboot_interface/allowed_uboot_variable_states.h"
#include <botan/hash.h>
#include <botan/hex.h>
#include <iostream> /* cout */
#include <sys/stat.h>
#include <errno.h>

#define TARGET_ARCHIV_DIR_PATH "/tmp/adu/.update"
#define TARGET_ARCHIVE_UPDATE_STORE "/tmp/adu/.update/tmp.tar.bz2"

fs::FSUpdate::FSUpdate(const std::shared_ptr<logger::LoggerHandler> &ptr)
    : uboot_handler(std::make_shared<UBoot::UBoot>(UBOOT_CONFIG_PATH)), logger(ptr),
      update_handler(uboot_handler, logger), work_dir(TEMP_ADU_WORK_DIR),
      work_dir_perms(std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                     std::filesystem::perms::group_read | std::filesystem::perms::group_write |
                     std::filesystem::perms::others_read | std::filesystem::perms::others_write )
{
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "fsupdate: construct", logger::logLevel::DEBUG));
}

fs::FSUpdate::~FSUpdate()
{
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "fsupdate: deconstruct", logger::logLevel::DEBUG));
}

bool fs::FSUpdate::create_work_dir()
{
    std::string msg(work_dir);

    if (std::filesystem::exists(work_dir))
    {
        msg += " does exist.";
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, msg , logger::logLevel::DEBUG));
        return false;
    }

    try
    {
        std::filesystem::create_directory(work_dir);
        std::filesystem::permissions(work_dir, work_dir_perms, std::filesystem::perm_options::replace);
    }
    catch (std::filesystem::filesystem_error const& ex)
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, ex.what(), logger::logLevel::DEBUG));
        throw GenericException(ex.code().message(), ex.code().value());
    }
    msg += " exists.";
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, msg , logger::logLevel::DEBUG));
    return true;
}

std::filesystem::path fs::FSUpdate::get_work_dir()
{
    return this->work_dir;
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
        update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS, true)) = '1';

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
        update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP, true)) = '1';

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
            update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS, true)) = '1';
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
            update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP, true)) = '1';
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
            update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS, true)) = '0';
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

void fs::FSUpdate::update_image(const std::string &path_to_update_image, uint8_t &installed_update_type)
{
    UpdateStore update_store;
    std::string target_archiv_path(TARGET_ARCHIV_DIR_PATH);
    std::string work_dir("/tmp/adu/.work");
    std::string updateInstalled("updateInstalled");

    /* create temporary directory to extract and install update file */
    mkdir(TARGET_ARCHIV_DIR_PATH, 0644);

    /* extract update image */
    update_store.ExtractUpdateStore(path_to_update_image);
    /* read and parse fsupdate.json */
    update_store.ReadUpdateConfiguration((target_archiv_path + std::string("/fsupdate.json")));
    /* read fw and/or application hashes from update configuration and compare it
     * calculated.
     */
    if(!update_store.CheckUpdateSha256Sum(target_archiv_path))
    {
        /* remove arch directory */
        remove(TARGET_ARCHIV_DIR_PATH);
    }

    target_archiv_path += "/";
    updateInstalled = work_dir + "/" + updateInstalled;
    /* Check update for firmware, application or both */
    if( update_store.IsApplicationAvailable() && update_store.IsFirmwareAvailable() )
    {
        this->update_firmware_and_application( (target_archiv_path + update_store.getFirmwareStoreName()), 
        (target_archiv_path + update_store.getApplicationStoreName()));
        mkdir(work_dir.c_str(), 0644);
        std::ofstream installed(updateInstalled.c_str());
        if(!installed.is_open())
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("Create file for state update installed fails."), logger::logLevel::ERROR));
            /* errno: Operation not permitted */
            std::string output = "Can not create " + updateInstalled;
            throw GenericException(output.c_str(), ENOENT);
        }
        else
        {
            std::filesystem::permissions(updateInstalled.c_str(),
            std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read,
            std::filesystem::perm_options::replace
            );
            installed.close();
        }
        /* firmware and application update */
        /* static_cast<int>(UPDATER_FIRMWARE_AND_APPLICATION_STATE::UPDATE_SUCCESSFUL); */
        installed_update_type = 3;
    }
    else if(update_store.IsFirmwareAvailable())
    {
        this->update_firmware((target_archiv_path + update_store.getFirmwareStoreName()));
        mkdir(work_dir.c_str(), 0644);
        std::ofstream installed(updateInstalled.c_str());
        if(!installed.is_open())
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("Create file for state firmware installed fails."), logger::logLevel::ERROR));
            std::string output = "Can not create " + updateInstalled;
            throw GenericException(output.c_str(), ENOENT);
        }
        else
        {
            std::filesystem::permissions(updateInstalled.c_str(),
            std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read,
            std::filesystem::perm_options::replace
            );
            installed.close();
        }
        /* firmware  update */
        /* static_cast<int>(UPDATER_FIRMWARE_STATE::UPDATE_SUCCESSFUL) */
        installed_update_type = 1;
    }
    else if(update_store.IsApplicationAvailable())
    {
        this->update_application((target_archiv_path + update_store.getApplicationStoreName()));
        mkdir(work_dir.c_str(), 0644);
        std::ofstream installed(updateInstalled.c_str());
        if(!installed.is_open())
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("Create file for state application installed fails."), logger::logLevel::ERROR));
            std::string output = "Can not create " + updateInstalled;
            throw GenericException(output.c_str(), ENOENT);
        }
        else
        {
            std::filesystem::permissions(updateInstalled.c_str(),
            std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read,
            std::filesystem::perm_options::replace
            );
            installed.close();
        }
        /* application update */
        /* static_cast<int>(UPDATER_APPLICATION_STATE::UPDATE_SUCCESSFUL) */
        installed_update_type = 2;
    }
    else
    {
        /* errno: Operation not permitted */
        throw GenericException("Invalid update", EPERM);
    }
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
    else
    {
        update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));
        if (this->update_handler.pendingUpdateRollback(update_reboot_state))
        {
            this->update_handler.confirmUpdateRollback();
            retValue = true;
        }
        else
        {
            this->logger->setLogEntry(
                logger::LogEntry(FSUPDATE_DOMAIN, "commit_update: not allowed update state", logger::logLevel::ERROR));
            throw(NotAllowedUpdateState());
        }
    }

    this->uboot_handler->flushEnvironment();
    return retValue;
}

void fs::FSUpdate::automatic_update_application(const std::string & path_to_application,
                                                const version_t & dest_version)
{
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);
    std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
    update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP, true)) = '1';
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
    update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS, true)) = '1';

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
    update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS, true)) = '1';
    update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP, true)) = '1';

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
                update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS, true)) = '0';
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
                update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP, true)) = '0';
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
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, std::string("rollback_firmware: Start rollback."),
                                                   logger::logLevel::DEBUG));
        /* Check for pending firmware update. This is rollback from
         *  uncommited state of the firmware.
         */
        bool app_fw_update_pending = this->update_handler.pendingApplicationFirmwareUpdate();
        if (this->update_handler.pendingFirmwareUpdate() || app_fw_update_pending == true)
        {
            this->logger->setLogEntry(logger::LogEntry(
                FSUPDATE_DOMAIN, std::string("rollback_firmware: Proceed rollback."), logger::logLevel::DEBUG));
            this->update_handler.firmware_rollback();
            if (app_fw_update_pending == true)
            {
                /* rollback fw and application progress  */
                this->uboot_handler->addVariable(
                    "update_reboot_state",
                    update_definitions::to_string(
                        update_definitions::UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING));
            }
            this->uboot_handler->flushEnvironment();
            this->logger->setLogEntry(logger::LogEntry(
                FSUPDATE_DOMAIN, std::string("rollback_firmware: Finish rollback."), logger::logLevel::DEBUG));
        }
        else
        {
            update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
                this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));
            if (this->update_handler.pendingUpdateRollback(update_reboot_state) == true)
            {
                this->logger->setLogEntry(logger::LogEntry(
                    FSUPDATE_DOMAIN, std::string("rollback_firmware: Stop rollback."), logger::logLevel::DEBUG));
                throw(GenericException("Commit for rollback required"));
            }
            else
            {
                /* Do rollback from commited firmware state.
                 * Change state is a kind of switch back to other commited state.
                 * The system will switch to other commited state or
                 * fails if next state is not commited.
                 */
                this->logger->setLogEntry(logger::LogEntry(
                    BOOTSTATE_DOMAIN, std::string("rollback_firmware: Start rollback."), logger::logLevel::DEBUG));
                size_t fw_index = FIRMWARE_A_INDEX;
                int next_update_state = 0;
                std::string s("rollback_firmware: ");
                const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
                const std::string current_slot = util::split(rauc_cmd, '=').back();

                std::vector<uint8_t> update =
                    util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));

                if (current_slot == "A")
                {
                    fw_index = FIRMWARE_B_INDEX;
                }
                /* get next update state */
                next_update_state = update.at(fw_index) - '0';
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
                /* Rollback is not allowed to uncommited or bad state.*/
                if ((next_update_state & STATE_UPDATE_UNCOMMITED) == STATE_UPDATE_UNCOMMITED)
                {
                    /* firwmare rollback was executed before and is't possible */
                    s += "fails commit FW_";
                    s += current_slot;
                    s += " requred.";
                    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, s, logger::logLevel::WARNING));
                    this->logger->setLogEntry(logger::LogEntry(
                        BOOTSTATE_DOMAIN, std::string("rollback_firmware: Stop rollback."), logger::logLevel::DEBUG));
                    throw(GenericException("Firmware rollback is not allowed.", ECANCELED));
                }
                else if ((next_update_state & STATE_UPDATE_BAD) == STATE_UPDATE_BAD)
                {
                    s += "fails FW_";
                    s += current_slot;
                    s += " state is bad.";
                    /* firmware rollback is't possible because other state is bad. */
                    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, s, logger::logLevel::WARNING));
                    this->logger->setLogEntry(logger::LogEntry(
                        BOOTSTATE_DOMAIN, std::string("rollback_firmware: Stop rollback."), logger::logLevel::DEBUG));
                    throw(GenericException("Firmware rollback is not allowed.", EPERM));
                }

                /* switch to other firmware state */
                this->uboot_handler->addVariable("BOOT_ORDER", "A B");
                this->uboot_handler->addVariable("BOOT_ORDER_OLD", "B A");
                /*  uncommited update state */
                if (current_slot == "A")
                {
                    this->uboot_handler->addVariable("BOOT_ORDER", "B A");
                    this->uboot_handler->addVariable("BOOT_ORDER_OLD", "A B");
                }
                /* to switch reboot should be done */
                this->uboot_handler->addVariable(
                    "update_reboot_state",
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING));
                this->uboot_handler->flushEnvironment();
                this->logger->setLogEntry(logger::LogEntry(
                    BOOTSTATE_DOMAIN, std::string("rollback_firmware: Finish rollback."), logger::logLevel::DEBUG));
            }
        }
    }
    catch (const updater::RollbackFirmwareUpdate &e)
    {
        std::throw_with_nested(GenericException(e.what()));
    }
}

void fs::FSUpdate::rollback_application()
{
    try
    {
        updater::applicationUpdate app_update(this->uboot_handler, this->logger);
        bool app_pendig = this->update_handler.pendingApplicationUpdate();
        if (app_pendig == true || this->update_handler.pendingApplicationFirmwareUpdate())
        {
            this->logger->setLogEntry(logger::LogEntry(
                FSUPDATE_DOMAIN, std::string("rollback_application: Proceed rollback"), logger::logLevel::DEBUG));
            this->update_handler.applicaton_rollback(app_update);
            /* If application and firmware rollback pending don't change the update_reboot_state.
            *  Firwmare rollback must be done too.
            */
            if(app_pendig == true)
            {
                /* Only change and save the state if application rollback in progress. */
                this->uboot_handler->flushEnvironment();
            }
        }
        else
        {
            update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
                this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

            if (this->update_handler.pendingUpdateRollback(update_reboot_state) == true)
            {
                this->logger->setLogEntry(logger::LogEntry(
                    FSUPDATE_DOMAIN, std::string("rollback_application: Stop rollback."), logger::logLevel::DEBUG));
                throw(GenericException("Commit for rollback required"));
            }
            else
            {
                /* Do rollback from commited application state.
                 *  Change state is a kind of switch back to other commited state.
                 *  The system will switch to other commited state or
                 *  fails if next state is not commited.
                 */
                this->logger->setLogEntry(logger::LogEntry(
                    BOOTSTATE_DOMAIN, std::string("rollback_application: commited app -> start rollback "),
                    logger::logLevel::DEBUG));
                /* get currect application state */
                const char current_app = this->uboot_handler->getVariable("application", allowed_application_variables);
                std::vector<uint8_t> update =
                    util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
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
                    throw(GenericException("Application rollback is not allowed.", ECANCELED));
                }
                else if ((current_update_state & STATE_UPDATE_BAD) == STATE_UPDATE_BAD)
                {
                    s += "fails APP_";
                    s.push_back(current_app);
                    s += " state is bad.";
                    /* application rollback was executed before and is't possible */
                    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, s, logger::logLevel::WARNING));
                    throw(GenericException("Application rollback is not allowed.", EPERM));
                }

                /* switch to other application */
                app_update.rollback();

                /* to switch reboot should be done */
                this->uboot_handler->addVariable(
                    "update_reboot_state", update_definitions::to_string(
                                               update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING));
                /* save to bootloader env. block */
                this->uboot_handler->flushEnvironment();
            }
        }
    }
    catch (const updater::RollbackApplicationUpdate &e)
    {
        std::throw_with_nested(GenericException(e.what()));
    }
}

int fs::FSUpdate::set_update_state_bad(const char &state, uint32_t update_id)
{
    int current_state = 0;
    size_t update_index;
    std::string out_string;

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
        return this->update_handler.firmware_reboot();
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

bool fs::FSUpdate::pendingUpdateRollback()
{
    update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));
    return this->update_handler.pendingUpdateRollback(update_reboot_state);
}

fs::UpdateStore::UpdateStore()
{
    this->app_available = false;
    this->fw_available = false;
}

void fs::UpdateStore::ReadUpdateConfiguration(const std::string configuration_path)
{
    /* create stream for update configuration file */
    this->update_configuration = std::ifstream(configuration_path, std::ifstream::in);
    /* check if the stream good is and no error flags are set */
    if (!this->update_configuration.good())
    {
        if (this->update_configuration.bad() || this->update_configuration.fail())
        {
            throw GenericException(configuration_path, ENOENT);
        }
    }

    std::string strline;
    std::stringstream strjson;
    Json::CharReaderBuilder builder;
    std::string errs;

    while (std::getline(this->update_configuration, strline))
    {
        strjson << strline;
    }

    if (!Json::parseFromStream(builder, strjson, &root, &errs))
    {
        std::string fails("Parsing of update configuration fails.");
        throw GenericException(fails, ENOENT);
    }
}

bool fs::UpdateStore::CheckUpdateSha256Sum(const std::filesystem::path & path_to_update_image)
{
    if(root.isMember("images") && !root["images"].empty())
    {
        Json::Value images = root["images"];
        if(images.isMember("updates") && !images["updates"].empty())
        {
            Json::Value updates = images["updates"];
            std::string sha256_str;
            std::string update_image_file;

            for(Json::Value::iterator counter = updates.begin(); counter != updates.end(); ++counter)
            {
                if(!(*counter).isMember("version") || !(*counter).isMember("handler")
                || !(*counter).isMember("file") || !(*counter).isMember("hashes"))
                {
                    /* wrong format nodes needed */
                    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "Nodes version, handler, files or hashes not available.", 
                    logger::logLevel::DEBUG));

                    std::string fails("Parsing of update configuration fails.");
                    throw GenericException(fails, ENOENT);
                }

                update_image_file = (*counter)["file"].asString();
                sha256_str = (*counter)["hashes"]["sha256"].asString();

                std::string image_full_path = path_to_update_image;
                image_full_path += "/" + update_image_file;

                std::string command = "sha256sum " + image_full_path;

                /* sha265sum calculate hash of 64 bytes */
                const unsigned int kBufferSize = 64 + 1;
                char buffer[kBufferSize];

                FILE *file = popen (command.c_str (), "r");
                if (file == nullptr)
                {
                    std::string fails(" popen :");
                    fails += command;
                    fails += " fails.";
                    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, fails, logger::logLevel::DEBUG));
                    throw GenericException(fails, errno);
                    //return false;
                }

                if (fgets (buffer, kBufferSize, file) == nullptr)
                {
                    std::string fails("Read of calculated hash for ");
                    fails += image_full_path;
                    fails += " fails.";
                    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, fails, logger::logLevel::DEBUG));
                    pclose (file);

                    throw GenericException(fails, errno);
                    //return false;
                }

                pclose (file);

                if (strncmp (buffer, sha256_str.c_str (), kBufferSize) != 0)
                {
                    std::string fails("Compare of hashes for ");
                    fails += image_full_path;
                    fails += " fails.";
                    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, fails, logger::logLevel::DEBUG));
                    throw GenericException(fails, errno);
                }

                if(update_image_file.compare(app_store_name) == 0)
                {
                    this->SetApplicationAvailable(true);
                } else if(update_image_file.compare(fw_store_name) == 0)
                {
                    this->SetFirmwareAvailable(true);
                } else
                {
                    std::string fails("Image ");
                    fails += update_image_file;
                    fails += " is not supported.";
                    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, fails, logger::logLevel::DEBUG));
                    throw GenericException(fails, EINVAL);
                }
            }
        }
        else
        {
            /* wrong json file format. node updates needed..*/
            std::string fails("Node updates is not available or empty.");
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, fails, logger::logLevel::DEBUG));
            throw GenericException(fails, EINVAL);
        }
    }
    else
    {
        /* wrong json file format. node images needed..*/
        std::string fails("Node images not available or empty.");
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, fails, logger::logLevel::DEBUG));
        throw GenericException(fails, EINVAL);
    }

    return true;
}

void fs::UpdateStore::ExtractUpdateStore(const std::filesystem::path & path_to_update_image)
{
    std::unique_ptr<struct fs_header_v1_0> fsheader10 = std::make_unique<struct fs_header_v1_0>();
    std::ifstream update_img(path_to_update_image, (std::ifstream::in | std::ifstream::binary));
    std::string target_update_store = TARGET_ARCHIVE_UPDATE_STORE;
    std::string update_image_file = path_to_update_image;
    // open file
    if (!update_img.good())
    {
        if (update_img.bad() || update_img.fail())
        {
            std::string error_str = std::string("Open file ") + update_image_file + std::string("fails");
            throw GenericException(update_image_file, -EACCES);
        }
    }

    update_img.read((char *)fsheader10.get(), sizeof(struct fs_header_v1_0));

    uint64_t file_size = 0;
    file_size = fsheader10->info.file_size_high & 0xFFFFFFFF;
    file_size = file_size << 32;
    file_size = file_size | (fsheader10->info.file_size_low & 0xFFFFFFFF);

    if (!std::strcmp("CERT", fsheader10->type) && (file_size > 0))
    {
        std::ofstream archive_store(target_update_store, (std::ofstream::out | std::ofstream::binary));
        if (!archive_store.good())
        {
            if (archive_store.bad() || archive_store.fail())
            {
                std::string error_str = std::string("Open file ") + target_update_store + std::string("fails");
                throw GenericException(target_update_store, -ENOENT);
            }
        }
        archive_store << update_img.rdbuf();
    }
    else
    {
        throw GenericException(std::string("Update has wrong format") , -ENOENT);
    }

    std::string cmd = uncompress_cmd_source_archive;
    cmd += target_update_store;
    cmd += uncompress_cmd_dest_folder;
    cmd += std::string(TARGET_ARCHIV_DIR_PATH);
    /* extract files from update archive to  TARGET_ARCHIV_DIR_PATH directory */
    const int ret = ::system(cmd.c_str());
    if ((ret == -1) || (ret == 127))
    {
        std::string error_str = std::string("Extract file ") + target_update_store + std::string("fails");
        throw GenericException(target_update_store, ret);
    }
    remove(target_update_store.c_str());
}
