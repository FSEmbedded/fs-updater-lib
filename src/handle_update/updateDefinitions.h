#pragma once

#include <exception>
#include <stdexcept>
#include <string>
#include <cstdint>

namespace update_definitions
{
    enum class Flags : unsigned char
    {
        OS,
        APP
    };

    enum class UBootBootstateFlags : unsigned char
    {
        NO_UPDATE_REBOOT_PENDING = 0,
        FW_UPDATE_REBOOT_FAILED = 1,
        INCOMPLETE_FW_UPDATE = 2,
        INCOMPLETE_APP_UPDATE = 3,
        INCOMPLETE_APP_FW_UPDATE = 4,
        FAILED_FW_UPDATE = 5,
        FAILED_APP_UPDATE = 6
    };

    enum class UpdateStatus :  unsigned char
    {
        DOWNLOADING = 0,
        INSTALLING = 1,
        REBOOTING = 2,
        SUCCESS = 3,
        FAILURE = 4,
        NONE = 5
    };

    ///////////////////////////////////////////////////////////////////////////
    /// updateDefinitions function collection
    ///////////////////////////////////////////////////////////////////////////

    /**
     * Convert numerical value to enum class update_definitions::UBootBootstateFlags.
     * @param number Numerical value that represents a state of UBootBootstateFlags.
     * @return update_definitions::UBootBootstateFlags Enum class.
     * @throw std::logic_error If number does not match to any state of UBootBootstateFlags.
     */
    UBootBootstateFlags to_UBootBootstateFlags(uint8_t number);

    /**
     * Convert string value to enum class update_definitions::UBootBootstateFlags.
     * @param number String that represents a state of UBootBootstateFlags.
     * @return update_definitions::UBootBootstateFlags Enum class,
     * @throw std::logic_error If number does not match to any state of UBootBootstateFlags.
     */
    UBootBootstateFlags to_UBootBootstateFlags(char number);

    /**
     * Convert enum class update_definitions::UBootBootstateFlags to string.
     * @param enum_state Value that should be converted into an one char string value.
     * @return One char string of update_definitions::UBootBootstateFlags.
     * @throw std::logic_error If state of UBootBootstateFlags is nit defined in function.
     */
    std::string to_string(UBootBootstateFlags enum_state);
}