#pragma once

#include <cstdint>

// The run mode: how a session's frames are produced (REQ-LOG-007,
// REQ-TRANS-004). One vocabulary for the two places it travels: the log
// header, which records the run mode of the session that wrote the file
// and the source mode in which its frames were originally produced (the
// run mode, except for a replay, which copies its input's forward), and
// the session status on the wire, which carries both to a consumer. The
// log format includes this header and so does the transport, and neither
// includes the other.
//
// UNKNOWN is the zero: a header written before format version 3, whose
// two bytes were one reserved field and zero, reads UNKNOWN for both, and
// a replay of such a file carries UNKNOWN forward as its source mode
// rather than guessing from the frames. main never chooses UNKNOWN for a
// run it starts.
enum class RunMode : uint8_t {
    UNKNOWN = 0,     // a file older than format version 3, or a replay of one
    LIVE = 1,        // the sensors (--live)
    SIMULATED = 2,   // the simulator, the default source
    REPLAY = 3,      // a recorded log (--replay)
    ADSB = 4,        // one aircraft from the OpenSky Network (--adsb)
};

// The name a log line, the receiver or a test shows. A byte outside the
// enumeration, which a corrupt header could carry, names itself UNKNOWN
// rather than as a mode it is not.
const char* mode_name(RunMode mode);
