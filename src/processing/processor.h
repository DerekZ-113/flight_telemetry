#pragma once

#include "telemetry_frame.h"

// The processing pipeline. Sits between a DataSource (raw frames) and the
// outputs (logger, transports, display). Fills every computed field of a
// TelemetryFrame from the raw fields (REQ-PROC-005).
//
// Why a class and not a free function: the pipeline will hold state that
// persists across frames. Kalman filter estimates and covariance
// (REQ-PROC-003), complementary filter angles (REQ-PROC-002), and fault
// detection counters (REQ-FAULT-002) all depend on the previous frame.
// A free function would have to be handed that state on every call.
//
// Why process() takes const& and returns a new frame: the raw frame is
// the source's record of what the sensors said. Leaving it untouched
// means a logger can write the raw frame, and replay (REQ-LOG-003) can
// feed it back through this same method and compare the result.
class TelemetryProcessor {
public:
    // No parameters yet. Later takes a config struct with sampling rate,
    // filter tuning, and fault thresholds (REQ-CFG-001).
    TelemetryProcessor() = default;

    // Copies all raw fields from `raw`, then fills the computed fields.
    // Today only barometric altitude is computed (REQ-PROC-001). Attitude,
    // fused altitude, and vertical speed are placeholders until their
    // filters exist. Channel status passes through unchanged until the
    // fault detection module owns it.
    TelemetryFrame process(const TelemetryFrame& raw);
};
