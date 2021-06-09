#pragma once
#include <filesystem>
#include <string>

namespace update
{
    class fs_update_ex
    {  
        public:
            fs_update_ex() {}
            ~fs_update_ex() {}

            fs_update_ex(const fs_update_ex &) = delete;
            fs_update_ex &operator=(const fs_update_ex &) = delete;
            fs_update_ex(fs_update_ex &&) = delete;
            fs_update_ex &operator=(fs_update_ex &&) = delete;

            virtual bool update_firmware(
                const std::filesystem::path & path_to_firmware
            ) = 0;

            virtual bool update_application(
                const std::filesystem::path & path_to_application
            ) = 0;

            virtual bool update_firmware_and_application(
                const std::filesystem::path & path_to_firmware, 
                const std::filesystem::path & path_to_application
            ) = 0;

            virtual bool commit_update() = 0;

            virtual bool automatic_update_application(
                const std::filesystem::path & path_to_application, 
                const unsigned int & dest_version
            ) = 0;

            virtual bool automatic_update_firmware(
                const std::filesystem::path & path_to_firmware,
                const unsigned int & dest_version
            ) = 0;

            virtual bool automatic_update_firmware_and_application(
                const std::filesystem::path & path_to_firmware,
                const std::filesystem::path & path_to_application,
                const unsigned int & dest_ver_application, 
                const unsigned int & dest_ver_firmware
            ) = 0;
    };
};