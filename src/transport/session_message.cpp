#include "transport/session_message.h"

#include <cstring>

namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

bool operator==(const SessionIdentity& a, const SessionIdentity& b) {
    return a.icao24 == b.icao24 && a.callsign == b.callsign;
}

bool operator!=(const SessionIdentity& a, const SessionIdentity& b) {
    return !(a == b);
}

SessionIdentity session_identity(const std::string& icao24_hex, const std::string& callsign) {
    SessionIdentity identity;
    if (icao24_hex.size() == 6) {
        std::array<uint8_t, 3> bytes{};
        bool valid = true;
        for (size_t i = 0; i < bytes.size(); i++) {
            const int high = hex_value(icao24_hex[2 * i]);
            const int low = hex_value(icao24_hex[2 * i + 1]);
            if (high < 0 || low < 0) {
                valid = false;
                break;
            }
            bytes[i] = static_cast<uint8_t>(high * 16 + low);
        }
        if (valid) {
            identity.icao24 = bytes;
        }
    }
    identity.callsign.fill(' ');
    for (size_t i = 0; i < identity.callsign.size() && i < callsign.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(callsign[i]);
        identity.callsign[i] = (c >= 0x20 && c < 0x7f) ? c : '?';
    }
    return identity;
}

std::array<uint8_t, kSessionMessageBytes> session_message(RunMode mode, StreamState state,
                                                          const SessionIdentity& identity) {
    std::array<uint8_t, kSessionMessageBytes> out{};
    std::memcpy(out.data(), "FTSS", 4);
    out[4] = kSessionMessageVersion;
    out[5] = static_cast<uint8_t>(mode);
    out[6] = static_cast<uint8_t>(state);
    out[7] = 0;   // reserved: zero until a version assigns it
    std::memcpy(out.data() + 8, identity.icao24.data(), identity.icao24.size());
    std::memcpy(out.data() + 11, identity.callsign.data(), identity.callsign.size());
    return out;
}

const char* mode_name(RunMode mode) {
    switch (mode) {  // LCOV_EXCL_BR_LINE: exhaustive over enum class, no-match branch unreachable
        case RunMode::LIVE:      return "LIVE";
        case RunMode::SIMULATED: return "SIMULATED";
        case RunMode::REPLAY:    return "REPLAY";
        case RunMode::ADSB:      return "ADSB";
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
