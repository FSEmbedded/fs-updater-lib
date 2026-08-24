#pragma once

#include "updateDefinitions.h"

#include <memory>
#include <string>

namespace UBoot
{
    class IUBootEnv;
}

namespace logger
{
    class LoggerHandler;
}

namespace update_definitions
{
    /**
     * Decode the raw environment content of update_reboot_state. Total: every
     * input yields a state, nothing throws. Anything the alphabet does not
     * contain -- non-numeric, empty, out of range -- yields UNKNOWN_STATE.
     * @param raw Raw string content of the environment variable.
     * @return update_definitions::UBootBootstateFlags Enum class.
     */
    [[nodiscard]] UBootBootstateFlags decode_update_reboot_state(const std::string &raw) noexcept;

    /**
     * Read update_reboot_state through the environment seam and decode it.
     * Total: an absent or unreadable variable yields UNKNOWN_STATE. Reads
     * only; never stages and never flushes a value. With a logger present,
     * a failed read and undecodable raw content are reported at error
     * level; logging failures are contained, the read stays total.
     * @param env Environment accessor to read through.
     * @param log Optional logger for the failure diagnostics.
     * @return update_definitions::UBootBootstateFlags Enum class.
     */
    [[nodiscard]] UBootBootstateFlags read_update_reboot_state(
        UBoot::IUBootEnv &env,
        const std::shared_ptr<logger::LoggerHandler> &log = nullptr) noexcept;
}
