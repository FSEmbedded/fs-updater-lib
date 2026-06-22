#include "fsupdate.h"

#include "updateFirmware.h"
#include "updateApplication.h"
#include "UpdateStore.h"
#include "UpdateContainerReader.h" // v2.0 streaming reader
#include "sources/UpdateSourceRegistry.h" // make_update_source — format-detecting front door
#include "sources/UpdateSource.h"         // StagingContext, UpdateArtifacts
#include "UpdateStreamSink.h"     // FileSink for v2.0 member extraction
#include "progress_remap.h"       // remap_extract_progress() — unit-tested
#include "fs_consts.h"            // FSUPDATE_DOMAIN, DEFAULT_RAUC_SCRATCH_PATH
#ifdef BUILD_RAUC_SCRATCH_OVERRIDE
#include "scratch_path.h"         // resolve_scratch_dir() — only needed when override is integrated
#endif
#include "fs_header_types.h"      // fs_header_v1_0 + detect_format_version
#include "utils.h"
#include <fstream>
#include "../uboot_interface/allowed_uboot_variable_states.h"
#include <botan/hash.h>
#include <botan/hex.h>
#include <iostream>  /* cout */
#include <algorithm> /* transform */
#include <cctype>    /* tolower */
#include <sys/stat.h>
#include <errno.h>

using namespace std;

fs::FSUpdate::FSUpdate(const shared_ptr<logger::LoggerHandler> &ptr)
    : uboot_handler(make_shared<UBoot::UBoot>(UBOOT_CONFIG_PATH)), logger(ptr),
      update_handler(uboot_handler, logger), work_dir(TEMP_ADU_WORK_DIR),
      /* Mode 0777 for the ADU work directory: read/write/traverse for
       * everyone. The dir holds inter-process marker files between
       * the CLI, the ADU handler, and the service; all three may run
       * as different effective users. Execute bits are required on
       * directories for path traversal (open() of files inside). */
      work_dir_perms(filesystem::perms::owner_read  | filesystem::perms::owner_write |
                     filesystem::perms::owner_exec  |
                     filesystem::perms::group_read  | filesystem::perms::group_write |
                     filesystem::perms::group_exec  |
                     filesystem::perms::others_read | filesystem::perms::others_write |
                     filesystem::perms::others_exec)
{
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "fsupdate: construct", logger::logLevel::DEBUG));
}

fs::FSUpdate::~FSUpdate()
{
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "fsupdate: deconstruct", logger::logLevel::DEBUG));
}

bool fs::FSUpdate::create_work_dir()
{
    string msg = work_dir;

    if (filesystem::exists(work_dir))
    {
        msg += " does exist.";
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, (const string)msg, logger::logLevel::DEBUG));
        return false;
    }

    try
    {
        filesystem::create_directories(work_dir);
        filesystem::permissions(work_dir, work_dir_perms, filesystem::perm_options::replace);
    }
    catch (filesystem::filesystem_error const &ex)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, ex.what(), logger::logLevel::DEBUG));
        throw GenericException(ex.code().message(), ex.code().value());
    }
    msg += " exists.";
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, msg, logger::logLevel::DEBUG));
    return true;
}

filesystem::path fs::FSUpdate::get_work_dir()
{
    return this->work_dir;
}

void fs::FSUpdate::setInstallProgressCallback(updater::ProgressCb callback)
{
    install_progress_cb_ = std::move(callback);
}

void fs::FSUpdate::decorator_update_state(function<void()> func)
{
    if (this->update_handler.noUpdateProcessing())
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "decorator_update_state: no update in progress pending", logger::logLevel::DEBUG));
        func();
    }
    else if (this->update_handler.failedFirmwareUpdate())
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "decorator_update_state: failed firmware update pending", logger::logLevel::ERROR));
        throw(UpdateInProgress("Failed firmware update is uncommited"));
    }
    else if (this->update_handler.failedApplicationUpdate())
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "decorator_update_state: failed application update pending", logger::logLevel::ERROR));
        throw(UpdateInProgress("Failed application update is uncommited"));
    }
    else if (this->update_handler.pendingFirmwareUpdate())
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "decorator_update_state: firmware update pending", logger::logLevel::ERROR));
        throw(UpdateInProgress("Pending firmware update is not commited"));
    }
    else if (this->update_handler.pendingApplicationUpdate())
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "decorator_update_state: application update pending", logger::logLevel::ERROR));
        throw(UpdateInProgress("Pending application update is not commited"));
    }
    else if (this->update_handler.pendingApplicationFirmwareUpdate())
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "decorator_update_state: application & firmware update pending", logger::logLevel::ERROR));
        throw(UpdateInProgress("Pending application & firmware update is not commited"));
    }
    else
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "decorator_update_state: Unknown update state: HEAVY PROGRAMMING ERROR", logger::logLevel::ERROR));
        throw(UpdateInProgress("Unknown state of update process"));
    }
}

void fs::FSUpdate::update_firmware(const string &path_to_firmware)
{
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);
    update_fw.setProgressCallback(install_progress_cb_);

    function<void()> update_firmware = [&](){
        {
            UBoot::UBoot::EnvTransaction txn(*this->uboot_handler);
            vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
            update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS, true)) = '1';

            this->uboot_handler->addVariable("update", string(update.begin(), update.end()));
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
            );
            this->uboot_handler->flushEnvironment();
        }

        try
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "update_firmware: start firmware update", logger::logLevel::DEBUG));
            update_fw.install(path_to_firmware);
        }
        catch (const exception &e)
        {
            const string msg = "update_firmware: firmware exception: " + string(e.what());
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, msg, logger::logLevel::ERROR));
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
            );
            this->uboot_handler->flushEnvironment();
            throw;
        }
    };

    this->decorator_update_state(update_firmware);
}

void fs::FSUpdate::update_application(const string &path_to_application)
{
    auto update_app = std::make_shared<updater::applicationUpdate>(this->uboot_handler, this->logger);
    update_app->setProgressCallback(install_progress_cb_);
    this->tmp_app_path = update_app->getTempAppPath();

    function<void()> update_application = [this, update_app, path_to_application]() {
        {
            UBoot::UBoot::EnvTransaction txn(*this->uboot_handler);
            vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
            update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP, true)) = '1';
            this->uboot_handler->addVariable("update", string(update.begin(), update.end()));
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE));
            this->uboot_handler->flushEnvironment();
        }

        try {
            update_app->install(path_to_application);
        }
        catch (const exception &e)
        {
            const string msg = "application exception: " + string(e.what());
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, msg, logger::logLevel::ERROR));
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE));
            this->uboot_handler->flushEnvironment();
            throw;
        }
    };
    this->decorator_update_state(update_application);
}

void fs::FSUpdate::update_firmware_and_application(const string &path_to_firmware,
                                                   const string &path_to_application)
{
    updater::applicationUpdate update_app(this->uboot_handler, this->logger);
    updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);

    if (install_progress_cb_) {
        update_fw.setProgressCallback([this](int p){ install_progress_cb_(p / 2); });
        update_app.setProgressCallback([this](int p){ install_progress_cb_(50 + p / 2); });
    }

    this->tmp_app_path = update_app.getTempAppPath();
    vector<uint8_t> update;

    function<void()> update_firmware_and_application = [&](){
        try
        {
            {
                UBoot::UBoot::EnvTransaction txn(*this->uboot_handler);
                update = util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
                update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS, true)) = '1';
                this->uboot_handler->addVariable("update", string(update.begin(), update.end()));
                this->uboot_handler->addVariable("update_reboot_state",
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
                );
                this->uboot_handler->flushEnvironment();
            }

            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "update_firmware_and_application: start firmware update", logger::logLevel::DEBUG));
            update_fw.install(path_to_firmware);
        }
        catch (const exception &e)
        {
            this->uboot_handler->freeVariables();
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
            );
            this->uboot_handler->flushEnvironment();
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, string("update_firmware_and_application: error during firmware update"), logger::logLevel::ERROR));
            throw;
        }

        try
        {
            {
                UBoot::UBoot::EnvTransaction txn(*this->uboot_handler);
                update.at(this->update_handler.get_update_bit(update_definitions::Flags::APP, true)) = '1';
                this->uboot_handler->addVariable("update", string(update.begin(), update.end()));
                this->uboot_handler->addVariable("update_reboot_state",
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE)
                );
                this->uboot_handler->flushEnvironment();
            }
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "update_firmware_and_application: start application update", logger::logLevel::DEBUG));
            update_app.install(path_to_application);
        }
        catch (const exception &e)
        {
            UBoot::UBoot::EnvTransaction txn(*this->uboot_handler);
            update.at(this->update_handler.get_update_bit(update_definitions::Flags::OS, true)) = '0';
            this->uboot_handler->addVariable("update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE)
            );
            const string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD");
            this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
            const string msg = string("update_firmware_and_application: error during application update") + string(e.what());
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, msg, logger::logLevel::ERROR));
            this->uboot_handler->addVariable("update", string(update.begin(), update.end()));
            this->uboot_handler->flushEnvironment();
            throw;
        }
    };

    this->decorator_update_state(update_firmware_and_application);
}

void fs::FSUpdate::update_image(string &path_to_update_image,
                                string &update_type,
                                uint8_t &installed_update_type,
                                [[maybe_unused]] const std::string &rauc_scratch_path)
{
    UpdateStore update_store;

    // v2.0 stages members on persistent storage (the parent dir of the
    // configured RAUC scratch path). This branch supports v2.0 containers
    // only; the legacy v1.0 tar.bz2 reader was removed. Build-time toggle:
    // with -DBUILD_RAUC_SCRATCH_OVERRIDE=ON (default) the per-call
    // rauc_scratch_path arg can redirect the staging dir; with =OFF no
    // override integration is compiled in and the compile-time
    // DEFAULT_RAUC_SCRATCH_PATH is used directly.
#ifdef BUILD_RAUC_SCRATCH_OVERRIDE
    filesystem::path target_archiv_dir = fs::resolve_scratch_dir(rauc_scratch_path);
#else
    filesystem::path target_archiv_dir =
        filesystem::path(DEFAULT_RAUC_SCRATCH_PATH).parent_path();
#endif
    filesystem::path updateInstalled_path(work_dir / "updateInstalled");
    bool use_common_update = false;

    /* Reserve EXTRACT_PCT of the progress bar for the v2.0 extract phase
     * and remap downstream dispatch's 0..100 to EXTRACT_PCT..100.
     * The restorer covers the whole function so the original callback is
     * put back even if dispatch throws. Inactive when update_type is set
     * (no extract phase) or when no callback was registered. */
    constexpr int EXTRACT_PCT = 20;

    struct ProgressCbRestorer
    {
        FSUpdate*           self;
        updater::ProgressCb saved;
        bool                active{false};
        ~ProgressCbRestorer() { if (active) self->install_progress_cb_ = std::move(saved); }
    } cb_restorer{this, install_progress_cb_, false};

    /* create persistent staging directory for v2.0 member extraction.
     * Mode 0755: owner rwx, group/others r-x. The execute bits are
     * required on directories for traversal — without them callers
     * (including RAUC, which reads update.fw from this dir) can't
     * open files inside even with read permission on the dir itself. */
    try
    {
        filesystem::create_directories(target_archiv_dir);
        filesystem::permissions(target_archiv_dir,
                                (filesystem::perms::owner_read  | filesystem::perms::owner_write |
                                 filesystem::perms::owner_exec  |
                                 filesystem::perms::group_read  | filesystem::perms::group_exec |
                                 filesystem::perms::others_read | filesystem::perms::others_exec),
                                filesystem::perm_options::replace);

        /* Free the prior install's member files BEFORE extracting the
         * new bundle. On tight /rw_fs partitions (~480 MB) a 152 MB
         * cached `update.fw` from the previous cycle plus the new
         * 152 MB `update.fw.tmp` exceeds the free space and the
         * extract hits ENOSPC mid-stream (caught cleanly by FileSink
         * now, but the install still fails). Removing the prior
         * artifacts gives the new extract the headroom it needs.
         * Best-effort: log on failure, don't throw — the FileSink
         * write itself will report any remaining space issue. Also
         * sweeps stale .tmp leftovers from a crashed prior run. */
        for (const auto& name : { update_store.getFirmwareStoreName(),
                                  update_store.getApplicationStoreName() })
        {
            for (const std::string& suffix : { std::string{}, std::string{".tmp"} })
            {
                const filesystem::path victim = target_archiv_dir / (name + suffix);
                std::error_code ec;
                if (filesystem::remove(victim, ec) && !ec) {
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                        FSUPDATE_DOMAIN,
                        "pre-extract cleanup: removed " + victim.string(),
                        logger::logLevel::DEBUG));
                } else if (ec) {
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                        FSUPDATE_DOMAIN,
                        "pre-extract cleanup: " + victim.string() + ": " + ec.message(),
                        logger::logLevel::WARNING));
                }
            }
        }
    }
    catch (filesystem::filesystem_error const &ex)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, ex.what(), logger::logLevel::DEBUG));
        throw GenericException(ex.what(), ex.code().value());
    }

    if (update_type.empty())
    {
        use_common_update = true;
    }

    /* Resolved payloads for the engine dispatch. Populated either by the
     * UpdateSource (container / raw bundle) or, for an explicit update_type,
     * directly from the original path. */
    UpdateArtifacts artifacts;

    /* check for update_type */
    if (use_common_update == true)
    {
        /* Detect the input format and resolve its payloads into
         * target_archiv_dir via the UpdateSource layer. Non-container
         * inputs are rejected here (UnknownUpdateFormat /
         * UpdateFormatNotSupported, both deriving from GenericException);
         * the v1.0 fallback stays removed. Inline SHA-256 verification
         * still happens inside the container source's extract. The
         * downstream dispatch below (update_firmware / update_application /
         * combined) is unchanged and reads the staged update.fw/update.app. */

        /* The first 0-tick bootstraps the bar; the source's per-chunk hook
         * fires byte-weighted intermediates across the EXTRACT_PCT band as
         * each member streams. */
        if (install_progress_cb_) install_progress_cb_(0);

        StagingContext staging_ctx;
        staging_ctx.staging_dir = target_archiv_dir;
        if (install_progress_cb_)
        {
            staging_ctx.on_progress =
                [this](std::uint64_t bytes_done, std::uint64_t bytes_total) {
                    if (bytes_total > 0)
                    {
                        install_progress_cb_(
                            static_cast<int>(bytes_done * EXTRACT_PCT / bytes_total));
                    }
                };
        }

        const auto source = make_update_source(path_to_update_image);
        artifacts = source->prepare(staging_ctx);

        /* Remap downstream dispatch's 0..100 emissions to EXTRACT_PCT..100
         * for the remainder of update_image(). cb_restorer (declared above
         * at function scope) puts the original back on exit, including the
         * exception path. */
        if (cb_restorer.saved)
        {
            auto saved = cb_restorer.saved;
            install_progress_cb_ = [saved](int p) {
                saved(remap_extract_progress(p, EXTRACT_PCT));
            };
            cb_restorer.active = true;
        }
    }
    else
    {
        /* update_type is defined: the raw single payload is installed
         * directly from its original path (no staging). */
        if (update_type.compare("app") == 0)
        {
            artifacts.application = path_to_update_image;
        }
        else if (update_type.compare("fw") == 0)
        {
            artifacts.firmware = path_to_update_image;
        }
    }

    /* Dispatch on the resolved artifacts. classify_dispatch throws
     * GenericException(EPERM) for the empty set; for the container the
     * artifact paths are the staged update.fw/update.app, for a raw
     * payload the original path. */
    switch (classify_dispatch(artifacts))
    {
    case DispatchKind::FirmwareAndApplication:
        this->update_firmware_and_application(artifacts.firmware.value(),
                                              artifacts.application.value());

        /* firmware and application update */
        installed_update_type = 3;
#if !BUILD_DBUS_SUPPORT
        /* Legacy signal-file for the non-D-Bus build only. D-Bus
         * subscribers use InstallCompleted + InstallState instead;
         * see fs-updater-cli/src/cli/cli.cpp #else branches around
         * lines 1095 / 1153 for the file-watching consumers. */
        this->create_work_dir();
        {
            ofstream installed(updateInstalled_path);
            if (!installed.is_open())
            {
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN,
                                                           string("update_image: Create file for state update installed fails."),
                                                           logger::logLevel::ERROR));
                /* errno: Operation not permitted */
                string output = "Can not create " + updateInstalled_path.string();
                throw GenericException(output.c_str(), ENOENT);
            }
            filesystem::permissions(updateInstalled_path,
                                    (filesystem::perms::owner_read | filesystem::perms::group_read |
                                     filesystem::perms::others_read),
                                    filesystem::perm_options::replace);
            installed.close();
        }
#endif
        break;

    case DispatchKind::Firmware:
        this->update_firmware(artifacts.firmware.value());

        /* firmware  update */
        installed_update_type = 1;
#if !BUILD_DBUS_SUPPORT
        this->create_work_dir();
        {
            ofstream installed(updateInstalled_path);
            if (!installed.is_open())
            {
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN,
                                                           string("Create file for state firmware installed fails."),
                                                           logger::logLevel::ERROR));
                string output = "Can not create " + updateInstalled_path.string();
                throw GenericException(output.c_str(), ENOENT);
            }
            filesystem::permissions(updateInstalled_path,
                                    filesystem::perms::owner_read | filesystem::perms::group_read |
                                        filesystem::perms::others_read,
                                    filesystem::perm_options::replace);
            installed.close();
        }
#endif
        break;

    case DispatchKind::Application:
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "update_image: application update", logger::logLevel::DEBUG));
        this->update_application(artifacts.application.value());

        /* application update */
        installed_update_type = 2;
#if !BUILD_DBUS_SUPPORT
        this->create_work_dir();
        {
            ofstream installed(updateInstalled_path);
            if (!installed.is_open())
            {
                const string msg = "Create file for state application installed fails.";
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, msg, logger::logLevel::ERROR));
                string output = "Can not create " + updateInstalled_path.string();
                throw GenericException(output.c_str(), ENOENT);
            }
            filesystem::permissions(updateInstalled_path,
                                    filesystem::perms::owner_read | filesystem::perms::group_read |
                                        filesystem::perms::others_read,
                                    filesystem::perm_options::replace);
            installed.close();
        }
#endif
        break;
    }
}

bool fs::FSUpdate::commit_update()
{
    UBoot::UBoot::EnvTransaction txn(*this->uboot_handler);
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "commit_update: commit update", logger::logLevel::DEBUG));
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
        const string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
        const string current_slot = util::split(rauc_cmd, '=').back();
        const uint8_t boot_slot_left =
            this->uboot_handler->getVariable("BOOT_"+current_slot+"_LEFT", allowed_boot_ab_left_variables);

        if(boot_slot_left < 3)
        {
            this->uboot_handler->addVariable("BOOT_"+current_slot+"_LEFT", "3");
            retValue = true;
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "commit_update: mark-good, restored BOOT_" + current_slot + "_LEFT to 3", logger::logLevel::DEBUG));
        }
        else
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "commit_update: nothing to commit", logger::logLevel::DEBUG));
        }
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
                std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "commit_update: not allowed update state", logger::logLevel::ERROR));
            throw(NotAllowedUpdateState());
        }
    }

    this->uboot_handler->flushEnvironment();
    return retValue;
}

bool fs::FSUpdate::apply_pending_update()
{
    using Flags = update_definitions::UBootBootstateFlags;

    /* Read the durable truth from U-Boot env — not any in-memory mirror.
     * A power loss between install completion and Apply would leave the
     * service's notion of state stale, but the env is authoritative. */
    const Flags state = this->get_update_reboot_state();

    if (state == Flags::INCOMPLETE_FW_UPDATE ||
        state == Flags::INCOMPLETE_APP_FW_UPDATE)
    {
        /* Firmware involved: ask RAUC to swap the boot order toward the
         * inactive slot. Throws on D-Bus failure; nothing in U-Boot env
         * is touched on throw, so the caller can retry safely. */
        updater::firmwareUpdate update_fw(this->uboot_handler, this->logger);
        update_fw.markOtherPartition();
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            FSUPDATE_DOMAIN,
            "apply_pending_update: state=" +
                std::to_string(static_cast<unsigned>(state)) +
                " markOtherPartition OK; reboot required",
            logger::logLevel::DEBUG));
        return true;
    }

    if (state == Flags::INCOMPLETE_APP_UPDATE)
    {
        /* App-only: no RAUC slot swap (firmware slot unchanged) — but
         * the new application squashfs is selected at preinit time by
         * dynamic-overlay based on the `application` U-Boot variable
         * (set during install). Preinit only runs at boot, so a reboot
         * is still required for the new app to be mounted. */
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            FSUPDATE_DOMAIN,
            "apply_pending_update: app-only; reboot required for preinit "
            "to mount the new app squashfs",
            logger::logLevel::DEBUG));
        return true;
    }

    /* Any other state: no pending install to apply. State machine is
     * left untouched; surface the diagnostic to the caller. */
    throw ApplyUpdateInvalidState(static_cast<unsigned>(state));
}

update_definitions::UBootBootstateFlags fs::FSUpdate::get_update_reboot_state()
{
    const uint8_t update_reboot_state = this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables);
    const string msg = "update_reboot_state: " + to_string(update_reboot_state);
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, msg, logger::logLevel::DEBUG));
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
    UBoot::UBoot::EnvTransaction txn(*this->uboot_handler);
    try
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, string("rollback_firmware: Start rollback."),
                                                   logger::logLevel::DEBUG));
        /* Check for pending firmware update. This is rollback from
         *  uncommited state of the firmware.
         */
        bool app_fw_update_pending = this->update_handler.pendingApplicationFirmwareUpdate();
        if (this->update_handler.pendingFirmwareUpdate() || app_fw_update_pending == true)
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                FSUPDATE_DOMAIN, string("rollback_firmware: Proceed rollback."), logger::logLevel::DEBUG));
            this->update_handler.firmware_rollback();
            if (app_fw_update_pending == true)
            {
                /* rollback fw and application progress  */
                updater::applicationUpdate app_update(this->uboot_handler, this->logger);
                app_update.rollback();
                this->uboot_handler->addVariable(
                    "update_reboot_state",
                    update_definitions::to_string(
                        update_definitions::UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING));
            }
            this->uboot_handler->flushEnvironment();
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                FSUPDATE_DOMAIN, string("rollback_firmware: Finish rollback."), logger::logLevel::DEBUG));
        }
        else
        {
            update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
                this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));
            if (this->update_handler.pendingUpdateRollback(update_reboot_state) == true)
            {
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
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
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                    BOOTSTATE_DOMAIN, string("rollback_firmware: Start rollback."), logger::logLevel::DEBUG));
                size_t fw_index = FIRMWARE_A_INDEX;
                int next_update_state = 0;
                string s("rollback_firmware: ");
                const string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
                const string current_slot = util::split(rauc_cmd, '=').back();

                vector<uint8_t> update =
                    util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));

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

                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, s, logger::logLevel::DEBUG));

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
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, s, logger::logLevel::WARNING));
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
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
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, s, logger::logLevel::WARNING));
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                        BOOTSTATE_DOMAIN, string("rollback_firmware: Stop rollback."), logger::logLevel::DEBUG));
                    throw(GenericException("Firmware rollback is not allowed.", EPERM));
                }

                /* switch to other firmware state */
                if (current_slot == "A")
                {
                    this->uboot_handler->addVariable("BOOT_ORDER", "B A");
                    this->uboot_handler->addVariable("BOOT_ORDER_OLD", "A B");
                }
                else
                {
                    this->uboot_handler->addVariable("BOOT_ORDER", "A B");
                    this->uboot_handler->addVariable("BOOT_ORDER_OLD", "B A");
                }
                /* to switch reboot should be done */
                this->uboot_handler->addVariable(
                    "update_reboot_state",
                    update_definitions::to_string(update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING));
                this->uboot_handler->flushEnvironment();
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
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
    UBoot::UBoot::EnvTransaction txn(*this->uboot_handler);
    try
    {
        updater::applicationUpdate app_update(this->uboot_handler, this->logger);
        bool app_pendig = this->update_handler.pendingApplicationUpdate();
        if (app_pendig == true || this->update_handler.pendingApplicationFirmwareUpdate())
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                FSUPDATE_DOMAIN, string("rollback_application: Proceed rollback"), logger::logLevel::DEBUG));
            this->update_handler.applicaton_rollback(app_update);
            /* If application and firmware rollback pending don't change the update_reboot_state.
             *  Firwmare rollback must be done too.
             */
            if (app_pendig == true)
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
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
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
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                    BOOTSTATE_DOMAIN, string("rollback_application: commited app -> start rollback "),
                    logger::logLevel::DEBUG));
                /* get currect application state */
                const char current_app = this->uboot_handler->getVariable("application", allowed_application_variables);
                vector<uint8_t> update =
                    util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
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
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, s, logger::logLevel::DEBUG));

                s = "rollback_application: ";

                if ((current_update_state & STATE_UPDATE_UNCOMMITED) == STATE_UPDATE_UNCOMMITED)
                {
                    /* application rollback was executed before and is't possible */
                    s += "fails commit APP_";
                    s.push_back(current_app);
                    s += " requred.";
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, s, logger::logLevel::WARNING));
                    throw(GenericException("Application rollback is not allowed.", ECANCELED));
                }
                else if ((current_update_state & STATE_UPDATE_BAD) == STATE_UPDATE_BAD)
                {
                    s += "fails APP_";
                    s.push_back(current_app);
                    s += " state is bad.";
                    /* application rollback was executed before and is't possible */
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, s, logger::logLevel::WARNING));
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
    UBoot::UBoot::EnvTransaction txn(*this->uboot_handler);
    int current_state = 0;
    size_t update_index;
    string out_string;

    /* check passing parameter */
    if ((state != 'a' && state != 'A' && state != 'b' && state != 'B') || (update_id >= 2))
        return EINVAL;
    /* get update state */
    vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));

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
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, out_string, logger::logLevel::DEBUG));
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
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, out_string, logger::logLevel::DEBUG));

    return 0;
}

bool fs::FSUpdate::is_update_state_bad(const char &state, uint32_t update_id)
{
    bool ret_state = false;
    int current_state = 0;
    size_t update_index;
    string out_string;
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, string("application state: set application state bad "), logger::logLevel::DEBUG));

    /* get update state */
    vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));

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

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, out_string, logger::logLevel::DEBUG));
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

void fs::FSUpdate::update_reboot_state(update_definitions::UBootBootstateFlags flag)
{
    /* to switch reboot should be done */
    this->uboot_handler->addVariable(
        "update_reboot_state", update_definitions::to_string(
                                   flag));
    /* save to bootloader env. block */
    this->uboot_handler->flushEnvironment();
}

bool fs::FSUpdate::pendingUpdateRollback()
{
    UBoot::UBoot::EnvTransaction txn(*this->uboot_handler);
    update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));
    return this->update_handler.pendingUpdateRollback(update_reboot_state);
}

filesystem::path &fs::FSUpdate::getTempAppPath()
{
    return this->tmp_app_path;
}
