#include "updateBase.h"

updater::updateBase::updateBase(const std::shared_ptr<UBoot::UBoot> & ptr):
    uboot_handler(ptr)
{
    
}

updater::updateBase::~updateBase()
{

}

update_definitions::UBootBootstateFlags updater::updateBase::status()
{
    const std::string update_state = this->uboot_handler->getVariable("update_state");
    update_definitions::UBootBootstateFlags ret_value;

    if (update_state == "0")
    {
        ret_value = update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;
    }
    else if (update_state == "1")
    {
        ret_value = update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED;
    }
    else if (update_state == "2")
    {
        ret_value = update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE;
    }
    else if (update_state == "3")
    {
        ret_value = update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE;
    }
    else if (update_state == "4")
    {
        ret_value = update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE;
    }
    else if (update_state == "5")
    {
        ret_value = update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE;
    }
    else
    {
        throw(std::logic_error( std::string("Value: ") + update_state + std::string(" is not in update_definitions::UBootBootstateFlags")));
    }

    return ret_value;
}