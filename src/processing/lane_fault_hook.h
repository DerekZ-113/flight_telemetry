#pragma once

#include "telemetry_frame.h"

// The lane-fault interface of REQ-RED-005, for verification only.
// Identical lanes never disagree, so every path of the voter is reachable
// only by altering a lane: the fault-injection harness implements this and
// hands it to RedundantProcessor, which calls it around each lane on every
// cycle. The binary never builds one (design.md section 13).
//
// Lanes are numbered 1 to 3, as in the voter's events and bits. An altered
// output is a one-cycle upset that leaves the lane's state intact; an
// altered input runs through the lane's filters and diverges its state for
// many cycles, a persistent lane fault from one injection; an input given
// another timestamp or sequence comes out as a stale output, an overrun
// (REQ-RED-006). Pure virtual, as Publisher is: a test that needs only one
// of the two leaves the other's body empty.
class LaneFaultHook {
public:
    virtual ~LaneFaultHook() = default;

    // Before lane `lane` processes its copy of the checked frame.
    virtual void perturb_input(int lane, TelemetryFrame& lane_input) = 0;

    // After lane `lane` produced its output, before the vote.
    virtual void perturb_output(int lane, TelemetryFrame& lane_output) = 0;
};
