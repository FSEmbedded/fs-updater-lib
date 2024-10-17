#include "fsupdate.h"

#include "updateFirmware.h"
#include "updateApplication.h"
#include "utils.h"
#include "../uboot_interface/allowed_uboot_variable_states.h"
#include <botan/hash.h>
#include <botan/hex.h>
#include <iostream> /* cout */
#include <algorithm> /* transform */
#include <cctype>    /* tolower */
#include <sys/stat.h>
#include <errno.h>

#define TARGET_ARCHIV_DIR_PATH "/tmp/adu/.update"
#define TARGET_ARCHIVE_UPDATE_STORE "/tmp/adu/.update/tmp.tar.bz2"

static constexpr unsigned char to_lower(unsigned char c)
{
    return tolower(c);
}

fs::FSUpdate::FSUpdate(const shared_ptr<logger::LoggerHandler> &ptr)
    : uboot_handler(make_shared<UBoot::UBoot>(UBOOT_CONFIG_PATH)), logger(ptr),
      update_handler(uboot_handler, logger), work_dir(TEMP_ADU_WORK_DIR),
      work_dir_perms(filesystem::perms::owner_read | filesystem::perms::owner_write |
                     filesystem::perms::group_read | filesystem::perms::group_write |
                     filesystem::perms::others_read | filesystem::perms::others_write )
{
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "fsupdate: construct", logger::logLevel::DEBUG));
}

fs::FSUpdate::~FSUpdate()
{
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "fsupdate: deconstruct", logger::logLevel::DEBUG));
}

bool fs::FSUpdate::create_work_dir()
{
    string msg = work_dir;

    if (filesystem::exists(work_dir))
    {
        msg += " does exist.";
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, (const string)msg , logger::logLevel::DEBUG));
        return false;
    }

    try
    {
        filesystem::create_directory(work_dir);
        filesystem::permissions(work_dir, work_dir_perms, filesystem::perm_options::replace);
    }
    catch (filesystem::filesystem_error const& ex)
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, ex.what(), logger::logLevel::DEBUG));
        throw GenericException(ex.code().message(), ex.code().value());
    }
    msg += " exists.";
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, msg , logger::logLevel::DEBUG));
    return true;
}

filesystem::path fs::FSUpdate::get_work_dir()
{
    return this->work_dir;
}

void fs::FSUpdate::decorator_update_state(function<void()> func)
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

void fs::FSUpdate::update_firmware(const string & path_to_firmware)
{
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);

    function<void()> update_firmware = [&](){
        vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
        update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS, true)) = '1';

        this->uboot_handler->addVariable("update", string(update.begin(), update.end()));
        this->uboot_handler->flushEnvironment();

        try
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "update_firmware: start firmware update", logger::logLevel::DEBUG));
            update_fw.install(path_to_firmware);
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
            );
            this->uboot_handler->flushEnvironment();
        }
        catch(const exception& e)
        {
            const string msg = "update_firmware: firmware exception: " + string(e.what());
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, msg, logger::logLevel::ERROR));
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
            );
            this->uboot_handler->flushEnvironment();
            throw;
        }
    };

    this->decorator_update_state(update_firmware);
}

void fs::FSUpdate::update_application(const string & path_to_application)
{
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);
    this->tmp_app_path = update_app.getTempAppPath();
    function<void()> update_application = [&](){

        vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
        update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP, true)) = '1';
        this->uboot_handler->addVariable("update", string(update.begin(), update.end()));
        this->uboot_handler->flushEnvironment();

        try
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "update_application: start application update", logger::logLevel::DEBUG));
            update_app.install(path_to_application);
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE)
            );
            this->uboot_handler->flushEnvironment();
        }
        catch(const exception & e)
        {
            const string msg = "application exception: " + string(e.what());
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, msg, logger::logLevel::ERROR));
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE)
            );
            this->uboot_handler->flushEnvironment();
            throw;
        }
    };

    this->decorator_update_state(update_application);
}

void fs::FSUpdate::update_firmware_and_application(const string & path_to_firmware,
                                                   const string & path_to_application)
{
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);

    this->tmp_app_path = update_app.getTempAppPath();
    vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));

    function<void()> update_firmware_and_application = [&](){
        try
        {
            update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS, true)) = '1';
            this->uboot_handler->addVariable("update", string(update.begin(), update.end()));
            this->uboot_handler->flushEnvironment();

            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "update_firmware_and_application: start firmware update", logger::logLevel::DEBUG));
            update_fw.install(path_to_firmware);
        }
        catch(const exception& e)
        {
            this->uboot_handler->freeVariables();
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
            );
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, string("update_firmware_and_application: error during firmware update"), logger::logLevel::ERROR));
            throw;
        }

        try
        {
            update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP, true)) = '1';
            this->uboot_handler->addVariable("update", string(update.begin(), update.end()));
            this->uboot_handler->flushEnvironment();
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "update_firmware_and_application: start application update", logger::logLevel::DEBUG));
            update_app.install(path_to_application);

            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE)
            );
            this->uboot_handler->flushEnvironment();
        }
        catch(const exception& e)
        {
            update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS, true)) = '0';
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE)
            );
            const string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD");
            this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
            const string msg = string("update_firmware_and_application: error during application update") + string(e.what());
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, msg, logger::logLevel::ERROR));
            this->uboot_handler->addVariable("update", string(update.begin(), update.end()));
            this->uboot_handler->flushEnvironment();
            throw;
        }
    };

    this->decorator_update_state(update_firmware_and_application);
}

void fs::FSUpdate::update_image(string &path_to_update_image, string &update_type, uint8_t &installed_update_type)
{
    UpdateStore update_store;
    filesystem::path target_archiv_dir(TARGET_ARCHIV_DIR_PATH);
    filesystem::path updateInstalled_path(work_dir / "updateInstalled");
    bool use_common_update = false;

    /* create temporary directory to extract and install update file */
    try
    {
        filesystem::create_directories(target_archiv_dir);
        filesystem::permissions(target_archiv_dir,
                                     (filesystem::perms::owner_read | filesystem::perms::owner_write |
                                      filesystem::perms::group_read | filesystem::perms::others_read),
                                     filesystem::perm_options::replace);
    }
    catch (filesystem::filesystem_error const &ex)
    {
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, ex.what(), logger::logLevel::DEBUG));
        throw GenericException(ex.what(), ex.code().value());
    }

    if (update_type.empty())
    {
        use_common_update = true;
    }

    /* check for update_type */
    if (use_common_update == true)
    {
        /* uptate type is empty so use common update functionality */
        /* extract update image */
        update_store.ExtractUpdateStore(path_to_update_image);
        /* read and parse fsupdate.json */
        update_store.ReadUpdateConfiguration((target_archiv_dir / "fsupdate.json"));
        /* read fw and/or application hashes from update configuration and compare it
         * calculated.
         */
        if (!update_store.CheckUpdateSha256Sum(target_archiv_dir))
        {
            try
            {
                /* remove arch directory */
                filesystem::remove_all(target_archiv_dir);
            }
            catch (filesystem::filesystem_error const &ex)
            {
                this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, ex.what(), logger::logLevel::DEBUG));
                throw GenericException(ex.what(), ex.code().value());
            }
            string output = "Checksum calculation " + target_archiv_dir.string() + " fails.";
            throw GenericException(output.c_str(), errno);
        }
    }
    else
    {
        /* Update_type is defined.  */
        if (update_type.compare("app") == 0)
        {
            update_store.SetApplicationAvailable(true);
        }
        else if (update_type.compare("fw") == 0)
        {
            update_store.SetFirmwareAvailable(true);
        }
    }

    /* Check update for firmware, application or both */
    if (update_store.IsApplicationAvailable() && update_store.IsFirmwareAvailable())
    {
        this->update_firmware_and_application((target_archiv_dir / update_store.getFirmwareStoreName()),
                                              (target_archiv_dir / update_store.getApplicationStoreName()));

        this->create_work_dir();
        ofstream installed(updateInstalled_path);
        if (!installed.is_open())
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN,
                                                       string("update_image: Create file for state update installed fails."),
                                                       logger::logLevel::ERROR));
            /* errno: Operation not permitted */
            string output = "Can not create " + updateInstalled_path.string();
            throw GenericException(output.c_str(), ENOENT);
        }
        else
        {
            filesystem::permissions(updateInstalled_path,
                                         (filesystem::perms::owner_read | filesystem::perms::group_read |
                                          filesystem::perms::others_read),
                                         filesystem::perm_options::replace);
            installed.close();
        }
        /* firmware and application update */
        /* static_cast<int>(UPDATER_FIRMWARE_AND_APPLICATION_STATE::UPDATE_SUCCESSFUL); */
        installed_update_type = 3;
    }
    else if (update_store.IsFirmwareAvailable())
    {
        if(use_common_update == true)
        {
            this->update_firmware((target_archiv_dir / update_store.getFirmwareStoreName()));
        }
        else
        {
            this->update_firmware(path_to_update_image);
        }
        this->create_work_dir();
        ofstream installed(updateInstalled_path);
        if (!installed.is_open())
        {
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN,
                                                       string("Create file for state firmware installed fails."),
                                                       logger::logLevel::ERROR));
            string output = "Can not create " + updateInstalled_path.string();
            throw GenericException(output.c_str(), ENOENT);
        }
        else
        {
            filesystem::permissions(updateInstalled_path,
                                         filesystem::perms::owner_read | filesystem::perms::group_read |
                                             filesystem::perms::others_read,
                                         filesystem::perm_options::replace);
            installed.close();
        }
        /* firmware  update */
        /* static_cast<int>(UPDATER_FIRMWARE_STATE::UPDATE_SUCCESSFUL) */
        installed_update_type = 1;
    }
    else if (update_store.IsApplicationAvailable())
    {
        if(use_common_update == true)
        {
            this->update_application((target_archiv_dir / update_store.getApplicationStoreName()));
        }
        else
        {
            this->update_application(path_to_update_image);
        }
        this->create_work_dir();
        ofstream installed(updateInstalled_path);
        if (!installed.is_open())
        {
            const string msg = "Create file for state application installed fails.";
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, msg, logger::logLevel::ERROR));
            string output = "Can not create " + updateInstalled_path.string();
            throw GenericException(output.c_str(), ENOENT);
        }
        else
        {
            filesystem::permissions(updateInstalled_path,
                                         filesystem::perms::owner_read | filesystem::perms::group_read |
                                             filesystem::perms::others_read,
                                         filesystem::perm_options::replace);
            installed.close();
        }
        /* application update */
        /* static_cast<int>(UPDATER_APPLICATION_STATE::UPDATE_SUCCESSFUL) */
        installed_update_type = 2;
    }
    else
    {
        /* errno: Operation not permitted */
        string msg = "update_image: Invalid update: " + path_to_update_image;
        throw GenericException(msg, EPERM);
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

update_definitions::UBootBootstateFlags fs::FSUpdate::get_update_reboot_state()
{
    const uint8_t update_reboot_state = this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables);
    const string msg = "update_reboot_state: " + to_string(update_reboot_state);
    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, msg, logger::logLevel::DEBUG));
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
        this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, string("rollback_firmware: Start rollback."),
                                                   logger::logLevel::DEBUG));
        /* Check for pending firmware update. This is rollback from
         *  uncommited state of the firmware.
         */
        bool app_fw_update_pending = this->update_handler.pendingApplicationFirmwareUpdate();
        if (this->update_handler.pendingFirmwareUpdate() || app_fw_update_pending == true)
        {
            this->logger->setLogEntry(logger::LogEntry(
                FSUPDATE_DOMAIN, string("rollback_firmware: Proceed rollback."), logger::logLevel::DEBUG));
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
                FSUPDATE_DOMAIN, string("rollback_firmware: Finish rollback."), logger::logLevel::DEBUG));
        }
        else
        {
            update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
                this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));
            if (this->update_handler.pendingUpdateRollback(update_reboot_state) == true)
            {
                this->logger->setLogEntry(logger::LogEntry(
                    FSUPDATE_DOMAIN, string("rollback_firmware: Stop rollback."), logger::logLevel::DEBUG));
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
                    BOOTSTATE_DOMAIN, string("rollback_firmware: Start rollback."), logger::logLevel::DEBUG));
                size_t fw_index = FIRMWARE_A_INDEX;
                int next_update_state = 0;
                string s("rollback_firmware: ");
                const string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
                const string current_slot = util::split(rauc_cmd, '=').back();

                vector<uint8_t> update =
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
                    if (current_slot == "B")
                    {
                        s += "A";
                    }
                    else
                    {
                        s += "B";
                    }
                    s += " requred.";
                    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, s, logger::logLevel::WARNING));
                    this->logger->setLogEntry(logger::LogEntry(
                        BOOTSTATE_DOMAIN, string("rollback_firmware: Stop rollback."), logger::logLevel::DEBUG));
                    throw(GenericException("Firmware rollback is not allowed.", ECANCELED));
                }
                else if ((next_update_state & STATE_UPDATE_BAD) == STATE_UPDATE_BAD)
                {
                    s += "fails FW_";
                    if (current_slot == "B")
                    {
                        s += "A";
                    }
                    else
                    {
                        s += "B";
                    }
                    s += " state is bad.";
                    /* firmware rollback is't possible because other state is bad. */
                    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, s, logger::logLevel::WARNING));
                    this->logger->setLogEntry(logger::LogEntry(
                        BOOTSTATE_DOMAIN, string("rollback_firmware: Stop rollback."), logger::logLevel::DEBUG));
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
                    BOOTSTATE_DOMAIN, string("rollback_firmware: Finish rollback."), logger::logLevel::DEBUG));
            }
        }
    }
    catch (const updater::RollbackFirmwareUpdate &e)
    {
        throw_with_nested(GenericException(e.what()));
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
                FSUPDATE_DOMAIN, string("rollback_application: Proceed rollback"), logger::logLevel::DEBUG));
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
                    FSUPDATE_DOMAIN, string("rollback_application: Stop rollback."), logger::logLevel::DEBUG));
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
                    BOOTSTATE_DOMAIN, string("rollback_application: commited app -> start rollback "),
                    logger::logLevel::DEBUG));
                /* get currect application state */
                const char current_app = this->uboot_handler->getVariable("application", allowed_application_variables);
                vector<uint8_t> update =
                    util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
                size_t app_index = APPLICATION_A_INDEX;
                int current_update_state = 0;
                string s("rollback_application: ");

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
        throw_with_nested(GenericException(e.what()));
    }
}

int fs::FSUpdate::set_update_state_bad(const char &state, uint32_t update_id)
{
    int current_state = 0;
    size_t update_index;
    string out_string;

    /* check passing parameter */
    if ((state != 'a' && state != 'A' && state != 'b' && state != 'B') || (update_id >= 2))
        return EINVAL;
    /* get update state */
    vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));

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
        this->uboot_handler->addVariable("update", string(update.begin(), update.end()));
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
    string out_string;
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, string("application state: set application state bad "), logger::logLevel::DEBUG));

    /* get update state */
    vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));

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

filesystem::path & fs::FSUpdate::getTempAppPath()
{
    return this->tmp_app_path;
}

fs::UpdateStore::UpdateStore()
{
    this->app_available = false;
    this->fw_available = false;
}

void fs::UpdateStore::ReadUpdateConfiguration(const string configuration_path)
{
    Json::CharReaderBuilder builder;
    string errs;
    /* create stream for update configuration file */
    ifstream update_configuration(configuration_path, ifstream::in);
    /* check if the stream good is and no error flags are set */
    if (!update_configuration.good())
    {
        if (update_configuration.bad() || update_configuration.fail())
        {
            throw GenericException(configuration_path, ENOENT);
        }
    }
    /* parse */
    if (!Json::parseFromStream(builder, update_configuration, &root, &errs))
    {
        update_configuration.close();
        string fails("Parsing of update configuration fails.");
        throw GenericException(fails, ENOENT);
    }
    update_configuration.close();
}

bool fs::UpdateStore::CheckUpdateSha256Sum(const filesystem::path & path_to_update_image)
{
    if(root.isMember("images") && !root["images"].empty())
    {
        Json::Value images = root["images"];
        if(images.isMember("updates") && !images["updates"].empty())
        {
            Json::Value updates = images["updates"];
            string sha256_str;
            string update_image_file;

            for(Json::Value::iterator counter = updates.begin(); counter != updates.end(); ++counter)
            {
                if(!(*counter).isMember("version") || !(*counter).isMember("handler")
                || !(*counter).isMember("file") || !(*counter).isMember("hashes"))
                {
                    /* wrong format nodes needed */
                    this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, "Nodes version, handler, files or hashes not available.", 
                    logger::logLevel::DEBUG));
                    errno = ENOENT;
                    return false;
                }

                update_image_file = (*counter)["file"].asString();
                sha256_str = (*counter)["hashes"]["sha256"].asString();
                /* transform to low for hash compare */
                transform(sha256_str.begin(), sha256_str.end(), sha256_str.begin(), to_lower);
                filesystem::path image_full_path(path_to_update_image / update_image_file);

                string calc_hash = CalculateCheckSum(image_full_path, string("SHA-256"));
                if(calc_hash != sha256_str)
                {
                    cerr << "Hash compare of " << image_full_path <<"fails." << endl;
                    return false;
                }

                if(update_image_file.compare(app_store_name) == 0)
                {
                    this->SetApplicationAvailable(true);
                } else if(update_image_file.compare(fw_store_name) == 0)
                {
                    this->SetFirmwareAvailable(true);
                } else
                {
                    cerr << "Image " << update_image_file << " is not supported." << endl;
                    errno = EINVAL;
                    return false;
                }
            }
        }
        else
        {
            /* wrong json file format. node updates needed..*/
            const string fails = "Node updates is not available or empty.";
            this->logger->setLogEntry(logger::LogEntry(FSUPDATE_DOMAIN, fails, logger::logLevel::DEBUG));
            errno = EINVAL;
            return false;
        }
    }
    else
    {
        /* wrong json file format. node images needed..*/
        const string fails = "Node images not available or empty.";
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, fails, logger::logLevel::DEBUG));
        errno = EINVAL;
        return false;
    }

    return true;
}

void fs::UpdateStore::ExtractUpdateStore(const filesystem::path & path_to_update_image)
{
    unique_ptr<struct fs_header_v1_0> fsheader10 = make_unique<struct fs_header_v1_0>();
    ifstream update_img(path_to_update_image, (ifstream::in | ifstream::binary));
    string target_update_store = TARGET_ARCHIVE_UPDATE_STORE;
    string update_image_file = path_to_update_image;
    // open file
    if (!update_img.good())
    {
        if (update_img.bad() || update_img.fail())
        {
            string error_str = string("Open file ") + update_image_file + string("fails");
            throw GenericException(update_image_file, -EACCES);
        }
    }

    update_img.read((char *)fsheader10.get(), sizeof(struct fs_header_v1_0));

    uint64_t file_size = 0;
    file_size = fsheader10->info.file_size_high & 0xFFFFFFFF;
    file_size = file_size << 32;
    file_size = file_size | (fsheader10->info.file_size_low & 0xFFFFFFFF);

    if (!strcmp("CERT", fsheader10->type) && (file_size > 0))
    {
        ofstream archive_store(target_update_store, (ofstream::out | ofstream::binary));
        if (!archive_store.good())
        {
            if (archive_store.bad() || archive_store.fail())
            {
                string error_str = string("Open file ") + target_update_store + string("fails");
                throw GenericException(target_update_store, -ENOENT);
            }
        }
        archive_store << update_img.rdbuf();
        archive_store.close();
        update_img.close();
    }
    else
    {
        update_img.close();
        throw GenericException(string("Update has wrong format") , -ENOENT);
    }

    string cmd = uncompress_cmd_source_archive;
    cmd += target_update_store;
    cmd += uncompress_cmd_dest_folder;
    cmd += string(TARGET_ARCHIV_DIR_PATH);
    /* extract files from update archive to  TARGET_ARCHIV_DIR_PATH directory */
    const int ret = ::system(cmd.c_str());
    if ((ret == -1) || (ret == 127))
    {
        string error_str = string("Extract file ") + target_update_store + string("fails");
        throw GenericException(target_update_store, ret);
    }
    remove(target_update_store.c_str());
}

string fs::UpdateStore::CalculateCheckSum(const filesystem::path& filepath, const string& algorithm)
{
    try
    {
        ifstream file(filepath, ios::binary);
        if (!file)
        {
            throw GenericException("Open file " + filepath.string() + " fails.", errno);
        }
        auto hash = Botan::HashFunction::create(algorithm);
        vector<char> buffer(BUFFER_SIZE);
        while (file.read(buffer.data(), buffer.size()))
        {
            hash->update(reinterpret_cast<const uint8_t *>(buffer.data()), file.gcount());
        }
        hash->update(reinterpret_cast<const uint8_t *>(buffer.data()), file.gcount());
        vector<uint8_t> output(hash->output_length());
        hash->final(output.data());
        file.close();
        string hashstr = Botan::hex_encode(output);
        /* transform to low for compare */
        transform(hashstr.begin(), hashstr.end(), hashstr.begin(), to_lower);
        return hashstr;
    }
    catch(const exception& ex)
    {
        throw GenericException(string(ex.what()), errno);
    }
}
