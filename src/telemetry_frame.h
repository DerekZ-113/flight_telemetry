#pragma once

#include <cstddef>
#include <cstdint>

// Health of one sensor channel (REQ-FAULT-004, REQ-FAULT-005, REQ-FAULT-006,
// REQ-FAULT-007). Judged by the fault detector, with one exception. NOMINAL
// is zero on purpose: a value-initialized frame, a source's raw frame and
// every frame of a version-1 log read NOMINAL, which from a source means
// "unjudged"; the detector overwrites every status left so on each frame
// it checks (design.md section 7). NO_DATA is the detector's word for a
// channel that has never produced a reading: its raw fields are the zeros
// of a value-initialized frame, which a display would show as a plausible
// value (a position of 0, 0 is a real place). NOT_AVAILABLE is the one
// status a source asserts: it has no sensor for the channel (the GPS
// without a receiver, the barometer and IMU of the ADS-B source), a fact
// of the configuration rather than a judgment about a sensor, which the
// detector leaves as it is with no timeout and no event, and which a
// replay keeps. A new value of an existing byte, added 2026-10-15 with no
// format bump: a reader that predates it prints the number.
enum class ChannelStatus : uint8_t {
    NOMINAL = 0,
    DEGRADED = 1,
    NO_DATA = 2,
    NOT_AVAILABLE = 3,
};

// Validity of one computed field (REQ-PROC-007), written by the processor
// only, in ARINC 429's sign/status vocabulary. A value and its validity
// travel in the same record, so no consumer can take one without the other.
// Zero is deliberately none of the three judgments: a value-initialized
// frame, a source's raw frame and every frame of a version-1 log read
// NOT_JUDGED, which no consumer may take for NORMAL, and the processor
// writes one of the other three on every frame it emits, so "the pipeline
// forgot" is distinguishable from "no data". The order is worst first, so
// min() across several lanes is the worst-wins combiner a voter wants.
enum class Validity : uint8_t {
    NOT_JUDGED = 0,        // nothing has judged this field: a raw frame, a version-1 log
    NO_COMPUTED_DATA = 1,  // no estimate of this field has ever existed; the value is 0
    FAILURE_WARNING = 2,   // the source the field depends on is not trusted; the value is held or coasting
    NORMAL = 3,            // a current estimate
};

// Core data structure — one snapshot of all sensor readings, computed values,
// channel health, and the validity of every computed value. Produced at the
// configured processing rate.
//
// Every field traces to a requirement:
//   Sensor fields    → REQ-SENS-001 through REQ-SENS-005
//   Computed fields  → REQ-PROC-001 through REQ-PROC-003
//   Channel status   → REQ-FAULT-004, REQ-FAULT-005, REQ-FAULT-006
//   Read-ok flags    → REQ-FAULT-001 (communication timeout detection)
//   Fused outputs    → REQ-PROC-003 (Kalman states)
//   Validity bytes   → REQ-PROC-007
//   Reserved fields  → REQ-LOG-001 (format version 2: no unspecified byte)
//
// The layout is the log format (REQ-LOG-001, design.md section 9): the
// struct's bytes are written verbatim, so every byte is a named field and
// the static_asserts below hold the compiler to that. Any change here bumps
// kLogFormatVersion in log_format.h in the same commit. The number after
// each field is its byte offset.
struct TelemetryFrame {
    uint64_t timestamp_ms;      // 0: milliseconds since system start

    // BMP280 (barometer)
    float pressure_hpa;         // 8: raw pressure in hectopascals
    float temperature_c;        // 12: temperature in celsius
    float baro_altitude_m;      // 16: computed from pressure (REQ-PROC-001)

    // MPU6050 (IMU)
    float accel_x, accel_y, accel_z;   // 20, 24, 28: m/s^2
    float gyro_x, gyro_y, gyro_z;      // 32, 36, 40: degrees/s
    float pitch_deg, roll_deg;          // 44, 48: complementary filter (REQ-PROC-002)

    // Zero until a format version assigns it. Version 1 had the compiler's
    // four padding bytes here, before the 8-byte-aligned doubles; naming
    // them keeps the layout free of unspecified bytes.
    uint32_t reserved_a;        // 52

    // NEO-6M (GPS)
    // double, not float: float resolves to ~0.5 m at mid latitudes, which is
    // too close to the receiver's 2.5 m accuracy to throw away.
    double latitude, longitude; // 56, 64
    float gps_altitude_m;       // 72
    float ground_speed_mps;     // 76: meters per second
    float heading_deg;          // 80: course over ground, 0-360 (REQ-SENS-005)

    // Fused outputs (REQ-PROC-003: Kalman states are altitude and vertical velocity)
    float fused_altitude_m;     // 84
    float vertical_speed_mps;   // 88

    // Channel health (REQ-FAULT-004, REQ-FAULT-006, REQ-FAULT-007). Judged
    // by the fault detector; a source sets NOMINAL, or NOT_AVAILABLE for a
    // channel it does not provide, and never anything else.
    ChannelStatus baro_status;  // 92
    ChannelStatus imu_status;   // 93
    ChannelStatus gps_status;   // 94

    // Whether the driver obtained a fresh reading this cycle (REQ-FAULT-001).
    // false means the fields for that channel are stale from the previous
    // cycle. This is not health: one failed I2C transaction is normal, and
    // the fault detector decides when staleness has lasted long enough to
    // be a fault. Sources report; the detector judges.
    bool baro_read_ok;          // 95
    bool imu_read_ok;           // 96
    bool gps_read_ok;           // 97

    // Validity of each computed field (REQ-PROC-007). Owned by the
    // processor; sources and the replay source leave NOT_JUDGED. Pitch and
    // roll carry the same verdict on every frame (one filter, one seed);
    // one byte per field keeps the rule uniform for a consumer.
    Validity baro_altitude_validity;   // 98
    Validity pitch_validity;           // 99
    Validity roll_validity;            // 100
    Validity fused_altitude_validity;  // 101
    Validity vertical_speed_validity;  // 102

    // Zero until a format version assigns it: version 1's sixth tail
    // padding byte, the five validity fields having taken the other five.
    uint8_t reserved_b;                // 103
};

// The proof that no byte is unspecified: the sum of the members equals the
// size only when the compiler inserted nothing. (std::has_unique_object_
// representations would read better, but it is false for any type with a
// floating-point member.) The offsets pin the layout the Python side and
// the log format describe.
static_assert(sizeof(TelemetryFrame) == 8 + 11 * 4 + 4 + 2 * 8 + 5 * 4 + 3 + 3 + 5 + 1,
              "TelemetryFrame has an unspecified byte: name it, and bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, reserved_a) == 52, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, latitude) == 56, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, gps_read_ok) == 97, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, baro_altitude_validity) == 98, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, vertical_speed_validity) == 102, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, reserved_b) == 103, "layout changed: bump kLogFormatVersion");

// Helpers for printing. A byte outside either enumeration (a corrupt log)
// names itself UNKNOWN rather than as a state it is not.
const char* status_name(ChannelStatus status);
const char* validity_name(Validity validity);
