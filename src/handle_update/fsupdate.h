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

/* namespace block */
using namespace std;

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
    shared_ptr<UBoot::UBoot> uboot_handler;
    shared_ptr<logger::LoggerHandler> logger;
    updater::Bootstate update_handler;
    /* path to default work directory */
    filesystem::path work_dir;
    /* default permissions of work directory */
    filesystem::perms work_dir_perms;
    /* path to tmp app update */
    filesystem::path tmp_app_path;

    void decorator_update_state(function<void()>);

  public:
    /**
     * Init F&S update instance. Set logger handler object as refrence.
     */
    explicit FSUpdate(const shared_ptr<logger::LoggerHandler> &);
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
    filesystem::path get_work_dir();
    /**
     * Initiate firmware update.
     * @param path_to_firmware Path to RAUC artifact image.
     * @throw UpdateInProgress
     */
    void update_firmware(const string &path_to_firmware);

    /**
     * Initiate application update.
     * @param path_to_application Path to application bundle.
     * @throw UpdateInProgress
     */
    void update_application(const string &path_to_application);

    /**
     * Initiate firmware and application update.
     * @param path_to_firmware Path to RAUC artifact image.
     * @param path_to_application Path to application bundle.
     * @throw UpdateInProgress
     */
    void update_firmware_and_application(const string &path_to_firmware, const string &path_to_application);

    /**
     * Initiate fsupdate.
     * @param path_to_update_image Path to fs update image.
     * @param update_type Update type: fw or app.
     * @throw UpdateInProgress
     */
    void update_image(string &path_to_update_image, string &update_type, uint8_t &installed_update_type);

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
    filesystem::path & getTempAppPath();
};
}
