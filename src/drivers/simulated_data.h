#pragma once

#include <cstdint>
#include <random>

// Full include, not a forward declaration: generate() returns a
// TelemetryFrame by value, so every caller needs the complete type anyway.
#include "telemetry_frame.h"

// Generates simulated TelemetryFrames with realistic values and
// configurable Gaussian noise. Used to feed the processing pipeline
// during development and testing before real sensors are available.
//
// Implements REQ-SENS-006: raw readings with configurable noise, each
// channel at its own rate. The barometer and IMU are read every frame;
// the GPS fixes once per gps_interval_ms (a real NEO-6M reports at 1 Hz)
// and between fixes the frame repeats the last fix with gps_read_ok
// false, exactly as a driver holding stale fields reports it. That is the
// timing fact the pipeline's freshness gate (REQ-PROC-006) exists for.
//
// Deterministic within one toolchain: same seed → same noise sequence.
// std::mt19937 is fully specified by the standard, but the algorithm
// std::normal_distribution uses is not, so libstdc++ (Pi) and libc++ (Mac)
// produce different samples from the same seed. Reproducible tests on one
// machine: yes. Cross-platform bit-identical output: no.

// The simulator's noise: one standard deviation per channel (REQ-SENS-006,
// REQ-CFG-001 "simulated-source noise"). Default member initializers hold
// the shipped values, the same shape as FaultDetectorConfig: the defaults
// are the unit tests' baseline, the binary's values come from
// config/telemetry_config.yaml (section `simulation.noise`), and
// ConfigTest.ShippedFileEqualsDefaults keeps the two equal. Each sigma
// must be positive: std::normal_distribution requires it, and the
// configuration's validation enforces it.
struct SimulationNoise {
    float pressure_sigma_hpa = 0.2f;      // Intentionally pessimistic: the BMP280 datasheet quotes 0.2 to 1.3 Pa RMS, 15 to 100x lower.
    float temperature_sigma_c = 0.1f;
    float accel_sigma_mps2 = 0.05f;       // Matches MPU6050 400 ug/sqrt(Hz) density at 100 Hz bandwidth.
    float gyro_sigma_dps = 0.1f;          // Roughly 2x the MPU6050 datasheet figure at 100 Hz.
    double latitude_sigma_deg = 0.00002;  // About 2.2 m of latitude.
    double longitude_sigma_deg = 0.00002; // About 1.8 m of longitude at 37.5 N.
    float gps_altitude_sigma_m = 2.0f;    // Optimistic: NEO-6M vertical error is typically closer to 5 m.
};

class SimulatedDataGenerator {
public:
    // NEO-6M default output period: the default a bare generator and the
    // unit tests get; the binary's value is `simulation.gps_interval_ms`
    // (REQ-CFG-001).
    static constexpr uint64_t kDefaultGpsIntervalMs = 1000;

    // Seed controls the random sequence. Same seed = same data every time.
    // gps_interval_ms is the fix period; pass the frame interval (or 0) to
    // get a fix on every frame, which tests of the status gate rely on.
    // noise holds the seven sigmas; a different sigma changes the spread
    // of one channel and nothing else, because every distribution scales
    // the same standard draw.
    explicit SimulatedDataGenerator(uint32_t seed = 42,
                                    uint64_t gps_interval_ms = kDefaultGpsIntervalMs,
                                    const SimulationNoise& noise = SimulationNoise{});

    // Produce one frame stamped with the caller's timestamp. The generator
    // does not track time itself; SimulatedSource owns the clock.
    // Base values simulate a board sitting stationary in Foster City.
    // Noise is Gaussian, drawn independently per sensor channel.
    // Only RAW sensor fields are populated. Computed fields are left at
    // 0.0 for the processing pipeline (see DataSource contract).
    TelemetryFrame generate(uint64_t timestamp_ms);

private:
    std::mt19937 rng_;    // Mersenne Twister random engine

    // One distribution per noise profile. Each holds a standard deviation
    // (sigma), not a bound: about a third of samples fall outside ±1 sigma.
    // Declaration order here must match the constructor initializer list.
    std::normal_distribution<float> pressure_noise_;     // hPa
    std::normal_distribution<float> temperature_noise_;  // °C
    std::normal_distribution<float> accel_noise_;        // m/s²
    std::normal_distribution<float> gyro_noise_;         // deg/s
    std::normal_distribution<double> gps_lat_noise_;     // degrees (double to match the frame field)
    std::normal_distribution<double> gps_lon_noise_;     // degrees
    std::normal_distribution<float> gps_alt_noise_;      // meters

    // GPS fix schedule and the last fix, repeated between fixes. A fix is
    // due when the frame's timestamp falls in a period not yet served, so
    // the schedule is a function of the timestamps alone, not of how many
    // times generate() was called.
    uint64_t gps_interval_ms_;
    bool have_fix_ = false;
    uint64_t last_fix_index_ = 0;
    double last_latitude_ = 0.0;
    double last_longitude_ = 0.0;
    float last_gps_altitude_m_ = 0.0f;
};
