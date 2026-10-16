#include "processing/processor.h"
#include "processing/altitude.h"

// Every tuning value comes in through ProcessorConfig (processor.h): the
// Kalman variances, the complementary filter's alpha, and the sea-level
// reference. Nothing numeric is decided in this file (REQ-CFG-001); what
// each value means, and where its number came from, is written beside
// the field that holds it.

TelemetryProcessor::TelemetryProcessor(const ProcessorConfig& tuning, uint64_t imu_timeout_ms)
    : tuning_(tuning),
      attitude_filter_(tuning.complementary_alpha),
      imu_timeout_ms_(imu_timeout_ms)
{
}

TelemetryFrame TelemetryProcessor::process(const TelemetryFrame& raw) {
    // Start from a copy so every raw field carries through by default.
    // TelemetryFrame is a plain struct of numbers, so this is a memberwise
    // copy with no allocation.
    TelemetryFrame out = raw;

    // REQ-PROC-006: a reading enters a filter only when its channel is
    // NOMINAL and the reading was taken this cycle. Health is the
    // detector's judgment over time; read_ok is this cycle's fact. Both
    // must hold: a NOMINAL GPS is stale on 49 of 50 cycles, and a frame
    // whose I2C read failed inside the timeout window carries the previous
    // sample on a channel that is still NOMINAL. Re-applying a copy would
    // shrink the covariance as if new evidence had arrived and pin the
    // state to a value that is no longer current (design.md section 5).
    const bool baro_fresh = raw.baro_status == ChannelStatus::NOMINAL && raw.baro_read_ok;
    const bool imu_fresh = raw.imu_status == ChannelStatus::NOMINAL && raw.imu_read_ok;
    const bool gps_fresh = raw.gps_status == ChannelStatus::NOMINAL && raw.gps_read_ok;

    // REQ-PROC-001: barometric altitude from pressure, against the
    // configured sea-level reference, converted only from a reading the
    // detector trusts and the driver took this cycle, and held otherwise:
    // 0 before the first reading (the ISA formula on a pressure nobody
    // measured would read 44 km), the last accepted conversion while the
    // barometer is DEGRADED, so the altitude of a reading FAULT-008
    // discards never reaches the frame (REQ-PROC-007). A stale NOMINAL
    // frame carries the previous pressure, whose conversion is the held
    // value anyway.
    if (baro_fresh) {
        last_baro_altitude_m_ = pressure_to_altitude(raw.pressure_hpa, tuning_.sea_level_pressure_hpa);
        baro_ever_fresh_ = true;
    }
    out.baro_altitude_m = last_baro_altitude_m_;

    // REQ-PROC-002: pitch and roll from the complementary filter. The
    // gyroscope integrates over the time since the last sample this filter
    // accepted, not over the loop period, so a gap of stale frames is
    // rotation accounted for, not lost. On the first accepted sample the
    // interval is zero and the filter seeds itself from the accelerometer.
    // A DEGRADED or stale frame holds the angles; under DEGRADED they
    // leave flagged FAILURE_WARNING (judge, below), which is what FAULT-006
    // asks of the display.
    //
    // A gap longer than the IMU timeout is an outage, not rotation: one
    // rate sample times ten seconds, with a few degrees per second of
    // uncalibrated offset in it, would be tens of degrees of invented
    // tilt. The filter is re-seeded from the accelerometer instead. The
    // comparison is strictly greater, like the detector's, so a gap of
    // exactly the timeout is still integrated (TC-006 step 6).
    if (imu_fresh) {
        float imu_dt = 0.0f;
        if (imu_last_fresh_ms_.has_value()) {
            const uint64_t gap_ms = raw.timestamp_ms - *imu_last_fresh_ms_;
            if (gap_ms > imu_timeout_ms_) {
                attitude_filter_.reset();
            }
            imu_dt = static_cast<float>(gap_ms) / 1000.0f;
        }
        attitude_filter_.update(raw.accel_x, raw.accel_y, raw.accel_z,
                                raw.gyro_x, raw.gyro_y, raw.gyro_z, imu_dt);
        imu_last_fresh_ms_ = raw.timestamp_ms;
        if (attitude_filter_.seeded()) {
            attitude_ever_seeded_ = true;
        }
    }
    out.pitch_deg = attitude_filter_.pitch_deg();
    out.roll_deg = attitude_filter_.roll_deg();

    // REQ-PROC-003: Kalman fusion of barometric and GPS altitude.
    // Prediction runs on every cycle the filter exists: time passes
    // whether or not a sensor reported, and the covariance must grow to
    // say so. Timestamps are integer ms; the filter works in seconds.
    if (altitude_filter_.has_value()) {
        const float dt = static_cast<float>(raw.timestamp_ms - previous_timestamp_ms_) / 1000.0f;
        altitude_filter_->predict(dt);
    } else if (baro_fresh) {
        // No estimate exists yet, and this cycle measured the barometer,
        // so the filter is born here: at this barometric altitude, with
        // zero vertical velocity, and no predict because there is no
        // previous state. emplace() constructs the filter in place inside
        // the optional; the arguments go straight to the KalmanFilter1D
        // constructor. A frame whose barometer read failed cannot start
        // it: a filter born from an unmeasured pressure would start 44 km
        // up. Until a fresh reading arrives the fused fields keep the
        // zero every source leaves in them. The initial altitude variance
        // is the barometric measurement variance (REQ-PROC-004): the
        // filter is born from one such reading.
        altitude_filter_.emplace(out.baro_altitude_m,
                                 0.0f,
                                 tuning_.baro_measurement_variance_m2,
                                 tuning_.initial_velocity_variance,
                                 tuning_.accel_noise_variance);
    } else if (gps_fresh) {
        // No barometer has read yet but the GPS has: the filter is born
        // from the GPS reading, at the GPS variance, by the same rule
        // (REQ-PROC-003, REQ-PROC-004, since 0.12.0). The barometer is
        // tried first, above, so every session that reads one on its
        // first cycle is born as before, byte for byte; the ADS-B source
        // never reads one, and until this branch its altitude never
        // existed (design.md section 12).
        altitude_filter_.emplace(raw.gps_altitude_m,
                                 0.0f,
                                 tuning_.gps_measurement_variance_m2,
                                 tuning_.initial_velocity_variance,
                                 tuning_.accel_noise_variance);
    }

    if (altitude_filter_.has_value()) {
        // Measurement updates, each behind its own freshness gate. Baro is
        // applied first because it is the primary altitude reference; GPS
        // refines it. On the filter's first cycle the baro update has zero
        // innovation (the state was set from this same reading), so it
        // only tightens the covariance.
        if (baro_fresh) {
            altitude_filter_->update(out.baro_altitude_m, tuning_.baro_measurement_variance_m2);
        }
        if (gps_fresh) {
            altitude_filter_->update(raw.gps_altitude_m, tuning_.gps_measurement_variance_m2);
            gps_ever_fresh_ = true;
        }
        out.fused_altitude_m = altitude_filter_->altitude();
        out.vertical_speed_mps = altitude_filter_->vertical_velocity();
    }

    previous_timestamp_ms_ = raw.timestamp_ms;

    // Channel status (baro_status, imu_status, gps_status) is left as the
    // fault detector set it (REQ-FAULT-004); this class only reads it.

    // REQ-PROC-007: every computed field is written above; judge each one
    // last, so the verdict and the value leave in the same frame.
    judge(out);

    return out;
}

// The validity of each computed field (REQ-PROC-007), by one ordered rule
// per field, the first that applies winning: "never existed" outranks "not
// trusted now", so a flag is never raised over a number nobody measured,
// and NORMAL is what remains. The detector's status is the trust; this
// class adds only what it alone knows, whether an estimate exists. A stale
// NOMINAL cycle inside the timeout stays NORMAL: staleness is the
// detector's call (REQ-PROC-006), not a flag.
void TelemetryProcessor::judge(TelemetryFrame& out) const {
    // Barometric altitude depends on the barometer alone. != NOMINAL, not
    // == DEGRADED: the field is NORMAL only while its source is trusted.
    if (!baro_ever_fresh_) {
        out.baro_altitude_validity = Validity::NO_COMPUTED_DATA;
    } else if (out.baro_status != ChannelStatus::NOMINAL) {
        out.baro_altitude_validity = Validity::FAILURE_WARNING;
    } else {
        out.baro_altitude_validity = Validity::NORMAL;
    }

    // Pitch and roll depend on the IMU and on a seeded filter: one verdict
    // for both, since one filter holds both angles from one seed. After a
    // reset the filter is unseeded until a usable accelerometer sample,
    // which on the recovery frame is the same frame; only an unusable one
    // leaves the held angles flagged.
    Validity attitude = Validity::NORMAL;
    if (!attitude_ever_seeded_) {
        attitude = Validity::NO_COMPUTED_DATA;
    } else if (out.imu_status != ChannelStatus::NOMINAL || !attitude_filter_.seeded()) {
        attitude = Validity::FAILURE_WARNING;
    }
    out.pitch_validity = attitude;
    out.roll_validity = attitude;

    // Fused altitude and vertical speed depend on an altitude source the
    // filter has taken a measurement from, NOMINAL now (FAULT-001: fused
    // altitude relies solely on GPS while the barometer is out; in ADS-B
    // mode there is no barometer and the GPS is the only source). With
    // neither the filter coasts on prediction and the estimate random-
    // walks: flagged at once and for as long as it lasts, never cleared by
    // time. A GPS that is NOMINAL but has never fixed does not contribute.
    Validity fused = Validity::NORMAL;
    if (!altitude_filter_.has_value()) {
        fused = Validity::NO_COMPUTED_DATA;
    } else {
        // Each source contributes only if the filter has ever taken a
        // measurement from it and it is trusted now: a filter born from
        // the GPS beside a barometer that is NOMINAL but has never read
        // must not count the barometer, and the symmetric case likewise.
        const bool baro_contributes = baro_ever_fresh_ && out.baro_status == ChannelStatus::NOMINAL;
        const bool gps_contributes = gps_ever_fresh_ && out.gps_status == ChannelStatus::NOMINAL;
        if (!baro_contributes && !gps_contributes) {
            fused = Validity::FAILURE_WARNING;
        }
    }
    out.fused_altitude_validity = fused;
    out.vertical_speed_validity = fused;
}
