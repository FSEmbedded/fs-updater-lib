#include <gtest/gtest.h>

#include "dbus/sd_bus_match.h"

namespace {

// ---- All tests use the test-only constructor; no live bus needed ----

TEST(SdBusMatchSlot, DefaultIsNull)
{
    dbus::SdBusMatchSlot slot;
    EXPECT_EQ(slot.get(), nullptr);
    EXPECT_FALSE(static_cast<bool>(slot));
}

TEST(SdBusMatchSlot, TestCtorStoresPointer)
{
    dbus::SdBusMatchSlot slot(static_cast<sd_bus_slot*>(nullptr));
    EXPECT_EQ(slot.get(), nullptr);
    EXPECT_FALSE(static_cast<bool>(slot));
}

TEST(SdBusMatchSlot, MoveCtorTransfersOwnership)
{
    dbus::SdBusMatchSlot a(static_cast<sd_bus_slot*>(nullptr));
    dbus::SdBusMatchSlot b(std::move(a));

    EXPECT_EQ(b.get(), nullptr);
    EXPECT_EQ(a.get(), nullptr); // NOLINT(bugprone-use-after-move)
}

TEST(SdBusMatchSlot, MoveAssignTransfersOwnership)
{
    dbus::SdBusMatchSlot a(static_cast<sd_bus_slot*>(nullptr));
    dbus::SdBusMatchSlot b(static_cast<sd_bus_slot*>(nullptr));
    b = std::move(a);

    EXPECT_EQ(b.get(), nullptr);
    EXPECT_EQ(a.get(), nullptr); // NOLINT(bugprone-use-after-move)
}

TEST(SdBusMatchSlot, SelfMoveAssignIsSafe)
{
    dbus::SdBusMatchSlot slot(static_cast<sd_bus_slot*>(nullptr));
    auto& ref = slot;
    slot = std::move(ref);
    EXPECT_EQ(slot.get(), nullptr);
}

TEST(SdBusMatchSlot, BoolFalseAfterMoveFrom)
{
    dbus::SdBusMatchSlot a(static_cast<sd_bus_slot*>(nullptr));
    dbus::SdBusMatchSlot b(std::move(a));
    EXPECT_FALSE(static_cast<bool>(a)); // NOLINT(bugprone-use-after-move)
}

TEST(SdBusMatchSlot, ResetClearsPointer)
{
    dbus::SdBusMatchSlot slot(static_cast<sd_bus_slot*>(nullptr));
    slot.reset();
    EXPECT_EQ(slot.get(), nullptr);
    EXPECT_FALSE(static_cast<bool>(slot));
}

TEST(SdBusMatchSlot, ResetIsIdempotent)
{
    dbus::SdBusMatchSlot slot(static_cast<sd_bus_slot*>(nullptr));
    slot.reset();
    slot.reset();
    EXPECT_EQ(slot.get(), nullptr);
}

} // namespace
