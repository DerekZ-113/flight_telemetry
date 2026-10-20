// Tests for src/timing/timestamp_pacer.cpp (REQ-TIME-004). Test card
// TC-020 steps 1 through 5, on the fake clock of TC-009: the pacer maps each
// recorded timestamp to a deadline measured from the first frame, sleeps
// until it through the Clock seam, never waits on a deadline that has
// passed, and reports lateness without accumulating it.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "fake_clock.h"
#include "timing/timestamp_pacer.h"

namespace {

constexpr uint64_t kMs = 1'000'000ull;       // nanoseconds per millisecond
constexpr uint64_t kPeriodNs = 20 * kMs;     // the log's own period: the unit of lateness

// The fake clock, also recording every target it was asked to sleep until.
class RecordingClock : public FakeClock {
public:
    void sleep_until_ns(uint64_t target_ns) override {
        targets.push_back(target_ns);
        FakeClock::sleep_until_ns(target_ns);
    }
    std::vector<uint64_t> targets;
};

}  // namespace

// Step 1: frames at 0, 20, 40 and 100 ms map to origin + 0, 20, 40 and
// 100 ms; a punctual clock reports no lateness; the cycle index counts.
TEST(PacerTest, DeadlinesFollowRecordedTimestamps) {
    RecordingClock clock;
    const uint64_t origin = clock.now_ns();
    TimestampPacer pacer(clock, kPeriodNs);
    const uint64_t stamps[] = {0, 20, 40, 100};
    for (size_t i = 0; i < 4; i++) {
        const CycleTiming t = pacer.wait_for(stamps[i]);
        EXPECT_EQ(t.cycle, i);
        EXPECT_EQ(t.scheduled_ns, origin + stamps[i] * kMs) << "frame " << i;
        EXPECT_EQ(t.actual_ns, origin + stamps[i] * kMs) << "frame " << i;
        EXPECT_EQ(t.jitter_ns, 0) << "frame " << i;
        EXPECT_EQ(t.missed_cycles, 0u) << "frame " << i;
    }
    const std::vector<uint64_t> expected = {origin, origin + 20 * kMs, origin + 40 * kMs, origin + 100 * kMs};
    EXPECT_EQ(clock.targets, expected);
}

// Step 2: a clock that wakes 3 ms late every time reports 3 ms on every
// frame, and every deadline stays origin-relative: lateness never
// accumulates, as the scheduler's never does.
TEST(PacerTest, LateWakeIsReportedNotAccumulated) {
    RecordingClock clock;
    clock.late_by_ns_ = 3 * kMs;
    const uint64_t origin = clock.now_ns();
    TimestampPacer pacer(clock, kPeriodNs);
    const uint64_t stamps[] = {0, 20, 40, 60};
    for (size_t i = 0; i < 4; i++) {
        const CycleTiming t = pacer.wait_for(stamps[i]);
        EXPECT_EQ(t.scheduled_ns, origin + stamps[i] * kMs) << "frame " << i;
        EXPECT_EQ(t.jitter_ns, static_cast<int64_t>(3 * kMs)) << "frame " << i;
        EXPECT_EQ(t.missed_cycles, 0u) << "frame " << i;   // 3 ms is under a period
    }
}

// Step 3: a timestamp behind the previous one has a deadline in the past.
// The pacer asks the clock for it anyway (the contract returns at once), time
// does not move, the lateness is positive, and the frame is not skipped:
// whole periods of lateness are reported as the scheduler reports missed
// cycles.
TEST(PacerTest, BackwardsTimestampReleasesAtOnce) {
    RecordingClock clock;
    const uint64_t origin = clock.now_ns();
    TimestampPacer pacer(clock, kPeriodNs);
    pacer.wait_for(0);
    pacer.wait_for(100);
    const uint64_t before = clock.now_ns();
    EXPECT_EQ(before, origin + 100 * kMs);

    const CycleTiming t = pacer.wait_for(50);
    EXPECT_EQ(clock.now_ns(), before);
    EXPECT_EQ(t.scheduled_ns, origin + 50 * kMs);
    EXPECT_EQ(t.actual_ns, before);
    EXPECT_EQ(t.jitter_ns, static_cast<int64_t>(50 * kMs));
    EXPECT_EQ(t.missed_cycles, 2u);   // 50 ms late is two whole periods
    EXPECT_EQ(clock.targets.back(), origin + 50 * kMs);
}

// Step 3, the arithmetic's edge: a deadline that would fall before the
// clock's epoch is clamped to zero rather than wrapping.
TEST(PacerTest, DeadlineBeforeTheClockEpochIsClampedToZero) {
    RecordingClock clock;
    clock.now_ = 1'000 * kMs;   // one second after the epoch
    TimestampPacer pacer(clock, kPeriodNs);
    pacer.wait_for(5000);
    const CycleTiming t = pacer.wait_for(0);   // five seconds before an origin one second in
    EXPECT_EQ(t.scheduled_ns, 0u);
    EXPECT_EQ(clock.targets.back(), 0u);
    EXPECT_EQ(t.actual_ns, 1'000 * kMs);
    EXPECT_EQ(t.jitter_ns, static_cast<int64_t>(1'000 * kMs));
}

// Step 4: a gap in the log is honoured as recorded, one sleep to the far
// side of it.
TEST(PacerTest, GapIsHonoured) {
    RecordingClock clock;
    const uint64_t origin = clock.now_ns();
    TimestampPacer pacer(clock, kPeriodNs);
    pacer.wait_for(0);
    const CycleTiming t = pacer.wait_for(1000);
    EXPECT_EQ(clock.now_ns(), origin + 1000 * kMs);
    EXPECT_EQ(t.scheduled_ns, origin + 1000 * kMs);
    EXPECT_EQ(t.jitter_ns, 0);
    EXPECT_EQ(clock.targets.size(), 2u);
}

// Step 5: the first call sets the origin, (now, its timestamp), so a log
// whose first frame is stamped 3000 ms starts now, not three seconds from
// now.
TEST(PacerTest, FirstFrameSetsTheOrigin) {
    RecordingClock clock;
    clock.advance(123 * kMs);
    const uint64_t t0 = clock.now_ns();
    TimestampPacer pacer(clock, kPeriodNs);
    const CycleTiming first = pacer.wait_for(3000);
    EXPECT_EQ(first.scheduled_ns, t0);
    EXPECT_EQ(first.cycle, 0u);
    const CycleTiming second = pacer.wait_for(3020);
    EXPECT_EQ(second.scheduled_ns, t0 + 20 * kMs);
    EXPECT_EQ(second.cycle, 1u);
}
