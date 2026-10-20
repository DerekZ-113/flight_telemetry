// Tests for src/transport/session_message (REQ-TRANS-004). Test card
// TC-021 step 1: the status is eight bytes under its own magic, which no
// frame reader accepts, and every mode and state has a name.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include "logging/log_format.h"
#include "transport/session_message.h"

TEST(SessionMessageTest, BytesAreMagicVersionModeState) {
    const std::array<uint8_t, kSessionMessageBytes> bytes =
        session_message(RunMode::REPLAY, StreamState::RUNNING);
    const std::array<uint8_t, kSessionMessageBytes> expected = {
        0x46, 0x54, 0x53, 0x53,   // "FTSS"
        0x01,                     // kSessionMessageVersion
        0x03,                     // REPLAY
        0x01,                     // RUNNING
        0x00,                     // reserved
    };
    EXPECT_EQ(bytes, expected);
}

TEST(SessionMessageTest, EveryModeAndStateEncodes) {
    const std::array<RunMode, 3> modes = {RunMode::LIVE, RunMode::SIMULATED, RunMode::REPLAY};
    const std::array<StreamState, 2> states = {StreamState::RUNNING, StreamState::ENDED};
    for (const RunMode mode : modes) {
        for (const StreamState state : states) {
            const std::array<uint8_t, kSessionMessageBytes> bytes = session_message(mode, state);
            EXPECT_EQ(std::memcmp(bytes.data(), "FTSS", 4), 0);
            EXPECT_EQ(bytes[4], kSessionMessageVersion);
            EXPECT_EQ(bytes[5], static_cast<uint8_t>(mode));
            EXPECT_EQ(bytes[6], static_cast<uint8_t>(state));
            EXPECT_EQ(bytes[7], 0u);
        }
    }
}

// A status laid over a frame-message header is refused by the first check a
// frame reader makes, so a status can never be read as a frame.
TEST(SessionMessageTest, IsRefusedAsAFrameHeader) {
    LogFileHeader header{};
    const std::array<uint8_t, kSessionMessageBytes> bytes =
        session_message(RunMode::SIMULATED, StreamState::ENDED);
    std::memcpy(&header, bytes.data(), bytes.size());
    EXPECT_EQ(header_problem(header), "bad magic: not a telemetry log");
}

TEST(SessionMessageTest, EveryModeAndStateHasAName) {
    EXPECT_STREQ(mode_name(RunMode::LIVE), "LIVE");
    EXPECT_STREQ(mode_name(RunMode::SIMULATED), "SIMULATED");
    EXPECT_STREQ(mode_name(RunMode::REPLAY), "REPLAY");
    EXPECT_STREQ(state_name(StreamState::RUNNING), "RUNNING");
    EXPECT_STREQ(state_name(StreamState::ENDED), "ENDED");
}
