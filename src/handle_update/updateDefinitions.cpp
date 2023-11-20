#include "updateDefinitions.h"

update_definitions::UBootBootstateFlags update_definitions::to_UBootBootstateFlags(uint8_t number)
{
    update_definitions::UBootBootstateFlags ret_flag = UBootBootstateFlags::UNKNOWN_STATE;

    switch (number)
    {
    case 0:
        ret_flag = UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;
        break;
    case 1:
        ret_flag = UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED;
        break;
    case 2:
        ret_flag = UBootBootstateFlags::INCOMPLETE_FW_UPDATE;
        break;
    case 3:
        ret_flag = UBootBootstateFlags::INCOMPLETE_APP_UPDATE;
        break;
    case 4:
        ret_flag = UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE;
        break;
    case 5:
        ret_flag = UBootBootstateFlags::FAILED_FW_UPDATE;
        break;
    case 6:
        ret_flag = UBootBootstateFlags::FAILED_APP_UPDATE;
        break;
    case 7:
        ret_flag = UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING;
        break;
    case 8:
        ret_flag = UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING;
        break;
    case 9:
        ret_flag = UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING;
        break;
    case 10:
        ret_flag = UBootBootstateFlags::INCOMPLETE_FW_ROLLBACK;
        break;
    case 11:
        ret_flag = UBootBootstateFlags::INCOMPLETE_APP_ROLLBACK;
        break;
    case 12:
        ret_flag = UBootBootstateFlags::INCOMPLETE_APP_FW_ROLLBACK;
        break;
    default:
        ret_flag = UBootBootstateFlags::UNKNOWN_STATE;
    }

    if (ret_flag == UBootBootstateFlags::UNKNOWN_STATE)
    {
        std::string error_msg("Value: ");
        error_msg += std::to_string(number);
        error_msg += std::string(" cannot be converted");
        throw(std::logic_error(error_msg));
    }

    return ret_flag;
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
    else if (enum_state == UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING)
    {
        return std::string("8");
    }
    else if (enum_state == UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING)
    {
        return std::string("9");
    }
    else if (enum_state == UBootBootstateFlags::INCOMPLETE_FW_ROLLBACK)
    {
        return std::string("10");
    }
    else if (enum_state == UBootBootstateFlags::INCOMPLETE_APP_ROLLBACK)
    {
        return std::string("11");
    }
    else if (enum_state == UBootBootstateFlags::INCOMPLETE_APP_FW_ROLLBACK)
    {
        return std::string("12");
    }
    else
    {   
        std::string error_msg("Illegal state of UBootBootstateFlags, should never be reached");
        throw(std::logic_error(error_msg));
    }
} 
