#pragma once

#include <cstddef>

#include "telemetry_frame.h"

// The voter of the three processing lanes (REQ-RED-001 to REQ-RED-006,
// design.md section 13). This part: the one comparison between two lanes.

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
