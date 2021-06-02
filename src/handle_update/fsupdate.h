#ifndef FS_UPDATE_H
#define FS_UPDATE_H

#include "updateFirmware.h"
#include "updateApplication.h"
#include "updateDefinitions.h"
#include "utils.h"
#include "handleUpdate.h"

#include "../uboot_interface/UBoot.h"

#include <exception>
#include <string>
#include <filesystem>
#include <memory>

namespace update
{
    class FSUpdate
    {
        private:
            std::string error_msg;
            std::shared_ptr<UBoot::UBoot> uboot_handler;

        public:
            FSUpdate();
            ~FSUpdate();

            FSUpdate(const FSUpdate &) = delete;
            FSUpdate &operator=(const FSUpdate &) = delete;
            FSUpdate(FSUpdate &&) = delete;
            FSUpdate &operator=(FSUpdate &&) = delete;
            
            bool update_firmware(const std::filesystem::path & path_to_firmware);
            bool update_application(const std::filesystem::path & path_to_application);
            bool update_firmware_and_application(const std::filesystem::path & path_to_firmware, 
                                                const std::filesystem::path & path_to_application);
            bool commit_update();
            bool automatic_update_application(const std::filesystem::path & path_to_application, 
                                            const unsigned int & dest_version);
            bool automatic_update_firmware(const std::filesystem::path & path_to_firmware,
                                           const unsigned int & dest_version);
            bool automatic_update_firmware_and_application(const std::filesystem::path & path_to_firmware,
                                                            const std::filesystem::path & path_to_application,
                                                            const unsigned int & dest_ver_application, 
                                                            const unsigned int & dest_ver_firmware);
            std::string getErrorString() const;
    };
};

#endif