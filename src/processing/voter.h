#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "telemetry_frame.h"
#include "processing/fault_detector.h"

// The voter of the three processing lanes (REQ-RED-001 to REQ-RED-006,
// design.md section 13): it compares the lanes' outputs for one checked
// frame and chooses the frame the system emits, outvoting a wrong lane on
// every cycle and excluding a lane that stays wrong.

// Three is the design, not a tuning (REQ-RED-001): the fewest lanes in
// which a majority survives one wrong lane.
constexpr std::size_t kLaneCount = 3;

// Whether two lanes' outputs agree (REQ-RED-002): bit for bit in every
// computed field, every validity and every channel status, NOT_AVAILABLE
// included, a NaN agreeing with nothing. Identical code on identical input
// has no legitimate difference, so there is no tolerance; this one
// function is where a dissimilar lane would add one. The raw fields, the
// timestamp, the sequence, the flags and the tail take no part: the voter
// takes them from the checked frame, never from a lane.
bool lanes_agree(const TelemetryFrame& a, const TelemetryFrame& b);

class Voter {
public:
    // REQ-RED-003's count: a lane outside the majority on this many
    // consecutive cycles that have a majority is excluded until restart.
    // A named constant, not a key, because the requirement states it and a
    // new key would stop every recorded configuration from replaying; why
    // three is design.md section 12. It is the constructor's default so the
    // fault-injection harness can sweep it by building a voter directly.
    static constexpr int kLanePersistence = 3;

    // explicit: a bare integer must never silently become a voter.
    explicit Voter(int persistence = kLanePersistence);

    // One cycle's vote over the lanes' outputs for `checked`, the frame the
    // detector judged and every lane was given. The result is `checked`
    // carrying the majority's computed fields and validities, or, with no
    // majority, the last voted ones degraded (REQ-RED-004), and the voter's
    // decisions in `voter_status`. Lanes are numbered 1 to 3 in the events
    // and the bits, 0 to 2 in the array.
    TelemetryFrame vote(const TelemetryFrame& checked, const std::array<TelemetryFrame, kLaneCount>& lanes);

    // Events recorded since the previous call (channel VOTER), handed out
    // once each, as FaultDetector::take_events does.
    std::vector<FaultEvent> take_events();

private:
    struct LaneState {
        bool faulted = false;   // excluded until restart (REQ-RED-003)
        int misses = 0;         // consecutive majority cycles outside the majority
    };

    // The lanes' counts on a cycle that has a majority, whose representative
    // is lane `representative`; events are stamped `now_ms`.
    void count(const std::array<TelemetryFrame, kLaneCount>& lanes,
               const std::array<bool, kLaneCount>& eligible,
               std::size_t representative, uint64_t now_ms);

    // The held frame before any vote: zeros, every validity
    // NO_COMPUTED_DATA, the "never voted" of REQ-RED-004.
    void clear_held();

    // The only place a voter's FaultEvent is constructed: value-initialized,
    // then assigned, so the reserved bytes are zero (design.md section 9).
    void record_event(uint64_t now_ms, FaultType type, float value);

    int persistence_;
    std::array<LaneState, kLaneCount> lanes_{};
    // The computed fields and validities of the last vote with a majority,
    // what a no-majority frame holds (REQ-RED-004); only those ten fields
    // of it are read.
    TelemetryFrame held_{};
    bool in_no_majority_ = false;
    std::vector<FaultEvent> events_;
};
