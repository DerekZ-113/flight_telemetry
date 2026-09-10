#pragma once

#include <cstdint>

#include "timing/clock.h"

// What one cycle's wait measured (REQ-TIME-002).
struct CycleTiming {
    uint64_t cycle;           // 0-based index of this wait
    uint64_t scheduled_ns;    // the deadline this cycle was meant to start at
    uint64_t actual_ns;       // when it actually started
    // actual - scheduled. Never negative with a correct absolute sleep;
    // signed so the type says "difference" and a test can assert the
    // invariant instead of an unsigned wrap hiding it.
    int64_t jitter_ns;
    uint64_t missed_cycles;   // whole periods skipped to re-phase after an overrun
};

// Paces a loop at a fixed rate with absolute deadlines (REQ-TIME-001).
//
// Every deadline is start + n * period, computed by adding the period to
// the previous deadline, never by sleeping "period" after the work. The
// difference is drift: sleep(period) after w of work gives a real period
// of period + w + wake latency, and that error adds up every cycle. With
// absolute deadlines each cycle's lateness is measured against a fixed
// grid and never accumulates.
//
// Overrun policy: if a deadline is already a whole period or more in the
// past when the loop comes back, the missed deadlines are skipped, counted,
// and the grid is kept. The loop never fires a burst of back-to-back
// cycles to catch up; a control loop that did would be worse than one
// that missed cycles and said so.
class FixedRateScheduler {
public:
    FixedRateScheduler(Clock& clock, uint64_t period_ns);

    // First deadline = now + period. Called implicitly by the first wait.
    void start();

    // Sleep until the next deadline, report what happened, advance the grid.
    CycleTiming wait_for_next_cycle();

    uint64_t period_ns() const { return period_ns_; }

private:
    Clock& clock_;
    uint64_t period_ns_;
    uint64_t next_deadline_ns_ = 0;
    uint64_t cycle_ = 0;
    bool started_ = false;
};
