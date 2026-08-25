#pragma once
#include "../BaseException.h"
#include <cerrno>
#include <string>

namespace fs {

/**
 * Generic exception for update framework.
 * Derives from BaseFSUpdateException which already provides what().
 */
class GenericException : public BaseFSUpdateException {
public:
    int errorno{0};

    explicit GenericException(const std::string& msg)
    {
        this->error_msg = msg;
    }

    explicit GenericException(const std::string& msg, int err)
    {
        this->error_msg = msg;
        this->errorno = err;
    }
};

/**
 * Other FSUpdate-specific exceptions previously grouped in fsupdate.h.
 * Keeping them here avoids coupling fsupdate.h with exception definitions.
 */

class UpdateInProgress : public BaseFSUpdateException {
public:
    explicit UpdateInProgress(const std::string& msg)
    {
        this->error_msg = std::string("Already update in progress or uncommitted: ") + msg;
    }
};

class ApplicationVersion : public BaseFSUpdateException {
public:
    const unsigned int destination_version;
    const unsigned int current_version;

    ApplicationVersion(unsigned int destVersion, unsigned int curVersion)
        : destination_version(destVersion), current_version(curVersion)
    {
        this->error_msg = std::string("Current application version is: ") + std::to_string(curVersion)
                          + ", destination version: " + std::to_string(destVersion);
    }
};

class FirmwareVersion : public BaseFSUpdateException {
public:
    const unsigned int destination_version;
    const unsigned int current_version;

    FirmwareVersion(unsigned int destVersion, unsigned int curVersion)
        : destination_version(destVersion), current_version(curVersion)
    {
        this->error_msg = std::string("Current firmware version is: ") + std::to_string(curVersion)
                          + ", destination version: " + std::to_string(destVersion);
    }
};

class FirmwareApplicationVersion : public BaseFSUpdateException {
public:
    const unsigned int fw_destination_version, fw_current_version, app_destination_version, app_current_version;

    FirmwareApplicationVersion(unsigned int dfw, unsigned int cfw, unsigned int dapp, unsigned int capp)
        : fw_destination_version(dfw), fw_current_version(cfw),
          app_destination_version(dapp), app_current_version(capp)
    {
        this->error_msg = std::string("Current firmware version is: ") + std::to_string(cfw)
                          + ", destination version: " + std::to_string(dfw)
                          + "; Current application version is: " + std::to_string(capp)
                          + ", destination version: " + std::to_string(dapp);
    }
};

class NotAllowedUpdateState : public BaseFSUpdateException {
public:
    NotAllowedUpdateState()
    {
        this->error_msg = "Current state is not allowed";
    }
    /* Naming the state is the point: reaching this refusal means no arm
     * claimed the value, and a caller told only that something was not
     * allowed cannot tell an unowned state from one whose owner found its
     * bitfield precondition false. */
    explicit NotAllowedUpdateState(const std::string &state)
    {
        this->error_msg = "Current state is not allowed: " + state;
    }
};

/**
 * The input could not be identified as any known update format (bad magic,
 * truncated, garbage). errno ENOTSUP — same input always yields this.
 */
class UnknownUpdateFormat : public GenericException {
public:
    explicit UnknownUpdateFormat(const std::string& what)
        : GenericException("unknown update format: " + what, ENOTSUP)
    {
    }
};

/**
 * The input was recognised as a known format that this build cannot
 * install (e.g. the legacy tarball, or a format with no source
 * implementation in this build). errno ENOSYS.
 */
class UpdateFormatNotSupported : public GenericException {
public:
    explicit UpdateFormatNotSupported(const std::string& what)
        : GenericException("update format not supported on this build: " + what, ENOSYS)
    {
    }
};

/* Apply was called when no update is pending apply. The state machine
 * is unchanged on throw; caller can decide whether to surface as
 * "nothing to apply" or as an error. */
class ApplyUpdateInvalidState : public fs::BaseFSUpdateException
{
  public:
    explicit ApplyUpdateInvalidState(unsigned int state)
    {
        this->error_msg = "apply_pending_update: no pending update; "
                          "update_reboot_state=" + std::to_string(state);
    }

    /* For content the decoder rejected: the message carries a description
     * instead of asserting a numeral the environment cannot hold. */
    explicit ApplyUpdateInvalidState(const std::string& state_desc)
    {
        this->error_msg = "apply_pending_update: no pending update; "
                          "update_reboot_state=" + state_desc;
    }
};

} // namespace fs
