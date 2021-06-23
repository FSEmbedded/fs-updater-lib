#ifndef FS_UPDATE_H
#define FS_UPDATE_H

#include "updateFirmware.h"
#include "updateApplication.h"
#include "updateDefinitions.h"
#include "utils.h"
#include "handleUpdate.h"
#include "../uboot_interface/UBoot.h"
#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"


#include <exception>
#include <string>
#include <filesystem>
#include <memory>


constexpr char FSUPDATE_DOMAIN[] = "fsupdate";

namespace fs
{
    class FSUpdate
    {
        private:
            std::shared_ptr<UBoot::UBoot> uboot_handler;
            std::shared_ptr<logger::LoggerHandler> logger;

        public:
            FSUpdate(const std::shared_ptr<logger::LoggerHandler>& );
            ~FSUpdate();

            FSUpdate(const FSUpdate &) = delete;
            FSUpdate &operator=(const FSUpdate &) = delete;
            FSUpdate(FSUpdate &&) = delete;
            FSUpdate &operator=(FSUpdate &&) = delete;
        
        bool update_firmware(
            const std::filesystem::path & path_to_firmware
        );

        bool update_application(
            const std::filesystem::path & path_to_application
        );

        bool update_firmware_and_application(
            const std::filesystem::path & path_to_firmware, 
            const std::filesystem::path & path_to_application
        );

        bool commit_update();

        bool automatic_update_application(
            const std::filesystem::path & path_to_application, 
            const unsigned int & dest_version
        );

        bool automatic_update_firmware(
            const std::filesystem::path & path_to_firmware,
            const unsigned int & dest_version
        );

        bool automatic_update_firmware_and_application(
            const std::filesystem::path & path_to_firmware,
            const std::filesystem::path & path_to_application,
            const unsigned int & dest_ver_application, 
            const unsigned int & dest_ver_firmware
        );

        update_definitions::UBootBootstateFlags get_update_reboot_state();
    };
};

#endif