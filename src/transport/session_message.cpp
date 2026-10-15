#include "transport/session_message.h"

#include <cstring>

std::array<uint8_t, kSessionMessageBytes> session_message(RunMode mode, StreamState state) {
    std::array<uint8_t, kSessionMessageBytes> out{};
    std::memcpy(out.data(), "FTSS", 4);
    out[4] = kSessionMessageVersion;
    out[5] = static_cast<uint8_t>(mode);
    out[6] = static_cast<uint8_t>(state);
    out[7] = 0;   // reserved: zero until a version assigns it
    return out;
}

const char* mode_name(RunMode mode) {
    switch (mode) {  // LCOV_EXCL_BR_LINE: exhaustive over enum class, no-match branch unreachable
        case RunMode::LIVE:      return "LIVE";
        case RunMode::SIMULATED: return "SIMULATED";
        case RunMode::REPLAY:    return "REPLAY";
    }
    // A byte outside the enumeration: never main, which chooses the mode.
    return "UNKNOWN";  // LCOV_EXCL_LINE
}

const char* state_name(StreamState state) {
    switch (state) {  // LCOV_EXCL_BR_LINE: exhaustive over enum class, no-match branch unreachable
        case StreamState::RUNNING: return "RUNNING";
        case StreamState::ENDED:   return "ENDED";
    }
    return "UNKNOWN";  // LCOV_EXCL_LINE (unreachable, see mode_name)
}
