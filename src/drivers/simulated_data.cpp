#include "drivers/simulated_data.h"

#include <algorithm>
#include <cmath>

#include "processing/altitude.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kDegToRad = static_cast<float>(kPi / 180.0);

// The reference the script's pressures are written against: the ISA
// standard, which is also the processor's default (`processing.sea_level_
// pressure_hpa` as shipped), so a scripted altitude reads back as itself.
// The simulator never sees the configuration; a run with another reference
// reads the script's altitude shifted, as a real day would.
constexpr float kIsaSeaLevelHpa = 1013.25f;

}  // namespace

// Constructor — sets up the random engine and noise distributions.
//
// Each distribution is zero-mean with the standard deviation (sigma) the
// SimulationNoise struct carries (simulated_data.h): the shipped values
// until real noise characterization replaces them (REQ-PROC-004,
// docs/noise_profile.md), and then the measured ones, through the
// configuration file (REQ-CFG-001). Where a shipped value is deliberately
// worse than the datasheet, the struct's comment says so: a pessimistic
// simulator gives the Kalman filter something to actually correct.
//
// The seed makes the sequence deterministic on a given toolchain. Two
// generators built with the same seed produce identical frame sequences.
// All seven distributions draw from the one engine, so the order of draws
// inside generate() defines the sequence. Reordering two lines there
// changes every frame that follows.

SimulatedDataGenerator::SimulatedDataGenerator(uint32_t seed, uint64_t gps_interval_ms,
                                               const SimulationNoise& noise, const MotionProfile* profile)
    : rng_(seed),
      pressure_noise_(0.0f, noise.pressure_sigma_hpa),
      temperature_noise_(0.0f, noise.temperature_sigma_c),
      accel_noise_(0.0f, noise.accel_sigma_mps2),
      gyro_noise_(0.0f, noise.gyro_sigma_dps),
      gps_lat_noise_(0.0, noise.latitude_sigma_deg),
      gps_lon_noise_(0.0, noise.longitude_sigma_deg),
      gps_alt_noise_(0.0f, noise.gps_altitude_sigma_m),
      gps_interval_ms_(gps_interval_ms == 0 ? 1 : gps_interval_ms),   // 0 would divide by zero; 1 ms is "every frame"
      profile_(profile)
{
}

SimulatedDataGenerator::BaseReadings SimulatedDataGenerator::scripted_readings(uint64_t timestamp_ms) {
    const MotionState s = profile_->state_at(timestamp_ms);

    // The position: the scripted velocity integrated once per generated
    // frame over the interval since the previous frame, the first frame
    // being the origin, so the fixes are a function of the frame sequence
    // as the noise is. A course is measured from north, clockwise, so east
    // is its sine and north its cosine.
    if (positioned_ && timestamp_ms > last_timestamp_ms_) {
        const double dt_s = static_cast<double>(timestamp_ms - last_timestamp_ms_) / 1000.0;
        const double track_rad = static_cast<double>(s.track_deg) * kPi / 180.0;
        east_m_ += static_cast<double>(s.ground_speed_mps) * std::sin(track_rad) * dt_s;
        north_m_ += static_cast<double>(s.ground_speed_mps) * std::cos(track_rad) * dt_s;
    }
    positioned_ = true;
    last_timestamp_ms_ = timestamp_ms;

    // The readings are the pipeline's models run backwards, so that the
    // pipeline recovers the script (REQ-SENS-007): the pressure the
    // hypsometric relation turns back into the altitude; an accelerometer
    // vector the complementary filter's two atan2 forms turn back into
    // exactly the scripted pitch and roll (a physical gravity vector would
    // give asin(sin roll · cos pitch) for the roll, which differs in the
    // second decimal at the demo's angles; the exact inverse is the cleaner
    // oracle, FTS-DS-001 §7); the gyroscope the segment's rates, roll
    // about x, pitch about y, the turn rate on z, which the filter ignores
    // and the log keeps; the GPS at the integrated position, with the
    // script's altitude, speed and course. The chip temperature stays the
    // scenario's: the script does not model it.
    BaseReadings r = kStationary;
    r.pressure_hpa = altitude_to_pressure(s.altitude_m, kIsaSeaLevelHpa);
    const float sin_pitch = std::sin(s.pitch_deg * kDegToRad);
    const float sin_roll = std::sin(s.roll_deg * kDegToRad);
    const float gravity = kStationary.accel_z;
    r.accel_x = gravity * sin_pitch;
    r.accel_y = gravity * sin_roll;
    r.accel_z = gravity * std::sqrt(std::max(0.0f, 1.0f - sin_pitch * sin_pitch - sin_roll * sin_roll));
    r.gyro_x = s.roll_rate_dps;
    r.gyro_y = s.pitch_rate_dps;
    r.gyro_z = s.track_rate_dps;
    r.latitude = kStationary.latitude + north_m_ / kMetersPerDegreeLatitude;
    r.longitude = kStationary.longitude +
                  east_m_ / (kMetersPerDegreeLatitude * std::cos(kStationary.latitude * kPi / 180.0));
    r.gps_altitude_m = s.altitude_m;
    r.ground_speed_mps = s.ground_speed_mps;
    r.track_deg = s.track_deg;
    return r;
}

TelemetryFrame SimulatedDataGenerator::generate(uint64_t timestamp_ms) {
    // Value-initialized, not default-initialized. Every field is assigned
    // below, but the struct also has padding bytes (after roll_deg and
    // after gps_read_ok) that no assignment touches. Left uninitialized
    // they carry stack garbage into the binary log, and two identical
    // sessions could then differ in bytes that mean nothing. Zeroing the
    // whole object makes a frame's bytes a function of its fields alone.
    // SimulatedDataTest.SameSeedSameSequence guards this with memcmp.
    TelemetryFrame frame{};
    frame.timestamp_ms = timestamp_ms;

    // The scenario or the script (REQ-SENS-007): the stationary literals,
    // or the profile's state turned into readings. Every draw below is the
    // same in both cases and in the same order; a reading is always
    // base + noise, so with the stationary base the arithmetic is the one
    // the default always did, byte for byte. The call sits on a line of its
    // own, off the decision: GCC gives it an exception-unwind edge that no
    // run can take, which the branch filter removes only from a line with
    // no conditional (CLAUDE.md; FTS-VR-001 Entry 27).
    BaseReadings base = kStationary;
    if (profile_ != nullptr) {
        base = scripted_readings(timestamp_ms);
    }

    // BMP280 simulation — stationary at ~2m above sea level, or the script's
    // altitude: base pressure slightly below standard (1013.25 hPa) gives ~2m
    frame.pressure_hpa = base.pressure_hpa + pressure_noise_(rng_);
    frame.temperature_c = base.temperature_c + temperature_noise_(rng_);

    // Raw source only: the processing pipeline computes baro_altitude_m
    // from pressure_hpa (REQ-PROC-001). Computing it here would make the
    // simulator a source and a pipeline at once, which breaks REQ-LOG-004.
    frame.baro_altitude_m = 0.0f;

    // MPU6050 simulation — board sitting flat on a desk (gravity ~9.81 m/s²
    // on the z-axis, other axes near zero), or the script's attitude
    frame.accel_x = base.accel_x + accel_noise_(rng_);
    frame.accel_y = base.accel_y + accel_noise_(rng_);
    frame.accel_z = base.accel_z + accel_noise_(rng_);
    frame.gyro_x = base.gyro_x + gyro_noise_(rng_);
    frame.gyro_y = base.gyro_y + gyro_noise_(rng_);
    frame.gyro_z = base.gyro_z + gyro_noise_(rng_);

    // Attitude — not computed yet, placeholder zeros
    // Complementary filter (REQ-PROC-002) replaces these
    frame.pitch_deg = 0.0f;
    frame.roll_deg = 0.0f;

    // NEO-6M simulation — stationary in Foster City. A real receiver fixes
    // once per second while the loop runs at 50 Hz, so most frames carry
    // no new GPS reading. A fix is due when this frame's timestamp falls
    // in a period not yet served; the first frame always is. Between
    // fixes the fields repeat the last fix and gps_read_ok says so: the
    // frame carries a value, and the value is stale (REQ-PROC-006 is what
    // keeps the pipeline from using it twice). Noise is drawn only when a
    // fix is produced, so a frame between fixes consumes no random
    // numbers and the sequence stays a function of the timestamps.
    const uint64_t fix_index = timestamp_ms / gps_interval_ms_;
    const bool gps_fix = !have_fix_ || fix_index != last_fix_index_;
    if (gps_fix) {
        last_latitude_ = base.latitude + gps_lat_noise_(rng_);
        last_longitude_ = base.longitude + gps_lon_noise_(rng_);
        last_gps_altitude_m_ = base.gps_altitude_m + gps_alt_noise_(rng_);
        last_ground_speed_mps_ = base.ground_speed_mps;   // no noise on these two, as before
        last_track_deg_ = base.track_deg;
        have_fix_ = true;
        last_fix_index_ = fix_index;
    }
    frame.latitude = last_latitude_;
    frame.longitude = last_longitude_;
    frame.gps_altitude_m = last_gps_altitude_m_;
    frame.ground_speed_mps = last_ground_speed_mps_;
    frame.course_deg = last_track_deg_;
    // The receiver's judgment of the fix it holds, with the bit that says
    // it is present (REQ-SENS-005): the first frame always fixes, so every
    // frame carries it. No noise is drawn, so the sequence of draws, and
    // with it every other byte of the stationary default, is unchanged.
    frame.gps_fix_quality = kFixQuality;
    frame.gps_satellites = kSatellites;
    set_flag(frame, FrameFlag::FIX_QUALITY);

    // Fused outputs — pipeline's Kalman filter fills these (REQ-PROC-003)
    frame.fused_altitude_m = 0.0f;
    frame.vertical_speed_mps = 0.0f;

    // All channels healthy in simulation mode. Status is NOMINAL because
    // the fault detector owns it; the I2C reads succeed because there is
    // no bus to fail; the GPS read "succeeds" only on a fix, because
    // freshness is a fact about this cycle, not about health. Fault
    // injection wraps this source rather than editing it.
    frame.baro_status = ChannelStatus::NOMINAL;
    frame.imu_status = ChannelStatus::NOMINAL;
    frame.gps_status = ChannelStatus::NOMINAL;
    frame.baro_read_ok = true;
    frame.imu_read_ok = true;
    frame.gps_read_ok = gps_fix;

    return frame;
}
