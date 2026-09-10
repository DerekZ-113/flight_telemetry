#pragma once

#include <cstdint>

// The boundary to the operating system's clock (REQ-TIME-001).
//
// The scheduler talks to this interface and never to the OS directly,
// for the same reason the pipeline talks to DataSource and never to a
// sensor: the OS clock is the one thing a unit test cannot control. A
// test substitutes a fake that advances time by fiat, so a thousand
// scheduled cycles run in microseconds and never sleep (FTS-TP-001 §2.1).
//
// Times are nanoseconds on a monotonic clock with an arbitrary epoch.
// Monotonic means it never jumps when NTP corrects the wall clock or a
// user sets the date; scheduling on wall-clock time is a classic bug.
// uint64_t nanoseconds cover 584 years with exact integer arithmetic,
// so no floating point appears anywhere in the timing path.
class Clock {
public:
    virtual ~Clock() = default;
    virtual uint64_t now_ns() = 0;
    // Block until now_ns() >= target_ns. Returns at once if it already is.
    virtual void sleep_until_ns(uint64_t target_ns) = 0;
};

// The real clock: CLOCK_MONOTONIC.
//
// On Linux, sleep_until_ns is clock_nanosleep with TIMER_ABSTIME: the
// kernel is handed the absolute target, so nothing between reading the
// clock and going to sleep can stretch the wait. That is the call
// REQ-TIME-001 names, and it is what runs on the Raspberry Pi and in CI.
//
// macOS has no clock_nanosleep. There the fallback computes the
// remaining interval and calls nanosleep in a loop until the target is
// reached. The scheduling logic above it is identical; only the sleep
// primitive is weaker (a preemption between now_ns() and nanosleep()
// lengthens that one wait). The 1 ms bound of REQ-TIME-003 is judged on
// the Linux path only.
class MonotonicClock : public Clock {
public:
    uint64_t now_ns() override;
    void sleep_until_ns(uint64_t target_ns) override;
};
