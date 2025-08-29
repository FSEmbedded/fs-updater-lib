#pragma once
#include "../BaseException.h"
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
 * Module-specific exception for libarchive related errors.
 * Inherits from GenericException to allow catching fs::GenericException as well.
 */
class LibArchiveException : public GenericException {
public:
    explicit LibArchiveException(const std::string& msg, int err = 0)
        : GenericException(msg, err) {}
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
};

} // namespace fs
