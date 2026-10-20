#pragma once

#include <cstdint>

#include "timing/clock.h"
#include "timing/fixed_rate_scheduler.h"   // CycleTiming

// Paces a replay to its log's own timestamps (REQ-TIME-004).
//
// The scheduler's grid is start + n × period; this one's is the log:
// deadline(t) = origin_wall + (t − t0), with the origin set by the first
// frame, so a live log replays with its real read times and their jitter in
// them, not on a grid the log never had. The absolute-deadline idea is the
// same: each frame's lateness is measured against one fixed mapping and
// never accumulates.
//
// The overrun policy differs from the scheduler's, and must. A deadline that
// has passed is handed to the clock anyway (the Clock contract returns at
// once), the frame is released late and reported late, and nothing is
// skipped: a skipped frame would change the output, and REQ-TIME-004 claims
// a paced replay's log is byte-identical to an unpaced one. Pacing decides
// when a frame goes out, never what is in it. Lateness of a whole period or
// more is reported in missed_cycles as whole periods, the way the scheduler
// reports missed deadlines, so JitterLog's summary reads the same for both.
//
// A gap in the log (a missed cycle's 40 ms step, a stall) is honoured as
// recorded: fidelity is the point. A timestamp behind the previous one (two
// sessions' files handed in together) has a deadline in the past and is
// released at once; one that would fall before the clock's epoch is clamped
// to zero rather than wrapping. The scheduler is not extended with a second
// mode because the two grids are two ideas; this class is the second one.
class TimestampPacer {
public:
    // period_ns: the log's own period, the unit lateness is reported in.
    TimestampPacer(Clock& clock, uint64_t period_ns);

    // Block until the wall time that corresponds to timestamp_ms, report
    // what happened, count the cycle. The first call sets the origin.
    CycleTiming wait_for(uint64_t timestamp_ms);

private:
    Clock& clock_;
    uint64_t period_ns_;
    uint64_t origin_wall_ns_ = 0;
    uint64_t origin_timestamp_ms_ = 0;
    uint64_t cycle_ = 0;
    bool started_ = false;
};
