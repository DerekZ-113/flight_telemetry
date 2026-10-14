#include "drivers/simulated_data.h"

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
                                               const SimulationNoise& noise)
    : rng_(seed),
      pressure_noise_(0.0f, noise.pressure_sigma_hpa),
      temperature_noise_(0.0f, noise.temperature_sigma_c),
      accel_noise_(0.0f, noise.accel_sigma_mps2),
      gyro_noise_(0.0f, noise.gyro_sigma_dps),
      gps_lat_noise_(0.0, noise.latitude_sigma_deg),
      gps_lon_noise_(0.0, noise.longitude_sigma_deg),
      gps_alt_noise_(0.0f, noise.gps_altitude_sigma_m),
      gps_interval_ms_(gps_interval_ms == 0 ? 1 : gps_interval_ms)   // 0 would divide by zero; 1 ms is "every frame"
{
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

    // BMP280 simulation — stationary at ~2m above sea level
    // Base pressure slightly below standard (1013.25 hPa) to get ~2m altitude
    frame.pressure_hpa = 1013.00f + pressure_noise_(rng_);
    frame.temperature_c = 21.0f + temperature_noise_(rng_);

    // Raw source only: the processing pipeline computes baro_altitude_m
    // from pressure_hpa (REQ-PROC-001). Computing it here would make the
    // simulator a source and a pipeline at once, which breaks REQ-LOG-004.
    frame.baro_altitude_m = 0.0f;

    // MPU6050 simulation — board sitting flat on a desk
    // Gravity is ~9.81 m/s² on the z-axis, other axes near zero
    frame.accel_x = 0.0f + accel_noise_(rng_);
    frame.accel_y = 0.0f + accel_noise_(rng_);
    frame.accel_z = 9.81f + accel_noise_(rng_);
    frame.gyro_x = 0.0f + gyro_noise_(rng_);
    frame.gyro_y = 0.0f + gyro_noise_(rng_);
    frame.gyro_z = 0.0f + gyro_noise_(rng_);

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
        last_latitude_ = 37.5585 + gps_lat_noise_(rng_);
        last_longitude_ = -122.2711 + gps_lon_noise_(rng_);
        last_gps_altitude_m_ = 3.0f + gps_alt_noise_(rng_);
        have_fix_ = true;
        last_fix_index_ = fix_index;
    }
    frame.latitude = last_latitude_;
    frame.longitude = last_longitude_;
    frame.gps_altitude_m = last_gps_altitude_m_;
    frame.ground_speed_mps = 0.0f;
    frame.heading_deg = 0.0f;

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
