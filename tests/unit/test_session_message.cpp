// Tests for src/transport/session_message (REQ-TRANS-004, REQ-DISP-007).
// Test cards TC-021 step 1 and TC-031 step 10: the status is nineteen bytes
// under its own magic, which no frame reader accepts, carrying the mode,
// the state, since version 3 the source mode of REQ-LOG-007 in the byte
// version 2 reserved, and the identity of the selected aircraft or eleven
// zero bytes; every mode and state has a name.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include "logging/log_format.h"
#include "transport/session_message.h"

TEST(SessionMessageTest, BytesAreMagicVersionModeStateSourceAndIdentity) {
    EXPECT_EQ(kSessionMessageVersion, 3);
    const std::array<uint8_t, kSessionMessageBytes> bytes =
        session_message(SessionStatus{RunMode::REPLAY, RunMode::ADSB, StreamState::RUNNING, SessionIdentity{}});
    const std::array<uint8_t, kSessionMessageBytes> expected = {
        0x46, 0x54, 0x53, 0x53,   // "FTSS"
        0x03,                     // kSessionMessageVersion
        0x03,                     // REPLAY
        0x01,                     // RUNNING
        0x04,                     // the source mode: the replay of an ADS-B recording
        0x00, 0x00, 0x00,         // no address
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // no callsign: a replay carries none
    };
    EXPECT_EQ(bytes, expected);

    const std::array<uint8_t, kSessionMessageBytes> with_identity = session_message(
        SessionStatus{RunMode::ADSB, RunMode::ADSB, StreamState::RUNNING, session_identity("a1b2c3", "TEST001")});
    const std::array<uint8_t, kSessionMessageBytes> expected_identity = {
        0x46, 0x54, 0x53, 0x53, 0x03, 0x04, 0x01, 0x04,
        0xa1, 0xb2, 0xc3,
        'T', 'E', 'S', 'T', '0', '0', '1', ' ',   // space-padded to eight
    };
    EXPECT_EQ(with_identity, expected_identity);
}

// Every mode, every source mode a status can name (UNKNOWN for a replay of
// a log older than format version 3) and every state, each in its byte.
TEST(SessionMessageTest, EveryModeAndStateEncodes) {
    const std::array<RunMode, 4> modes = {RunMode::LIVE, RunMode::SIMULATED, RunMode::REPLAY, RunMode::ADSB};
    const std::array<RunMode, 4> sources = {RunMode::UNKNOWN, RunMode::LIVE, RunMode::SIMULATED, RunMode::ADSB};
    const std::array<StreamState, 2> states = {StreamState::RUNNING, StreamState::ENDED};
    for (const RunMode mode : modes) {
        for (const RunMode source : sources) {
            for (const StreamState state : states) {
                const std::array<uint8_t, kSessionMessageBytes> bytes =
                    session_message(SessionStatus{mode, source, state, SessionIdentity{}});
                EXPECT_EQ(std::memcmp(bytes.data(), "FTSS", 4), 0);
                EXPECT_EQ(bytes[4], kSessionMessageVersion);
                EXPECT_EQ(bytes[5], static_cast<uint8_t>(mode));
                EXPECT_EQ(bytes[6], static_cast<uint8_t>(state));
                EXPECT_EQ(bytes[7], static_cast<uint8_t>(source));
                for (size_t i = 8; i < bytes.size(); i++) {
                    EXPECT_EQ(bytes[i], 0u) << i;
                }
            }
        }
    }
}

// The identity on the wire: the address's hex digits as bytes, upper or
// lower case; an address that is not six hex digits is zero; the callsign
// truncated to eight, space-padded, non-printable bytes replaced; an
// aircraft without a callsign is eight spaces, which is not "none".
TEST(SessionMessageTest, IdentityEncodesAddressAndCallsign) {
    const SessionIdentity lower = session_identity("a1b2c3", "UAL1234");
    EXPECT_EQ(lower.icao24, (std::array<uint8_t, 3>{0xa1, 0xb2, 0xc3}));
    EXPECT_EQ(std::string(lower.callsign.begin(), lower.callsign.end()), "UAL1234 ");
    EXPECT_EQ(session_identity("A1B2C3", "UAL1234"), lower);
    EXPECT_EQ(session_identity("a1b2cz", "X").icao24, (std::array<uint8_t, 3>{0, 0, 0}));
    EXPECT_EQ(session_identity("a1b2c", "X").icao24, (std::array<uint8_t, 3>{0, 0, 0}));
    const SessionIdentity long_callsign = session_identity("000001", "TOOLONGCALLSIGN");
    EXPECT_EQ(std::string(long_callsign.callsign.begin(), long_callsign.callsign.end()), "TOOLONGC");
    const SessionIdentity odd = session_identity("000001", std::string("AB\x01\xff") + "C");
    EXPECT_EQ(std::string(odd.callsign.begin(), odd.callsign.end()), "AB??C   ");
    const SessionIdentity nameless = session_identity("000001", "");
    EXPECT_EQ(std::string(nameless.callsign.begin(), nameless.callsign.end()), "        ");
    EXPECT_NE(nameless, SessionIdentity{});
    EXPECT_EQ(SessionIdentity{}, SessionIdentity{});
}

// A status laid over a frame-message header is refused by the first check a
// frame reader makes, so a status can never be read as a frame.
TEST(SessionMessageTest, IsRefusedAsAFrameHeader) {
    LogFileHeader header{};
    const std::array<uint8_t, kSessionMessageBytes> bytes =
        session_message(SessionStatus{RunMode::SIMULATED, RunMode::SIMULATED, StreamState::ENDED, SessionIdentity{}});
    std::memcpy(&header, bytes.data(), sizeof(header));
    EXPECT_EQ(header_problem(header), "bad magic: not a telemetry log");
}

TEST(SessionMessageTest, EveryModeAndStateHasAName) {
    EXPECT_STREQ(mode_name(RunMode::LIVE), "LIVE");
    EXPECT_STREQ(mode_name(RunMode::SIMULATED), "SIMULATED");
    EXPECT_STREQ(mode_name(RunMode::REPLAY), "REPLAY");
    EXPECT_STREQ(mode_name(RunMode::ADSB), "ADSB");
    // UNKNOWN is a named value since format version 3 (a header older than
    // it, or a replay of one), and a byte outside the enumeration, which a
    // corrupt header could carry, names itself the same way.
    EXPECT_STREQ(mode_name(RunMode::UNKNOWN), "UNKNOWN");
    EXPECT_STREQ(mode_name(static_cast<RunMode>(9)), "UNKNOWN");
    EXPECT_STREQ(state_name(StreamState::RUNNING), "RUNNING");
    EXPECT_STREQ(state_name(StreamState::ENDED), "ENDED");
}
