// Tests for src/replay/log_replay_source.cpp (REQ-LOG-003, REQ-LOG-004).
// Test card TC-004, executed here at unit level; the Python comparison in
// TC-004 is the integration-level execution and lands with the receiver.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

#include "drivers/fault_injecting_source.h"
#include "drivers/simulated_source.h"
#include "logging/binary_logger.h"
#include "logging/log_reader.h"
#include "processing/fault_detector.h"
#include "processing/processor.h"
#include "replay/log_replay_source.h"

namespace fs = std::filesystem;

namespace {

class ReplayTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        dir_ = fs::temp_directory_path() / ("ftlg_replay_" + std::to_string(counter++));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    fs::path dir_;
};

}  // namespace

// REQ-LOG-004: what comes out of replay is raw. Computed fields zeroed,
// status NOMINAL, everything the sensors said and the read_ok flags kept.
TEST_F(ReplayTest, ReplayStripsComputedFieldsAndStatus) {
    TelemetryFrame processed{};
    processed.timestamp_ms = 40;
    processed.pressure_hpa = 1000.0f;
    processed.accel_z = 9.81f;
    processed.baro_altitude_m = 111.0f;
    processed.pitch_deg = 5.0f;
    processed.roll_deg = -3.0f;
    processed.fused_altitude_m = 110.0f;
    processed.vertical_speed_mps = 0.4f;
    processed.baro_status = ChannelStatus::DEGRADED;
    processed.imu_status = ChannelStatus::DEGRADED;
    processed.gps_status = ChannelStatus::DEGRADED;
    processed.baro_read_ok = false;
    processed.imu_read_ok = true;
    processed.gps_read_ok = true;
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        logger.log_frame(processed);
    }

    LogReplaySource source({dir_ / "t_000.bin"});
    const TelemetryFrame raw = source.read_frame();

    EXPECT_EQ(raw.timestamp_ms, 40u);
    EXPECT_EQ(raw.pressure_hpa, 1000.0f);
    EXPECT_EQ(raw.accel_z, 9.81f);
    EXPECT_FALSE(raw.baro_read_ok);
    EXPECT_TRUE(raw.imu_read_ok);

    EXPECT_EQ(raw.baro_altitude_m, 0.0f);
    EXPECT_EQ(raw.pitch_deg, 0.0f);
    EXPECT_EQ(raw.roll_deg, 0.0f);
    EXPECT_EQ(raw.fused_altitude_m, 0.0f);
    EXPECT_EQ(raw.vertical_speed_mps, 0.0f);
    EXPECT_EQ(raw.baro_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(raw.imu_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(raw.gps_status, ChannelStatus::NOMINAL);
}

// Rotated files replay as one session, in order.
TEST_F(ReplayTest, ReplaySpansRotatedFiles) {
    const uint64_t limit = sizeof(LogFileHeader) + 3 * (1 + sizeof(TelemetryFrame));
    {
        BinaryLogger logger(dir_, "t", limit);
        for (int i = 0; i < 10; i++) {
            TelemetryFrame f{};
            f.timestamp_ms = static_cast<uint64_t>(i) * 20;
            logger.log_frame(f);
        }
    }
    LogReplaySource source(list_log_files(dir_, "t"));
    for (int i = 0; i < 10; i++) {
        EXPECT_FALSE(source.exhausted());
        EXPECT_EQ(source.read_frame().timestamp_ms, static_cast<uint64_t>(i) * 20) << "frame " << i;
    }
    EXPECT_EQ(source.frames_read(), 10u);
}

// The end-of-stream decision, pinned: past the end, the last frame
// repeats and exhausted() says so.
TEST_F(ReplayTest, ExhaustedReturnsLastFrame) {
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        TelemetryFrame f{};
        f.timestamp_ms = 100;
        logger.log_frame(f);
    }
    LogReplaySource source({dir_ / "t_000.bin"});
    EXPECT_EQ(source.read_frame().timestamp_ms, 100u);
    EXPECT_FALSE(source.exhausted());
    EXPECT_EQ(source.read_frame().timestamp_ms, 100u);
    EXPECT_TRUE(source.exhausted());
    EXPECT_EQ(source.read_frame().timestamp_ms, 100u);
    EXPECT_EQ(source.frames_read(), 1u);
}

TEST_F(ReplayTest, UnreadableFileIsSkipped) {
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        TelemetryFrame f{};
        f.timestamp_ms = 7;
        logger.log_frame(f);
    }
    LogReplaySource source({dir_ / "missing.bin", dir_ / "t_000.bin"});
    EXPECT_EQ(source.read_frame().timestamp_ms, 7u);
    EXPECT_EQ(source.frames_read(), 1u);
}

TEST_F(ReplayTest, EmptyFileListIsExhaustedImmediately) {
    LogReplaySource source({});
    const TelemetryFrame f = source.read_frame();
    EXPECT_TRUE(source.exhausted());
    EXPECT_EQ(f.timestamp_ms, 0u);
}

// REQ-LOG-003, TC-004 at unit level. This is the thesis test.
//
// Live: simulator with injected faults -> detector -> processor -> log.
// Replay: log -> LogReplaySource -> fresh detector -> fresh processor.
// Every processed frame and every fault event must be bit-identical.
// A tolerance is not acceptable here; the requirement says identical.
TEST_F(ReplayTest, ReplayReproducesLiveSession) {
    constexpr int kFrames = 500;
    const uint64_t limit = sizeof(LogFileHeader) + 120 * (1 + sizeof(TelemetryFrame));  // force rotation

    std::vector<TelemetryFrame> live_frames;
    std::vector<FaultEvent> live_events;
    {
        FaultInjectingSource source(std::make_unique<SimulatedSource>(20, 42));
        FaultDetector detector;
        TelemetryProcessor processor;
        BinaryLogger logger(dir_, "live", limit);

        for (int i = 0; i < kFrames; i++) {
            // Barometer dropout 100..140, stuck IMU 300..320: both fault
            // types, both recoveries, so status changes are in the log.
            source.fail_reads(Channel::BARO, i >= 100 && i < 140);
            source.hold_values(Channel::IMU, i >= 300 && i < 320);

            const TelemetryFrame processed = processor.process(detector.check(source.read_frame()));
            logger.log_frame(processed);
            live_frames.push_back(processed);
            for (const FaultEvent& e : detector.take_events()) {
                logger.log_event(e);
                live_events.push_back(e);
            }
        }
    }
    ASSERT_GE(live_events.size(), 4u) << "the injected faults must have produced events";

    const auto files = list_log_files(dir_, "live");
    ASSERT_GT(files.size(), 1u) << "rotation must have occurred";

    std::vector<TelemetryFrame> replay_frames;
    std::vector<FaultEvent> replay_events;
    {
        LogReplaySource source(files);
        FaultDetector detector;
        TelemetryProcessor processor;
        for (int i = 0; i < kFrames; i++) {
            replay_frames.push_back(processor.process(detector.check(source.read_frame())));
            for (const FaultEvent& e : detector.take_events()) {
                replay_events.push_back(e);
            }
        }
        EXPECT_FALSE(source.exhausted());
        EXPECT_EQ(source.frames_read(), static_cast<size_t>(kFrames));
    }

    ASSERT_EQ(replay_frames.size(), live_frames.size());
    for (size_t i = 0; i < live_frames.size(); i++) {
        EXPECT_EQ(std::memcmp(&replay_frames[i], &live_frames[i], sizeof(TelemetryFrame)), 0)
            << "frame " << i << " differs";
    }
    ASSERT_EQ(replay_events.size(), live_events.size());
    for (size_t i = 0; i < live_events.size(); i++) {
        EXPECT_EQ(std::memcmp(&replay_events[i], &live_events[i], sizeof(FaultEvent)), 0)
            << "event " << i << " differs";
    }
}
