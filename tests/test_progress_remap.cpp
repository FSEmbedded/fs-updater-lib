#include <gtest/gtest.h>

#include "handle_update/progress_remap.h"

using fs::remap_extract_progress;

TEST(ProgressRemap, ZeroMapsToExtractPct)
{
    EXPECT_EQ(remap_extract_progress(0, 20), 20);
    EXPECT_EQ(remap_extract_progress(0, 0),  0);
    EXPECT_EQ(remap_extract_progress(0, 50), 50);
}

TEST(ProgressRemap, HundredAlwaysMapsToHundred)
{
    EXPECT_EQ(remap_extract_progress(100, 20),  100);
    EXPECT_EQ(remap_extract_progress(100, 0),   100);
    EXPECT_EQ(remap_extract_progress(100, 99),  100);
}

TEST(ProgressRemap, MidpointMapsToMidpointOfRemainingBand)
{
    // 50 of [20..100] is 20 + 40 = 60
    EXPECT_EQ(remap_extract_progress(50, 20), 60);
    // 50 of [40..100] is 40 + 30 = 70
    EXPECT_EQ(remap_extract_progress(50, 40), 70);
}

TEST(ProgressRemap, ZeroExtractPctIsIdentity)
{
    EXPECT_EQ(remap_extract_progress(0,   0), 0);
    EXPECT_EQ(remap_extract_progress(42,  0), 42);
    EXPECT_EQ(remap_extract_progress(100, 0), 100);
}

TEST(ProgressRemap, ClampsNegativePToExtractPct)
{
    EXPECT_EQ(remap_extract_progress(-5,   20), 20);
    EXPECT_EQ(remap_extract_progress(-100, 20), 20);
}

TEST(ProgressRemap, ClampsAbove100ToHundred)
{
    EXPECT_EQ(remap_extract_progress(150, 20), 100);
    EXPECT_EQ(remap_extract_progress(101, 0),  100);
}

TEST(ProgressRemap, ClampsExtractPctOutOfRange)
{
    // Negative extract_pct → treated as 0 → identity remap
    EXPECT_EQ(remap_extract_progress(50, -10), 50);
    // extract_pct > 100 → treated as 100 → always pin at 100
    EXPECT_EQ(remap_extract_progress(50, 200), 100);
    EXPECT_EQ(remap_extract_progress(0,  200), 100);
}

TEST(ProgressRemap, MonotonicallyNonDecreasing)
{
    // For a fixed extract_pct, the remap should be monotonically
    // non-decreasing in p — subscribers should never see a backwards step.
    constexpr int pct = 20;
    int prev = remap_extract_progress(0, pct);
    for (int p = 1; p <= 100; ++p)
    {
        const int cur = remap_extract_progress(p, pct);
        EXPECT_GE(cur, prev) << "remap(" << p << ") < remap(" << (p - 1) << ")";
        prev = cur;
    }
}
