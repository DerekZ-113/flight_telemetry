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

class SimulatedDataGenerator {
public:
    // NEO-6M default output period. Placeholder until config (REQ-CFG-001).
    static constexpr uint64_t kDefaultGpsIntervalMs = 1000;

    // Seed controls the random sequence. Same seed = same data every time.
    // gps_interval_ms is the fix period; pass the frame interval (or 0) to
    // get a fix on every frame, which tests of the status gate rely on.
    explicit SimulatedDataGenerator(uint32_t seed = 42,
                                    uint64_t gps_interval_ms = kDefaultGpsIntervalMs);

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
