#include "drivers/simulated_source.h"

SimulatedSource::SimulatedSource(uint64_t interval_ms, uint32_t seed)
    : generator_(seed),
      interval_ms_(interval_ms),
      next_timestamp_ms_(0)
{
}

TelemetryFrame SimulatedSource::read_frame() {
    // Stamp the frame, then advance the clock for the next call. The first
    // frame is at t=0, matching how the fixed-rate loop will count from
    // system start (timestamp_ms is "milliseconds since system start").
    TelemetryFrame frame = generator_.generate(next_timestamp_ms_);
    next_timestamp_ms_ += interval_ms_;
    return frame;
}
