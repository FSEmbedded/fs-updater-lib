#include "sd_bus_connection.h"

#include <stdexcept>
#include <string>

namespace dbus {

SdBusConnection::SdBusConnection()
{
    int const r = sd_bus_default_system(&bus_);
    if (r < 0) {
        throw std::runtime_error(
            std::string("sd_bus_default_system failed: ") + strerror(-r));
    }
}

SdBusConnection::SdBusConnection(sd_bus* owned) noexcept
    : bus_(owned)
{
}

SdBusConnection::~SdBusConnection()
{
    sd_bus_unref(bus_);
}

SdBusConnection::SdBusConnection(SdBusConnection&& other) noexcept
    : bus_(other.bus_)
{
    other.bus_ = nullptr;
}

SdBusConnection& SdBusConnection::operator=(SdBusConnection&& other) noexcept
{
    if (this != &other) {
        sd_bus_unref(bus_);
        bus_       = other.bus_;
        other.bus_ = nullptr;
    }
    return *this;
}

sd_bus* SdBusConnection::get() const noexcept
{
    return bus_;
}

SdBusConnection::operator bool() const noexcept
{
    return bus_ != nullptr;
}

} // namespace dbus
