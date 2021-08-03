#ifndef UPDATE_FIRMWARE_H
#define UPDATE_FIRMWARE_H

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
    class ErrorFirmwareUpdateInstall : public fs::BaseFSUpdateException
    {
        public:
            explicit ErrorFirmwareUpdateInstall(const std::string & error_msg)
            {
                this->error_msg = std::string("Error during firmware update: ") + error_msg;
            }
    };

    class ErrorFirmwareRollback : public fs::BaseFSUpdateException
    {
        public:
            explicit ErrorFirmwareRollback(const std::string & error_msg)
            {
                this->error_msg = std::string("Error during firmware rollback: ") + error_msg;
            }
    };

    class ErrorGetFirmwareVersion : public fs::BaseFSUpdateException
    {
        public:
            ErrorGetFirmwareVersion(const std::string & path_to_version_file, const std::string & error_msg)
            {
                this->error_msg = std::string("Could not get firmware version; path: \"") + path_to_version_file;
                this->error_msg += std::string("\" error message: ") + error_msg; 
            }
    };

    class ErrorWrongVariableContent : public fs::BaseFSUpdateException
    {
        public:
            explicit ErrorWrongVariableContent(const std::string & wrong_var)
            {
                this->error_msg = std::string("Wrong Variable content: \"") + wrong_var + std::string("\"");
            }
    };

    class ErrorRaucDetection : public fs::BaseFSUpdateException
    {
        public:
            ErrorRaucDetection()
            {
                this->error_msg = std::string("Boot/Update slot could not be detected!");
            }
    };

    class ErrorRaucMarkUpdateSuccessfull : public fs::BaseFSUpdateException
    {
        public:
            explicit ErrorRaucMarkUpdateSuccessfull(const std::string & msg)
            {
                this->error_msg = std::string("Forward: ") + msg;
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
            firmwareUpdate(const std::shared_ptr<UBoot::UBoot> &, const std::shared_ptr<logger::LoggerHandler> &);
            ~firmwareUpdate();

            firmwareUpdate(const firmwareUpdate &) = delete;
            firmwareUpdate &operator=(const firmwareUpdate &) = delete;
            firmwareUpdate(firmwareUpdate &&) = delete;
            firmwareUpdate &&operator=(firmwareUpdate &) = delete;

            void install(const std::filesystem::path &) override;
            void rollback() override;
            unsigned int getCurrentVersion() override;
            bool failedUpdateReboot();
            void markSuccessfull();
    };
};

#endif