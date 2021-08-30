/**
 * Error classes and firmware update functionality.
 */

#pragma once

#include "updateBase.h"

#include "../uboot_interface/UBoot.h"

#include "../rauc/rauc_handler.h"

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "./../BaseException.h"

#include <exception>
#include <string>
#include <memory>
#include <filesystem>
#include <regex>
#include <fstream>

#define PATH_TO_FIRMWARE_VERSION_FILE "/etc/fw_version"
constexpr char FIRMWARE_UPDATE[] = "firmware update";


namespace updater 
{  
    ///////////////////////////////////////////////////////////////////////////
    /// firmwareUpdate' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    class FirmwareUpdateInstall : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Firmware update failed.
             * @param error_msg Report reason for failure.
             */
            explicit FirmwareUpdateInstall(const std::string & error_msg)
            {
                this->error_msg = std::string("Error during firmware update: ") + error_msg;
            }
    };

    class FirmwareRollback : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Rollback of firmware failed.
             * @param error_msg Report reason for failure.
             */
            explicit FirmwareRollback(const std::string & error_msg)
            {
                this->error_msg = std::string("Error during firmware rollback: ") + error_msg;
            }
    };

    class GetFirmwareVersion : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Could to read current firmware version.
             * @param path_to_version_file File which contains the curret version string.
             * @param error_msg Report reason for failure.
             */
            GetFirmwareVersion(const std::string & path_to_version_file, const std::string & error_msg)
            {
                this->error_msg = std::string("Could not get firmware version; path: \"") + path_to_version_file;
                this->error_msg += std::string("\" error message: ") + error_msg; 
            }
    };

    class WrongVariableContent : public fs::BaseFSUpdateException
    {
        public:
            /**
             * UBoot variable does not fulfill expected logical content.
             * @param wrong_var Name of variable with wrong content.
             */
            explicit WrongVariableContent(const std::string & wrong_var)
            {
                this->error_msg = std::string("Wrong Variable content: \"") + wrong_var + std::string("\"");
            }
    };

    class RaucDetection : public fs::BaseFSUpdateException
    {
        public:
            /**
             * RAUC could not detect the active boot slot.
             */
            RaucDetection()
            {
                this->error_msg = std::string("Boot/Update slot could not be detected!");
            }
    };

    ///////////////////////////////////////////////////////////////////////////
    /// firmwareUpdate declaration
    ///////////////////////////////////////////////////////////////////////////
    class firmwareUpdate : public updateBase
    {
        private:
            rauc::rauc_handler system_installer;

        public:

            /**
             * Create firmware update object. Use reference from UBoot and Loggerhandler object.
             * @param ptr UBoot::UBoot reference.
             * @param logger logger::LoggerHandler reference.
             */
            firmwareUpdate(const std::shared_ptr<UBoot::UBoot> &, const std::shared_ptr<logger::LoggerHandler> &);

            ~firmwareUpdate();

            firmwareUpdate(const firmwareUpdate &) = delete;
            firmwareUpdate &operator=(const firmwareUpdate &) = delete;
            firmwareUpdate(firmwareUpdate &&) = delete;
            firmwareUpdate &&operator=(firmwareUpdate &) = delete;

            /**
             * Install firmware object for given path.
             * @param path_to_bundle Path to RAUC artifact.
             * @throw FirmwareUpdateInstall Error when error occurs during installation.
             */
            void install(const std::filesystem::path &) override;

            /**
             * Rolllback from current state to former.
             * @throw FirmwareRollback When rollback is not possible or failed.
             */
            void rollback() override;

            /**
             * Return current firmware version.
             * @return Firmware version as number.
             * @throw GetFirmwareVersion When current version can not be read or parsed.
             */
            unsigned int getCurrentVersion() override;

            /**
             * Return if current RAUC state is a failed update or not.
             * @return Return boolean value of failed firmware update or not.
             * @throw WrongVariableContent Variable content missmatch to the expected one.
             */
            bool failedUpdateReboot();
    };
}