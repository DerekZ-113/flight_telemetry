#include "processing/processor.h"
#include "processing/altitude.h"

// Kalman filter tuning. Placeholder values until noise characterization
// (REQ-PROC-004, docs/noise_profile.md) measures the real sensors, at
// which point these move to config/telemetry_config.yaml (REQ-CFG-001).
// The complementary filter's alpha lives with the filter itself
// (ComplementaryFilter::kDefaultAlpha): it is a time constant, not a
// noise parameter, and REQ-PROC-004 does not govern it.
//
// All are variances (sigma squared), so units are m^2, (m/s)^2, (m/s^2)^2.
namespace {

// Barometric altitude noise. The simulator adds sigma 0.2 hPa of pressure
// noise, and near sea level 1 hPa is about 8.4 m, so sigma is about 1.7 m
// and variance about 3 m^2.
constexpr float kBaroMeasurementVariance = 3.0f;

// GPS altitude noise. The simulator uses sigma 2 m, but a real NEO-6M is
// closer to sigma 5 m vertically, so the filter is told 25 m^2. Being
// pessimistic about GPS means it nudges the estimate rather than jerking it.
constexpr float kGpsMeasurementVariance = 25.0f;

// Initial altitude uncertainty. The filter starts from one baro reading,
// so its uncertainty is that reading's uncertainty.
constexpr float kInitialAltitudeVariance = kBaroMeasurementVariance;

// Initial vertical velocity uncertainty. The filter starts at 0 m/s with
// no evidence, so this is deliberately generous (sigma 1 m/s).
constexpr float kInitialVelocityVariance = 1.0f;

// Unmodelled vertical acceleration (sigma 1 m/s^2). This is the process
// noise strength: how much the constant-velocity assumption is expected
// to be violated between frames. Too small and the filter lags real
// climbs; too large and it stops smoothing.
constexpr float kAccelNoiseVariance = 1.0f;

}  // namespace

TelemetryFrame TelemetryProcessor::process(const TelemetryFrame& raw) {
    // Start from a copy so every raw field carries through by default.
    // TelemetryFrame is a plain struct of numbers, so this is a memberwise
    // copy with no allocation.
    TelemetryFrame out = raw;

    // REQ-PROC-001: barometric altitude from pressure. The sea-level
    // reference is the ISA default until config supplies it (REQ-CFG-001).
    out.baro_altitude_m = pressure_to_altitude(raw.pressure_hpa);

    // Time since the previous frame, shared by both filters. Timestamps
    // are integer ms; the filters work in seconds. On the first frame
    // there is no previous timestamp, and neither filter integrates.
    const bool first_frame = !altitude_filter_.has_value();
    float dt = 0.0f;
    if (!first_frame) {
        dt = static_cast<float>(raw.timestamp_ms - previous_timestamp_ms_) / 1000.0f;
    }

    // REQ-PROC-002: pitch and roll from the complementary filter.
    // A DEGRADED IMU contributes nothing and the last angles are held.
    // FAULT-006 says attitude must be marked invalid rather than held;
    // that needs a validity marker the frame does not carry yet (sidecar
    // decision, Sprint 2), so hold is the interim behavior.
    if (raw.imu_status == ChannelStatus::NOMINAL) {
        attitude_filter_.update(raw.accel_x, raw.accel_y, raw.accel_z,
                                raw.gyro_x, raw.gyro_y, raw.gyro_z, dt);
    }
    out.pitch_deg = attitude_filter_.pitch_deg();
    out.roll_deg = attitude_filter_.roll_deg();

    // REQ-PROC-003: Kalman fusion of barometric and GPS altitude.
    if (first_frame) {
        // There is no previous estimate to predict from, so the filter is
        // born at the first barometric altitude with zero vertical
        // velocity. emplace() constructs the filter in place inside the
        // optional; the arguments go straight to the KalmanFilter1D
        // constructor.
        altitude_filter_.emplace(out.baro_altitude_m,
                                 0.0f,
                                 kInitialAltitudeVariance,
                                 kInitialVelocityVariance,
                                 kAccelNoiseVariance);
    } else {
        altitude_filter_->predict(dt);
    }

    // Measurement updates. Each sensor is applied only when its channel
    // is healthy; a DEGRADED channel contributes nothing and the filter
    // coasts on its prediction. Baro is applied first because it is the
    // primary altitude reference; GPS refines it. On the first frame the
    // baro update has zero innovation (the state was set from this same
    // reading), so it only tightens the covariance.
    if (raw.baro_status == ChannelStatus::NOMINAL) {
        altitude_filter_->update(out.baro_altitude_m, kBaroMeasurementVariance);
    }
    if (raw.gps_status == ChannelStatus::NOMINAL) {
        altitude_filter_->update(raw.gps_altitude_m, kGpsMeasurementVariance);
    }

    out.fused_altitude_m = altitude_filter_->altitude();
    out.vertical_speed_mps = altitude_filter_->vertical_velocity();

    previous_timestamp_ms_ = raw.timestamp_ms;

    // Channel status (baro_status, imu_status, gps_status) is left as the
    // source set it. Fault detection (REQ-FAULT-001..005) will own these.

    return out;
}
