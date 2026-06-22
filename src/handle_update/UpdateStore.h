#pragma once

#include <string>

namespace fs {

/// Bookkeeping for which payloads (firmware / application) were discovered
/// in the update container. Used by `FSUpdate::update_image` to drive the
/// downstream firmware-only / application-only / combined dispatch and to
/// resolve the destination filenames inside the staging directory.
class UpdateStore
{
    const std::string app_store_name = "update.app";
    const std::string fw_store_name = "update.fw";
    bool fw_available = false;
    bool app_available = false;

  public:
    UpdateStore() = default;
    ~UpdateStore() = default;

    UpdateStore(const UpdateStore &) = delete;
    UpdateStore &operator=(const UpdateStore &) = delete;
    UpdateStore(UpdateStore &&) = delete;
    UpdateStore &operator=(UpdateStore &&) = delete;

    [[nodiscard]] bool IsFirmwareAvailable() const { return fw_available; }
    void SetFirmwareAvailable(bool available) { fw_available = available; }

    [[nodiscard]] bool IsApplicationAvailable() const { return app_available; }
    void SetApplicationAvailable(bool available) { app_available = available; }

    [[nodiscard]] std::string getFirmwareStoreName() const { return fw_store_name; }
    [[nodiscard]] std::string getApplicationStoreName() const { return app_store_name; }
};

} // namespace fs
