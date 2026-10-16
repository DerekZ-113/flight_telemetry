#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

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
//   "FTSS" | version (2) | mode | state | reserved (0) | icao24 (3) | callsign (8)
//
// The identity is the selected aircraft's ICAO address as three bytes and
// its callsign as eight ASCII bytes padded with spaces, all eleven zero
// when nothing is selected and in every other mode. A reader dispatches on
// the first four bytes: "FTLG" is a frame message, "FTSS" a status,
// anything else is refused as before. The status is not a log record and
// is never logged: the log's prefix says replay and its end is its end, so
// the log format and the frame message are unchanged and the format
// version stays at 2 (FTS-DD-001 section 9). The version byte lets the
// status grow on its own: version 2 since 2026-10-15, with the identity;
// version 1, eight bytes, is no longer read, because nothing persists a
// status and a mismatched pair should fail by name.
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

enum class RunMode : uint8_t {
    LIVE = 1,        // the sensors (--live)
    SIMULATED = 2,   // the simulator, the default source
    REPLAY = 3,      // a recorded log (--replay)
    ADSB = 4,        // one aircraft from the OpenSky Network (--adsb)
};

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

constexpr uint8_t kSessionMessageVersion = 2;
constexpr size_t kSessionMessageBytes = 19;

// At least one RUNNING status per this many milliseconds of cycles.
constexpr int kStatusIntervalMs = 1000;
// How many times ENDED is sent after the last frame.
constexpr int kEndedRepeats = 3;

std::array<uint8_t, kSessionMessageBytes> session_message(RunMode mode, StreamState state,
                                                          const SessionIdentity& identity);

const char* mode_name(RunMode mode);
const char* state_name(StreamState state);
