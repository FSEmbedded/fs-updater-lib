#pragma once

#include "updateDefinitions.h"

#include "../uboot_interface/UBoot.h"
#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "./../BaseException.h"
#include "handleUpdate.h"

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

constexpr char FSUPDATE_DOMAIN[] = "fsupdate";
/* use 8KB buffer size */
constexpr size_t BUFFER_SIZE = 8192;

/**
 * Extern interface for usage of F&S Update Framework.
 *
 * Offers the application and firmware update functionality.
 * Can read and commit the different states of update process.
 */
namespace fs
{
/* fsheader structures */
struct fs_header_v0_0
{                            /* Size: 16 Bytes */
    char magic[4];           /* "FS" + two bytes operating system */
                             /* (e.g. "LX" for Linux) */
    uint32_t file_size_low;  /* Image size [31:0] */
    uint32_t file_size_high; /* Image size [63:32] */
    uint16_t flags;          /* See flags below */
    uint8_t padsize;         /* Number of padded bytes at end */
    uint8_t version;         /* Header version x.y:
                                 [7:4] major x, [3:0] minor y */
};

struct fs_header_v1_0
{                               /* Size: 64 bytes */
    struct fs_header_v0_0 info; /* Image info, see above */
    char type[16];              /* Image type, e.g. "U-BOOT" */
    union {
        char descr[32];   /* Description, null-terminated */
        uint8_t p8[32];   /* 8-bit parameters */
        uint16_t p16[16]; /* 16-bit parameters */
        uint32_t p32[8];  /* 32-bit parameters */
        uint64_t p64[4];  /* 64-bit parameters */
    } param;
};
///////////////////////////////////////////////////////////////////////////
/// FSUpdate' exception definitions
///////////////////////////////////////////////////////////////////////////

class UpdateInProgress : public fs::BaseFSUpdateException
{
  public:
    /**
     * Update can not be performed, because already one is in progress.
     * @param msg Error message.
     */
    explicit UpdateInProgress(const string &msg)
    {
        this->error_msg = "Already update in progress or uncommited: ";
        this->error_msg += msg;
    }
};

class ApplicationVersion : public fs::BaseFSUpdateException
{
  public:
    const unsigned int destination_version, current_version;

    /**
     * Application destination version is the same or higher than current version.
     * @param destVersion Destination version of application.
     * @param curVersion Current version of application.
     */
    ApplicationVersion(const unsigned int destVersion, const unsigned int curVersion)
        : destination_version(destVersion), current_version(curVersion)
    {
        this->error_msg = string("Current application version is: ") + to_string(curVersion);
        this->error_msg += string(", destination version: ") + to_string(destVersion);
    }
};

class FirmwareVersion : public fs::BaseFSUpdateException
{
  public:
    const unsigned int destination_version, current_version;

    /**
     * Firmware destination version is the same or higher than current version.
     * @param destVersion Destination version of firmware.
     * @param curVersion Current version of firmware.
     */
    FirmwareVersion(const unsigned int destVersion, const unsigned int curVersion)
        : destination_version(destVersion), current_version(curVersion)
    {
        this->error_msg = string("Current firmware version is: ") + to_string(curVersion);
        this->error_msg += string(", destination version: ") + to_string(destVersion);
    }
};

class FirmwareApplicationVersion : public fs::BaseFSUpdateException
{
  public:
    const unsigned int fw_destination_version, fw_current_version, app_destination_version, app_current_version;

    /**
     * Application and firmware version are the same or higher.
     * @param destVersion_fw Destination version of firmware.
     * @param curVersion_fw Current version of firmware.
     * @param destVersion_app Destination of application version.
     * @param curVersion_app Current application version.
     */
    FirmwareApplicationVersion(const unsigned int destVersion_fw, const unsigned int curVersion_fw,
                               const unsigned int destVersion_app, const unsigned int curVersion_app)
        : fw_destination_version(destVersion_fw), fw_current_version(curVersion_fw),
          app_destination_version(destVersion_app), app_current_version(curVersion_app)
    {
        this->error_msg = string("Current firmware version is: ") + to_string(curVersion_fw);
        this->error_msg += string(", destination version: ") + to_string(destVersion_fw);
        this->error_msg += string("; Current application version is: ") + to_string(curVersion_app);
        this->error_msg += string(", destination version: ") + to_string(destVersion_app);
    }
};

class NotAllowedUpdateState : public fs::BaseFSUpdateException
{
  public:
    /**
     * Update state is not defined.
     */
    NotAllowedUpdateState()
    {
        this->error_msg = "Current state is not allowed";
    }
};

class GenericException : public fs::BaseFSUpdateException
{
  public:
    int errorno;

  public:
    /**
     * Error during firmware rollback.
     */
    explicit GenericException(const string &msg)
    {
        this->error_msg = msg;
    }

    explicit GenericException(const string &msg, int errorno)
    {
        this->error_msg = msg;
        this->errorno = errorno;
    }
};

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

class UpdateStore
{
  private:
    const string app_store_name = "update.app";
    const string fw_store_name = "update.fw";
    bool fw_available;
    bool app_available;
    shared_ptr<logger::LoggerHandler> logger;

    string CalculateCheckSum(const filesystem::path& filepath, const string& algorithm);
    void ExtractTarBz2(const std::filesystem::path& filepath, const std::filesystem::path& targetdir);
  protected:
    Json::Value root;

    bool parseFSUpdateJsonConfig();

  public:
    bool IsFirmwareAvailable()
    {
        return fw_available;
    }
    void SetFirmwareAvailable(bool available)
    {
        this->fw_available = available;
    }
    bool IsApplicationAvailable()
    {
        return app_available;
    }
    void SetApplicationAvailable(bool available)
    {
        this->app_available = available;
    }

    string getApplicationStoreName()
    {
        return app_store_name;
    }

    string getFirmwareStoreName()
    {
        return fw_store_name;
    }

  public:
    UpdateStore();
    ~UpdateStore() = default;

    UpdateStore(const UpdateStore &) = delete;
    UpdateStore &operator=(const UpdateStore &) = delete;
    UpdateStore(UpdateStore &&) = delete;
    UpdateStore &operator=(UpdateStore &&) = delete;

    void ExtractUpdateStore(const filesystem::path &path_to_update_image);
    void ReadUpdateConfiguration(const string configuration_path);
    bool CheckUpdateSha256Sum(const filesystem::path &path_to_update_image);
};
}
