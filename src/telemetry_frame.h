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

// Validity of one computed field (REQ-PROC-007), judged by the processor
// and, since the voter, chosen from a majority of lanes or degraded by the
// voter when there is none (REQ-RED-002, REQ-RED-004), in ARINC 429's
// sign/status vocabulary. A value and its validity
// travel in the same record, so no consumer can take one without the other.
// Zero is deliberately none of the three judgments: a value-initialized
// frame, a source's raw frame and every frame of a version-1 log read
// NOT_JUDGED, which no consumer may take for NORMAL, and the processor
// writes one of the other three on every frame it emits, so "the pipeline
// forgot" is distinguishable from "no data". The order is worst first, so
// min() is the worst-wins combiner, the voter's degrade under no majority.
enum class Validity : uint8_t {
    NOT_JUDGED = 0,        // nothing has judged this field: a raw frame, a version-1 log
    NO_COMPUTED_DATA = 1,  // no estimate of this field has ever existed; the value is 0
    FAILURE_WARNING = 2,   // the source the field depends on is not trusted; the value is held or coasting
    NORMAL = 3,            // a current estimate
};

// The facts a source may state about a frame (format version 3), one bit
// each in TelemetryFrame::flags: written by the source, recorded by the
// log, kept by the replay's strip and judged by nobody (design.md section
// 7). NEW_VEHICLE marks the first frame of a newly presented vehicle, on
// which the processor discards every estimate and begins again from that
// frame's readings (REQ-PROC-008). The other three say that an optional
// raw field is present: an optional measurement needs a yes or no that no
// value can carry, since zero is a rate and NaN is silent in arithmetic
// and false in every comparison (the rule since REQ-PROC-007).
enum class FrameFlag : uint8_t {
    NEW_VEHICLE = 0x01,        // the first frame of a new vehicle: every filter restarts (REQ-PROC-008)
    VERTICAL_RATE = 0x02,      // gps_vertical_rate_mps is present (REQ-SENS-008)
    PRESSURE_ALTITUDE = 0x04,  // gps_pressure_altitude_m is present (REQ-SENS-008)
    FIX_QUALITY = 0x08,        // gps_fix_quality and gps_satellites are present (REQ-SENS-005)
};

// The voter's status (format version 3, offset 114): the decisions the
// voter of design.md section 13 states about the frame it produced, one
// bit each: VOTED (REQ-RED-002), a lane excluded from the vote
// (REQ-RED-003), no majority (REQ-RED-004). Zero means no voter ran, which
// every writer put until the voter landed (2026-10-18) and every source
// still puts; the field was assigned with version 3 so that the voter
// never needed a format version 4. A verdict of the pipeline, not a fact a
// source states, so the replay's strip zeroes it as it zeroes validity and
// the replay's voter recomputes it.
enum class VoterStatus : uint16_t {
    VOTED = 0x0001,            // the voter produced this frame
    LANE_1_FAULTED = 0x0002,   // lane 1 was excluded from the vote
    LANE_2_FAULTED = 0x0004,   // lane 2 was excluded from the vote
    LANE_3_FAULTED = 0x0008,   // lane 3 was excluded from the vote
    NO_MAJORITY = 0x0010,      // no two lanes agreed
};

// Core data structure — one snapshot of all sensor readings, computed values,
// channel health, and the validity of every computed value. Produced at the
// configured processing rate.
//
// Every field traces to a requirement:
//   Sensor fields    → REQ-SENS-001 through REQ-SENS-005, REQ-SENS-008
//   Computed fields  → REQ-PROC-001 through REQ-PROC-003
//   Channel status   → REQ-FAULT-004, REQ-FAULT-005, REQ-FAULT-006
//   Read-ok flags    → REQ-FAULT-001 (communication timeout detection)
//   Fused outputs    → REQ-PROC-003 (Kalman states)
//   Validity bytes   → REQ-PROC-007
//   Sequence number  → REQ-LOG-008 (the source counts its own frames)
//   Flags byte       → REQ-PROC-008, REQ-SENS-005, REQ-SENS-008 (FrameFlag)
//   Voter status     → REQ-RED-002, REQ-RED-003, REQ-RED-004 (VoterStatus; zero: no voter ran)
//   Reserved field   → REQ-LOG-001 (no unspecified byte; zero until a version assigns it)
//
// The layout is the log format (REQ-LOG-001, design.md section 9): the
// struct's bytes are written verbatim, so every byte is a named field and
// the static_asserts below hold the compiler to that. Any change here bumps
// kLogFormatVersion in log_format.h in the same commit. The number after
// each field is its byte offset. Format version 3 (2026-10-15) kept every
// version-2 field where it was, spent the two reserved fields by name and
// appended a 16-byte tail, so an older frame reads as a prefix of this one
// (REQ-LOG-005).
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

    // The source's own count of its frames, from zero for the run
    // (REQ-LOG-008): a raw fact of the stream, recorded and reproduced by a
    // replay, so a consumer can tell a lost frame from a timestamp gap.
    // Version 1 had the compiler's four padding bytes here, version 2
    // named them reserved_a, version 3 spent them; a frame of an older log
    // reads 0.
    uint32_t sequence;          // 52

    // NEO-6M (GPS)
    // double, not float: float resolves to ~0.5 m at mid latitudes, which is
    // too close to the receiver's 2.5 m accuracy to throw away.
    double latitude, longitude; // 56, 64
    float gps_altitude_m;       // 72
    float ground_speed_mps;     // 76: meters per second
    float course_deg;           // 80: course over ground, 0-360 (REQ-SENS-005; heading_deg until format version 3)

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

    // The facts the source states about this frame, FrameFlag bits
    // (version 1's sixth tail padding byte, version 2's reserved_b).
    uint8_t flags;                     // 103

    // The version-3 tail: the ADS-B broadcast's vertical rate (positive
    // climbing) and pressure altitude (the air data computer's, at
    // 1013.25 hPa), raw readings of the GPS channel like gps_altitude_m,
    // present only under their FrameFlag bits (REQ-SENS-008); the
    // receiver's own judgment of its fix, GGA's quality (1 GPS, 2
    // differential, 6 estimated) and satellite count, under FIX_QUALITY
    // (REQ-SENS-005); the voter's status, VoterStatus bits, zero from a
    // source; and one reserved field, zero until a format version
    // assigns it, so the next change has room before it needs a bump.
    float gps_vertical_rate_mps;       // 104
    float gps_pressure_altitude_m;     // 108
    uint8_t gps_fix_quality;           // 112
    uint8_t gps_satellites;            // 113
    uint16_t voter_status;             // 114
    uint32_t reserved_d;               // 116
};

// The proof that no byte is unspecified: the sum of the members equals the
// size only when the compiler inserted nothing. (std::has_unique_object_
// representations would read better, but it is false for any type with a
// floating-point member.) The offsets pin the layout the Python side and
// the log format describe.
static_assert(sizeof(TelemetryFrame) == 8 + 11 * 4 + 4 + 2 * 8 + 5 * 4 + 3 + 3 + 5 + 1 + 4 + 4 + 1 + 1 + 2 + 4,
              "TelemetryFrame has an unspecified byte: name it, and bump kLogFormatVersion");
static_assert(sizeof(TelemetryFrame) == 120, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, sequence) == 52, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, latitude) == 56, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, course_deg) == 80, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, gps_read_ok) == 97, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, baro_altitude_validity) == 98, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, vertical_speed_validity) == 102, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, flags) == 103, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, gps_vertical_rate_mps) == 104, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, gps_pressure_altitude_m) == 108, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, gps_fix_quality) == 112, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, gps_satellites) == 113, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, voter_status) == 114, "layout changed: bump kLogFormatVersion");
static_assert(offsetof(TelemetryFrame, reserved_d) == 116, "layout changed: bump kLogFormatVersion");

// Helpers for printing. A byte outside either enumeration (a corrupt log)
// names itself UNKNOWN rather than as a state it is not.
const char* status_name(ChannelStatus status);
const char* validity_name(Validity validity);
const char* flag_name(FrameFlag flag);
const char* voter_status_name(VoterStatus bit);

// The flags byte, bit by bit: a source sets, a consumer tests. Setting a
// bit twice is setting it once.
bool has_flag(const TelemetryFrame& frame, FrameFlag flag);
void set_flag(TelemetryFrame& frame, FrameFlag flag);
