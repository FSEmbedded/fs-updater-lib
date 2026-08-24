#include "reboot_state.h"

#include "../logger/LoggerEntry.h"
#include "../logger/LoggerHandler.h"
#include "../uboot_interface/IUBootEnv.h"
#include "../uboot_interface/allowed_uboot_variable_states.h"
#include "../uboot_interface/uboot_exceptions.h"

#include <algorithm>

namespace
{
    constexpr char READER_DOMAIN[] = "bootstate";

    /* The raw content is wanted here; validation happens in the decoder. */
    bool accept_any_content(const std::string &content)
    {
        static_cast<void>(content);
        return true;
    }

    /* Message assembly happens inside the guard: nothing on this path may
     * allocate or throw outside it, so the total reader stays total. */
    void log_read_failure(const std::shared_ptr<logger::LoggerHandler> &log,
                          const char *prefix, const char *detail,
                          const char *suffix = "") noexcept
    {
        if (!log)
        {
            return;
        }
        try
        {
            log->setLogEntry(std::make_shared<logger::LogEntry>(
                READER_DOMAIN, std::string(prefix) + detail + suffix, logger::logLevel::ERROR));
        }
        catch (...)
        {
            /* A failed diagnostic must not turn a read into a throw. */
        }
    }
}

update_definitions::UBootBootstateFlags update_definitions::decode_update_reboot_state(const std::string &raw) noexcept
{
    /* The only writer of this variable emits canonical decimal ("0".."12")
     * and nothing else. Parsing is therefore strict and fully consuming:
     * digits only, no sign, no base prefix, no whitespace, no trailing
     * characters, no leading zero. Any other shape was not written by this
     * library and surfaces as the recovery state instead of a guess -- a
     * prefix parse would read "0x02" as a clean device. */
    if (raw.empty() || (raw.size() > 2))
    {
        return UBootBootstateFlags::UNKNOWN_STATE;
    }

    unsigned int value = 0U;
    for (const char digit : raw)
    {
        if ((digit < '0') || (digit > '9'))
        {
            return UBootBootstateFlags::UNKNOWN_STATE;
        }
        value = (value * 10U) + static_cast<unsigned int>(digit - '0');
    }

    if ((raw.size() == 2U) && (raw.front() == '0'))
    {
        /* "00".."09" are numerically in range but no writer produces them;
         * a non-canonical form is treated as corruption, not as its value. */
        return UBootBootstateFlags::UNKNOWN_STATE;
    }

    /* The accepted numeric values are governed by the shared allowed-values
     * list, so the accepted set has one declaration. The two-digit cap above
     * keeps the value within uint8_t, and the lookup neither allocates nor
     * throws. */
    const std::vector<uint8_t> &allowed = allowed_update_reboot_state_variables;
    if (std::find(allowed.cbegin(), allowed.cend(), static_cast<uint8_t>(value)) == allowed.cend())
    {
        return UBootBootstateFlags::UNKNOWN_STATE;
    }

    /* Enumerator values match the numerals; membership in the allowed set
     * makes the cast closed over named states. */
    return static_cast<UBootBootstateFlags>(value);
}

update_definitions::UBootBootstateFlags update_definitions::read_update_reboot_state(
    UBoot::IUBootEnv &env, const std::shared_ptr<logger::LoggerHandler> &log) noexcept
{
    try
    {
        const std::string raw = env.getVariable("update_reboot_state", &accept_any_content);
        const UBootBootstateFlags state = decode_update_reboot_state(raw);
        if (state == UBootBootstateFlags::UNKNOWN_STATE)
        {
            /* The raw content is the one fact a field diagnosis needs. */
            log_read_failure(log, "update_reboot_state holds uninterpretable content: \"", raw.c_str(), "\"");
        }
        return state;
    }
    catch (const std::exception &e)
    {
        /* Absent or unreadable variable: the state cannot be told. */
        log_read_failure(log, "update_reboot_state could not be read: ", e.what());
        return UBootBootstateFlags::UNKNOWN_STATE;
    }
    catch (...)
    {
        log_read_failure(log, "update_reboot_state could not be read", "");
        return UBootBootstateFlags::UNKNOWN_STATE;
    }
}
