#pragma once

#include <systemd/sd-bus.h>

namespace dbus {

class SdBusConnection {
public:
    SdBusConnection();
    explicit SdBusConnection(sd_bus* owned) noexcept;
    ~SdBusConnection();

    SdBusConnection(const SdBusConnection&)            = delete;
    SdBusConnection& operator=(const SdBusConnection&) = delete;
    SdBusConnection(SdBusConnection&&) noexcept;
    SdBusConnection& operator=(SdBusConnection&&) noexcept;

    [[nodiscard]] sd_bus* get() const noexcept;
    explicit operator bool() const noexcept;

private:
    sd_bus* bus_ = nullptr;
};

} // namespace dbus
