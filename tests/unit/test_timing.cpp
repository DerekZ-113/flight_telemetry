// Tests for src/timing (REQ-TIME-001, REQ-TIME-002). Test card TC-009.
//
// Almost every test drives the scheduler with a FakeClock that advances
// time by fiat, so a thousand cycles take microseconds and nothing
// sleeps (FTS-TP-001 section 2.1). Three tests at the end use the real
// MonotonicClock: the OS boundary has to be exercised once or a typo in
// the clock_nanosleep call ships. Those assert only what the OS
// guarantees (never early, monotonic) with loose upper bounds so CI
// cannot flake on a slow runner.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "timing/clock.h"
#include "timing/fixed_rate_scheduler.h"
#include "timing/jitter_log.h"

namespace {

constexpr uint64_t kPeriod = 20'000'000;   // 20 ms, 50 Hz
constexpr uint64_t kMs = 1'000'000;
constexpr uint64_t kUs = 1'000;

// A clock whose time moves only when the test says so.
class FakeClock : public Clock {
public:
    uint64_t now_ns() override { return now_; }

    // "Sleep" by jumping to the target, then add whatever wake latency the
    // test configured. A target already in the past does not move time
    // backward, matching the real contract.
    void sleep_until_ns(uint64_t target_ns) override {
        sleeps_++;
        if (target_ns > now_) {
            now_ = target_ns;
        }
        now_ += late_by_ns_;
    }

    void advance(uint64_t ns) { now_ += ns; }   // simulated processing time

    uint64_t now_ = 1'000'000'000ull;           // arbitrary epoch, like the real clock
    uint64_t late_by_ns_ = 0;
    int sleeps_ = 0;
};

}  // namespace

// The requirement in numbers. With 5 ms of work and 0.3 ms of wake latency
// every cycle, a loop that slept "period" after its work would run at
// 25.3 ms per cycle and be 5.3 s late after 1000 cycles. Absolute
// deadlines keep every cycle on start + n * period exactly. (REQ-TIME-001)
TEST(TimingTest, DeadlinesAdvanceByExactPeriod) {
    FakeClock clock;
    clock.late_by_ns_ = 300 * kUs;
    FixedRateScheduler scheduler(clock, kPeriod);
    const uint64_t start = clock.now_ns();
    scheduler.start();

    constexpr int kCycles = 1000;
    uint64_t last_scheduled = 0;
    for (int n = 0; n < kCycles; n++) {
        const CycleTiming t = scheduler.wait_for_next_cycle();
        ASSERT_EQ(t.scheduled_ns, start + static_cast<uint64_t>(n + 1) * kPeriod) << "cycle " << n;
        ASSERT_EQ(t.missed_cycles, 0u) << "cycle " << n;
        last_scheduled = t.scheduled_ns;
        clock.advance(5 * kMs);
    }

    const uint64_t absolute_span = last_scheduled - start;
    const uint64_t naive_span = static_cast<uint64_t>(kCycles) * (kPeriod + 5 * kMs + 300 * kUs);
    EXPECT_EQ(absolute_span, static_cast<uint64_t>(kCycles) * kPeriod);
    EXPECT_EQ(naive_span - absolute_span, 5'300 * kMs);   // the drift that did not happen
}

// jitter = actual - scheduled, per cycle. (REQ-TIME-002)
TEST(TimingTest, JitterIsActualMinusScheduled) {
    FakeClock clock;
    clock.late_by_ns_ = 300 * kUs;
    FixedRateScheduler scheduler(clock, kPeriod);
    for (int n = 0; n < 10; n++) {
        const CycleTiming t = scheduler.wait_for_next_cycle();
        EXPECT_EQ(t.jitter_ns, static_cast<int64_t>(300 * kUs)) << "cycle " << n;
        EXPECT_EQ(t.actual_ns, t.scheduled_ns + 300 * kUs);
    }
}

TEST(TimingTest, PunctualClockHasZeroJitterAndNoMisses) {
    FakeClock clock;
    FixedRateScheduler scheduler(clock, kPeriod);
    for (int n = 0; n < 10; n++) {
        const CycleTiming t = scheduler.wait_for_next_cycle();
        EXPECT_EQ(t.jitter_ns, 0);
        EXPECT_EQ(t.missed_cycles, 0u);
        EXPECT_EQ(t.actual_ns, t.scheduled_ns);
    }
}

// Work that fits inside the period leaves the grid untouched and sleeps
// exactly once per cycle. (REQ-TIME-001)
TEST(TimingTest, WorkWithinPeriodDoesNotShiftPhase) {
    FakeClock clock;
    FixedRateScheduler scheduler(clock, kPeriod);
    const uint64_t start = clock.now_ns();
    scheduler.start();
    for (int n = 0; n < 50; n++) {
        const CycleTiming t = scheduler.wait_for_next_cycle();
        EXPECT_EQ(t.scheduled_ns, start + static_cast<uint64_t>(n + 1) * kPeriod);
        clock.advance(15 * kMs);   // 75% of the period
    }
    EXPECT_EQ(clock.sleeps_, 50);
}

// Late by less than a period: that is jitter, the deadline stands, nothing
// is missed. (REQ-TIME-001, REQ-TIME-002)
TEST(TimingTest, LateByLessThanPeriodIsJitterNotAMiss) {
    FakeClock clock;
    FixedRateScheduler scheduler(clock, kPeriod);
    const uint64_t start = clock.now_ns();
    scheduler.wait_for_next_cycle();          // scheduled at start + P
    clock.advance(25 * kMs);                  // 5 ms past the next deadline

    const CycleTiming t = scheduler.wait_for_next_cycle();
    EXPECT_EQ(t.scheduled_ns, start + 2 * kPeriod);
    EXPECT_EQ(t.jitter_ns, static_cast<int64_t>(5 * kMs));
    EXPECT_EQ(t.missed_cycles, 0u);
}

// The overrun policy. 65 ms of work after the first cycle with a 20 ms
// period: the deadlines at 2P and 3P are gone. The scheduler skips them,
// reports missed = 2, runs the 4P cycle 5 ms late, and the cycle after
// that lands on 5P. The grid never moves. (REQ-TIME-001)
TEST(TimingTest, OverrunRephasesAndCountsMissed) {
    FakeClock clock;
    FixedRateScheduler scheduler(clock, kPeriod);
    const uint64_t start = clock.now_ns();
    scheduler.wait_for_next_cycle();          // 1P
    clock.advance(65 * kMs);                  // now = start + 85 ms

    const CycleTiming late = scheduler.wait_for_next_cycle();
    EXPECT_EQ(late.missed_cycles, 2u);
    EXPECT_EQ(late.scheduled_ns, start + 4 * kPeriod);
    EXPECT_EQ(late.jitter_ns, static_cast<int64_t>(5 * kMs));

    const CycleTiming next = scheduler.wait_for_next_cycle();
    EXPECT_EQ(next.scheduled_ns, start + 5 * kPeriod);
    EXPECT_EQ(next.jitter_ns, 0);
    EXPECT_EQ(next.missed_cycles, 0u);
}

TEST(TimingTest, StartIsImplicitOnFirstWait) {
    FakeClock clock;
    FixedRateScheduler scheduler(clock, kPeriod);
    const uint64_t start = clock.now_ns();
    const CycleTiming t = scheduler.wait_for_next_cycle();
    EXPECT_EQ(t.scheduled_ns, start + kPeriod);
    EXPECT_EQ(scheduler.period_ns(), kPeriod);
}

TEST(TimingTest, CycleIndexIncrements) {
    FakeClock clock;
    FixedRateScheduler scheduler(clock, kPeriod);
    for (uint64_t n = 0; n < 5; n++) {
        EXPECT_EQ(scheduler.wait_for_next_cycle().cycle, n);
    }
}

// (REQ-TIME-002, REQ-TIME-003 reporting)
TEST(TimingTest, JitterStatsAggregate) {
    JitterStats stats;
    EXPECT_EQ(stats.mean_ns(), 0.0);
    stats.add(CycleTiming{0, 0, 100, 100, 0});
    stats.add(CycleTiming{1, 0, 300, 300, 0});
    stats.add(CycleTiming{2, 0, 200, 200, 2});
    EXPECT_EQ(stats.cycles, 3u);
    EXPECT_EQ(stats.min_ns, 100);
    EXPECT_EQ(stats.max_ns, 300);
    EXPECT_DOUBLE_EQ(stats.mean_ns(), 200.0);
    EXPECT_EQ(stats.overruns, 1u);
    EXPECT_EQ(stats.missed_cycles, 2u);
}

// One CSV line per cycle, fields in the documented order. (REQ-TIME-002)
TEST(TimingTest, JitterLogWritesCsv) {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "ftlg_jitter_test.csv";
    {
        JitterLog log(file);
        ASSERT_TRUE(log.ok());
        log.record(CycleTiming{0, 1000, 1010, 10, 0});
        log.record(CycleTiming{1, 2000, 2050, 50, 0});
        log.record(CycleTiming{2, 5000, 5100, 100, 2});
        EXPECT_EQ(log.stats().cycles, 3u);
        EXPECT_EQ(log.stats().max_ns, 100);
    }

    std::ifstream in(file);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    std::filesystem::remove(file);

    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(lines[0], "cycle,scheduled_ns,actual_ns,jitter_ns,missed_cycles");
    EXPECT_EQ(lines[1], "0,1000,1010,10,0");
    EXPECT_EQ(lines[3], "2,5000,5100,100,2");
}

TEST(TimingTest, JitterLogUnwritablePathReportsNotOk) {
    JitterLog log(std::filesystem::temp_directory_path() / "no_such_dir_ftlg" / "j.csv");
    EXPECT_FALSE(log.ok());
    log.record(CycleTiming{0, 0, 0, 0, 0});   // must not throw; stats still count
    EXPECT_EQ(log.stats().cycles, 1u);
}

// ---------------------------------------------------------------------------
// Real clock. About 20 ms of wall time in total.
// ---------------------------------------------------------------------------

// The one property an absolute sleep guarantees: it never returns early.
// The upper bound is loose on purpose; CI runners stall. (REQ-TIME-001)
TEST(TimingTest, MonotonicClockNeverWakesEarly) {
    MonotonicClock clock;
    FixedRateScheduler scheduler(clock, 1 * kMs);
    const uint64_t t0 = clock.now_ns();
    for (int n = 0; n < 20; n++) {
        const CycleTiming t = scheduler.wait_for_next_cycle();
        EXPECT_GE(t.jitter_ns, 0) << "cycle " << n;
        EXPECT_GE(t.actual_ns, t.scheduled_ns);
    }
    const uint64_t elapsed = clock.now_ns() - t0;
    EXPECT_GE(elapsed, 20 * kMs);
    EXPECT_LE(elapsed, 20 * kMs + 2'000 * kMs);
}

TEST(TimingTest, MonotonicClockSleepUntilPastReturnsPromptly) {
    MonotonicClock clock;
    const uint64_t now = clock.now_ns();
    const uint64_t past = now > 1'000 * kMs ? now - 1'000 * kMs : 0;
    clock.sleep_until_ns(past);
    EXPECT_LT(clock.now_ns() - now, 1'000 * kMs);
}

TEST(TimingTest, MonotonicClockIsMonotonic) {
    MonotonicClock clock;
    const uint64_t a = clock.now_ns();
    const uint64_t b = clock.now_ns();
    EXPECT_LE(a, b);
    EXPECT_GT(a, 0u);
}
