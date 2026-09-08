#pragma once

#include <cstdint>

#include "data_source.h"
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
    // seed is forwarded to the generator so tests stay reproducible.
    SimulatedSource(uint64_t interval_ms, uint32_t seed = 42);

    // `override` asks the compiler to confirm this really overrides a
    // virtual function in the base. A typo in the name or signature would
    // otherwise silently create a new, unrelated function.
    TelemetryFrame read_frame() override;

private:
    SimulatedDataGenerator generator_;  // owned by value: lives and dies with this object
    uint64_t interval_ms_;
    uint64_t next_timestamp_ms_;        // timestamp the next frame will carry
};
