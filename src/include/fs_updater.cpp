// To create a one header system I need to break the header include conventions 
#include "../handle_update/fsupdate.h"
#include "fs_updater.hpp"

fs::fs_updater::fs_updater()
{
    update::FSUpdate * session_ptr_src = new update::FSUpdate();
    update::fs_update_ex * session_ptr_dest = dynamic_cast<update::fs_update_ex*>(session_ptr_src);

    if (session_ptr_dest != nullptr )
    {
        this->fs_framework_handler = std::make_unique<update::fs_update_ex>(session_ptr_dest);
    }
    else
    {
        delete session_ptr_src;
        throw(std::bad_cast());
    }
}

fs::fs_updater::~fs_updater()
{

}

bool fs::fs_updater::update_firmware(
    const std::filesystem::path & path_to_firmware)
{
    bool retValue = false;

    try
    {
        retValue = this->fs_framework_handler->update_firmware(path_to_firmware);
    }
    catch(const std::exception& e)
    {
        throw(ErrorFSUpdateFramework(e.what()));
    }

    return retValue;
}

bool fs::fs_updater::update_application(
    const std::filesystem::path & path_to_application)
{
    bool retValue = false;
    
    try
    {
        retValue = this->fs_framework_handler->update_application(path_to_application);
    }
    catch(const std::exception& e)
    {
        throw(ErrorFSUpdateFramework(e.what()));
    }

    return retValue;
}

bool fs::fs_updater::update_firmware_and_application(
    const std::filesystem::path & path_to_firmware, 
    const std::filesystem::path & path_to_application)
{
    bool retValue = false;
    
    try
    {
        retValue = this->fs_framework_handler->update_firmware_and_application(
            path_to_firmware,
            path_to_application
        );
    }
    catch(const std::exception& e)
    {
        throw(ErrorFSUpdateFramework(e.what()));
    }

    return retValue;
}

bool fs::fs_updater::commit_update()
{
    bool retValue = false;
    
    try
    {
        retValue = this->fs_framework_handler->commit_update();
    }
    catch(const std::exception& e)
    {
        throw(ErrorFSUpdateFramework(e.what()));
    }

    return retValue;
}

bool fs::fs_updater::automatic_update_application(
    const std::filesystem::path & path_to_application, 
    const unsigned int & dest_version)
{
    bool retValue = false;
    
    try
    {
        retValue = this->fs_framework_handler->automatic_update_application(
            path_to_application,
            dest_version
        );
    }
    catch(const std::exception& e)
    {
        throw(ErrorFSUpdateFramework(e.what()));
    }

    return retValue;
}

bool fs::fs_updater::automatic_update_firmware(
    const std::filesystem::path & path_to_firmware,
    const unsigned int & dest_version)
{
    bool retValue = false;
    
    try
    {
        retValue = this->fs_framework_handler->automatic_update_firmware(
            path_to_firmware,
            dest_version
        );
    }
    catch(const std::exception& e)
    {
        throw(ErrorFSUpdateFramework(e.what()));
    }

    return retValue;
}

bool fs::fs_updater::automatic_update_firmware_and_application(
    const std::filesystem::path & path_to_firmware,
    const std::filesystem::path & path_to_application,
    const unsigned int & dest_ver_application, 
    const unsigned int & dest_ver_firmware)
{
    bool retValue = false;
    
    try
    {
        retValue = this->fs_framework_handler->automatic_update_firmware_and_application(
            path_to_firmware,
            path_to_application,
            dest_ver_application,
            dest_ver_firmware
        );
    }
    catch(const std::exception& e)
    {
        throw(ErrorFSUpdateFramework(e.what()));
    }

    return retValue;
}
