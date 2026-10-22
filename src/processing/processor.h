#pragma once

#include <cstdint>
#include <optional>

#include "telemetry_frame.h"
#include "processing/complementary_filter.h"
#include "processing/kalman_filter.h"

// The processor's tuning (REQ-CFG-001): the Kalman filter's noise
// variances (REQ-PROC-003; their provenance is REQ-PROC-004 and
// docs/noise_profile.md), the complementary filter's gyro weight
// (REQ-PROC-002), and the sea-level reference pressure (REQ-PROC-001).
// Default member initializers make this an aggregate with the shipped
// values, the same shape as FaultDetectorConfig: a test can override one
// field, and ProcessorConfig{} is a complete configuration. The defaults
// are the unit tests' baseline, never the binary's fallback: main loads
// config/telemetry_config.yaml (section `processing`), and
// ConfigTest.ShippedFileEqualsDefaults holds the file equal to these, so
// the two cannot drift apart unnoticed.
//
// Variances are sigma squared: m^2, (m/s)^2, (m/s^2)^2.
struct ProcessorConfig {
    // Barometric altitude noise, measured on the stationary capture of
    // 2026-10-21: the white part of the barometric altitude's scatter,
    // ADEV(tau0)^2, sigma 0.235 m (FTS-NP-001 sections 3.2 and 6). Also the
    // filter's initial altitude variance: it is born from one barometric
    // reading, so its uncertainty is that reading's (REQ-PROC-004).
    float baro_measurement_variance_m2 = 0.0551f;

    // GPS altitude noise. The simulator uses sigma 2 m, but a real NEO-6M
    // is closer to sigma 5 m vertically, so the filter is told 25 m^2:
    // pessimistic about GPS means it nudges the estimate rather than
    // jerking it. The datasheet figure until a capture with the receiver
    // fixing is analysed (the stationary capture runs with it off).
    float gps_measurement_variance_m2 = 25.0f;

    // Initial vertical velocity uncertainty. The filter starts at 0 m/s,
    // and a telemetry unit boots on the ground: chosen by replaying the
    // capture through 1.0, 0.1 and 0.01 (FTS-NP-001 section 3.5), the
    // largest with no startup transient above the room's own pressure
    // wander that still sees 90% of a 1 m/s climb at birth within 2 s.
    float initial_velocity_variance = 0.1f;

    // Unmodelled vertical acceleration (sigma 1 m/s^2): the process noise
    // strength, how much the constant-velocity assumption is expected to
    // be violated between frames. Too small and the filter lags real
    // climbs; too large and it stops smoothing.
    float accel_noise_variance = 1.0f;

    // Weight on the gyro path of the complementary filter. Meaningful
    // only with the sample interval (complementary_filter.h): at 0.98 and
    // 50 Hz the accelerometer's time constant is about one second.
    float complementary_alpha = ComplementaryFilter::kDefaultAlpha;

    // Reference for the hypsometric formula: the ISA standard atmosphere.
    // A METAR value makes the barometric altitude absolute.
    float sea_level_pressure_hpa = 1013.25f;
};

// The processing pipeline. Sits between a DataSource (raw frames) and the
// outputs (logger, transports, display). Fills every computed field of a
// TelemetryFrame from the raw fields (REQ-PROC-005) and judges each one's
// validity (REQ-PROC-007): it is the only judge of Validity, as the fault
// detector is the only writer of ChannelStatus. The system runs three of
// these as lanes (REQ-RED-001, redundant_processor.h), and the voter
// passes a majority's verdicts through or degrades the held ones.
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
    // The IMU communication timeout (REQ-FAULT-001) doubles as the
    // longest gap the gyroscope is integrated across (REQ-PROC-002).
    // The default equals FaultDetectorConfig::i2c_timeout_ms; main hands
    // the one configured value (`fault.i2c_timeout_ms`, REQ-CFG-001) to
    // the detector and to this class, and a static_assert there keeps
    // the two defaults equal for the tests that construct both bare.
    static constexpr uint64_t kDefaultImuTimeoutMs = 500;

    // explicit: a bare struct or integer must never silently become a
    // processor. Both arguments default to the test baseline; main
    // passes the configuration's values (REQ-CFG-001).
    explicit TelemetryProcessor(const ProcessorConfig& tuning = ProcessorConfig{},
                                uint64_t imu_timeout_ms = kDefaultImuTimeoutMs);

    // Copies all raw fields from `raw`, then fills the computed fields:
    // barometric altitude (REQ-PROC-001), pitch and roll from the
    // complementary filter (REQ-PROC-002), fused altitude and vertical
    // speed from the Kalman filter (REQ-PROC-003). Channel status is the
    // fault detector's and passes through unchanged.
    //
    // Each filter predicts every cycle and takes a measurement from a
    // channel only when that channel is NOMINAL and its reading is fresh
    // (read_ok), so a reading is used once, on the cycle it was obtained
    // (REQ-PROC-006). The Kalman filter is born from the first fresh
    // altitude reading of either source, the barometer first when both
    // are fresh on one cycle (REQ-PROC-003). A 1 Hz GPS in a 50 Hz loop is fresh on one cycle in
    // fifty; a failed I2C read inside the timeout window is stale on a
    // channel that is still NOMINAL. Both are normal, and neither may be
    // re-applied as if it were new evidence.
    //
    // The gyroscope integrates over the time since the last accepted IMU
    // sample, up to the IMU timeout. Past it the gap is an outage, and the
    // attitude is re-seeded from the accelerometer instead (REQ-PROC-002).
    //
    // Every computed field leaves with a validity (REQ-PROC-007), judged
    // last, in order of precedence: NO_COMPUTED_DATA while no estimate of
    // the field has ever existed, FAILURE_WARNING while the channel it
    // depends on is not trusted, NORMAL otherwise. A field that is not
    // NORMAL holds a defined number, never NaN: 0 before its first
    // estimate, the held or coasting estimate under FAILURE_WARNING.
    //
    // A frame the source flags NEW_VEHICLE (REQ-PROC-008) ends every
    // estimate before anything else happens: both filters, the IMU's
    // freshness clock, the held barometric altitude and the "ever" flags
    // start again, so the new vehicle's fields begin from its own readings,
    // on that frame when they are fresh.
    TelemetryFrame process(const TelemetryFrame& raw);

private:
    // Copied in, so the processor owns its tuning for its whole life and
    // nothing outside can change a value mid-run (REQ-CFG-002: validated
    // once, at startup). Declared first: the attitude filter below is
    // constructed from its alpha.
    ProcessorConfig tuning_;

    // std::optional because the filter cannot be built until the first
    // fresh altitude reading arrives, the barometer's or, when none has
    // read, the GPS's (REQ-PROC-003): that reading is its initial altitude
    // and its source's variance the initial uncertainty. Empty means "no
    // estimate yet". Constructing it with a made-up altitude and
    // overwriting later would work, but would leave a window where the
    // filter holds a value nobody measured; a driver reports read_ok false
    // until its first successful transaction.
    std::optional<KalmanFilter1D> altitude_filter_;

    // Not optional, unlike the Kalman member: its constructor takes only
    // alpha (tuning_.complementary_alpha), nothing that depends on the
    // first frame, and it seeds its own angles from the accelerometer on
    // the first update.
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

    // Longest gap the gyroscope is integrated across; a longer one
    // re-seeds the attitude filter (REQ-PROC-002).
    uint64_t imu_timeout_ms_;

    // What judge() needs that the filters do not expose (REQ-PROC-007).
    // The "ever" flags separate "never existed" from "not trusted now",
    // the first rule of the validity's precedence; the altitude flags also
    // say which source the Kalman filter has taken a measurement from, so
    // that a NOMINAL GPS that never fixed cannot keep a coasting estimate
    // NORMAL (design.md section 5).
    bool baro_ever_fresh_ = false;        // the barometer has been accepted at least once (the filter's birth or an update)
    bool gps_ever_fresh_ = false;         // the Kalman filter has taken a GPS measurement at least once
    bool attitude_ever_seeded_ = false;   // the attitude filter has adopted an accelerometer tilt at least once

    // The conversion of the last accepted barometer reading (REQ-PROC-001):
    // 0 before the first, held while the barometer is DEGRADED, so the
    // altitude of a reading FAULT-008 discards never reaches the frame.
    float last_baro_altitude_m_ = 0.0f;

    // The one writer of Validity: five ordered judgments at the end of
    // process(), from the flags above, the filters' state and the frame's
    // statuses, after every computed field is written.
    void judge(TelemetryFrame& out) const;
};
