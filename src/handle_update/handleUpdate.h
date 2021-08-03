#ifndef HANDLE_UPDATE_H
#define HANDLE_UPDATE_H

#include "updateDefinitions.h"
#include "utils.h"

#include "../uboot_interface/UBoot.h"

#include "updateApplication.h"
#include "updateApplication.h"

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "./../BaseException.h"

#include <memory>
#include <algorithm>
#include <exception>

constexpr char BOOTSTATE_DOMAIN[] = "bootstate";

namespace updater
{

    ///////////////////////////////////////////////////////////////////////////
    /// Bootstate' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    class ErrorGetLoopDevices : public fs::BaseFSUpdateException
    {
        public:
            explicit ErrorGetLoopDevices(const std::string &msg)
            {
                this->error_msg = std::string("Could not get mounted loop devices: ") + msg;
            }

    };

    class ErrorConfirmPendingFirmwareUpdate : public fs::BaseFSUpdateException
    {
        public:
            explicit ErrorConfirmPendingFirmwareUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Pending firmware update cannot be confirmed: ") + msg;
            }
    };

    class ErrorConfirmPendingApplicationUpdate : public fs::BaseFSUpdateException
    {
        public:
            explicit ErrorConfirmPendingApplicationUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Pending application update cannot be confirmed: ") + msg;
            }
    };

    class ErrorConfirmFailedFirmwareUpdate : public fs::BaseFSUpdateException
    {
        public:
            explicit ErrorConfirmFailedFirmwareUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Failed firmware update cannot be confirmed: ") + msg;
            }
    };

    class ErrorConfirmFailedApplicationUpdate : public fs::BaseFSUpdateException
    {
        public:
            explicit ErrorConfirmFailedApplicationUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Failed application update cannot be confirmed: ") + msg;
            }
    };
    class ErrorFirmwareRebootStateNotDefined : public fs::BaseFSUpdateException
    {
        public:
            ErrorFirmwareRebootStateNotDefined()
            {
                this->error_msg = std::string("This error state is not defined in a firmware update process!");
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
            Bootstate(const std::shared_ptr<UBoot::UBoot> & ptr, const std::shared_ptr<logger::LoggerHandler> & logger);
            ~Bootstate();

            Bootstate(const Bootstate &) = delete;
            Bootstate &operator=(const Bootstate &) = delete;
            Bootstate(Bootstate &&) = delete;
            Bootstate &operator=(Bootstate &&) = delete;

            bool pendingApplicationUpdate();
            bool pendingFirmwareUpdate();
            bool pendingApplicationFirmwareUpdate();

            bool failedFirmwareUpdate();
            bool failedApplicationUpdate();
            
            void confirmFailedFirmwareUpdate();
            void confirmFailedApplicationeUpdate();

            void confirmPendingFirmwareUpdate();
            void confirmPendingApplicationUpdate();
            void confirmPendingApplicationFirmwareUpdate();

            bool noUpdateProcessing();      
    };
};

#endif