#include "processing/processor.h"
#include "processing/altitude.h"

// Kalman filter tuning. Placeholder values until noise characterization
// (REQ-PROC-004, docs/noise_profile.md) measures the real sensors, at
// which point these move to config/telemetry_config.yaml (REQ-CFG-001).
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

    // REQ-PROC-002: complementary filter not implemented yet.
    out.pitch_deg = 0.0f;
    out.roll_deg = 0.0f;

    // REQ-PROC-003: Kalman fusion of barometric and GPS altitude.
    if (!altitude_filter_.has_value()) {
        // First frame: there is no previous estimate to predict from, so
        // the filter is born at the first barometric altitude with zero
        // vertical velocity. emplace() constructs the filter in place
        // inside the optional; the arguments go straight to the
        // KalmanFilter1D constructor.
        altitude_filter_.emplace(out.baro_altitude_m,
                                 0.0f,
                                 kInitialAltitudeVariance,
                                 kInitialVelocityVariance,
                                 kAccelNoiseVariance);
    } else {
        // Every later frame: propagate the estimate across the time that
        // passed since the previous frame. Timestamps are integer ms;
        // the filter works in seconds.
        const float dt = static_cast<float>(raw.timestamp_ms - previous_timestamp_ms_) / 1000.0f;
        altitude_filter_->predict(dt);
    }
    previous_timestamp_ms_ = raw.timestamp_ms;

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

    // Channel status (baro_status, imu_status, gps_status) is left as the
    // source set it. Fault detection (REQ-FAULT-001..005) will own these.

    return out;
}
