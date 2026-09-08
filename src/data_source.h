#pragma once

#include "telemetry_frame.h"

// Abstract interface for anything that can produce telemetry frames:
// live sensors, the simulator, or a recorded log during replay.
//
// The processing pipeline holds a DataSource pointer and never learns which
// concrete source is behind it. That is what makes REQ-LOG-004 possible:
// live mode and replay mode run the exact same pipeline code, differing
// only in which DataSource object was constructed at startup.
//
// Contract for implementers: read_frame() fills RAW sensor fields only
// (pressure, temperature, accel, gyro, GPS). Computed fields such as
// baro_altitude_m, pitch/roll, fused_altitude_m, and vertical_speed_mps
// are left at 0.0 for the pipeline to fill. If a source computed them
// itself, replay could never verify the pipeline, because the work would
// already be done before the pipeline ran.
class DataSource {
public:
    // Virtual destructor. Callers own sources through DataSource pointers
    // (see std::unique_ptr<DataSource> in main.cpp). When such a pointer is
    // deleted, C++ picks which destructor to run the same way it picks any
    // other virtual function: by looking at the real object. Without
    // `virtual` here, only DataSource's destructor would run, and the
    // derived class's members (the simulator's random engine, a file
    // handle, an I2C bus) would leak. This is undefined behavior per the
    // standard, not merely a leak.
    virtual ~DataSource() = default;

    // Produce the next frame. `= 0` makes this "pure virtual": DataSource
    // provides no implementation, and any class that fails to override it
    // is itself abstract and cannot be instantiated.
    virtual TelemetryFrame read_frame() = 0;
};
