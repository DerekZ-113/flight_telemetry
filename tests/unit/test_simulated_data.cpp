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

// REQ-SENS-006: raw sensor readings only. Every computed field is zero,
// every channel NOMINAL, every read successful. The pipeline owns the
// rest (REQ-LOG-004).
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
        EXPECT_TRUE(f.gps_read_ok);
    }
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
