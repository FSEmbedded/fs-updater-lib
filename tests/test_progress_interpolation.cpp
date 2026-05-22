#include <gtest/gtest.h>

#include "dbus/progress_interpolation.h"

#include <chrono>

using rauc::next_emit;
using rauc::ProgressInterpolatorState;
using rauc::RAUC_INTERP_HEADROOM_PCT;
using rauc::RAUC_INTERP_RATE_PCT_PER_SEC;

using clock_t_ = std::chrono::steady_clock;

namespace {

clock_t_::time_point t0()
{
    return clock_t_::time_point{};   // zero point — arbitrary base for tests
}

clock_t_::time_point at_secs(std::int64_t s)
{
    return t0() + std::chrono::seconds(s);
}

} // namespace

TEST(ProgressInterp, FirstCallEmitsRealValue)
{
    ProgressInterpolatorState s{};
    const auto out = next_emit(s, 0, t0());
    ASSERT_TRUE(out);
    EXPECT_EQ(*out, 0);
    EXPECT_TRUE(s.primed);
}

TEST(ProgressInterp, RealAdvanceResetsAnchorAndEmits)
{
    ProgressInterpolatorState s{};
    (void)next_emit(s, 0, t0());
    const auto out = next_emit(s, 50, at_secs(1));
    ASSERT_TRUE(out);
    EXPECT_EQ(*out, 50);
    EXPECT_EQ(s.anchor_pct, 50);
}

TEST(ProgressInterp, StallEmitsInterpolatedAdvanceUpToHeadroom)
{
    ProgressInterpolatorState s{};
    (void)next_emit(s, 50, t0());   // anchor at 50

    // 1s later: anchor + 1 = 51
    auto e1 = next_emit(s, 50, at_secs(1));
    ASSERT_TRUE(e1);
    EXPECT_EQ(*e1, 50 + 1 * RAUC_INTERP_RATE_PCT_PER_SEC);

    // 2s later: anchor + 2 = 52
    auto e2 = next_emit(s, 50, at_secs(2));
    ASSERT_TRUE(e2);
    EXPECT_EQ(*e2, 50 + 2 * RAUC_INTERP_RATE_PCT_PER_SEC);

    // HEADROOM seconds later: capped at anchor + HEADROOM
    auto e3 = next_emit(s, 50, at_secs(RAUC_INTERP_HEADROOM_PCT));
    ASSERT_TRUE(e3);
    EXPECT_EQ(*e3, 50 + RAUC_INTERP_HEADROOM_PCT);

    // Past HEADROOM: still capped, no new emit
    auto e4 = next_emit(s, 50, at_secs(RAUC_INTERP_HEADROOM_PCT + 5));
    EXPECT_FALSE(e4);
}

TEST(ProgressInterp, RealCatchupAfterStallReanchorsAndAdvances)
{
    ProgressInterpolatorState s{};
    (void)next_emit(s, 50, t0());                   // anchor=50, emit=50
    (void)next_emit(s, 50, at_secs(3));             // interp=53, emit=53
    auto e = next_emit(s, 80, at_secs(4));    // real jumps past interp
    ASSERT_TRUE(e);
    EXPECT_EQ(*e, 80);
    EXPECT_EQ(s.anchor_pct, 80);
}

TEST(ProgressInterp, RealRegressionDoesNotMoveAnchor)
{
    ProgressInterpolatorState s{};
    (void)next_emit(s, 80, t0());                   // anchor=80
    auto e = next_emit(s, 50, at_secs(1));    // real lower; stall path
    // Anchor stays at 80; interp advances from 80, gated by last_emitted.
    EXPECT_EQ(s.anchor_pct, 80);
    ASSERT_TRUE(e);
    EXPECT_EQ(*e, 81);
}

TEST(ProgressInterp, MonotonicNeverDecreases)
{
    ProgressInterpolatorState s{};
    (void)next_emit(s, 50, t0());
    (void)next_emit(s, 50, at_secs(5));             // emit reaches 55
    auto e = next_emit(s, 30, at_secs(6));    // real drops to 30
    // Must not return a value below the last emitted 55.
    EXPECT_FALSE(e);                          // nothing to emit (still 55)
    EXPECT_EQ(s.last_emitted, 55);
}

TEST(ProgressInterp, ClampsOver100)
{
    ProgressInterpolatorState s{};
    (void)next_emit(s, 98, t0());
    auto e = next_emit(s, 98, at_secs(10));   // would be 98+5=103, capped
    ASSERT_TRUE(e);
    EXPECT_EQ(*e, 100);
}

TEST(ProgressInterp, ClampsNegativeRealToZero)
{
    ProgressInterpolatorState s{};
    auto e = next_emit(s, -7, t0());
    ASSERT_TRUE(e);
    EXPECT_EQ(*e, 0);
}

TEST(ProgressInterp, ClampsRealOver100)
{
    ProgressInterpolatorState s{};
    auto e = next_emit(s, 150, t0());
    ASSERT_TRUE(e);
    EXPECT_EQ(*e, 100);
}

TEST(ProgressInterp, NoEmitWhenNothingChanges)
{
    ProgressInterpolatorState s{};
    (void)next_emit(s, 50, t0());
    auto e = next_emit(s, 50, t0());          // same call, same time
    EXPECT_FALSE(e);
}

TEST(ProgressInterp, SimulatedRaucSparseSequenceProducesSmoothBar)
{
    /* Simulate the observed real sequence: RAUC ticks at 0s=0, 5s=50,
     * 12s=100. We poll every 500ms via next_emit. Expect intermediate
     * synthetic ticks during each stall, capped at HEADROOM. */
    ProgressInterpolatorState s{};

    auto poll = [&](int real, std::int64_t s_at) {
        return next_emit(s, real, at_secs(s_at));
    };

    auto e0  = poll(0,  0);  ASSERT_TRUE(e0);  EXPECT_EQ(*e0, 0);
    auto e1  = poll(0,  1);  ASSERT_TRUE(e1);  EXPECT_EQ(*e1, 1);
    auto e2  = poll(0,  5);  ASSERT_TRUE(e2);  EXPECT_EQ(*e2, RAUC_INTERP_HEADROOM_PCT);
    auto e3  = poll(50, 5);  ASSERT_TRUE(e3);  EXPECT_EQ(*e3, 50);
    auto e4  = poll(50, 8);  ASSERT_TRUE(e4);  EXPECT_EQ(*e4, 53);
    auto e5  = poll(100, 12); ASSERT_TRUE(e5); EXPECT_EQ(*e5, 100);
}
