#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The session status message (REQ-TRANS-004): what a consumer cannot learn
// from the frames. Every frame message is a one-record log file
// (log_format.h), and live, simulated and replayed frames have one layout,
// so the wire needs one more message to say which it carries and whether
// the stream has ended; without it a finished replay and a dead link look
// the same at a receiver, and a page could not label its data.
//
// Eight bytes under a magic of their own:
//
//   "FTSS" | version (1) | mode | state | reserved (0)
//
// A reader dispatches on the first four bytes: "FTLG" is a frame message,
// "FTSS" a status, anything else is refused as before. The status is not a
// log record and is never logged: the log's prefix says replay and its end
// is its end, so the log format and the frame message are unchanged and the
// format version stays at 2 (FTS-DD-001 section 9). The version byte lets
// the status grow on its own.
//
// Cadence (main.cpp): RUNNING before the first frame and at least once per
// second after it, because pub/sub has no handshake and a datagram no
// connection, so a consumer that joins late learns the mode from the next
// one; ENDED after the last frame, repeated, because the end is the one
// message no later message can repair on a lossy channel, and a consumer
// treats repeats as one. Both figures are facts about the wire, constants
// and not configuration keys, like the ZeroMQ high-water mark (FTS-DD-001
// section 12).

enum class RunMode : uint8_t {
    LIVE = 1,        // the sensors (--live)
    SIMULATED = 2,   // the simulator, the default source
    REPLAY = 3,      // a recorded log (--replay)
};

enum class StreamState : uint8_t {
    RUNNING = 1,
    ENDED = 2,       // the last frame has been published; nothing follows
};

constexpr uint8_t kSessionMessageVersion = 1;
constexpr size_t kSessionMessageBytes = 8;

// At least one RUNNING status per this many milliseconds of cycles.
constexpr int kStatusIntervalMs = 1000;
// How many times ENDED is sent after the last frame.
constexpr int kEndedRepeats = 3;

std::array<uint8_t, kSessionMessageBytes> session_message(RunMode mode, StreamState state);

const char* mode_name(RunMode mode);
const char* state_name(StreamState state);
