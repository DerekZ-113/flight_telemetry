#pragma once

#include <cstdint>
#include <optional>

#include "telemetry_frame.h"
#include "processing/complementary_filter.h"
#include "processing/kalman_filter.h"

// The processing pipeline. Sits between a DataSource (raw frames) and the
// outputs (logger, transports, display). Fills every computed field of a
// TelemetryFrame from the raw fields (REQ-PROC-005).
//
// Why a class and not a free function: the pipeline holds state that
// persists across frames. The Kalman filter's estimate and covariance
// (REQ-PROC-003) and the complementary filter's angles (REQ-PROC-002)
// depend on every frame that came before; fault detection counters
// (REQ-FAULT-002) will too. A free function would have to be handed that
// state on every call.
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

    // Copies all raw fields from `raw`, then fills the computed fields:
    // barometric altitude (REQ-PROC-001), pitch and roll from the
    // complementary filter (REQ-PROC-002), fused altitude and vertical
    // speed from the Kalman filter (REQ-PROC-003). Channel status is the
    // fault detector's and passes through unchanged.
    //
    // Each filter predicts every cycle and takes a measurement from a
    // channel only when that channel is NOMINAL and its reading is fresh
    // (read_ok), so a reading is used once, on the cycle it was obtained
    // (REQ-PROC-006). A 1 Hz GPS in a 50 Hz loop is fresh on one cycle in
    // fifty; a failed I2C read inside the timeout window is stale on a
    // channel that is still NOMINAL. Both are normal, and neither may be
    // re-applied as if it were new evidence.
    TelemetryFrame process(const TelemetryFrame& raw);

private:
    // std::optional because the filter cannot be built until the first
    // fresh barometric reading arrives: that reading is its initial
    // altitude. Empty means "no estimate yet". Constructing it with a
    // made-up altitude and overwriting later would work, but would leave
    // a window where the filter holds a value nobody measured; a driver
    // reports read_ok false until its first successful transaction.
    std::optional<KalmanFilter1D> altitude_filter_;

    // Not optional, unlike the Kalman member: its constructor takes only
    // alpha, nothing that depends on the first frame, and it seeds its
    // own angles from the accelerometer on the first update.
    ComplementaryFilter attitude_filter_;

    // Timestamp of the previous frame: the Kalman filter's prediction
    // interval, because time passes every cycle whether or not a sensor
    // reported. Read only while altitude_filter_ has a value.
    uint64_t previous_timestamp_ms_ = 0;

    // Timestamp of the last IMU sample the attitude filter accepted. The
    // gyroscope integrates over the time since that sample, not over the
    // loop period: three stale frames between two fresh ones are 80 ms of
    // rotation, not 20. Empty until the first accepted sample.
    std::optional<uint64_t> imu_last_fresh_ms_;
};
