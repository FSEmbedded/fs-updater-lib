#pragma once

namespace rauc {

enum class WaitLoss { None, Bus, Service };

/* What the wait loop lost, if that matters. A Completed verdict wins over
 * either loss: RAUC's Completed can be processed in the same batch that ends
 * with the connection closing or the service name vanishing (a service restart
 * right after a successful install does exactly that), and reporting the
 * transport instead of the verdict would revert a slot switch that worked. */
[[nodiscard]] constexpr WaitLoss classify_wait_loss(int bus_rc, bool completed, bool svc_lost) noexcept
{
    if (completed) {
        return WaitLoss::None;
    }
    if (bus_rc < 0) {
        return WaitLoss::Bus;
    }
    return svc_lost ? WaitLoss::Service : WaitLoss::None;
}

} // namespace rauc
