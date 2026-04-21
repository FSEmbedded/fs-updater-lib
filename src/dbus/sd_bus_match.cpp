#include "sd_bus_match.h"

namespace dbus {

SdBusMatchSlot::SdBusMatchSlot(sd_bus_slot* owned) noexcept
    : slot_(owned)
{
}

SdBusMatchSlot::~SdBusMatchSlot()
{
    sd_bus_slot_unref(slot_);
}

SdBusMatchSlot::SdBusMatchSlot(SdBusMatchSlot&& other) noexcept
    : slot_(other.slot_)
{
    other.slot_ = nullptr;
}

SdBusMatchSlot& SdBusMatchSlot::operator=(SdBusMatchSlot&& other) noexcept
{
    if (this != &other) {
        sd_bus_slot_unref(slot_);
        slot_       = other.slot_;
        other.slot_ = nullptr;
    }
    return *this;
}

sd_bus_slot* SdBusMatchSlot::get() const noexcept
{
    return slot_;
}

SdBusMatchSlot::operator bool() const noexcept
{
    return slot_ != nullptr;
}

void SdBusMatchSlot::reset() noexcept
{
    sd_bus_slot_unref(slot_);
    slot_ = nullptr;
}

} // namespace dbus
