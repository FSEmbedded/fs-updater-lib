#include <gtest/gtest.h>

#include <unistd.h>

#include "dbus/sd_bus_connection.h"

namespace {

// ---- Move semantics (test-only constructor, no live bus needed) ----

TEST(SdBusConnection, DefaultIsNull)
{
    dbus::SdBusConnection conn(static_cast<sd_bus*>(nullptr));
    EXPECT_EQ(conn.get(), nullptr);
    EXPECT_FALSE(static_cast<bool>(conn));
}

TEST(SdBusConnection, MoveCtorTransfersOwnership)
{
    dbus::SdBusConnection a(static_cast<sd_bus*>(nullptr));
    dbus::SdBusConnection b(std::move(a));

    EXPECT_EQ(b.get(), nullptr);
    EXPECT_EQ(a.get(), nullptr); // NOLINT(bugprone-use-after-move)
}

TEST(SdBusConnection, MoveAssignTransfersOwnership)
{
    dbus::SdBusConnection a(static_cast<sd_bus*>(nullptr));
    dbus::SdBusConnection b(static_cast<sd_bus*>(nullptr));
    b = std::move(a);

    EXPECT_EQ(b.get(), nullptr);
    EXPECT_EQ(a.get(), nullptr); // NOLINT(bugprone-use-after-move)
}

TEST(SdBusConnection, SelfMoveAssignIsSafe)
{
    dbus::SdBusConnection conn(static_cast<sd_bus*>(nullptr));
    auto& ref = conn;
    conn = std::move(ref);
    EXPECT_EQ(conn.get(), nullptr);
}

TEST(SdBusConnection, BoolFalseAfterMoveFrom)
{
    dbus::SdBusConnection a(static_cast<sd_bus*>(nullptr));
    dbus::SdBusConnection b(std::move(a));
    EXPECT_FALSE(static_cast<bool>(a)); // NOLINT(bugprone-use-after-move)
}

// ---- Live bus (requires systemd on the test host) ----

static bool system_bus_available()
{
    return access("/run/systemd/private", F_OK) == 0
        || access("/run/dbus/system_bus_socket", F_OK) == 0;
}

TEST(SdBusConnection, PrimaryCtorOpensBus)
{
    if (!system_bus_available()) {
        GTEST_SKIP() << "systemd/D-Bus socket not found — skipping live-bus test";
    }

    dbus::SdBusConnection conn;
    EXPECT_NE(conn.get(), nullptr);
    EXPECT_TRUE(static_cast<bool>(conn));
}

TEST(SdBusConnection, MoveCtorFromLiveBus)
{
    if (!system_bus_available()) {
        GTEST_SKIP() << "systemd/D-Bus socket not found — skipping live-bus test";
    }

    dbus::SdBusConnection a;
    sd_bus* raw = a.get();
    ASSERT_NE(raw, nullptr);

    dbus::SdBusConnection b(std::move(a));
    EXPECT_EQ(b.get(), raw);
    EXPECT_EQ(a.get(), nullptr); // NOLINT(bugprone-use-after-move)
}

} // namespace
