// Tests for tests/fault_injection/schedule_rng.cpp (REQ-TEST-005): the
// campaign's own draws. Test card TC-035 step 2: the arithmetic on the
// engine's words, checked against the sequence the C++ standard fixes, so
// a seed's schedule is the same on libc++ and libstdc++.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <random>

#include "schedule_rng.h"

// [rand.predef]: the 10,000th output of a default-constructed std::mt19937
// is 4123659995 on every conforming implementation. The harness's engine
// is that engine.
TEST(ScheduleRngTest, TheEngineIsTheStandardsMt19937) {
    ScheduleRng rng{std::mt19937{}};
    for (int i = 0; i < 9999; i++) {
        rng.next();
    }
    EXPECT_EQ(rng.next(), 4123659995u);
}

// The harness's arithmetic on the standard's first four words (3499211612,
// 581869302, 3890346734, 3586334585): below(3) is 3499211612 mod 3 = 2, no
// word rejected; between(1, 3) is 1 + 581869302 mod 3 = 1; uniform(0, 1)
// is 3890346734 / 2^32; coin() is the low bit of an odd word.
TEST(ScheduleRngTest, DrawsAreTheHarnesssArithmeticOnTheStandardsWords) {
    ScheduleRng rng{std::mt19937{}};
    EXPECT_EQ(rng.below(3), 2u);
    EXPECT_EQ(rng.between(1, 3), 1);
    EXPECT_EQ(rng.uniform(0.0, 1.0), 3890346734.0 / 4294967296.0);
    EXPECT_TRUE(rng.coin());
}

// below(n) never reaches n, and over many draws every value comes up about
// equally often: no modulo bias.
TEST(ScheduleRngTest, BelowIsInRangeAndEven) {
    ScheduleRng rng(7, 0x5eedu);
    std::array<int, 10> counts{};
    for (int i = 0; i < 100000; i++) {
        const uint32_t v = rng.below(10);
        ASSERT_LT(v, 10u);
        counts[v]++;
    }
    for (int c : counts) {
        EXPECT_NEAR(c, 10000, 500);
    }
    EXPECT_EQ(rng.below(1), 0u);
}

TEST(ScheduleRngTest, BetweenIncludesBothEnds) {
    ScheduleRng rng(11, 0x5eedu);
    bool low = false;
    bool high = false;
    for (int i = 0; i < 1000; i++) {
        const int64_t v = rng.between(2, 6);
        ASSERT_GE(v, 2);
        ASSERT_LE(v, 6);
        low = low || v == 2;
        high = high || v == 6;
    }
    EXPECT_TRUE(low);
    EXPECT_TRUE(high);
}

// Two words seed the engine through std::seed_seq, so a seed and a stream
// tag give sequences of their own.
TEST(ScheduleRngTest, SeedsAndTagsGiveDifferentSequences) {
    ScheduleRng a(1, 0x5eedu);
    ScheduleRng b(2, 0x5eedu);
    ScheduleRng c(1, 1);
    ScheduleRng a_again(1, 0x5eedu);
    const uint32_t first = a.next();
    EXPECT_NE(first, b.next());
    EXPECT_NE(first, c.next());
    EXPECT_EQ(first, a_again.next());
}
