#include "timing/fixed_rate_scheduler.h"

FixedRateScheduler::FixedRateScheduler(Clock& clock, uint64_t period_ns)
    : clock_(clock),
      period_ns_(period_ns)
{
}

void FixedRateScheduler::start() {
    next_deadline_ns_ = clock_.now_ns() + period_ns_;
    started_ = true;
}

CycleTiming FixedRateScheduler::wait_for_next_cycle() {
    if (!started_) {
        start();
    }

    const uint64_t now = clock_.now_ns();
    uint64_t missed = 0;
    if (now >= next_deadline_ns_ + period_ns_) {
        // A whole period or more behind: skip the deadlines that already
        // passed, keep the grid. Integer division floors, so the new
        // deadline is the latest grid point at or before now.
        missed = (now - next_deadline_ns_) / period_ns_;
        next_deadline_ns_ += missed * period_ns_;
    }

    clock_.sleep_until_ns(next_deadline_ns_);
    const uint64_t actual = clock_.now_ns();

    CycleTiming timing;
    timing.cycle = cycle_;
    timing.scheduled_ns = next_deadline_ns_;
    timing.actual_ns = actual;
    timing.jitter_ns = static_cast<int64_t>(actual) - static_cast<int64_t>(next_deadline_ns_);
    timing.missed_cycles = missed;

    // The requirement, in one line: the next deadline is the previous
    // deadline plus the period, not "now plus the period".
    next_deadline_ns_ += period_ns_;
    cycle_++;
    return timing;
}
