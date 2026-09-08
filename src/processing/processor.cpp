#include "processing/processor.h"
#include "processing/altitude.h"

TelemetryFrame TelemetryProcessor::process(const TelemetryFrame& raw) {
    // Start from a copy so every raw field carries through by default.
    // TelemetryFrame is a plain struct of numbers, so this is a memberwise
    // copy with no allocation.
    TelemetryFrame out = raw;

    // REQ-PROC-001: barometric altitude from pressure. The sea-level
    // reference is the ISA default until config supplies it (REQ-CFG-001).
    out.baro_altitude_m = pressure_to_altitude(raw.pressure_hpa);

    // REQ-PROC-002: complementary filter not implemented yet.
    out.pitch_deg = 0.0f;
    out.roll_deg = 0.0f;

    // REQ-PROC-003: Kalman filter not implemented yet. Fused altitude
    // mirrors barometric altitude so downstream consumers see a plausible
    // value; vertical speed has no source until the filter provides it.
    out.fused_altitude_m = out.baro_altitude_m;
    out.vertical_speed_mps = 0.0f;

    // Channel status (baro_status, imu_status, gps_status) is left as the
    // source set it. Fault detection (REQ-FAULT-001..005) will own these.

    return out;
}
