#pragma once

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

/**
 * Extern interface for usage of F&S Update Framework.
 *
 * Offers the application and firmware update functionality.
 * Can read and commit the different states of update process.
 */
namespace fs
{
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

            /**
             * Application destination version is the same or higher than current version.
             * @param destVersion Destination version of application.
             * @param curVersion Current version of application.
             */
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

            /**
             * Firmware destination version is the same or higher than current version.
             * @param destVersion Destination version of firmware.
             * @param curVersion Current version of firmware.
             */
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

            /**
             * Application and firmware version are the same or higher.
             * @param destVersion_fw Destination version of firmware.
             * @param curVersion_fw Current version of firmware.
             * @param destVersion_app Destination of application version.
             * @param curVersion_app Current application version.
             */
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
            /**
             * Update state is not defined.
             */
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
            /**
             * Init F&S update instance. Set logger handler object as refrence.
             */
            FSUpdate(const std::shared_ptr<logger::LoggerHandler>& );
            ~FSUpdate();

            FSUpdate(const FSUpdate &) = delete;
            FSUpdate &operator=(const FSUpdate &) = delete;
            FSUpdate(FSUpdate &&) = delete;
            FSUpdate &operator=(FSUpdate &&) = delete;

        /**
         * Initiate firmware update.
         * @param path_to_firmware Path to RAUC artifact image.
         * @throw UpdateInProgress
         */
        void update_firmware(
            const std::filesystem::path & path_to_firmware
        );

        /**
         * Initiate application update.
         * @param path_to_application Path to application bundle.
         * @throw UpdateInProgress
         */
        void update_application(
            const std::filesystem::path & path_to_application
        );

        /**
         * Initiate firmware and application update.
         * @param path_to_firmware Path to RAUC artifact image.
         * @param path_to_application Path to application bundle.
         * @throw UpdateInProgress
         */
        void update_firmware_and_application(
            const std::filesystem::path & path_to_firmware, 
            const std::filesystem::path & path_to_application
        );

        /**
         * Commit running updates.
         * @throw NotAllowedUpdateState If possible states of update process are unknown
         * @return Boolean, if update is commited: true; if no update processing: false.
         * @throw UpdateInProgress
         */
        bool commit_update();

        /**
         * Perform application update with version check.
         * If version check fails, update will nor be marked as failed.
         * @param path_to_application Path to application package.
         * @param dest_version Destination version of application image.
         * @throw ApplicationVersion
         * @throw UpdateInProgress
         */
        void automatic_update_application(
            const std::filesystem::path & path_to_application, 
            const unsigned int & dest_version
        );

        /**
         * Perform firmware update with version check.
         * @param path_to_firmware Path to firmware update bundle.
         * @param dest_version Destination version of firmware image.
         * @throw FirmwareVersion
         * @throw UpdateInProgress
         */
        void automatic_update_firmware(
            const std::filesystem::path & path_to_firmware,
            const unsigned int & dest_version
        );

        /**
         * Perform firmware & application update with version check
         * @param path_to_firmware Path to firmware update bundle.
         * @param path_to_application Path to application update bundle.
         * @param dest_ver_application Destination version of application bundle.
         * @param dest_ver_firmware Destination version of firmware bundle.
         * @throw FirmwareApplicationVersion Firmware version is not newer that current one.
         * @throw UpdateInProgress Already update in progress.
         */
        void automatic_update_firmware_and_application(
            const std::filesystem::path & path_to_firmware,
            const std::filesystem::path & path_to_application,
            const unsigned int & dest_ver_application, 
            const unsigned int & dest_ver_firmware
        );

        /**
         * Return current update state.
         * @return update_definitions::UBootBootstateFlags
         */
        update_definitions::UBootBootstateFlags get_update_reboot_state();
    };
}