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
// Implements REQ-SENS-006:
//   "The system shall support a simulated data source that produces
//    telemetry frames with configurable noise, for use in testing and replay."
//
// Deterministic within one toolchain: same seed → same noise sequence.
// std::mt19937 is fully specified by the standard, but the algorithm
// std::normal_distribution uses is not, so libstdc++ (Pi) and libc++ (Mac)
// produce different samples from the same seed. Reproducible tests on one
// machine: yes. Cross-platform bit-identical output: no.

class SimulatedDataGenerator {
public:
    // Seed controls the random sequence. Same seed = same data every time.
    explicit SimulatedDataGenerator(uint32_t seed = 42);

    // Produce one frame stamped with the caller's timestamp. The generator
    // does not track time itself; the fixed-rate loop owns the clock.
    // Base values simulate a board sitting stationary in Foster City.
    // Noise is Gaussian, drawn independently per sensor channel.
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
};
