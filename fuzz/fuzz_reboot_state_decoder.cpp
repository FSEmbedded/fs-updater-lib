// libFuzzer target for the durable-state decoder (reboot_state.h).
//
// The decoder is declared total and noexcept, and that declaration carries
// weight beyond style: the value it reads is written by one generation of the
// image and read by another, so an input the decoder cannot interpret is an
// ordinary event on a device in the field, not a programming error. A throw
// there would take down the verb a user runs to recover.
//
// The target therefore checks properties rather than merely calling the
// function. A target that only called it would find a crash and nothing else.
#include "handle_update/reboot_state.h"
#include "handle_update/updateDefinitions.h"
#include "uboot_interface/allowed_uboot_variable_states.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace {

// Every answer must be a published value or the sentinel. Anything else would
// be handed to a caller that trusts the decoder enough to persist it.
bool in_alphabet(update_definitions::UBootBootstateFlags state)
{
    if (state == update_definitions::UBootBootstateFlags::UNKNOWN_STATE) {
        return true;
    }
    for (const auto allowed : allowed_update_reboot_state_variables) {
        if (static_cast<int>(state) == static_cast<int>(allowed)) {
            return true;
        }
    }
    return false;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const std::string raw(reinterpret_cast<const char *>(data), size);

    const auto state = update_definitions::decode_update_reboot_state(raw);
    if (!in_alphabet(state)) {
        std::abort();
    }

    // Round trip: whatever the decoder accepted must render back to the same
    // value, or a write-then-read across a reboot would change the state. The
    // sentinel is excluded deliberately -- it has no encoding at all, and that
    // missing encoding is the structural guarantee that it is never persisted.
    if (state != update_definitions::UBootBootstateFlags::UNKNOWN_STATE) {
        const std::string rendered = update_definitions::to_string(state);
        if (update_definitions::decode_update_reboot_state(rendered) != state) {
            std::abort();
        }
    }
    return 0;
}
