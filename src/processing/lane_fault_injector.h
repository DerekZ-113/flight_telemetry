#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "processing/fault_detector.h"
#include "processing/lane_fault_hook.h"
#include "processing/voter.h"
#include "telemetry_frame.h"

// The lane faults of the fault-injection campaign and of the demonstration
// option (REQ-RED-005, REQ-RED-007, REQ-TEST-005; FTS-DD-001 Section 14):
// one implementation of LaneFaultHook, so the demonstration shows exactly
// the fault the campaign measured.
//
// A fault alters one lane on every cycle from the first frame at or after
// its onset to the frame before its end. An output fault is an upset of the
// lane's result that leaves its state intact; an input fault runs through
// the lane's filters, a state upset that outlives its cycle; a stale output
// is the lane's previous real output handed to the voter, a computer one
// cycle late (FAULT-022), made on the output side so the lane's own state
// stays intact. The injector keeps every lane's real output of each cycle,
// before it alters anything: the stale fault's source, and the campaign's
// healthy reference.

enum class LaneFaultKind : uint8_t {
    OUTPUT_BIT_FLIP,   // one bit of one computed value of the lane's output
    OUTPUT_NAN,        // that value replaced by NaN
    OUTPUT_STALE,      // the lane's previous real output handed to the voter
    INPUT_BIT_FLIP,    // one bit of one raw value of the lane's input
};

// The float a fault touches: an output fault names one of the five
// computed values, an input fault a raw value the lane's filters read.
enum class LaneField : uint8_t {
    BARO_ALTITUDE, PITCH, ROLL, FUSED_ALTITUDE, VERTICAL_SPEED,                  // computed
    PRESSURE, ACCEL_X, ACCEL_Y, ACCEL_Z, GYRO_X, GYRO_Y, GYRO_Z, GPS_ALTITUDE,   // raw
};
constexpr std::size_t kLaneFieldCount = 13;

struct LaneFault {
    static constexpr uint64_t kPersistent = UINT64_MAX;

    int lane = 1;                                   // 1 to 3, as in the voter's events and bits
    LaneFaultKind kind = LaneFaultKind::OUTPUT_BIT_FLIP;
    uint64_t onset_ms = 0;                          // altered from the first frame at or after this time
    uint64_t end_ms = kPersistent;                  // and up to the frame before this one
    LaneField field = LaneField::FUSED_ALTITUDE;
    uint8_t bit = 0;                                // 0 to 31: the bit a flip inverts
    bool recorded = false;                          // logs LANE_FAULT_INJECTED at its first altered frame
};

class LaneFaultInjector : public LaneFaultHook {
public:
    explicit LaneFaultInjector(const std::vector<LaneFault>& faults);

    void perturb_input(int lane, TelemetryFrame& lane_input) override;
    void perturb_output(int lane, TelemetryFrame& lane_output) override;

    // The LANE_FAULT_INJECTED events of recorded faults since the previous
    // call (REQ-RED-007), handed out once each as the detector's are. Not
    // the voter's events: main logs them between the detector's and the
    // voter's, and the voter's tally never counts them.
    std::vector<FaultEvent> take_events();

    // This cycle's output of every lane as the lane made it, before any
    // alteration; after the third lane, the whole cycle's.
    const std::array<TelemetryFrame, kLaneCount>& real_outputs() const { return real_; }

private:
    struct Scheduled {
        LaneFault fault;
        bool announced = false;   // its event logged
    };

    std::vector<Scheduled> faults_;
    std::array<TelemetryFrame, kLaneCount> real_{};
    std::array<TelemetryFrame, kLaneCount> previous_{};   // the cycle before's real outputs
    bool have_previous_ = false;
    std::vector<FaultEvent> events_;
};

// The demonstration's fault (REQ-RED-007): from the first frame at or after
// onset_ms, on every cycle, bit 22 of the lane's fused-altitude output
// inverted, recorded. Bit 22 is the mantissa's highest, so the value is
// visibly wrong and still finite: 2.0 m reads 3.0 m.
constexpr uint8_t kDemoBit = 22;
LaneFault demo_lane_fault(int lane, uint64_t onset_ms);

// The option's value, "<lane>@<seconds>": the lane 1 to 3, the seconds a
// finite time from the run's first frame, at least 0. Anything else is
// empty, with `error` naming the option and its form.
std::optional<LaneFault> parse_lane_fault(const std::string& text, std::string& error);

// The `Lane fault:` line for the demonstration's fault: the lane, the
// onset, the alteration, and whether the option put it there or the
// replay's input recorded it.
std::string describe_lane_fault(const LaneFault& fault, bool reapplied);
