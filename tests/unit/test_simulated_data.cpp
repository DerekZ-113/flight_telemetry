// Unit tests for src/drivers/simulated_data.cpp and
// src/drivers/simulated_source.cpp (REQ-SENS-006).
//
// The simulator is a DataSource like any other, so it is held to the
// raw-only contract in data_source.h: it fills sensor fields and nothing
// else. It is also the reference for reproducibility: the same seed must
// give the same frames, or every "run twice and compare" test in the
// suite is meaningless.

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "drivers/simulated_data.h"
#include "drivers/simulated_source.h"
#include "telemetry_frame.h"

namespace {

constexpr uint64_t kIntervalMs = 20;

double mean(const std::vector<float>& v) {
    double s = 0.0;
    for (float x : v) s += x;
    return s / static_cast<double>(v.size());
}

double sample_sigma(const std::vector<float>& v) {
    const double m = mean(v);
    double s = 0.0;
    for (float x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size()));
}

}  // namespace

// REQ-SENS-006: raw sensor readings only. Every computed field is zero and
// unjudged, every channel NOMINAL, every I2C read successful; the GPS read
// succeeds on its 1 Hz schedule, which in these 20 frames (0 to 380 ms) is
// the first frame only. The pipeline owns the rest (REQ-LOG-004).
TEST(SimulatedDataTest, ProducesRawReadingsOnly) {
    SimulatedDataGenerator generator(42);
    for (int i = 0; i < 20; i++) {
        const uint64_t ts = static_cast<uint64_t>(i) * kIntervalMs;
        const TelemetryFrame f = generator.generate(ts);
        EXPECT_EQ(f.timestamp_ms, ts);
        EXPECT_EQ(f.baro_altitude_m, 0.0f);
        EXPECT_EQ(f.pitch_deg, 0.0f);
        EXPECT_EQ(f.roll_deg, 0.0f);
        EXPECT_EQ(f.fused_altitude_m, 0.0f);
        EXPECT_EQ(f.vertical_speed_mps, 0.0f);
        EXPECT_EQ(f.baro_status, ChannelStatus::NOMINAL);
        EXPECT_EQ(f.imu_status, ChannelStatus::NOMINAL);
        EXPECT_EQ(f.gps_status, ChannelStatus::NOMINAL);
        EXPECT_TRUE(f.baro_read_ok);
        EXPECT_TRUE(f.imu_read_ok);
        EXPECT_EQ(f.gps_read_ok, i == 0) << "frame " << i;
        // Unjudged and the reserved bytes zero: the processor judges,
        // and a reserved byte is zero by definition (REQ-PROC-007).
        EXPECT_EQ(f.baro_altitude_validity, Validity::NOT_JUDGED);
        EXPECT_EQ(f.pitch_validity, Validity::NOT_JUDGED);
        EXPECT_EQ(f.roll_validity, Validity::NOT_JUDGED);
        EXPECT_EQ(f.fused_altitude_validity, Validity::NOT_JUDGED);
        EXPECT_EQ(f.vertical_speed_validity, Validity::NOT_JUDGED);
        EXPECT_EQ(f.reserved_a, 0u);
        EXPECT_EQ(f.reserved_b, 0u);
    }
}

// REQ-SENS-006: the GPS fixes once per second. In a 50 Hz frame stream
// that is one fresh reading in fifty; the other forty-nine repeat the
// last fix with gps_read_ok false, as a driver holding stale fields
// reports it. Barometer and IMU are read every frame.
TEST(SimulatedDataTest, GpsFixOncePerSecond) {
    SimulatedDataGenerator generator(42);
    TelemetryFrame previous{};
    int fixes = 0;
    for (int i = 0; i < 100; i++) {
        const uint64_t ts = static_cast<uint64_t>(i) * kIntervalMs;
        const TelemetryFrame f = generator.generate(ts);
        const bool expect_fix = (ts == 0 || ts == 1000);
        EXPECT_EQ(f.gps_read_ok, expect_fix) << "t=" << ts;
        EXPECT_TRUE(f.baro_read_ok);
        EXPECT_TRUE(f.imu_read_ok);
        if (i > 0 && expect_fix) {
            EXPECT_NE(f.latitude, previous.latitude) << "a new fix carries new noise, t=" << ts;
        }
        if (i > 0 && !expect_fix) {
            EXPECT_EQ(f.latitude, previous.latitude) << "t=" << ts;
            EXPECT_EQ(f.longitude, previous.longitude) << "t=" << ts;
            EXPECT_EQ(f.gps_altitude_m, previous.gps_altitude_m) << "t=" << ts;
        }
        fixes += f.gps_read_ok ? 1 : 0;
        previous = f;
    }
    EXPECT_EQ(fixes, 2);
}

// The fix period is a constructor parameter (configurable until
// REQ-CFG-001). The frame interval as the period gives a fix on every
// frame, which tests of the status gate rely on; a period that is not a
// multiple of the frame interval fixes on the first frame of each new
// period, not only on exact multiples; zero means every frame.
TEST(SimulatedDataTest, GpsIntervalIsConfigurable) {
    SimulatedDataGenerator every_frame(42, kIntervalMs);
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(every_frame.generate(static_cast<uint64_t>(i) * kIntervalMs).gps_read_ok) << "frame " << i;
    }

    SimulatedDataGenerator thirty_ms_frames(42, 1000);
    std::vector<uint64_t> fix_times;
    for (uint64_t ts = 0; ts <= 2100; ts += 30) {
        if (thirty_ms_frames.generate(ts).gps_read_ok) {
            fix_times.push_back(ts);
        }
    }
    EXPECT_EQ(fix_times, (std::vector<uint64_t>{0, 1020, 2010}));

    SimulatedDataGenerator zero(42, 0);
    EXPECT_TRUE(zero.generate(0).gps_read_ok);
    EXPECT_TRUE(zero.generate(kIntervalMs).gps_read_ok);
}

// Same seed, same frames, bit for bit. This is the property the
// determinism and replay tests stand on. (REQ-SENS-006)
TEST(SimulatedDataTest, SameSeedSameSequence) {
    SimulatedDataGenerator a(42);
    SimulatedDataGenerator b(42);
    for (int i = 0; i < 50; i++) {
        const uint64_t ts = static_cast<uint64_t>(i) * kIntervalMs;
        const TelemetryFrame fa = a.generate(ts);
        const TelemetryFrame fb = b.generate(ts);
        EXPECT_EQ(std::memcmp(&fa, &fb, sizeof(TelemetryFrame)), 0) << "frame " << i;
    }
}

TEST(SimulatedDataTest, DifferentSeedDifferentSequence) {
    SimulatedDataGenerator a(42);
    SimulatedDataGenerator b(43);
    const TelemetryFrame fa = a.generate(0);
    const TelemetryFrame fb = b.generate(0);
    EXPECT_NE(fa.pressure_hpa, fb.pressure_hpa);
}

// The noise is centered on the documented base values (stationary board,
// Foster City) and actually varies. Tolerances are several standard
// errors wide for 500 samples, and the sequence is seeded, so this
// cannot flake. (REQ-SENS-006)
TEST(SimulatedDataTest, NoiseIsCenteredOnBaseValues) {
    SimulatedDataGenerator generator(42);
    std::vector<float> pressure, temperature, accel_z, latitude;
    for (int i = 0; i < 500; i++) {
        const TelemetryFrame f = generator.generate(static_cast<uint64_t>(i) * kIntervalMs);
        pressure.push_back(f.pressure_hpa);
        temperature.push_back(f.temperature_c);
        accel_z.push_back(f.accel_z);
        latitude.push_back(static_cast<float>(f.latitude));
    }
    EXPECT_NEAR(mean(pressure), 1013.0, 0.05);
    EXPECT_NEAR(mean(temperature), 21.0, 0.03);
    EXPECT_NEAR(mean(accel_z), 9.81, 0.02);
    EXPECT_NEAR(mean(latitude), 37.5585, 1e-4);
    EXPECT_GT(sample_sigma(pressure), 0.0);
}

// The stated noise level is the noise level: pressure sigma 0.2 hPa
// (the comment in simulated_data.cpp), which is what the Kalman baro
// variance of 3 m^2 was derived from. (REQ-SENS-006, REQ-PROC-004)
TEST(SimulatedDataTest, PressureNoiseHasStatedSpread) {
    SimulatedDataGenerator generator(42);
    std::vector<float> pressure;
    for (int i = 0; i < 500; i++) {
        pressure.push_back(generator.generate(static_cast<uint64_t>(i) * kIntervalMs).pressure_hpa);
    }
    const double sigma = sample_sigma(pressure);
    EXPECT_GT(sigma, 0.15);
    EXPECT_LT(sigma, 0.25);
}

// The source adds the clock: timestamps advance by the interval.
TEST(SimulatedSourceTest, TimestampsAdvanceByInterval) {
    SimulatedSource source(kIntervalMs, 42);
    for (uint64_t i = 0; i < 5; i++) {
        EXPECT_EQ(source.read_frame().timestamp_ms, i * kIntervalMs);
    }
}

// The source is a thin wrapper: frame i from the source is exactly what
// the generator produces for timestamp i * interval with the same seed.
TEST(SimulatedSourceTest, SourceMatchesGeneratorWithSameSeed) {
    SimulatedSource source(kIntervalMs, 42);
    SimulatedDataGenerator generator(42);
    for (int i = 0; i < 20; i++) {
        const TelemetryFrame fs = source.read_frame();
        const TelemetryFrame fg = generator.generate(static_cast<uint64_t>(i) * kIntervalMs);
        EXPECT_EQ(std::memcmp(&fs, &fg, sizeof(TelemetryFrame)), 0) << "frame " << i;
    }
}

// TC-016 step 10. The sigmas reach the distributions. Same seed, the
// pressure sigma ten times larger: every other channel is bit-identical
// (the same engine draws, the same sigmas) and the pressure noise is ten
// times larger frame by frame, because a normal distribution scales one
// standard draw by its sigma. A near-silent generator (every sigma 1e-6;
// zero is outside std::normal_distribution's precondition) sits on the
// base values to within float rounding. (REQ-SENS-006, REQ-CFG-001)
TEST(SimulatedDataTest, NoiseIsConfigurable) {
    SimulationNoise loud;
    loud.pressure_sigma_hpa = 10.0f * SimulationNoise{}.pressure_sigma_hpa;
    SimulatedDataGenerator base(42, 1000);
    SimulatedDataGenerator scaled(42, 1000, loud);
    for (int i = 0; i < 100; i++) {
        const uint64_t ts = static_cast<uint64_t>(i) * kIntervalMs;
        const TelemetryFrame a = base.generate(ts);
        const TelemetryFrame b = scaled.generate(ts);
        EXPECT_NEAR(b.pressure_hpa - 1013.0f, 10.0f * (a.pressure_hpa - 1013.0f), 2e-3f) << "frame " << i;
        EXPECT_EQ(a.temperature_c, b.temperature_c) << "frame " << i;
        EXPECT_EQ(a.accel_z, b.accel_z) << "frame " << i;
        EXPECT_EQ(a.gyro_x, b.gyro_x) << "frame " << i;
        EXPECT_EQ(a.latitude, b.latitude) << "frame " << i;
        EXPECT_EQ(a.gps_altitude_m, b.gps_altitude_m) << "frame " << i;
    }

    SimulationNoise quiet;
    quiet.pressure_sigma_hpa = 1e-6f;
    quiet.temperature_sigma_c = 1e-6f;
    quiet.accel_sigma_mps2 = 1e-6f;
    quiet.gyro_sigma_dps = 1e-6f;
    quiet.latitude_sigma_deg = 1e-6;
    quiet.longitude_sigma_deg = 1e-6;
    quiet.gps_altitude_sigma_m = 1e-6f;
    SimulatedDataGenerator silent(42, 1000, quiet);
    for (int i = 0; i < 50; i++) {
        const TelemetryFrame f = silent.generate(static_cast<uint64_t>(i) * kIntervalMs);
        EXPECT_NEAR(f.pressure_hpa, 1013.0f, 1e-4f) << "frame " << i;
        EXPECT_NEAR(f.temperature_c, 21.0f, 1e-4f) << "frame " << i;
        EXPECT_NEAR(f.accel_x, 0.0f, 1e-4f) << "frame " << i;
        EXPECT_NEAR(f.accel_z, 9.81f, 1e-4f) << "frame " << i;
        EXPECT_NEAR(f.gyro_y, 0.0f, 1e-4f) << "frame " << i;
        EXPECT_NEAR(f.latitude, 37.5585, 1e-5) << "frame " << i;
        EXPECT_NEAR(f.longitude, -122.2711, 1e-5) << "frame " << i;
        EXPECT_NEAR(f.gps_altitude_m, 3.0f, 1e-4f) << "frame " << i;
    }
}
