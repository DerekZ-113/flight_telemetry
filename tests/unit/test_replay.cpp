// Tests for src/replay/log_replay_source.cpp (REQ-LOG-003, REQ-LOG-004).
// Test card TC-004, executed here at unit level; the Python comparison in
// TC-004 is tests/integration/test_replay_identity.py.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "drivers/fault_injecting_source.h"
#include "drivers/simulated_source.h"
#include "logging/binary_logger.h"
#include "logging/log_reader.h"
#include "processing/fault_detector.h"
#include "processing/processor.h"
#include "processing/redundant_processor.h"
#include "replay/log_replay_source.h"
#include "scratch_dir.h"
#include "scripted_lane_faults.h"

namespace fs = std::filesystem;

namespace {

class ReplayTest : public ::testing::Test {
protected:
    // A directory of this test's own, removed after it (scratch_dir.h).
    ScratchDir scratch_{"replay"};
    fs::path dir_ = scratch_.path();
};

}  // namespace

// REQ-LOG-004: what comes out of replay is raw. Computed fields zeroed,
// status NOMINAL, validity NOT_JUDGED, the reserved bytes zero, everything
// the sensors said and the read_ok flags kept (TC-017 step 9); since format
// version 3 the sequence, the flags and the tail's raw fields are kept too,
// facts the source stated, while the voter's status, a verdict the replay's
// voter recomputes, and the reserved field are zeroed (TC-031 step 4).
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
    processed.sequence = 7;
    processed.flags = 0x0F;
    processed.gps_vertical_rate_mps = -2.5f;
    processed.gps_pressure_altitude_m = 300.0f;
    processed.gps_fix_quality = 2;
    processed.gps_satellites = 9;
    processed.voter_status = 0x0011;   // VOTED | NO_MAJORITY: a verdict the replay's voter recomputes
    processed.reserved_d = 1;
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
    EXPECT_EQ(raw.sequence, 7u);
    EXPECT_EQ(raw.flags, 0x0F);
    EXPECT_EQ(raw.gps_vertical_rate_mps, -2.5f);
    EXPECT_EQ(raw.gps_pressure_altitude_m, 300.0f);
    EXPECT_EQ(raw.gps_fix_quality, 2u);
    EXPECT_EQ(raw.gps_satellites, 9u);

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
    EXPECT_EQ(raw.voter_status, 0u);
    EXPECT_EQ(raw.reserved_d, 0u);
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
    h.frame_size = kVersion2FrameBytes;                // a version-1 frame is 104 bytes
    TelemetryFrame logged{};
    logged.timestamp_ms = 20;
    logged.pressure_hpa = 1012.5f;
    logged.accel_z = 9.81f;
    logged.baro_read_ok = true;
    logged.imu_read_ok = true;
    logged.sequence = 0xDEADBEEFu;                     // stack contents in a pre-9/9 log
    logged.pitch_validity = Validity::NORMAL;
    logged.flags = 0x55;
    {
        std::ofstream out(dir_ / "v1_000.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        const uint8_t tag = static_cast<uint8_t>(RecordType::FRAME);
        out.write(reinterpret_cast<const char*>(&tag), 1);
        out.write(reinterpret_cast<const char*>(&logged), kVersion2FrameBytes);
    }
    LogReplaySource source({dir_ / "v1_000.bin"});
    const TelemetryFrame raw = source.read_frame();
    EXPECT_EQ(raw.timestamp_ms, 20u);
    EXPECT_EQ(raw.pressure_hpa, 1012.5f);
    EXPECT_EQ(raw.accel_z, 9.81f);
    EXPECT_TRUE(raw.baro_read_ok);
    EXPECT_EQ(raw.pitch_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(raw.sequence, 0u);
    EXPECT_EQ(raw.flags, 0u);
    EXPECT_EQ(raw.gps_fix_quality, 0u);
    EXPECT_EQ(raw.reserved_d, 0u);
    EXPECT_EQ(source.frames_read(), 1u);
}

// REQ-LOG-005, TC-031 step 3 and step 13's unit half. A version-2 log
// replays as a prefix: its 104-byte frames come through with every old
// field as logged, the sequence and the flags as the bytes held (zero,
// a version-2 source's reserved fields) and the tail zero.
TEST_F(ReplayTest, Version2LogReplaysAsAPrefix) {
    LogFileHeader h = make_log_header();
    h.version = 2;
    h.frame_size = kVersion2FrameBytes;
    h.run_mode = 0;
    h.source_mode = 0;
    const uint8_t tag = static_cast<uint8_t>(RecordType::FRAME);
    {
        std::ofstream out(dir_ / "v2_000.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        for (int i = 0; i < 3; i++) {
            TelemetryFrame logged{};
            logged.timestamp_ms = 20u * static_cast<uint64_t>(i);
            logged.pressure_hpa = 1012.5f + static_cast<float>(i);
            logged.course_deg = 270.0f;
            logged.gps_read_ok = true;
            logged.fused_altitude_m = 5.0f;              // a processed field: stripped on replay
            logged.fused_altitude_validity = Validity::NORMAL;
            logged.gps_status = ChannelStatus::DEGRADED;
            logged.gps_fix_quality = 9;                  // beyond the bytes written
            out.write(reinterpret_cast<const char*>(&tag), 1);
            out.write(reinterpret_cast<const char*>(&logged), kVersion2FrameBytes);
        }
    }
    LogReplaySource source({dir_ / "v2_000.bin"});
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(source.has_next()) << "frame " << i;
        const TelemetryFrame raw = source.read_frame();
        EXPECT_EQ(raw.timestamp_ms, 20u * static_cast<uint64_t>(i));
        EXPECT_EQ(raw.pressure_hpa, 1012.5f + static_cast<float>(i));
        EXPECT_EQ(raw.course_deg, 270.0f);
        EXPECT_TRUE(raw.gps_read_ok);
        EXPECT_EQ(raw.fused_altitude_m, 0.0f);
        EXPECT_EQ(raw.fused_altitude_validity, Validity::NOT_JUDGED);
        EXPECT_EQ(raw.gps_status, ChannelStatus::NOMINAL);
        EXPECT_EQ(raw.sequence, 0u);
        EXPECT_EQ(raw.flags, 0u);
        EXPECT_EQ(raw.gps_fix_quality, 0u);
        EXPECT_EQ(raw.gps_satellites, 0u);
        EXPECT_EQ(raw.gps_vertical_rate_mps, 0.0f);
        EXPECT_EQ(raw.gps_pressure_altitude_m, 0.0f);
        EXPECT_EQ(raw.voter_status, 0u);
        EXPECT_EQ(raw.reserved_d, 0u);
    }
    EXPECT_FALSE(source.has_next());
    EXPECT_EQ(source.frames_read(), 3u);
}

// REQ-LOG-008, TC-031 step 5. A replay hands out the sequence its log
// recorded, the recording source's count, and numbers nothing itself: a
// log whose first frame is the source's fifth replays from 5.
TEST_F(ReplayTest, ReplayReproducesTheRecordedSequence) {
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        SimulatedSource source(20, 42);
        for (int i = 0; i < 5; i++) {
            source.read_frame();          // frames 0 to 4, never logged
        }
        for (int i = 0; i < 300; i++) {
            logger.log_frame(source.read_frame());
        }
    }
    LogReplaySource replay({dir_ / "t_000.bin"});
    for (uint32_t i = 5; i < 305; i++) {
        ASSERT_TRUE(replay.has_next()) << i;
        EXPECT_EQ(replay.read_frame().sequence, i);
    }
    EXPECT_FALSE(replay.has_next());
}

// REQ-PROC-008, REQ-LOG-003; TC-031 step 14's unit half. The flag is a fact
// the source stated: the strip keeps it, so a replay re-births its filters
// at the same frame and every processed frame matches the live one. The
// re-birth itself shows on the flagged frame: the fused altitude born from
// that frame's barometer with zero velocity (the GPS is between fixes).
TEST_F(ReplayTest, NewVehicleReplaysAsTheSameRebirth) {
    constexpr int kSwitch = 120;   // 2.4 s in, between the simulator's 1 Hz fixes
    std::vector<TelemetryFrame> live;
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        SimulatedSource source(20, 42);
        FaultDetector detector;
        TelemetryProcessor processor;
        for (int i = 0; i < 200; i++) {
            TelemetryFrame raw = source.read_frame();
            if (i == kSwitch) {
                set_flag(raw, FrameFlag::NEW_VEHICLE);
            }
            const TelemetryFrame out = processor.process(detector.check(raw));
            logger.log_frame(out);
            live.push_back(out);
        }
    }
    ASSERT_TRUE(has_flag(live[kSwitch], FrameFlag::NEW_VEHICLE));
    ASSERT_FALSE(live[kSwitch].gps_read_ok);
    EXPECT_EQ(live[kSwitch].fused_altitude_m, live[kSwitch].baro_altitude_m);
    EXPECT_EQ(live[kSwitch].vertical_speed_mps, 0.0f);

    LogReplaySource replay({dir_ / "t_000.bin"});
    FaultDetector detector;
    TelemetryProcessor processor;
    for (size_t i = 0; i < live.size(); i++) {
        ASSERT_TRUE(replay.has_next()) << i;
        const TelemetryFrame out = processor.process(detector.check(replay.read_frame()));
        EXPECT_EQ(std::memcmp(&out, &live[i], sizeof(TelemetryFrame)), 0) << "frame " << i;
    }
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
// Live: simulator with injected faults -> detector -> three lanes and the
// voter (REQ-RED-001) -> log. Replay: log -> LogReplaySource -> fresh
// detector -> fresh lanes and voter. Every processed frame and every fault
// event, the detector's and the voter's, must be bit-identical. A
// tolerance is not acceptable here; the requirement says identical.
TEST_F(ReplayTest, ReplayReproducesLiveSession) {
    constexpr int kFrames = 500;
    const uint64_t limit = sizeof(LogFileHeader) + 120 * (1 + sizeof(TelemetryFrame));  // force rotation

    std::vector<TelemetryFrame> live_frames;
    std::vector<FaultEvent> live_events;
    {
        FaultInjectingSource source(std::make_unique<SimulatedSource>(20, 42));
        FaultDetector detector;
        RedundantProcessor processor;
        BinaryLogger logger(dir_, "live", limit);

        for (int i = 0; i < kFrames; i++) {
            // Barometer dropout 100..140, stuck IMU 300..320: both fault
            // types, both recoveries, so status changes are in the log.
            source.fail_reads(Channel::BARO, i >= 100 && i < 140);
            source.hold_values(Channel::IMU, i >= 300 && i < 320);

            const TelemetryFrame processed = processor.process(detector.check(source.read_frame()));
            logger.log_frame(processed);
            live_frames.push_back(processed);
            EXPECT_EQ(processed.voter_status, 0x0001) << "frame " << i;
            for (const FaultEvent& e : detector.take_events()) {
                logger.log_event(e);
                live_events.push_back(e);
            }
            for (const FaultEvent& e : processor.take_events()) {
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
        RedundantProcessor processor;
        // REQ-LOG-006 (TC-019 step 5): the loop asks the source, not a
        // count, and stops exactly at the log's end.
        while (source.has_next()) {
            replay_frames.push_back(processor.process(detector.check(source.read_frame())));
            for (const FaultEvent& e : detector.take_events()) {
                replay_events.push_back(e);
            }
            for (const FaultEvent& e : processor.take_events()) {
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
    h.frame_size = kVersion2FrameBytes;                // a version-1 frame is 104 bytes
    {
        std::ofstream out(dir_ / "v1_000.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        SimulatedSource source(20, 42);
        for (int i = 0; i < 5; i++) {
            const TelemetryFrame raw = source.read_frame();   // a version-1 log's raw bytes are the first 104 of these
            const uint8_t tag = static_cast<uint8_t>(RecordType::FRAME);
            out.write(reinterpret_cast<const char*>(&tag), 1);
            out.write(reinterpret_cast<const char*>(&raw), kVersion2FrameBytes);
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

// TC-034 step 5, REQ-LOG-005 and REQ-RED-002: logs recorded before the
// voter, at format version 3 (the single lane's frames, voter status 0)
// and at versions 1 and 2, replay through the three lanes with the voter's
// status recomputed: every byte of a version-3 frame equal but the status,
// which is VOTED; the version-1 and version-2 frames judged and voted, their
// raw fields the log's.
TEST_F(ReplayTest, PreVoterLogsReplayVoted) {
    std::vector<TelemetryFrame> recorded;
    {
        FaultInjectingSource source(std::make_unique<SimulatedSource>(20, 42));
        FaultDetector detector;
        TelemetryProcessor single;
        BinaryLogger logger(dir_, "prevoter", 1 << 20);
        for (int i = 0; i < 200; i++) {
            source.fail_reads(Channel::BARO, i >= 50 && i < 90);
            const TelemetryFrame processed = single.process(detector.check(source.read_frame()));
            ASSERT_EQ(processed.voter_status, 0u);
            logger.log_frame(processed);
            recorded.push_back(processed);
        }
    }
    {
        LogReplaySource source(list_log_files(dir_, "prevoter"));
        FaultDetector detector;
        RedundantProcessor processor;
        size_t i = 0;
        while (source.has_next()) {
            TelemetryFrame voted = processor.process(detector.check(source.read_frame()));
            ASSERT_LT(i, recorded.size());
            EXPECT_EQ(voted.voter_status, 0x0001) << "frame " << i;
            voted.voter_status = 0;
            EXPECT_EQ(std::memcmp(&voted, &recorded[i], sizeof voted), 0) << "frame " << i;
            EXPECT_TRUE(processor.take_events().empty()) << "frame " << i;
            i++;
        }
        EXPECT_EQ(i, recorded.size());
    }

    for (const uint8_t version : {uint8_t{1}, uint8_t{2}}) {
        LogFileHeader h = make_log_header();               // both modes UNKNOWN, zero: what older headers held
        h.version = version;
        h.frame_size = kVersion2FrameBytes;                // both older frames are 104 bytes
        const std::filesystem::path file = dir_ / ("older_v" + std::to_string(version) + "_000.bin");
        std::vector<TelemetryFrame> written;
        {
            std::ofstream out(file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(&h), sizeof(h));
            SimulatedSource source(20, 42);
            for (int i = 0; i < 5; i++) {
                TelemetryFrame raw = source.read_frame();
                raw.sequence = 0;                          // an older source's reserved bytes
                raw.flags = 0;
                const uint8_t tag = static_cast<uint8_t>(RecordType::FRAME);
                out.write(reinterpret_cast<const char*>(&tag), 1);
                out.write(reinterpret_cast<const char*>(&raw), kVersion2FrameBytes);
                written.push_back(raw);
            }
        }
        LogReplaySource older({file});
        FaultDetector detector;
        RedundantProcessor processor;
        for (size_t i = 0; i < written.size(); i++) {
            const TelemetryFrame out = processor.process(detector.check(older.read_frame()));
            const TelemetryFrame& in = written[i];
            SCOPED_TRACE("version " + std::to_string(version) + ", frame " + std::to_string(i));
            EXPECT_EQ(out.voter_status, 0x0001);
            EXPECT_EQ(out.fused_altitude_validity, Validity::NORMAL);
            EXPECT_EQ(out.timestamp_ms, in.timestamp_ms);
            EXPECT_EQ(out.pressure_hpa, in.pressure_hpa);
            EXPECT_EQ(out.temperature_c, in.temperature_c);
            EXPECT_EQ(out.accel_x, in.accel_x);
            EXPECT_EQ(out.accel_y, in.accel_y);
            EXPECT_EQ(out.accel_z, in.accel_z);
            EXPECT_EQ(out.gyro_x, in.gyro_x);
            EXPECT_EQ(out.gyro_y, in.gyro_y);
            EXPECT_EQ(out.gyro_z, in.gyro_z);
            EXPECT_EQ(out.latitude, in.latitude);
            EXPECT_EQ(out.longitude, in.longitude);
            EXPECT_EQ(out.gps_altitude_m, in.gps_altitude_m);
            EXPECT_EQ(out.ground_speed_mps, in.ground_speed_mps);
            EXPECT_EQ(out.course_deg, in.course_deg);
            EXPECT_EQ(out.baro_read_ok, in.baro_read_ok);
            EXPECT_EQ(out.imu_read_ok, in.imu_read_ok);
            EXPECT_EQ(out.gps_read_ok, in.gps_read_ok);
        }
    }
}

// TC-034 step 5, REQ-LOG-003 and REQ-RED-005: a verdict is recomputed, never
// copied. A live session whose lane 2 a test altered once (its input, so
// the lane diverges and is excluded) logs the exclusion's bits and events;
// its replay, with no alteration, votes every frame with all three lanes
// and logs no voter event, while every other byte of every frame is equal:
// the output always came from the agreeing lanes. The difference is the
// diagnosis, a lane fault the raw frames do not explain.
TEST_F(ReplayTest, ReplayRecomputesTheVoterVerdict) {
    std::vector<TelemetryFrame> live;
    std::vector<FaultEvent> live_voter_events;
    {
        SimulatedSource source(20, 42);
        FaultDetector detector;
        ScriptedLaneFaults hook;
        hook.on_input = [](int cycle, int lane, TelemetryFrame& in) {
            if (cycle == 40 && lane == 2) {
                in.pressure_hpa += 5.0f;
            }
        };
        RedundantProcessor processor(ProcessorConfig{}, TelemetryProcessor::kDefaultImuTimeoutMs,
                                     Voter::kLanePersistence, &hook);
        BinaryLogger logger(dir_, "hooked", 1 << 20);
        for (int i = 0; i < 100; i++) {
            const TelemetryFrame processed = processor.process(detector.check(source.read_frame()));
            logger.log_frame(processed);
            live.push_back(processed);
            for (const FaultEvent& e : processor.take_events()) {
                logger.log_event(e);
                live_voter_events.push_back(e);
            }
        }
    }
    ASSERT_EQ(live_voter_events.size(), 2u);
    EXPECT_EQ(live.back().voter_status, 0x0005);

    LogReplaySource source(list_log_files(dir_, "hooked"));
    FaultDetector detector;
    RedundantProcessor processor;
    size_t i = 0;
    while (source.has_next()) {
        TelemetryFrame replayed = processor.process(detector.check(source.read_frame()));
        EXPECT_EQ(replayed.voter_status, 0x0001) << "frame " << i;
        EXPECT_TRUE(processor.take_events().empty()) << "frame " << i;
        TelemetryFrame recorded = live[i];
        replayed.voter_status = 0;
        recorded.voter_status = 0;
        EXPECT_EQ(std::memcmp(&replayed, &recorded, sizeof replayed), 0) << "frame " << i;
        i++;
    }
    EXPECT_EQ(i, live.size());
}
