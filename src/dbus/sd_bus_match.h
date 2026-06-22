#pragma once

#include <systemd/sd-bus.h>

namespace dbus {

class SdBusMatchSlot {
public:
    SdBusMatchSlot() noexcept = default;
    explicit SdBusMatchSlot(sd_bus_slot* owned) noexcept;
    ~SdBusMatchSlot();

    SdBusMatchSlot(const SdBusMatchSlot&)            = delete;
    SdBusMatchSlot& operator=(const SdBusMatchSlot&) = delete;
    SdBusMatchSlot(SdBusMatchSlot&& /*other*/) noexcept;
    SdBusMatchSlot& operator=(SdBusMatchSlot&& /*other*/) noexcept;

    [[nodiscard]] sd_bus_slot* get() const noexcept;
    explicit operator bool() const noexcept;
    void reset() noexcept;

private:
    sd_bus_slot* slot_ = nullptr;
};

} // namespace dbus
