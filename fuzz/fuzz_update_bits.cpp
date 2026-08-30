// libFuzzer target for the update bitfield validator and its digit helpers
// (allowed_uboot_variable_states.h).
//
// The field is four characters in the boot environment, one per slot. The
// validator is the only thing between a caller and a value that every later
// read would refuse -- and a value refused on read is exactly the shape that
// once left a device unable to report its own state.
//
// The target restates the rule independently and compares the two answers.
// That differential is the point: a validator drifting from the documented
// rule would still look correct to a test that asked it what it thinks.
#include "uboot_interface/allowed_uboot_variable_states.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace {

// The rule as documented, written out here rather than reused from the header:
// four characters, each a digit in range, and at most one slot per dimension
// carrying the in-flight bit.
bool rule_says_valid(const std::string &val)
{
    if (val.size() != 4) {
        return false;
    }
    int in_flight_fw = 0;
    int in_flight_app = 0;
    for (std::size_t i = 0; i < val.size(); ++i) {
        const char c = val[i];
        if (c < '0' || c > '3') {
            return false;
        }
        const int digit = c - '0';
        if ((digit & STATE_UPDATE_UNCOMMITED) == 0) {
            continue;
        }
        if (static_cast<int>(i) == FIRMWARE_A_INDEX || static_cast<int>(i) == FIRMWARE_B_INDEX) {
            ++in_flight_fw;
        } else {
            ++in_flight_app;
        }
    }
    return in_flight_fw <= 1 && in_flight_app <= 1;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const std::string val(reinterpret_cast<const char *>(data), size);

    if (validate_update_bits(val) != rule_says_valid(val)) {
        std::abort();
    }

    // The digit helpers are total over a byte and must stay single-purpose:
    // settling may only clear the in-flight bit, and marking bad may only set
    // the bad bit. A helper that touched the other one would forget a verdict
    // or invent one, and both have happened elsewhere in this field's history.
    for (std::size_t i = 0; i < val.size(); ++i) {
        const auto digit = static_cast<uint8_t>(val[i]);
        if (digit < '0' || digit > '3') {
            continue;
        }
        const int before = digit - '0';
        const int settled = digit_settled(digit) - '0';
        const int marked = digit_marked_bad(digit) - '0';

        if ((settled & STATE_UPDATE_UNCOMMITED) != 0) {
            std::abort();
        }
        if ((settled & STATE_UPDATE_BAD) != (before & STATE_UPDATE_BAD)) {
            std::abort();
        }
        if ((marked & STATE_UPDATE_BAD) == 0) {
            std::abort();
        }
        if ((marked & STATE_UPDATE_UNCOMMITED) != (before & STATE_UPDATE_UNCOMMITED)) {
            std::abort();
        }
        if (digit_in_flight(digit) != ((before & STATE_UPDATE_UNCOMMITED) != 0)) {
            std::abort();
        }
    }
    return 0;
}
