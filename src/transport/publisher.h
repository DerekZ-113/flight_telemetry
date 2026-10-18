#pragma once

#include <cstdint>

#include "telemetry_frame.h"
#include "transport/session_message.h"

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
//
// The session status (REQ-TRANS-004) travels the same way: publish_status
// sends the nineteen bytes of session_message through the same socket a
// frame goes through, counted apart from frames, so a consumer can tell the
// mode of the run and of the data, the end of the stream and the aircraft an
// ADS-B run presents, which the frames cannot say.

struct PublishStats {
    uint64_t published = 0;       // frames the transport accepted
    uint64_t status_published = 0; // session status messages it accepted (REQ-TRANS-004)
    uint64_t send_failures = 0;   // sends refused or thrown, frames and statuses alike; the loop went on
    int last_errno = 0;           // from the most recent failure, e.g. ETERM, ECONNREFUSED
};

class Publisher {
public:
    virtual ~Publisher() = default;

    // Hands one frame to the transport and returns at once. false means the
    // frame was not sent; stats() says how often that has happened.
    virtual bool publish(const TelemetryFrame& frame) = 0;

    // Hands one session status to the transport and returns at once
    // (REQ-TRANS-004). false means it was not sent; counted like a frame.
    virtual bool publish_status(const SessionStatus& status) = 0;

    virtual const PublishStats& stats() const = 0;
};
