#include <gtest/gtest.h>

#include "dbus/wait_loss.h"

using rauc::classify_wait_loss;
using rauc::WaitLoss;

TEST(WaitLoss, NothingLostIsNone)
{
    EXPECT_EQ(classify_wait_loss(0, false, false), WaitLoss::None);
}

TEST(WaitLoss, BusErrorWithoutVerdictIsBus)
{
    EXPECT_EQ(classify_wait_loss(-32, false, false), WaitLoss::Bus);
}

TEST(WaitLoss, VanishedServiceWithoutVerdictIsService)
{
    EXPECT_EQ(classify_wait_loss(0, false, true), WaitLoss::Service);
}

TEST(WaitLoss, BusErrorOutranksVanishedService)
{
    EXPECT_EQ(classify_wait_loss(-32, false, true), WaitLoss::Bus);
}

TEST(WaitLoss, ArrivedVerdictBeatsVanishedService)
{
    EXPECT_EQ(classify_wait_loss(0, true, true), WaitLoss::None);
}

TEST(WaitLoss, ArrivedVerdictBeatsBusError)
{
    EXPECT_EQ(classify_wait_loss(-32, true, false), WaitLoss::None);
    EXPECT_EQ(classify_wait_loss(-32, true, true), WaitLoss::None);
}
