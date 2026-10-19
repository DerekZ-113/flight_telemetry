#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "telemetry_frame.h"

// Fault detector (REQ-FAULT-001 through REQ-FAULT-005, FTS-FM-001).
//
// Sits between the DataSource and the TelemetryProcessor. It is the only
// component that writes ChannelStatus. The filters downstream gate on that
// status and carry state from frame to frame, so a bad reading that
// reaches them marked NOMINAL corrupts every later estimate. Detection has
// to happen first, on the raw frame, or it is too late.
//
// Time comes from frame.timestamp_ms, never from a wall clock. That makes
// the detector a pure function of the frame sequence it is fed: replaying
// a log reproduces the same faults at the same frames (REQ-LOG-003), and
// tests advance time by handing in frames with later timestamps.
//
// Three detection strategies, one recovery rule, and a start state:
//   Communication timeout  no successful read within a bound   (FAULT-001..004)
//   Stuck sensor           N identical readings on any axis    (FAULT-005..007)
//   Out of range           value outside configured limits, or an
//                          accelerometer axis at a rail of the
//                          driver's full scale                  (FAULT-008..010)
//   Recovery               M consecutive valid readings        (FAULT-011)
//   NO_DATA                until a channel's first reading     (REQ-FAULT-006)

// The channel an event concerns: a sensor channel, or for the voter's own
// events (REQ-RED-003, REQ-RED-004, REQ-RED-006) and for the lane fault the
// demonstration option injects (REQ-RED-007) the voter. VOTER and the five
// lane types below are new values of the event's existing bytes
// (2026-10-18), not a change to the struct: no byte moved, the format
// version stayed 3, and a reader older than the values prints their
// numbers, as for ChannelStatus::NOT_AVAILABLE (design.md section 9).
enum class Channel : uint8_t { BARO, IMU, GPS, VOTER };

enum class FaultType : uint8_t {
    COMM_TIMEOUT, STUCK, OUT_OF_RANGE, RECOVERY,                  // the detector's (FAULT-001 to FAULT-011)
    LANE_DISAGREEMENT, LANE_FAULTED, NO_MAJORITY, LANE_OVERRUN,   // the voter's, 4 to 7 (FAULT-019 to FAULT-022)
    LANE_FAULT_INJECTED,   // 8: a lane fault put in by REQ-RED-007's option, a record and not a detection
};

// One logged event (REQ-FAULT-004: timestamp and fault type), persisted by
// BinaryLogger as a tagged record in the same stream as the frames
// (REQ-LOG-001). The detector accumulates these and hands them out through
// take_events(); tests assert on them, main prints them. The number after
// each field is its byte offset: the struct is written verbatim.
struct FaultEvent {
    uint64_t timestamp_ms;   // 0
    Channel channel;         // 8
    FaultType type;          // 9
    uint16_t reserved;       // 10: zero until a format version assigns it (version 1's two padding bytes)
    // STUCK: the repeated value. OUT_OF_RANGE: the offending value.
    // RECOVERY: how long the channel was DEGRADED, in ms.
    // COMM_TIMEOUT: elapsed ms since the last successful read.
    // LANE_DISAGREEMENT, LANE_FAULTED, LANE_OVERRUN, LANE_FAULT_INJECTED:
    // the lane, 1 to 3.
    // NO_MAJORITY: the number of lanes still in the vote.
    float value;             // 12
};

// No unspecified byte (design.md section 9): the members sum to the size.
static_assert(sizeof(FaultEvent) == 8 + 1 + 1 + 2 + 4,
              "FaultEvent has an unspecified byte: name it, and bump kLogFormatVersion");
static_assert(offsetof(FaultEvent, reserved) == 10 && offsetof(FaultEvent, value) == 12,
              "FaultEvent layout changed: bump kLogFormatVersion");

// Thresholds. Default member initializers make this an aggregate with
// sensible values, so `FaultDetectorConfig{}` is a complete config and a
// test can override one field. The configuration file fills this shape
// (section `fault`, REQ-CFG-001); the defaults are the unit tests'
// baseline, equal to the shipped file by test, never the binary's fallback.
struct FaultDetectorConfig {
    uint64_t i2c_timeout_ms = 500;         // REQ-FAULT-001; FAULT-001, FAULT-002
    uint64_t uart_timeout_ms = 2000;       // REQ-FAULT-001; FAULT-003. Must exceed the receiver's 1 Hz fix period.
    int stuck_count = 10;                  // REQ-FAULT-002, N
    int recovery_count = 5;                // REQ-FAULT-005, M
    float pressure_min_hpa = 300.0f;       // FAULT-008, BMP280 operating range
    float pressure_max_hpa = 1100.0f;
    float temperature_min_c = -40.0f;      // FAULT-009, BMP280 operating range
    float temperature_max_c = 85.0f;
    // FAULT-010 has no field: the accelerometer's limits are the rails of
    // the full scale the driver configures (Mpu6050::kAccelPositiveRailMps2
    // and kAccelNegativeRailMps2), a fact of the sensor, not a tuning.
};

class FaultDetector {
public:
    explicit FaultDetector(const FaultDetectorConfig& config = FaultDetectorConfig{});

    // Returns a copy of `raw` with baro_status, imu_status, and gps_status
    // set by this detector. No other field is touched. Reading raw fields
    // and writing only status keeps the raw record intact for the logger.
    TelemetryFrame check(const TelemetryFrame& raw);

    // Events recorded since the previous call. Clears the buffer, so each
    // event is delivered exactly once.
    std::vector<FaultEvent> take_events();

private:
    // Run-length counter for one measured quantity. push() returns true
    // when the same value has arrived `n` times in a row.
    struct StuckTracker {
        float last = 0.0f;
        int run = 0;
        bool primed = false;
        bool push(float value, int n);
    };

    // Everything the detector remembers about one channel between frames.
    // A channel starts NO_DATA (REQ-FAULT-006): nothing has been read, so
    // the raw fields a frame carries for it are a value-initialized zero,
    // not a reading. The first successful reading makes it NOMINAL with no
    // event; a fault, the timeout included, makes it DEGRADED as from
    // NOMINAL, with its event.
    struct ChannelState {
        ChannelStatus status = ChannelStatus::NO_DATA;
        bool has_last_ok = false;
        uint64_t last_ok_ms = 0;        // timestamp of the last frame with read_ok
        uint64_t degraded_since_ms = 0; // for the RECOVERY event's duration
        int valid_run = 0;              // consecutive valid frames while DEGRADED
    };

    // Result of examining one channel on one frame, before the state
    // machine decides what it means.
    struct Observation {
        bool read_ok = false;
        bool faulted = false;
        FaultType type = FaultType::COMM_TIMEOUT;
        float value = 0.0f;
    };

    Observation observe_baro(const TelemetryFrame& raw, uint64_t now_ms);
    Observation observe_imu(const TelemetryFrame& raw, uint64_t now_ms);
    Observation observe_gps(const TelemetryFrame& raw, uint64_t now_ms);

    // Shared comm-timeout check: updates last_ok on success, reports a
    // fault when the gap since the last success exceeds timeout_ms.
    // static: everything it needs arrives as a parameter, including the
    // channel's state, so it reads nothing from this object.
    static bool comm_timed_out(ChannelState& state, bool read_ok, uint64_t now_ms,
                               uint64_t timeout_ms, float& elapsed_out);

    // The NO_DATA -> NOMINAL <-> DEGRADED state machine, identical for
    // every channel.
    void transition(Channel channel, ChannelState& state, const Observation& obs,
                    uint64_t now_ms);

    // The only place a FaultEvent is constructed. See the definition for
    // why it is value-initialized and then assigned, never brace-built.
    void record_event(uint64_t now_ms, Channel channel, FaultType type, float value);

    FaultDetectorConfig config_;
    ChannelState baro_;
    ChannelState imu_;
    ChannelState gps_;
    StuckTracker baro_pressure_;
    std::array<StuckTracker, 6> imu_axes_;  // ax, ay, az, gx, gy, gz
    std::vector<FaultEvent> events_;
};

const char* channel_name(Channel channel);
const char* fault_type_name(FaultType type);
