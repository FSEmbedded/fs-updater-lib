#pragma once

#include <chrono>
#include <optional>

namespace rauc {

/* Synthetic interpolation between RAUC's sparse Progress emits.
 *
 * RAUC's de.pengutronix.rauc.Installer Progress property is updated
 * coarsely during slot writes — on eMMC a large firmware bundle shows
 * only a few distinct values across the whole write (e.g. 0 -> ~50 ->
 * 100). The user-visible bar stalls for tens of seconds at a time
 * between real ticks.
 *
 * `next_emit` advances the displayed value by a small fixed amount per
 * elapsed second of stall, capped at a small headroom past the last
 * real value so we don't overshoot RAUC's next genuine tick. When RAUC
 * eventually emits a higher real value the anchor resets and follows
 * it. The sequence is monotonic non-decreasing.
 */

inline constexpr int  RAUC_INTERP_RATE_PCT_PER_SEC = 1;
inline constexpr int  RAUC_INTERP_HEADROOM_PCT     = 5;
inline constexpr auto RAUC_INTERP_TICK             = std::chrono::milliseconds(500);

struct ProgressInterpolatorState
{
    int                                   anchor_pct{0};
    std::chrono::steady_clock::time_point anchor_time{};
    int                                   last_emitted{-1};
    bool                                  primed{false};
};

/* Update state with the latest real RAUC progress and return the next
 * value to emit, or nullopt if nothing changed since the previous call.
 * Pure function (no I/O); state is caller-owned for testability. */
[[nodiscard]] std::optional<int> next_emit(
    ProgressInterpolatorState& state,
    int real_pct,
    std::chrono::steady_clock::time_point now) noexcept;

} // namespace rauc
