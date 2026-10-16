#include "drivers/simulated_source.h"

#include <utility>

SimulatedSource::SimulatedSource(uint64_t interval_ms, uint32_t seed, uint64_t gps_interval_ms,
                                 const SimulationNoise& noise, std::optional<MotionProfile> profile)
    : profile_(std::move(profile)),
      generator_(seed, gps_interval_ms, noise, profile_.has_value() ? &*profile_ : nullptr),
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
