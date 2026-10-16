// Tests for src/replay/log_replay_source.cpp (REQ-LOG-003, REQ-LOG-004).
// Test card TC-004, executed here at unit level; the Python comparison in
// TC-004 is tests/integration/test_replay_identity.py.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
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
// status NOMINAL, validity NOT_JUDGED, the reserved bytes zero, everything
// the sensors said and the read_ok flags kept (TC-017 step 9).
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
    processed.baro_altitude_validity = Validity::NORMAL;
    processed.pitch_validity = Validity::NORMAL;
    processed.roll_validity = Validity::NORMAL;
    processed.fused_altitude_validity = Validity::NORMAL;
    processed.vertical_speed_validity = Validity::NORMAL;
    processed.reserved_a = 1;
    processed.reserved_b = 1;
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
    EXPECT_EQ(raw.baro_altitude_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(raw.pitch_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(raw.roll_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(raw.fused_altitude_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(raw.vertical_speed_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(raw.reserved_a, 0u);
    EXPECT_EQ(raw.reserved_b, 0u);
}

// TC-017 step 19 (REQ-FAULT-007, FAULT-018). The one status a source
// asserts survives the strip: a channel recorded NOT_AVAILABLE comes back
// NOT_AVAILABLE, so a replay reproduces the availability the source wrote
// into every frame, while the statuses the detector judged go back to
// NOMINAL for it to judge again, and the validities to NOT_JUDGED.
TEST_F(ReplayTest, NotAvailableSurvivesTheStrip) {
    TelemetryFrame processed{};
    processed.timestamp_ms = 60;
    processed.baro_status = ChannelStatus::DEGRADED;
    processed.imu_status = ChannelStatus::NOT_AVAILABLE;
    processed.gps_status = ChannelStatus::NO_DATA;
    processed.pitch_validity = Validity::NO_COMPUTED_DATA;
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        logger.log_frame(processed);
    }

    LogReplaySource source({dir_ / "t_000.bin"});
    const TelemetryFrame raw = source.read_frame();
    EXPECT_EQ(raw.baro_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(raw.imu_status, ChannelStatus::NOT_AVAILABLE);
    EXPECT_EQ(raw.gps_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(raw.pitch_validity, Validity::NOT_JUDGED);
}

// REQ-LOG-005, TC-017 step 9. A version-1 log replays: its raw fields come
// through as logged and its frames arrive unjudged, the reader having set
// the version-1 padding to what version 2 means by it.
TEST_F(ReplayTest, Version1LogReplaysAsRawFrames) {
    LogFileHeader h = make_log_header();
    h.version = 1;
    TelemetryFrame logged{};
    logged.timestamp_ms = 20;
    logged.pressure_hpa = 1012.5f;
    logged.accel_z = 9.81f;
    logged.baro_read_ok = true;
    logged.imu_read_ok = true;
    logged.reserved_a = 0xDEADBEEFu;                   // stack contents in a pre-9/9 log
    logged.pitch_validity = Validity::NORMAL;
    logged.reserved_b = 0x55;
    {
        std::ofstream out(dir_ / "v1_000.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        const uint8_t tag = static_cast<uint8_t>(RecordType::FRAME);
        out.write(reinterpret_cast<const char*>(&tag), 1);
        out.write(reinterpret_cast<const char*>(&logged), sizeof(logged));
    }
    LogReplaySource source({dir_ / "v1_000.bin"});
    const TelemetryFrame raw = source.read_frame();
    EXPECT_EQ(raw.timestamp_ms, 20u);
    EXPECT_EQ(raw.pressure_hpa, 1012.5f);
    EXPECT_EQ(raw.accel_z, 9.81f);
    EXPECT_TRUE(raw.baro_read_ok);
    EXPECT_EQ(raw.pitch_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(raw.reserved_a, 0u);
    EXPECT_EQ(raw.reserved_b, 0u);
    EXPECT_EQ(source.frames_read(), 1u);
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

// The fallback for a caller that never asks has_next(): past the end, the
// last frame repeats and exhausted() says so. REQ-LOG-006 makes the loop
// ask, so no cycle runs on a repeat (HasNextIsFalseAfterTheLastFrame).
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

// REQ-LOG-006, TC-019 step 1: a source can say whether a next frame exists.
// The answer comes from a one-frame look-ahead that consumes nothing, so the
// loop can ask before every read and stop exactly at the log's end.
TEST_F(ReplayTest, HasNextIsFalseAfterTheLastFrame) {
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        for (int i = 0; i < 3; i++) {
            TelemetryFrame f{};
            f.timestamp_ms = static_cast<uint64_t>(i) * 20;
            logger.log_frame(f);
        }
    }
    LogReplaySource source({dir_ / "t_000.bin"});
    for (int i = 0; i < 3; i++) {
        EXPECT_TRUE(source.has_next()) << "before frame " << i;
        EXPECT_EQ(source.read_frame().timestamp_ms, static_cast<uint64_t>(i) * 20) << "frame " << i;
    }
    EXPECT_FALSE(source.has_next());
    EXPECT_TRUE(source.exhausted());
    EXPECT_EQ(source.frames_read(), 3u);
    // The fallback for a caller that never asks: the last frame again,
    // counted once.
    EXPECT_EQ(source.read_frame().timestamp_ms, 40u);
    EXPECT_EQ(source.frames_read(), 3u);
}

// TC-019 step 1, the look-ahead consumes nothing: asking twice then reading
// gives the one frame once.
TEST_F(ReplayTest, HasNextDoesNotConsume) {
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        TelemetryFrame f{};
        f.timestamp_ms = 100;
        logger.log_frame(f);
    }
    LogReplaySource source({dir_ / "t_000.bin"});
    EXPECT_TRUE(source.has_next());
    EXPECT_TRUE(source.has_next());
    EXPECT_EQ(source.read_frame().timestamp_ms, 100u);
    EXPECT_EQ(source.frames_read(), 1u);
    EXPECT_FALSE(source.has_next());
}

// TC-019 step 2: the look-ahead opens files lazily and skips an unreadable
// one, as read_frame() always did.
TEST_F(ReplayTest, HasNextOpensLazilyAndSkipsUnreadable) {
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        TelemetryFrame f{};
        f.timestamp_ms = 7;
        logger.log_frame(f);
    }
    LogReplaySource source({dir_ / "missing.bin", dir_ / "t_000.bin"});
    EXPECT_TRUE(source.has_next());
    EXPECT_EQ(source.read_frame().timestamp_ms, 7u);
    EXPECT_FALSE(source.has_next());
}

// TC-019 step 3.
TEST_F(ReplayTest, EmptyListHasNoNext) {
    LogReplaySource source({});
    EXPECT_FALSE(source.has_next());
    EXPECT_TRUE(source.exhausted());
}

// TC-019 step 4: the injector wraps a source and answers as it does; an
// endless source answers true forever, which is the base class's default.
TEST_F(ReplayTest, FaultInjectorForwardsHasNext) {
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        for (int i = 0; i < 2; i++) {
            TelemetryFrame f{};
            f.timestamp_ms = static_cast<uint64_t>(i) * 20;
            logger.log_frame(f);
        }
    }
    FaultInjectingSource source(std::make_unique<LogReplaySource>(
        std::vector<fs::path>{dir_ / "t_000.bin"}));
    EXPECT_TRUE(source.has_next());
    EXPECT_EQ(source.read_frame().timestamp_ms, 0u);
    EXPECT_TRUE(source.has_next());
    EXPECT_EQ(source.read_frame().timestamp_ms, 20u);
    EXPECT_FALSE(source.has_next());
}

TEST_F(ReplayTest, SimulatedSourceAlwaysHasNext) {
    SimulatedSource source(20, 42);
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(source.has_next()) << "frame " << i;
        source.read_frame();
    }
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
        // REQ-LOG-006 (TC-019 step 5): the loop asks the source, not a
        // count, and stops exactly at the log's end.
        while (source.has_next()) {
            replay_frames.push_back(processor.process(detector.check(source.read_frame())));
            for (const FaultEvent& e : detector.take_events()) {
                replay_events.push_back(e);
            }
        }
        EXPECT_TRUE(source.exhausted());
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

    // The validity bytes are part of the identity above; this says what
    // they hold (REQ-PROC-007, TC-017 step 9): FAILURE_WARNING exactly on
    // the frames the detector marked DEGRADED, which begin inside each
    // injected window (after the timeout, or the tenth repeat) and end M
    // valid readings after it (REQ-FAULT-005), NORMAL everywhere else, and
    // the fused fields NORMAL throughout because the GPS contributes from
    // frame 0.
    const size_t m = static_cast<size_t>(FaultDetectorConfig{}.recovery_count);
    bool baro_flagged = false;
    bool imu_flagged = false;
    for (size_t i = 0; i < live_frames.size(); i++) {
        const TelemetryFrame& f = live_frames[i];
        const Validity baro = f.baro_status == ChannelStatus::DEGRADED ? Validity::FAILURE_WARNING : Validity::NORMAL;
        const Validity imu = f.imu_status == ChannelStatus::DEGRADED ? Validity::FAILURE_WARNING : Validity::NORMAL;
        EXPECT_EQ(f.baro_altitude_validity, baro) << "frame " << i;
        EXPECT_EQ(f.pitch_validity, imu) << "frame " << i;
        EXPECT_EQ(f.roll_validity, imu) << "frame " << i;
        EXPECT_EQ(f.fused_altitude_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_EQ(f.vertical_speed_validity, Validity::NORMAL) << "frame " << i;
        baro_flagged = baro_flagged || (i >= 100 && i < 140 && baro == Validity::FAILURE_WARNING);
        imu_flagged = imu_flagged || (i >= 300 && i < 320 && imu == Validity::FAILURE_WARNING);
        const bool baro_outside = i < 100 || i >= 140 + m - 1;
        const bool imu_outside = i < 300 || i >= 320 + m - 1;
        if (baro_outside) {
            EXPECT_EQ(baro, Validity::NORMAL) << "frame " << i;
        }
        if (imu_outside) {
            EXPECT_EQ(imu, Validity::NORMAL) << "frame " << i;
        }
    }
    EXPECT_TRUE(baro_flagged) << "the barometer dropout never reached FAILURE_WARNING";
    EXPECT_TRUE(imu_flagged) << "the stuck IMU never reached FAILURE_WARNING";
}

// REQ-LOG-005, TC-017 step 9. A version-1 log's frames come through the
// pipeline judged: the raw fields are the record, the rest is recomputed.
TEST_F(ReplayTest, Version1LogReplaysJudged) {
    LogFileHeader h = make_log_header();
    h.version = 1;
    {
        std::ofstream out(dir_ / "v1_000.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        SimulatedSource source(20, 42);
        for (int i = 0; i < 5; i++) {
            const TelemetryFrame raw = source.read_frame();   // a version-1 log's raw bytes are these
            const uint8_t tag = static_cast<uint8_t>(RecordType::FRAME);
            out.write(reinterpret_cast<const char*>(&tag), 1);
            out.write(reinterpret_cast<const char*>(&raw), sizeof(raw));
        }
    }
    LogReplaySource source({dir_ / "v1_000.bin"});
    FaultDetector detector;
    TelemetryProcessor processor;
    for (int i = 0; i < 5; i++) {
        const TelemetryFrame out = processor.process(detector.check(source.read_frame()));
        EXPECT_EQ(out.baro_altitude_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_EQ(out.pitch_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_EQ(out.fused_altitude_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_EQ(out.baro_status, ChannelStatus::NOMINAL) << "frame " << i;
    }
    EXPECT_EQ(source.frames_read(), 5u);
}
