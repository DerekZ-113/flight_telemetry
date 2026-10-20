// Tests for src/timing/periodic_worker (REQ-SENS-008; design.md section
// 10). Test card TC-029 step 10, the worker alone: a step that runs, waits
// what it returned, stops at once when told, never runs early, and is
// harmless to stop before a start or twice. On the real clock, in
// milliseconds, with generous bounds: a thread test that measures tight
// timing fails on a loaded runner.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "timing/periodic_worker.h"

namespace {

using SteadyClock = std::chrono::steady_clock;

// Waits up to `limit` for `done()`, the thread tests' one tool.
template <typename Predicate>
bool wait_until(Predicate done, std::chrono::milliseconds limit) {
    const SteadyClock::time_point deadline = SteadyClock::now() + limit;
    while (!done()) {
        if (SteadyClock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

long long elapsed_ms(SteadyClock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - since).count();
}

}  // namespace

// A 1 ms step runs again and again; after stop() the count is frozen and
// equals the calls made, so no step ran after the join.
TEST(PeriodicWorkerTest, RunsTheStepRepeatedlyAndFreezesAfterStop) {
    std::atomic<int> calls{0};
    PeriodicWorker worker([&calls] {
        calls++;
        return std::chrono::milliseconds(1);
    });
    EXPECT_FALSE(worker.running());
    EXPECT_EQ(worker.steps(), 0u);
    worker.start(std::chrono::milliseconds(0));
    EXPECT_TRUE(worker.running());
    EXPECT_TRUE(wait_until([&worker] { return worker.steps() >= 3; }, std::chrono::milliseconds(2000)));
    worker.stop();
    EXPECT_FALSE(worker.running());
    const size_t frozen = worker.steps();
    EXPECT_GE(frozen, 3u);
    EXPECT_EQ(static_cast<int>(frozen), calls.load());
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(worker.steps(), frozen);
    EXPECT_EQ(calls.load(), static_cast<int>(frozen));
}

// stop() before start(), a second start while running, stop() twice, and
// the destructor's stop() after one: all harmless.
TEST(PeriodicWorkerTest, StopBeforeStartAndTwiceIsHarmless) {
    PeriodicWorker worker([] { return std::chrono::milliseconds(1); });
    worker.stop();
    EXPECT_FALSE(worker.running());
    worker.start(std::chrono::milliseconds(1));
    worker.start(std::chrono::milliseconds(1));
    EXPECT_TRUE(worker.running());
    worker.stop();
    worker.stop();
    EXPECT_FALSE(worker.running());
}

// A worker parked on a ten-second delay leaves when told, not when the
// delay ends: the condition variable's predicate is the stop.
TEST(PeriodicWorkerTest, StopInterruptsALongWaitAtOnce) {
    PeriodicWorker worker([] { return std::chrono::milliseconds(10'000); });
    worker.start(std::chrono::milliseconds(10'000));
    const SteadyClock::time_point t0 = SteadyClock::now();
    worker.stop();
    EXPECT_LT(elapsed_ms(t0), 1000);
    EXPECT_EQ(worker.steps(), 0u);
}

// The first step runs after the first delay and never before it.
TEST(PeriodicWorkerTest, TheFirstStepIsNotBeforeTheFirstDelay) {
    std::atomic<bool> ran{false};
    SteadyClock::time_point first_call;   // written by the worker, read after the join
    PeriodicWorker worker([&ran, &first_call] {
        if (!ran) {
            first_call = SteadyClock::now();
            ran = true;
        }
        return std::chrono::milliseconds(1000);
    });
    const SteadyClock::time_point t0 = SteadyClock::now();
    worker.start(std::chrono::milliseconds(100));
    EXPECT_TRUE(wait_until([&worker] { return worker.steps() >= 1; }, std::chrono::milliseconds(2000)));
    worker.stop();
    ASSERT_TRUE(ran);
    EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(first_call - t0).count(), 100);
}
