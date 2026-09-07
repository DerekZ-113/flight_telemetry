#include "drivers/simulated_data.h"
#include "processing/altitude.h"

// Constructor — sets up the random engine and noise distributions.
//
// Each distribution is zero-mean with a standard deviation (sigma) chosen
// as a placeholder until real noise characterization replaces these values
// (REQ-PROC-004, docs/noise_profile.md). Where a value is deliberately
// worse than the datasheet, the comment says so: a pessimistic simulator
// gives the Kalman filter something to actually correct.
//
// The seed makes the sequence deterministic on a given toolchain. Two
// generators built with the same seed produce identical frame sequences.
// All seven distributions draw from the one engine, so the order of draws
// inside generate() defines the sequence. Reordering two lines there
// changes every frame that follows.

SimulatedDataGenerator::SimulatedDataGenerator(uint32_t seed)
    : rng_(seed),
      pressure_noise_(0.0f, 0.2f),       // sigma 0.2 hPa. Intentionally pessimistic: the BMP280 datasheet quotes 0.2 to 1.3 Pa RMS, 15 to 100x lower.
      temperature_noise_(0.0f, 0.1f),    // sigma 0.1 °C
      accel_noise_(0.0f, 0.05f),         // sigma 0.05 m/s². Matches MPU6050 400 ug/sqrt(Hz) density at 100 Hz bandwidth.
      gyro_noise_(0.0f, 0.1f),           // sigma 0.1 deg/s. Roughly 2x the MPU6050 datasheet figure at 100 Hz.
      gps_lat_noise_(0.0, 0.00002),      // sigma 0.00002 deg, about 2.2 m of latitude
      gps_lon_noise_(0.0, 0.00002),      // sigma 0.00002 deg, about 1.8 m of longitude at 37.5 N
      gps_alt_noise_(0.0f, 2.0f)         // sigma 2 m. Optimistic: NEO-6M vertical error is typically closer to 5 m.
{
}

TelemetryFrame SimulatedDataGenerator::generate(uint64_t timestamp_ms) {
    TelemetryFrame frame;
    frame.timestamp_ms = timestamp_ms;

    // BMP280 simulation — stationary at ~2m above sea level
    // Base pressure slightly below standard (1013.25 hPa) to get ~2m altitude
    frame.pressure_hpa = 1013.00f + pressure_noise_(rng_);
    frame.temperature_c = 21.0f + temperature_noise_(rng_);
    frame.baro_altitude_m = pressure_to_altitude(frame.pressure_hpa);

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

    // NEO-6M simulation — stationary in Foster City
    frame.latitude = 37.5585 + gps_lat_noise_(rng_);
    frame.longitude = -122.2711 + gps_lon_noise_(rng_);
    frame.gps_altitude_m = 3.0f + gps_alt_noise_(rng_);
    frame.ground_speed_mps = 0.0f;
    frame.heading_deg = 0.0f;

    // Fused outputs — Kalman filter replaces these (REQ-PROC-003)
    frame.fused_altitude_m = frame.baro_altitude_m;
    frame.vertical_speed_mps = 0.0f;

    // All channels healthy in simulation mode
    frame.baro_status = ChannelStatus::NOMINAL;
    frame.imu_status = ChannelStatus::NOMINAL;
    frame.gps_status = ChannelStatus::NOMINAL;

    return frame;
}
