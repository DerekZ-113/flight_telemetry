// Tests for src/replay/recorded_injections.cpp (REQ-RED-007): the lane
// faults a log records, which a replay re-applies. Test card TC-037 step 4.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "logging/binary_logger.h"
#include "processing/fault_detector.h"
#include "processing/lane_fault_injector.h"
#include "replay/recorded_injections.h"
#include "scratch_dir.h"
#include "telemetry_frame.h"

namespace fs = std::filesystem;

namespace {

FaultEvent event(uint64_t t_ms, Channel channel, FaultType type, float value) {
    FaultEvent e{};
    e.timestamp_ms = t_ms;
    e.channel = channel;
    e.type = type;
    e.value = value;
    return e;
}

// A log of `frames` frames 20 ms apart, with `events` after the frame each
// one's timestamp names, as main writes them.
void write_log(const fs::path& directory, const std::string& prefix, int frames,
               const std::vector<FaultEvent>& events, uint64_t max_file_bytes = 1 << 20) {
    BinaryLogger logger(directory, prefix, max_file_bytes);
    for (int i = 0; i < frames; i++) {
        TelemetryFrame frame{};
        frame.timestamp_ms = static_cast<uint64_t>(i) * 20;
        logger.log_frame(frame);
        for (const FaultEvent& e : events) {
            if (e.timestamp_ms == frame.timestamp_ms) {
                logger.log_event(e);
            }
        }
    }
    ASSERT_TRUE(logger.ok());
}

}  // namespace

// Every LANE_FAULT_INJECTED becomes the demonstration fault at its lane and
// timestamp, in the order the log holds them; the detector's and the
// voter's events beside them are not injections.
TEST(RecordedInjectionsTest, FindsEveryRecordedLaneFaultInOrder) {
    const ScratchDir dir("recorded");
    write_log(dir.path(), "t", 200,
              {event(1000, Channel::VOTER, FaultType::LANE_FAULT_INJECTED, 2.0f),
               event(1000, Channel::VOTER, FaultType::LANE_DISAGREEMENT, 2.0f),
               event(1500, Channel::BARO, FaultType::COMM_TIMEOUT, 520.0f),
               event(3000, Channel::VOTER, FaultType::LANE_FAULT_INJECTED, 3.0f)});
    std::string error;
    const std::vector<LaneFault> faults = recorded_lane_faults({dir.path() / "t_000.bin"}, error);
    EXPECT_TRUE(error.empty()) << error;
    ASSERT_EQ(faults.size(), 2u);
    const LaneFault first = demo_lane_fault(2, 1000);
    EXPECT_EQ(faults[0].lane, first.lane);
    EXPECT_EQ(faults[0].onset_ms, first.onset_ms);
    EXPECT_EQ(faults[0].kind, first.kind);
    EXPECT_EQ(faults[0].field, first.field);
    EXPECT_EQ(faults[0].bit, first.bit);
    EXPECT_TRUE(faults[0].recorded);
    EXPECT_EQ(faults[1].lane, 3);
    EXPECT_EQ(faults[1].onset_ms, 3000u);
}

TEST(RecordedInjectionsTest, ALogWithoutTheEventRecordsNone) {
    const ScratchDir dir("recorded");
    write_log(dir.path(), "t", 50, {event(400, Channel::IMU, FaultType::STUCK, 0.1f)});
    std::string error;
    EXPECT_TRUE(recorded_lane_faults({dir.path() / "t_000.bin"}, error).empty());
    EXPECT_TRUE(error.empty()) << error;
}

// Rotated files are read in the order given, as the replay source reads
// them, so a marker in a later file is found.
TEST(RecordedInjectionsTest, ReadsRotatedFilesInOrder) {
    const ScratchDir dir("recorded");
    write_log(dir.path(), "t", 40, {event(700, Channel::VOTER, FaultType::LANE_FAULT_INJECTED, 1.0f)},
              16 + 20 * 121);   // twenty frames a file
    std::string error;
    const std::vector<LaneFault> faults =
        recorded_lane_faults({dir.path() / "t_000.bin", dir.path() / "t_001.bin"}, error);
    EXPECT_TRUE(error.empty()) << error;
    ASSERT_EQ(faults.size(), 1u);
    EXPECT_EQ(faults[0].lane, 1);
    EXPECT_EQ(faults[0].onset_ms, 700u);
}

// A file the reader refuses ends the scan with the reader's message and no
// fault, so a replay refuses to start rather than replay a part of it.
TEST(RecordedInjectionsTest, AnUnreadableFileIsAnError) {
    const ScratchDir dir("recorded");
    std::string error;
    EXPECT_TRUE(recorded_lane_faults({dir.path() / "missing_000.bin"}, error).empty());
    EXPECT_FALSE(error.empty());
}

// An injection that names no lane cannot be re-applied: refused by name.
TEST(RecordedInjectionsTest, AnInjectionNamingNoLaneIsRefused) {
    const ScratchDir dir("recorded");
    write_log(dir.path(), "t", 60, {event(200, Channel::VOTER, FaultType::LANE_FAULT_INJECTED, 4.0f)});
    std::string error;
    EXPECT_TRUE(recorded_lane_faults({dir.path() / "t_000.bin"}, error).empty());
    EXPECT_NE(error.find("LANE_FAULT_INJECTED"), std::string::npos) << error;
    EXPECT_NE(error.find("t_000.bin"), std::string::npos) << error;
}
