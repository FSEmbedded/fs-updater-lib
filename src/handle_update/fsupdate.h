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
#include <sys/types.h> /* mode_t */

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
    std::string work_dir;
    /* default permissions of work directory */
    mode_t work_dir_perms;
    /* path to tmp app update */
    std::string tmp_app_path;
    /* optional progress callback set by setInstallProgressCallback() */
    updater::ProgressCb install_progress_cb_;

    void decorator_update_state(std::function<void()> /*func*/);

  public:
    /**
     * Init F&S update instance. Set logger handler object as refrence.
     */
    explicit FSUpdate(const std::shared_ptr<logger::LoggerHandler> & /*ptr*/);
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
    std::string get_work_dir();

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

    /* inspect_bundle (metadata-only header + descriptor read) is a free
     * function in fs:: namespace — see handle_update/inspect_bundle.h.
     * It doesn't need FSUpdate state and is independently testable. */

    /**
     * Commit running updates.
     * @throw NotAllowedUpdateState If possible states of update process are unknown
     * @return Boolean, if update is commited: true; if no update processing: false.
     * @throw UpdateInProgress
     */
    bool commit_update();

    /**
     * Apply an installed-but-not-yet-applied update. Reads
     * update_reboot_state from U-Boot env (durable source of truth)
     * and:
     *  - For INCOMPLETE_FW_UPDATE (2) or INCOMPLETE_APP_FW_UPDATE (4):
     *    asks RAUC to mark the inactive slot as good — RAUC's U-Boot
     *    pengutronix bootselect group then swaps BOOT_ORDER toward
     *    that slot so the next reboot lands on the new firmware.
     *    Returns true (reboot required).
     *  - For INCOMPLETE_APP_UPDATE (3): no RAUC slot swap (firmware
     *    slot unchanged), but a reboot is still required: the new app
     *    squashfs is selected at preinit time by dynamic-overlay from
     *    the `application` U-Boot variable, and preinit only runs at
     *    boot. Returns true.
     *  - For any other state: throws ApplyUpdateInvalidState — the
     *    caller has no update to apply, and the state machine is left
     *    untouched so a subsequent retry remains safe.
     *
     * Apply does no post-reboot work. After the caller reboots, the
     * post-reboot `--commit_update` (lib `commit_update`) detects the
     * successful slot switch and marks the new slot good.
     *
     * @return true when a reboot is required to take the update live.
     *         All INCOMPLETE_* states return true: firmware-bearing
     *         states need the boot-order swap to take effect, and
     *         app-only needs the next preinit pass.
     * @throw ApplyUpdateInvalidState when no update is pending apply.
     * @throw rauc::RaucBaseException on RAUC D-Bus failure (the slot
     *        was not swapped; state machine unchanged; retry is safe).
     */
    [[nodiscard]] bool apply_pending_update();

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
     * @return reference to the path string.
     */
    std::string & getTempAppPath();
};
}
