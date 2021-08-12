/**
 * Read system variables to get current update state.
 */

#pragma once

#include "updateDefinitions.h"
#include "../uboot_interface/UBoot.h"

#include "updateApplication.h"
#include "updateApplication.h"

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "./../BaseException.h"

#include <memory>
#include <exception>

constexpr char BOOTSTATE_DOMAIN[] = "bootstate";

/**
 * Namespace for all internal functions to create F&S Update functionality.
 */
namespace updater
{

    ///////////////////////////////////////////////////////////////////////////
    /// Bootstate' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    class ErrorGetLoopDevices : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not get mounted loop devices.
             * @param msg Error related message.
             */
            explicit ErrorGetLoopDevices(const std::string &msg)
            {
                this->error_msg = std::string("Could not get mounted loop devices: ") + msg;
            }
    };

    class ErrorConfirmPendingFirmwareUpdate : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not confirm pending firmware update.
             * @param msg Error message.
             */
            explicit ErrorConfirmPendingFirmwareUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Pending firmware update cannot be confirmed: ") + msg;
            }
    };

    class ErrorConfirmPendingApplicationUpdate : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not confirm pending application update.
             * @param msg Error message.
             */
            explicit ErrorConfirmPendingApplicationUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Pending application update cannot be confirmed: ") + msg;
            }
    };

    class ErrorConfirmFailedFirmwareUpdate : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not confirm failed firmware update.
             * @param msg Error message.
             */
            explicit ErrorConfirmFailedFirmwareUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Failed firmware update cannot be confirmed: ") + msg;
            }
    };

    class ErrorConfirmFailedApplicationUpdate : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not confirm failed application update.
             * @param msg Error message.
             */
            explicit ErrorConfirmFailedApplicationUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Failed application update cannot be confirmed: ") + msg;
            }
    };

    class ErrorFirmwareRebootStateNotDefined : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Firmware reboot state is not defined.
             */
            ErrorFirmwareRebootStateNotDefined()
            {
                this->error_msg = std::string("This error state is not defined in a firmware update process!");
            }
    };

    class ErrorConfirmPendingFirmwareApplicationUpdate : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not confirm pending firmware and application update.
             * @param msg Error message.
             */
            explicit ErrorConfirmPendingFirmwareApplicationUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Pending firmware & application update cannot be confirmed: ") + msg;
            }
    };
    ///////////////////////////////////////////////////////////////////////////
    /// firmwareUpdate declaration
    ///////////////////////////////////////////////////////////////////////////
    class Bootstate
    {
        private:
            std::shared_ptr<UBoot::UBoot> uboot_handler;
            std::shared_ptr<logger::LoggerHandler> logger;

            const std::vector<update_definitions::Flags> get_complete_update();


        public:
            /**
             * Bootstate constructor.
             * @param ptr UBoot reference.
             * @param logger Logger reference.
             */
            Bootstate(const std::shared_ptr<UBoot::UBoot> & ptr, const std::shared_ptr<logger::LoggerHandler> & logger);
            ~Bootstate();

            Bootstate(const Bootstate &) = delete;
            Bootstate &operator=(const Bootstate &) = delete;
            Bootstate(Bootstate &&) = delete;
            Bootstate &operator=(Bootstate &&) = delete;

            /**
             * Detect if an application update is pending.
             * @return Boolean state.
             */
            bool pendingApplicationUpdate();

            /**
             * Detect if a firmware update is pending.
             * @return Boolean state.
             */
            bool pendingFirmwareUpdate();

            /**
             * Detect if a firmware and an application update is pending.
             * @return Boolean state.
             */
            bool pendingApplicationFirmwareUpdate();

            /**
             * Detect if a firmware update is failed.
             * @return Boolean state.
             */
            bool failedFirmwareUpdate();

            /**
             * Detect if an application update is failed.
             * @return Boolean state.
             */
            bool failedApplicationUpdate();
            
            /**
             * Confirm failed firmware update.
             * @throw ErrorConfirmFailedFirmwareUpdate If a failed firmware update is not stated a failed firmware update can not be confirmed.
             */
            void confirmFailedFirmwareUpdate();

            /**
             * Confirm failed application update.
             * @throw ErrorConfirmFailedApplicationUpdate If a failed application update is not stated a failed application update can not be confirmed.
             */
            void confirmFailedApplicationeUpdate();

            /**
             * Confirm pending firmware update.
             * @throw ErrorConfirmPendingFirmwareUpdate If a pending firmware update is not stated a pending firmware update can not be confirmed.
             * @throw ErrorFirmwareRebootStateNotDefined A firmware reboot state is not defined.
             */
            void confirmPendingFirmwareUpdate();

            /**
             * Confirm pending application update.
             * @throw ErrorConfirmPendingApplicationUpdate If a pending application update is not stated a pending application update can not be confirmed.
             * @throw ErrorGetLoopDevices Can not get loop devie of application image. 
             */
            void confirmPendingApplicationUpdate();

            /**
             * Confirm pending application update.
             * @throw ErrorFirmwareRebootStateNotDefined A firmware reboot state is not defined.
             * @throw ErrorConfirmPendingFirmwareApplicationUpdate If a pending application & firmware update is not stated a pending application & firmware update can not be confirmed.
             */
            void confirmPendingApplicationFirmwareUpdate();

            /**
             * Check if a update process is currently running.
             * @return Boolean state.
             */
            bool noUpdateProcessing();      
    };
};