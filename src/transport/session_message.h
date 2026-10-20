#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "run_mode.h"

// The session status message (REQ-TRANS-004): what a consumer cannot learn
// from the frames. Every frame message is a one-record log file
// (log_format.h), and live, simulated, replayed and ADS-B frames have one
// layout, so the wire needs one more message to say which it carries,
// whether the stream has ended and, in ADS-B mode, which aircraft the
// frames present (REQ-SENS-009, REQ-DISP-007); without it a finished
// replay and a dead link look the same at a receiver, and a page could not
// label its data.
//
// Nineteen bytes under a magic of their own:
//
//   "FTSS" | version (3) | mode | state | source mode | icao24 (3) | callsign (8)
//
// The source mode (REQ-LOG-007, since version 3 in the byte version 2
// reserved) is how the frames were originally produced: the run mode, except
// in a replay, where main reads it from the input's header, UNKNOWN for a log
// older than format version 3. A page labels a replayed ADS-B recording's
// identity, datum and attribution by it, the properties of the data rather
// than of the run.
//
// The identity is the selected aircraft's ICAO address as three bytes and
// its callsign as eight ASCII bytes padded with spaces, all eleven zero
// when nothing is selected and in every other mode. A reader dispatches on
// the first four bytes: "FTLG" is a frame message, "FTSS" a status,
// anything else is refused as before. The status is not a log record and
// is never logged: the log's header says how it was recorded (REQ-LOG-007)
// and its end is its end (FTS-DD-001 section 9). The version byte lets the
// status grow on its own: version 2 since 2026-10-15, with the identity;
// version 3 the same day, with the source mode; earlier versions are no
// longer read, because nothing persists a status and a mismatched pair
// should fail by name.
//
// Cadence (main.cpp): RUNNING before the first frame, at least once per
// second after it, and at once when the identity changes, because pub/sub
// has no handshake and a datagram no connection, so a consumer that joins
// late learns the mode from the next one and a consumer re-takes its track
// before the first frame of a new aircraft; ENDED after the last frame,
// repeated, because the end is the one message no later message can
// repair on a lossy channel, and a consumer treats repeats as one. Both
// figures are facts about the wire, constants and not configuration keys,
// like the ZeroMQ high-water mark (FTS-DD-001 section 12).

// RunMode lives in run_mode.h since format version 3: the log header
// records it too (REQ-LOG-007), and logging does not include transport.

enum class StreamState : uint8_t {
    RUNNING = 1,
    ENDED = 2,       // the last frame has been published; nothing follows
};

// The selected aircraft as the wire carries it; every byte zero means none.
struct SessionIdentity {
    std::array<uint8_t, 3> icao24{};
    std::array<uint8_t, 8> callsign{};
};

bool operator==(const SessionIdentity& a, const SessionIdentity& b);
bool operator!=(const SessionIdentity& a, const SessionIdentity& b);

// The identity of a selected aircraft: the address's six hex digits as
// three bytes (zero when they are not six hex digits), the callsign's
// first eight characters padded with spaces, printable ASCII only and '?'
// for anything else. A selected aircraft without a callsign is eight
// spaces, which is not "none".
SessionIdentity session_identity(const std::string& icao24_hex, const std::string& callsign);

// One status as main builds it: how this session runs, how its frames were
// produced, whether it has ended, and the aircraft an ADS-B run presents.
struct SessionStatus {
    RunMode mode = RunMode::UNKNOWN;          // the run's own mode; main never sends UNKNOWN
    RunMode source_mode = RunMode::UNKNOWN;   // REQ-LOG-007: the mode the frames were produced in
    StreamState state = StreamState::RUNNING;
    SessionIdentity identity{};               // zero unless an ADS-B run has an aircraft selected
};

constexpr uint8_t kSessionMessageVersion = 3;
constexpr size_t kSessionMessageBytes = 19;

// At least one RUNNING status per this many milliseconds of cycles.
constexpr int kStatusIntervalMs = 1000;
// How many times ENDED is sent after the last frame.
constexpr int kEndedRepeats = 3;

std::array<uint8_t, kSessionMessageBytes> session_message(const SessionStatus& status);

const char* state_name(StreamState state);   // mode_name is run_mode.h's
