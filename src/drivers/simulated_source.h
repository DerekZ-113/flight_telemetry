#pragma once

#include <cstdint>
#include <optional>

#include "data_source.h"
#include "drivers/motion_profile.h"
#include "drivers/simulated_data.h"

// DataSource backed by the SimulatedDataGenerator (REQ-SENS-006).
//
// Adds the one thing the generator deliberately lacks: a clock. A real
// sensor driver is polled at a fixed rate and each reading carries the
// time it was taken. This class models that by advancing its own timestamp
// by interval_ms on every read_frame() call.
class SimulatedSource : public DataSource {
public:
    // interval_ms is the simulated sample period. 20 ms models a 50 Hz
    // sensor, the top of the BMP280 range in REQ-SENS-001.
    // seed, gps_interval_ms and noise are forwarded to the generator: the
    // seed keeps tests reproducible, the GPS period models a 1 Hz receiver
    // (pass interval_ms to get a fix on every frame), the noise is the
    // seven sigmas (REQ-CFG-001). main passes the configuration's values;
    // the defaults are the unit tests' baseline.
    // explicit: with the rest defaulted this is effectively a one-argument
    // constructor, and a bare integer must never silently become a source.
    // profile, when given, is the scripted flight the generator follows
    // (REQ-SENS-007, --motion); the source owns it for the generator.
    explicit SimulatedSource(uint64_t interval_ms, uint32_t seed = 42,
                             uint64_t gps_interval_ms = SimulatedDataGenerator::kDefaultGpsIntervalMs,
                             const SimulationNoise& noise = SimulationNoise{},
                             std::optional<MotionProfile> profile = std::nullopt);

    // The generator keeps a pointer into profile_, so a copy or a move
    // would leave it pointing at the old object; main holds the source by
    // unique_ptr and never needs either.
    SimulatedSource(const SimulatedSource&) = delete;
    SimulatedSource& operator=(const SimulatedSource&) = delete;

    // `override` asks the compiler to confirm this really overrides a
    // virtual function in the base. A typo in the name or signature would
    // otherwise silently create a new, unrelated function.
    TelemetryFrame read_frame() override;

private:
    std::optional<MotionProfile> profile_;   // before generator_: the generator points into it
    SimulatedDataGenerator generator_;       // owned by value: lives and dies with this object
    uint64_t interval_ms_;
    uint64_t next_timestamp_ms_;        // timestamp the next frame will carry
};
