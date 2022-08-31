#include "updateDefinitions.h"

update_definitions::UBootBootstateFlags update_definitions::to_UBootBootstateFlags(uint8_t number)
{
    if (number == 0)
    {
        return UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;
    }
    else if (number == 1)
    {
        return UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED;
    }
    else if (number == 2)
    {
        return UBootBootstateFlags::INCOMPLETE_FW_UPDATE;
    }
    else if (number == 3)
    {
        return UBootBootstateFlags::INCOMPLETE_APP_UPDATE;
    }
    else if (number == 4)
    {
        return UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE;
    }
    else if (number == 5)
    {
        return UBootBootstateFlags::FAILED_FW_UPDATE;
    }
    else if (number == 6)
    {
        return UBootBootstateFlags::FAILED_APP_UPDATE;
    }
    else if (number == 7)
    {
        return UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING;
    }
    else
    {   
        std::string error_msg("Value: ");
        error_msg += std::to_string(number);
        error_msg += std::string(" cannot be converted");
        throw(std::logic_error(error_msg));
    }
}

update_definitions::UBootBootstateFlags update_definitions::to_UBootBootstateFlags(char number)
{
    if (number == '0')
    {
        return UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;
    }
    else if (number == '1')
    {
        return UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED;
    }
    else if (number == '2')
    {
        return UBootBootstateFlags::INCOMPLETE_FW_UPDATE;
    }
    else if (number == '3')
    {
        return UBootBootstateFlags::INCOMPLETE_APP_UPDATE;
    }
    else if (number == '4')
    {
        return UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE;
    }
    else if (number == '5')
    {
        return UBootBootstateFlags::FAILED_FW_UPDATE;
    }
    else if (number == '6')
    {
        return UBootBootstateFlags::FAILED_APP_UPDATE;
    }
    else if (number == '7')
    {
        return UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING;
    }
    else
    {   
        std::string error_msg("Value: ");
        error_msg += std::to_string(number);
        error_msg += std::string(" cannot be converted");
        throw(std::logic_error(error_msg));
    }
}

std::string update_definitions::to_string(UBootBootstateFlags enum_state)
{
    if (enum_state == UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING)
    {
        return std::string("0");
    }
    else if (enum_state == UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED)
    {
        return std::string("1");
    }
    else if (enum_state == UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
    {
        return std::string("2");
    }
    else if (enum_state == UBootBootstateFlags::INCOMPLETE_APP_UPDATE)
    {
        return std::string("3");
    }
    else if (enum_state == UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE)
    {
        return std::string("4");
    }
    else if (enum_state == UBootBootstateFlags::FAILED_FW_UPDATE)
    {
        return std::string("5");
    }
    else if (enum_state == UBootBootstateFlags::FAILED_APP_UPDATE)
    {
        return std::string("6");
    }
    else if (enum_state == UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING)
    {
        return std::string("7");
    }
    else
    {   
        std::string error_msg("Illegal state of UBootBootstateFlags, should never be reached");
        throw(std::logic_error(error_msg));
    }
} 
