#pragma once

#include <cstdint>

#include "telemetry_frame.h"

// The output-side seam (REQ-TRANS-001, REQ-TRANS-002): what the processing
// loop sees of a transport. The loop holds base pointers and calls
// publish() once per processed frame; it never learns whether the frame
// went to a ZeroMQ socket, a UDP socket, or both, the same way it never
// learns which DataSource it reads (design.md section 9). The composition
// root owns the concrete objects, checks their startup status, and prints
// their summaries at exit. A third transport is a class and a flag, not a
// change to the loop.
//
// A publish never blocks and never throws (REQ-TRANS-003): each transport
// translates its own failures into the counters below at its boundary.

struct PublishStats {
    uint64_t published = 0;       // frames the transport accepted
    uint64_t send_failures = 0;   // sends refused or thrown; the loop went on
    int last_errno = 0;           // from the most recent failure, e.g. ETERM, ECONNREFUSED
};

class Publisher {
public:
    virtual ~Publisher() = default;

    // Hands one frame to the transport and returns at once. false means the
    // frame was not sent; stats() says how often that has happened.
    virtual bool publish(const TelemetryFrame& frame) = 0;

    virtual const PublishStats& stats() const = 0;
};
