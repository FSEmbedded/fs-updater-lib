#pragma once

#include "updateDefinitions.h"

#include "../uboot_interface/UBoot.h"
#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "./../BaseException.h"
#include "handleUpdate.h"
#include "fs_exceptions.h"
#include "fs_consts.h"
#include <exception>
#include <string>
#include <memory>
#include <functional>

#include <json/json.h> /* json update configuration*/

/* if not defined in configuration file set to default value */
#ifndef TEMP_ADU_WORK_DIR
#define TEMP_ADU_WORK_DIR "/tmp/adu/.work"
#endif

/**
 * Extern interface for usage of F&S Update Framework.
 *
 * Offers the application and firmware update functionality.
 * Can read and commit the different states of update process.
 */
namespace fs
{
/**
 * Metadata read from a v2.0 update bundle by FSUpdate::inspect_bundle().
 *
 * Populated without committing to an install — opens the file, reads the
 * F&S header, parses the JSON descriptor, then closes. Used by the D-Bus
 * service to populate UpdateType/UpdateVersion/UpdateSize properties on
 * InstallLocal before the install worker starts, so subscribers see
 * meaningful values during the install.
 *
 * Field semantics:
 *  - valid       false → stat() failed (ENOENT, EACCES, …). Other fields
 *                are zero/empty and not meaningful.
 *                true  → stat() succeeded. The file exists and `size` is
 *                set; whether it is a v2.0 container is reported via
 *                `update_type` / `version` being populated.
 *  - update_type "fw"     — descriptor contains only Firmware members
 *                "app"    — descriptor contains only Application members
 *                "fw+app" — descriptor contains both
 *                ""       — file isn't a recognized v2.0 bundle, or the
 *                           bundle had no Firmware/Application members.
 *                           The install can still be attempted and may
 *                           still fail at the worker.
 *  - version     Bundle version string. Precedence: descriptor.version,
 *                falling back to fw_version (for "fw"), app_version (for
 *                "app"), or empty when the descriptor wasn't readable.
 *  - size        File size in bytes (from stat).
 *
 * `valid=true` with empty update_type does NOT imply the file is
 * installable — the install worker re-validates the header and may
 * still fail. inspect_bundle is metadata-only.
 */
struct BundleInfo
{
    bool        valid{false};
    std::string update_type;
    std::string version;
    std::uint64_t size{0};
};

///////////////////////////////////////////////////////////////////////////
/// FSUpdate declaration
//////////////////////////////////////////////////////////////////////////
class FSUpdate
{
  private:
    std::shared_ptr<UBoot::UBoot> uboot_handler;
    std::shared_ptr<logger::LoggerHandler> logger;
    updater::Bootstate update_handler;
    /* path to default work directory */
    std::filesystem::path work_dir;
    /* default permissions of work directory */
    std::filesystem::perms work_dir_perms;
    /* path to tmp app update */
    std::filesystem::path tmp_app_path;
    /* optional progress callback set by setInstallProgressCallback() */
    updater::ProgressCb install_progress_cb_;

    void decorator_update_state(std::function<void()>);

  public:
    /**
     * Init F&S update instance. Set logger handler object as refrence.
     */
    explicit FSUpdate(const std::shared_ptr<logger::LoggerHandler> &);
    ~FSUpdate();

    FSUpdate(const FSUpdate &) = delete;
    FSUpdate &operator=(const FSUpdate &) = delete;
    FSUpdate(FSUpdate &&) = delete;
    FSUpdate &operator=(FSUpdate &&) = delete;

  public:
    /**
     * Create work directory if not available.
     * This directory is used to create temporary files
     * for the update handling such install, download, progress, rollback.
     * @return Boolean, can create: true; available: false.
     * @throw GenericException if can't create not exists directory.
     */
    bool create_work_dir();
    std::filesystem::path get_work_dir();

    /**
     * Register a callback that receives install progress (0-100) during
     * update_firmware(), update_application(), update_image(), and
     * update_firmware_and_application(). Must be called before the install.
     * Not thread-safe to replace while an install is in progress.
     */
    void setInstallProgressCallback(updater::ProgressCb callback);
    /**
     * Initiate firmware update.
     * @param path_to_firmware Path to RAUC artifact image.
     * @throw UpdateInProgress
     */
    void update_firmware(const std::string &path_to_firmware);

    /**
     * Initiate application update.
     * @param path_to_application Path to application bundle.
     * @throw UpdateInProgress
     */
    void update_application(const std::string &path_to_application);

    /**
     * Initiate firmware and application update.
     * @param path_to_firmware Path to RAUC artifact image.
     * @param path_to_application Path to application bundle.
     * @throw UpdateInProgress
     */
    void update_firmware_and_application(const std::string &path_to_firmware, const std::string &path_to_application);

    /**
     * Initiate fsupdate.
     * @param path_to_update_image Path to fs update image.
     * @param update_type Update type: fw or app.
     * @param installed_update_type Out: 1 fw, 2 app, 3 fw+app.
     * @param rauc_scratch_path Optional runtime override for the firmware
     *        scratch file path used by the v2.0 dispatcher; its parent
     *        directory becomes the v2.0 member staging dir. Empty (default)
     *        → DEFAULT_RAUC_SCRATCH_PATH from fs_consts.h (set at compile
     *        time via -DFSUP_RAUC_SCRATCH=...). API-additive: existing
     *        3-arg callers do not need to change. Effective only when the
     *        lib was built with -DBUILD_RAUC_SCRATCH_OVERRIDE=ON (the
     *        default); with =OFF the argument is silently ignored and
     *        DEFAULT_RAUC_SCRATCH_PATH is always used.
     * @throw UpdateInProgress
     */
    void update_image(std::string &path_to_update_image,
                      std::string &update_type,
                      uint8_t &installed_update_type,
                      const std::string &rauc_scratch_path = {});

    /**
     * Read metadata from a v2.0 update bundle without installing it.
     *
     * Opens @p path, runs the v2.0 header + descriptor parse, then closes.
     * Cheap: O(header + descriptor); does not stream any member bytes.
     *
     * Never throws — all errors are reported via the returned BundleInfo
     * (see the struct doc for field semantics). Safe to call from
     * callbacks that must not throw across language/ABI boundaries.
     *
     * @param path Path to the bundle on disk.
     * @return BundleInfo with valid/update_type/version/size populated.
     */
    [[nodiscard]] BundleInfo inspect_bundle(const std::filesystem::path &path) noexcept;

    /**
     * Commit running updates.
     * @throw NotAllowedUpdateState If possible states of update process are unknown
     * @return Boolean, if update is commited: true; if no update processing: false.
     * @throw UpdateInProgress
     */
    bool commit_update();

    /**
     * Return current update state.
     * @return update_definitions::UBootBootstateFlags
     */
    update_definitions::UBootBootstateFlags get_update_reboot_state();

    /**
     * Return current application version.
     * @return Application version.
     */
    version_t get_application_version();

    /**
     * Return current firmware version.
     * @return Application version.
     */
    version_t get_firmware_version();

    /**
     * Rollback firmware.
     * @throw RollbackFirmware Error during rollback progress
     */
    void rollback_firmware();

    /**
     * Rollback application.
     * @throw RollbackApplication Error during rollback progress
     */
    void rollback_application();

    /**
     * Set A or B application or firmware state to bad.
     * @param state Application or Firmware state A or B.
     * @param update_id Firmware: 0 or Application: 1
     * @return error state.
     */
    int set_update_state_bad(const char &state, uint32_t update_id);

    /**
     * Set A or B application or firmware state to bad.
     * @param state Application or Firmware state A or B.
     * @param update_id Firmware: 0 or Application: 1
     * @return Application or Firmware state is bad: true, not bad: false.
     */
    bool is_update_state_bad(const char &state, uint32_t update_id);

    /**
     * Is reboot complete state.
     * @param firmware firmware image: true or application: false.
     * @return reboot complete state : true, not complete: false.
     */
    bool is_reboot_complete(bool firmware);
    /**
     * Update value from update_reboot_state environment.
     * @param flags value from enum UBootBootstateFlags.
     * @return reboot complete state : true, not complete: false.
     */
    void update_reboot_state(update_definitions::UBootBootstateFlags flag);
    /**
     * Update value from update_reboot_state environment.
     * @param flags value from enum UBootBootstateFlags.
     * @return rollback pending : true, reboot required: false.
     */
    bool pendingUpdateRollback();

    /**
     * Get path to temporary application update file.
     * @return filesystem::path pointer to the path object.
     */
    std::filesystem::path & getTempAppPath();
};
}
