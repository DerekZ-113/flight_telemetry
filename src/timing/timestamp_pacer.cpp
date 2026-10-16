#include "timing/timestamp_pacer.h"

TimestampPacer::TimestampPacer(Clock& clock, uint64_t period_ns)
    : clock_(clock),
      period_ns_(period_ns)
{
}

CycleTiming TimestampPacer::wait_for(uint64_t timestamp_ms) {
    if (!started_) {
        origin_wall_ns_ = clock_.now_ns();
        origin_timestamp_ms_ = timestamp_ms;
        started_ = true;
    }

    // Signed arithmetic: a timestamp behind the origin gives a deadline in
    // the past, which the clock returns from at once; one so far behind that
    // the deadline would precede the clock's epoch is clamped, not wrapped.
    const int64_t offset_ns =
        (static_cast<int64_t>(timestamp_ms) - static_cast<int64_t>(origin_timestamp_ms_)) * 1'000'000;
    const int64_t signed_deadline = static_cast<int64_t>(origin_wall_ns_) + offset_ns;
    uint64_t deadline = 0;
    if (signed_deadline > 0) {
        deadline = static_cast<uint64_t>(signed_deadline);
    }

    clock_.sleep_until_ns(deadline);
    const uint64_t actual = clock_.now_ns();

    CycleTiming timing;
    timing.cycle = cycle_;
    timing.scheduled_ns = deadline;
    timing.actual_ns = actual;
    timing.jitter_ns = static_cast<int64_t>(actual) - static_cast<int64_t>(deadline);
    // Whole periods late, the scheduler's missed-cycle count in the pacer's
    // terms; zero for lateness under a period, which is jitter.
    timing.missed_cycles = 0;
    if (timing.jitter_ns >= static_cast<int64_t>(period_ns_)) {
        timing.missed_cycles = static_cast<uint64_t>(timing.jitter_ns) / period_ns_;
    }
    cycle_++;
    return timing;
}
