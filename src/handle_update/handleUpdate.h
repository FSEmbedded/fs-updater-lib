#ifndef HANDLE_UPDATE_H
#define HANDLE_UPDATE_H

#include "updateDefinitions.h"
#include "utils.h"

#include "../uboot_interface/UBoot.h"
#include "../subprocess/subprocess.h"

#include "updateApplication.h"
#include "updateApplication.h"

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"



#include <memory>
#include <algorithm>

constexpr char BOOTSTATE_DOMAIN[] = "bootstate";

namespace updater
{
    ///////////////////////////////////////////////////////////////////////////
    /// Bootstate' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    class ErrorGetLoopDevices : public ErrorUpdateBaseException
    {
        public:
            ErrorGetLoopDevices()
            {
                this->error_msg = std::string("Could not get mounted loop devices");
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
            bool checkPendingUpdate();
            
    };
};

#endif