#ifndef UPDATE_DEFINITIONS_H
#define UPDATE_DEFINITIONS_H

#include <exception>
#include <stdexcept>
#include <string>

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
    
    UBootBootstateFlags to_UBootBootstateFlags(unsigned char number);
    UBootBootstateFlags to_UBootBootstateFlags(const std::string & number);
    std::string to_string(UBootBootstateFlags enum_state);
};

#endif