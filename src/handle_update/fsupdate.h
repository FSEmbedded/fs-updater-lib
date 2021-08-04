#ifndef FS_UPDATE_H
#define FS_UPDATE_H


#include "updateDefinitions.h"

#include "../uboot_interface/UBoot.h"
#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "./../BaseException.h"
#include "handleUpdate.h"

#include <exception>
#include <string>
#include <filesystem>
#include <memory>
#include <functional>


constexpr char FSUPDATE_DOMAIN[] = "fsupdate";

namespace fs
{
    ///////////////////////////////////////////////////////////////////////////
    /// FSUpdate' exception definitions
    ///////////////////////////////////////////////////////////////////////////



    class UpdateInProgress : public fs::BaseFSUpdateException
    {
        public:
            explicit UpdateInProgress(const std::string & msg)
            {
                this->error_msg = "Already update in progress or uncommited: ";
                this->error_msg += msg;
            }
    };

    class ApplicationVersion : public fs::BaseFSUpdateException
    {
        public:
            const unsigned int destination_version, current_version;
            ApplicationVersion(const unsigned int destVersion, const unsigned int curVersion):
                destination_version(destVersion),
                current_version(curVersion)
            {
                this->error_msg = std::string("Current application version is: ") + std::to_string(curVersion);
                this->error_msg += std::string(", destination version: ") + std::to_string(destVersion);
            }
    };

    class FirmwareVersion : public fs::BaseFSUpdateException
    {
        public:
            const unsigned int destination_version, current_version;
            FirmwareVersion(const unsigned int destVersion, const unsigned int curVersion):
                destination_version(destVersion),
                current_version(curVersion)
            {
                this->error_msg = std::string("Current firmware version is: ") + std::to_string(curVersion);
                this->error_msg += std::string(", destination version: ") + std::to_string(destVersion);
            }
    };

    class FirmwareApplicationVersion : public fs::BaseFSUpdateException
    {
        public:
            const unsigned int fw_destination_version, fw_current_version, app_destination_version, app_current_version;
            FirmwareApplicationVersion( const unsigned int destVersion_fw, const unsigned int curVersion_fw,
                                        const unsigned int destVersion_app, const unsigned int curVersion_app):
                fw_destination_version(destVersion_fw),
                fw_current_version(curVersion_fw),
                app_destination_version(destVersion_app),
                app_current_version(curVersion_app)
            {
                this->error_msg = std::string("Current firmware version is: ") + std::to_string(curVersion_fw);
                this->error_msg += std::string(", destination version: ") + std::to_string(destVersion_fw);
                this->error_msg += std::string("; Current application version is: ") + std::to_string(curVersion_app);
                this->error_msg += std::string(", destination version: ") + std::to_string(destVersion_app);
            }
    };

    class NotAllowedUpdateState : public fs::BaseFSUpdateException
    {
        public:
            NotAllowedUpdateState()
            {
                this->error_msg = "Current state is not allowed";
            }
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

            void decorator_update_state(std::function<void()>);

        public:
            FSUpdate(const std::shared_ptr<logger::LoggerHandler>& );
            ~FSUpdate();

            FSUpdate(const FSUpdate &) = delete;
            FSUpdate &operator=(const FSUpdate &) = delete;
            FSUpdate(FSUpdate &&) = delete;
            FSUpdate &operator=(FSUpdate &&) = delete;
        
        void update_firmware(
            const std::filesystem::path & path_to_firmware
        );

        void update_application(
            const std::filesystem::path & path_to_application
        );

        void update_firmware_and_application(
            const std::filesystem::path & path_to_firmware, 
            const std::filesystem::path & path_to_application
        );

        bool commit_update();

        void automatic_update_application(
            const std::filesystem::path & path_to_application, 
            const unsigned int & dest_version
        );

        void automatic_update_firmware(
            const std::filesystem::path & path_to_firmware,
            const unsigned int & dest_version
        );

        void automatic_update_firmware_and_application(
            const std::filesystem::path & path_to_firmware,
            const std::filesystem::path & path_to_application,
            const unsigned int & dest_ver_application, 
            const unsigned int & dest_ver_firmware
        );

        update_definitions::UBootBootstateFlags get_update_reboot_state();
    };
};

#endif