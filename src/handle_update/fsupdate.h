#ifndef FS_UPDATE_H
#define FS_UPDATE_H

#include "updateFirmware.h"
#include "updateApplication.h"
#include "updateDefinitions.h"
#include "utils.h"
#include "handleUpdate.h"
// extern interface header
#include "../include/fsupdate_extern_interface.h"

#include "../uboot_interface/UBoot.h"

#include <exception>
#include <string>
#include <filesystem>
#include <memory>

namespace update
{
    class FSUpdate: public update::fs_update_ex
    {
        private:
            std::shared_ptr<UBoot::UBoot> uboot_handler;

        public:
            FSUpdate();
            ~FSUpdate();

            FSUpdate(const FSUpdate &) = delete;
            FSUpdate &operator=(const FSUpdate &) = delete;
            FSUpdate(FSUpdate &&) = delete;
            FSUpdate &operator=(FSUpdate &&) = delete;
        
        bool update_firmware(
            const std::filesystem::path & path_to_firmware
        ) override;

        bool update_application(
            const std::filesystem::path & path_to_application
        ) override;

        bool update_firmware_and_application(
            const std::filesystem::path & path_to_firmware, 
            const std::filesystem::path & path_to_application
        ) override;

        bool commit_update() override;

        bool automatic_update_application(
            const std::filesystem::path & path_to_application, 
            const unsigned int & dest_version
        ) override;

        bool automatic_update_firmware(
            const std::filesystem::path & path_to_firmware,
            const unsigned int & dest_version
        ) override;

        bool automatic_update_firmware_and_application(
            const std::filesystem::path & path_to_firmware,
            const std::filesystem::path & path_to_application,
            const unsigned int & dest_ver_application, 
            const unsigned int & dest_ver_firmware
        ) override;
    };
};

#endif