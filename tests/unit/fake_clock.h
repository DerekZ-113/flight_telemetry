#pragma once

#include <cstdint>

#include "timing/clock.h"

// A clock whose time moves only when the test says so (FTS-TP-001
// section 2.1). The scheduler tests drive a thousand cycles through it in
// microseconds; the live-source test uses it to make frame timestamps
// deterministic. Header-only: every member is defined in the class, so
// including it from several test files is one definition each.
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
