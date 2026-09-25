#include <gtest/gtest.h>

#include "uboot_interface/allowed_uboot_variable_states.h"

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
}

} // namespace
