#include <gtest/gtest.h>

#include "uboot_interface/allowed_uboot_variable_states.h"
#include "handle_update/utils.h"

#include <algorithm>
#include <string>

// Not covered here: the throwing path in UBoot::getVariable() needs
// libubootenv and is not exercisable in this harness. That behaviour is
// proven on hardware instead.

namespace {

TEST(AllowedBootOrderVariables, AcceptsSingleSlotOrder)
{
    EXPECT_NE(std::find(allowed_boot_order_variables.begin(), allowed_boot_order_variables.end(), "A"),
              allowed_boot_order_variables.end());
    EXPECT_NE(std::find(allowed_boot_order_variables.begin(), allowed_boot_order_variables.end(), "B"),
              allowed_boot_order_variables.end());
}

TEST(AllowedBootOrderVariables, StillRejectsMalformedOrders)
{
    EXPECT_EQ(std::find(allowed_boot_order_variables.begin(), allowed_boot_order_variables.end(), ""),
              allowed_boot_order_variables.end());
    EXPECT_EQ(std::find(allowed_boot_order_variables.begin(), allowed_boot_order_variables.end(), "a"),
              allowed_boot_order_variables.end());
    EXPECT_EQ(std::find(allowed_boot_order_variables.begin(), allowed_boot_order_variables.end(), "A  B"),
              allowed_boot_order_variables.end());
    EXPECT_EQ(std::find(allowed_boot_order_variables.begin(), allowed_boot_order_variables.end(), "AB"),
              allowed_boot_order_variables.end());
    // A trailing/leading space would also split to two fields (see BootOrderSplitSize
    // below) without naming two slots - the whitelist must never admit one.
    EXPECT_EQ(std::find(allowed_boot_order_variables.begin(), allowed_boot_order_variables.end(), "A "),
              allowed_boot_order_variables.end());
    EXPECT_EQ(std::find(allowed_boot_order_variables.begin(), allowed_boot_order_variables.end(), " A"),
              allowed_boot_order_variables.end());
    EXPECT_EQ(std::find(allowed_boot_order_variables.begin(), allowed_boot_order_variables.end(), "A B "),
              allowed_boot_order_variables.end());
}

// Pins the mechanism the single-slot guard in Bootstate::firmware_update_reboot_*
// relies on: a single-slot boot order splits to size 1, a two-slot order to size 2.
// The predicates themselves are Bootstate members and need libubootenv to reach;
// that behaviour is proven on hardware instead.
TEST(BootOrderSplitSize, DistinguishesSingleFromTwoSlotOrder)
{
    EXPECT_EQ(util::split("A", ' ').size(), 1u);
    EXPECT_EQ(util::split("A B", ' ').size(), 2u);
}

} // namespace
