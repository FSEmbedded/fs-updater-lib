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

    /* Take care to change or add new values. Dependency to package dynamic overlay
     * available.
     *
     * Every value carries a `flow:` marker saying whether a defined flow still
     * writes it. The marker is a claim about this source tree; a consumer's build
     * gate re-counts the writers and fails when the two disagree, so the marker
     * cannot quietly rot. Meaning:
     *
     *   live            a flow in this tree writes it
     *   legacy-inbound  nothing writes it any more; an older generation did, and a
     *                   device flashed then can still carry it, so it must stay
     *                   readable and recoverable
     *   reserved        nothing writes it and nothing is expected to until the
     *                   flow that did is decided; the number is held, never reused
     *   sentinel        never written by design -- the decoder's answer
     *
     * A value no flow writes is not dead weight to be reclaimed: readers outside
     * this repository branch on the raw numbers, so the numbering must outlive
     * the flow.
     */
    enum class UBootBootstateFlags : unsigned char
    {
        NO_UPDATE_REBOOT_PENDING = 0,       /* flow: live */
        FW_UPDATE_REBOOT_FAILED = 1,        /* flow: legacy-inbound */
        INCOMPLETE_FW_UPDATE = 2,           /* flow: live */
        INCOMPLETE_APP_UPDATE = 3,          /* flow: live */
        INCOMPLETE_APP_FW_UPDATE = 4,       /* flow: live */
        FAILED_FW_UPDATE = 5,               /* flow: live */
        FAILED_APP_UPDATE = 6,              /* flow: live */
        ROLLBACK_FW_REBOOT_PENDING = 7,     /* flow: live */
        ROLLBACK_APP_REBOOT_PENDING = 8,    /* flow: live */
        ROLLBACK_APP_FW_REBOOT_PENDING = 9, /* flow: live */
        /* Nothing writes these three; a device flashed earlier can still carry
         * one, so they stay decodable and a commit finalizes them. Unlike
         * FW_UPDATE_REBOOT_FAILED they keep their slot precondition and report
         * no distinct outcome, so a device recovered from one is not visible as
         * such to the caller.
         */
        INCOMPLETE_FW_ROLLBACK = 10,        /* flow: legacy-inbound */
        INCOMPLETE_APP_ROLLBACK = 11,       /* flow: legacy-inbound */
        INCOMPLETE_APP_FW_ROLLBACK = 12,    /* flow: legacy-inbound */
        /* Recovery state: the environment holds a value this version cannot
         * interpret (also serves as the last-element marker). Read-only by
         * design: to_string() has no case for it, so it can never be written
         * into the environment.
         */
        UNKNOWN_STATE = 13                  /* flow: sentinel */
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
     * Convert enum class update_definitions::UBootBootstateFlags to string.
     * @param enum_state Value that should be converted into an one char string value.
     * @return One char string of update_definitions::UBootBootstateFlags.
     * @throw std::logic_error If state of UBootBootstateFlags is nit defined in function.
     */
    std::string to_string(UBootBootstateFlags enum_state);

    /**
     * Render a state for diagnostics. Real states render as their canonical
     * numeral; the recovery state renders as prose, because no numeral for
     * it can exist in the environment.
     * @param enum_state Value to render.
     * @return Diagnostic string.
     */
    std::string describe(UBootBootstateFlags enum_state);
}
