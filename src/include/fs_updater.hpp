#pragma once

#include <string>
#include <filesystem>
#include <exception>
#include <stdexcept>
#include <memory>

#include "fsupdate_extern_interface.h"

namespace fs
{
    class ErrorFSUpdateFramework : public std::exception
    {
        private:
            const char * error_msg;
        
        public:
            ErrorFSUpdateFramework(const char * msg):
                error_msg(msg)
            {
            }
            
            const char * what() const throw () 
            {
                return this->error_msg;
            }

    };

    class fs_updater: public update::fs_update_ex
    {
        private:
            std::unique_ptr<update::fs_update_ex> fs_framework_handler;

        public:
            fs_updater();
            ~fs_updater();

            fs_updater(const fs_updater &) = delete;
            fs_updater &operator=(const fs_updater &) = delete;
            fs_updater(fs_updater &&) = delete;
            fs_updater &operator=(fs_updater &&) = delete;

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
}
