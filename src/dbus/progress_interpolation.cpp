#include "progress_interpolation.h"

#include <algorithm>

namespace rauc {

std::optional<int> next_emit(
    ProgressInterpolatorState& state,
    int real_pct,
    std::chrono::steady_clock::time_point now) noexcept
{
    if (real_pct < 0)   real_pct = 0;
    if (real_pct > 100) real_pct = 100;

    if (!state.primed) {
        state.anchor_pct   = real_pct;
        state.anchor_time  = now;
        state.last_emitted = real_pct;
        state.primed       = true;
        return real_pct;
    }

    int target;
    if (real_pct > state.anchor_pct) {
        state.anchor_pct  = real_pct;
        state.anchor_time = now;
        target            = real_pct;
    } else {
        const auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(
                                   now - state.anchor_time).count();
        int advance = (elapsed_s > 0)
            ? static_cast<int>(elapsed_s) * RAUC_INTERP_RATE_PCT_PER_SEC
            : 0;
        if (advance > RAUC_INTERP_HEADROOM_PCT) advance = RAUC_INTERP_HEADROOM_PCT;
        target = state.anchor_pct + advance;
        if (target > 100) target = 100;
    }

    target = std::max(target, state.last_emitted);

    if (target == state.last_emitted) {
        return std::nullopt;
    }
    state.last_emitted = target;
    return target;
}

} // namespace rauc
