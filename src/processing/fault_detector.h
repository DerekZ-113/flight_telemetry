#pragma once

#include <array>
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
// Three detection strategies, one recovery rule:
//   Communication timeout  no successful read within a bound   (FAULT-001..004)
//   Stuck sensor           N identical readings on any axis    (FAULT-005..007)
//   Out of range           value outside configured limits     (FAULT-008..010)
//   Recovery               M consecutive valid readings        (FAULT-011)

enum class Channel : uint8_t { BARO, IMU, GPS };

enum class FaultType : uint8_t { COMM_TIMEOUT, STUCK, OUT_OF_RANGE, RECOVERY };

// One logged event (REQ-FAULT-004: timestamp and fault type). The logger
// does not exist yet; the detector accumulates these and hands them out
// through take_events(). Tests assert on them, main prints them.
struct FaultEvent {
    uint64_t timestamp_ms;
    Channel channel;
    FaultType type;
    // STUCK: the repeated value. OUT_OF_RANGE: the offending value.
    // RECOVERY: how long the channel was DEGRADED, in ms.
    // COMM_TIMEOUT: elapsed ms since the last successful read.
    float value;
};

// Thresholds. Default member initializers make this an aggregate with
// sensible values, so `FaultDetectorConfig{}` is a complete config and a
// test can override one field. This is the shape REQ-CFG-001 will fill
// from YAML.
struct FaultDetectorConfig {
    uint64_t i2c_timeout_ms = 500;         // REQ-FAULT-001; FAULT-001, FAULT-002
    uint64_t uart_timeout_ms = 2000;       // REQ-FAULT-001; FAULT-003a. Must exceed the 1 Hz NMEA period.
    int stuck_count = 10;                  // REQ-FAULT-002, N
    int recovery_count = 5;                // REQ-FAULT-005, M
    float pressure_min_hpa = 300.0f;       // FAULT-008, BMP280 operating range
    float pressure_max_hpa = 1100.0f;
    float temperature_min_c = -40.0f;      // FAULT-009, BMP280 operating range
    float temperature_max_c = 85.0f;
    float accel_limit_mps2 = 2.0f * 9.81f; // FAULT-010, MPU6050 power-on full scale (±2g)
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
    struct ChannelState {
        ChannelStatus status = ChannelStatus::NOMINAL;
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

    // The NOMINAL <-> DEGRADED state machine, identical for every channel.
    void transition(Channel channel, ChannelState& state, const Observation& obs,
                    uint64_t now_ms);

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
