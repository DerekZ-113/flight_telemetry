// Tests for src/processing/redundant_processor.cpp (REQ-RED-001,
// REQ-RED-005): three identical lanes behind one detector and a voter.
// Test card TC-034 steps 1 to 4 and TC-032 step 9. The single-lane
// pipeline, one detector and one TelemetryProcessor, is the oracle: the
// voted frame must equal its output in every byte but the voter's status.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include "drivers/fault_injecting_source.h"
#include "drivers/motion_profile.h"
#include "drivers/simulated_source.h"
#include "processing/fault_detector.h"
#include "processing/processor.h"
#include "processing/redundant_processor.h"
#include "processing/voter.h"
#include "scripted_lane_faults.h"
#include "telemetry_frame.h"

namespace {

constexpr uint64_t kDtMs = 20;
constexpr std::size_t kVoterStatusOffset = offsetof(TelemetryFrame, voter_status);

// The voted frame against the single lane's: every byte equal but the two
// of the voter's status.
bool equal_but_voter_status(const TelemetryFrame& voted, const TelemetryFrame& single) {
    const auto* a = reinterpret_cast<const unsigned char*>(&voted);
    const auto* b = reinterpret_cast<const unsigned char*>(&single);
    return std::memcmp(a, b, kVoterStatusOffset) == 0 &&
           std::memcmp(a + kVoterStatusOffset + 2, b + kVoterStatusOffset + 2,
                       sizeof(TelemetryFrame) - kVoterStatusOffset - 2) == 0;
}

// The seed-42 simulator with a fault in every channel at some point: a
// barometer dropout, a stuck IMU, a GPS outage past its timeout, and an
// out-of-range pressure, each long enough to degrade and recover, so the
// detector's statuses and the processor's validity take every value a
// session can show.
class FaultySession {
public:
    explicit FaultySession(std::optional<MotionProfile> profile = std::nullopt)
        : source_(std::make_unique<SimulatedSource>(kDtMs, 42, 1000, SimulationNoise{}, std::move(profile))) {}

    TelemetryFrame next_checked() {
        source_.fail_reads(Channel::BARO, frame_ >= 100 && frame_ < 140);
        source_.hold_values(Channel::IMU, frame_ >= 300 && frame_ < 320);
        source_.fail_reads(Channel::GPS, frame_ >= 5000 && frame_ < 5300);
        if (frame_ == 7000) {
            source_.override_pressure(200.0f);
        }
        if (frame_ == 7010) {
            source_.clear_pressure_override();
        }
        frame_++;
        return detector_.check(source_.read_frame());
    }

    std::vector<FaultEvent> take_detector_events() { return detector_.take_events(); }

private:
    FaultInjectingSource source_;
    FaultDetector detector_;
    int frame_ = 0;
};

}  // namespace

// TC-034 step 1: over 10,000 frames with a fault in every channel, the voted
// frame equals the single lane's in every byte but the voter's status,
// which is VOTED on every frame, and the voter adds no event.
TEST(RedundantProcessorTest, MatchesOneLaneExceptVoterStatus) {
    FaultySession session;
    RedundantProcessor redundant;
    TelemetryProcessor single;
    std::size_t detector_events = 0;
    for (int i = 0; i < 10000; i++) {
        const TelemetryFrame checked = session.next_checked();
        const TelemetryFrame voted = redundant.process(checked);
        const TelemetryFrame expected = single.process(checked);
        ASSERT_TRUE(equal_but_voter_status(voted, expected)) << "frame " << i;
        ASSERT_EQ(voted.voter_status, 0x0001) << "frame " << i;
        detector_events += session.take_detector_events().size();
        ASSERT_TRUE(redundant.take_events().empty()) << "frame " << i;
    }
    EXPECT_GE(detector_events, 8u) << "every injected fault must have degraded and recovered its channel";
}

// The byte-identity argument's premise: a processor writes the five
// computed values and their five validities and nothing else, so copying
// those ten fields from a lane onto the checked frame is the lane's output.
// If the processor ever writes another field, this fails before the seed-42
// log would.
TEST(RedundantProcessorTest, ProcessorWritesOnlyTheFieldsTheVoterCopies) {
    struct Range {
        std::size_t begin;
        std::size_t end;
    };
    const std::vector<Range> written{
        {offsetof(TelemetryFrame, baro_altitude_m), offsetof(TelemetryFrame, baro_altitude_m) + 4},
        {offsetof(TelemetryFrame, pitch_deg), offsetof(TelemetryFrame, roll_deg) + 4},
        {offsetof(TelemetryFrame, fused_altitude_m), offsetof(TelemetryFrame, vertical_speed_mps) + 4},
        {offsetof(TelemetryFrame, baro_altitude_validity), offsetof(TelemetryFrame, vertical_speed_validity) + 1},
    };
    FaultySession session;
    TelemetryProcessor processor;
    for (int i = 0; i < 8000; i++) {
        const TelemetryFrame checked = session.next_checked();
        const TelemetryFrame out = processor.process(checked);
        const auto* a = reinterpret_cast<const unsigned char*>(&checked);
        const auto* b = reinterpret_cast<const unsigned char*>(&out);
        for (std::size_t byte = 0; byte < sizeof(TelemetryFrame); byte++) {
            bool inside = false;
            for (const Range& range : written) {
                inside = inside || (byte >= range.begin && byte < range.end);
            }
            if (!inside) {
                ASSERT_EQ(a[byte], b[byte]) << "frame " << i << " byte " << byte;
            }
        }
    }
}

// TC-034 step 2: two redundant processors over the same 10,000 frames agree
// frame for frame and event for event, and within each the three lanes are
// bit-identical on every cycle: no disagreement, no voter event.
TEST(RedundantProcessorTest, DeterministicOverTenThousandFrames) {
    FaultySession first_session;
    FaultySession second_session;
    ScriptedLaneFaults first_watch;
    ScriptedLaneFaults second_watch;
    RedundantProcessor first(ProcessorConfig{}, TelemetryProcessor::kDefaultImuTimeoutMs, Voter::kLanePersistence,
                             &first_watch);
    RedundantProcessor second(ProcessorConfig{}, TelemetryProcessor::kDefaultImuTimeoutMs, Voter::kLanePersistence,
                              &second_watch);
    for (int i = 0; i < 10000; i++) {
        const TelemetryFrame a = first.process(first_session.next_checked());
        const TelemetryFrame b = second.process(second_session.next_checked());
        ASSERT_EQ(std::memcmp(&a, &b, sizeof a), 0) << "frame " << i;
        ASSERT_TRUE(first.take_events().empty()) << "frame " << i;
        ASSERT_TRUE(second.take_events().empty()) << "frame " << i;
    }
    EXPECT_EQ(first_watch.cycles_lanes_differed, 0);
    EXPECT_EQ(second_watch.cycles_lanes_differed, 0);
    EXPECT_EQ(first_watch.cycle(), 9999);
}

// TC-034 step 3: the demo's scripted flight, 7,500 frames of climbs, turns
// and descents: the lanes never differ and the voted frame is the single
// lane's but for the status.
TEST(RedundantProcessorTest, IdenticalLanesAgreeOnEveryFrame) {
    SimulatedSource source(kDtMs, 42, 1000, SimulationNoise{}, MotionProfile::named("demo"));
    FaultDetector detector;
    ScriptedLaneFaults watch;
    RedundantProcessor redundant(ProcessorConfig{}, TelemetryProcessor::kDefaultImuTimeoutMs,
                                 Voter::kLanePersistence, &watch);
    TelemetryProcessor single;
    for (int i = 0; i < 7500; i++) {
        const TelemetryFrame checked = detector.check(source.read_frame());
        const TelemetryFrame voted = redundant.process(checked);
        ASSERT_TRUE(equal_but_voter_status(voted, single.process(checked))) << "frame " << i;
        ASSERT_EQ(voted.voter_status, 0x0001) << "frame " << i;
    }
    EXPECT_EQ(watch.cycles_lanes_differed, 0);
    EXPECT_TRUE(redundant.take_events().empty());
}

// TC-034 step 4: a bus failure (barometer and IMU silent together) is the
// detector's two events, once each; the lanes see the same statuses and
// the voter raises nothing.
TEST(RedundantProcessorTest, SensorEventsStayTheDetectorsOnce) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42, kDtMs));
    FaultDetector detector;
    RedundantProcessor redundant;
    std::vector<FaultEvent> sensor_events;
    for (int i = 0; i < 100; i++) {
        source.fail_reads(Channel::BARO, i >= 20);
        source.fail_reads(Channel::IMU, i >= 20);
        redundant.process(detector.check(source.read_frame()));
        for (const FaultEvent& e : detector.take_events()) {
            sensor_events.push_back(e);
        }
        EXPECT_TRUE(redundant.take_events().empty()) << "frame " << i;
    }
    ASSERT_EQ(sensor_events.size(), 2u);
    EXPECT_EQ(sensor_events[0].channel, Channel::BARO);
    EXPECT_EQ(sensor_events[1].channel, Channel::IMU);
    EXPECT_EQ(sensor_events[0].type, FaultType::COMM_TIMEOUT);
}

// TC-032 step 9, an upset: lane 2's output altered on one cycle leaves its
// state intact. It is outvoted on that cycle, agrees again on the next, and
// the voted frame is the single lane's throughout.
TEST(RedundantProcessorTest, PerturbedLaneOutputIsIsolated) {
    FaultySession session;
    ScriptedLaneFaults hook;
    hook.on_output = [](int cycle, int lane, TelemetryFrame& out) {
        if (cycle == 50 && lane == 2) {
            out.fused_altitude_m += 1.0f;
        }
    };
    RedundantProcessor redundant(ProcessorConfig{}, TelemetryProcessor::kDefaultImuTimeoutMs, Voter::kLanePersistence,
                                 &hook);
    TelemetryProcessor single;
    std::vector<FaultEvent> events;
    for (int i = 0; i < 200; i++) {
        const TelemetryFrame checked = session.next_checked();
        const TelemetryFrame voted = redundant.process(checked);
        EXPECT_TRUE(equal_but_voter_status(voted, single.process(checked))) << "frame " << i;
        EXPECT_EQ(voted.voter_status, 0x0001) << "frame " << i;
        for (const FaultEvent& e : redundant.take_events()) {
            events.push_back(e);
        }
    }
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, FaultType::LANE_DISAGREEMENT);
    EXPECT_EQ(events[0].value, 2.0f);
    EXPECT_EQ(events[0].timestamp_ms, 50 * kDtMs);
    // The upset touched the output alone: cycle 50 is the only one on which
    // the three lane outputs were not bit-identical, so lane 2 agreed again
    // from cycle 51.
    EXPECT_EQ(hook.cycles_lanes_differed, 1);
}

// TC-032 step 9, damage: lane 2's input altered once (a pressure 5 hPa off)
// runs through its Kalman filter, so its fused altitude differs from then
// on. Lane 2 is excluded on the third cycle; lanes 1 and 3 and the voted
// frame stay the single lane's on every frame.
TEST(RedundantProcessorTest, PerturbedLaneInputDivergesOnlyThatLane) {
    FaultySession session;
    ScriptedLaneFaults hook;
    hook.on_input = [](int cycle, int lane, TelemetryFrame& in) {
        if (cycle == 50 && lane == 2) {
            in.pressure_hpa += 5.0f;
        }
    };
    RedundantProcessor redundant(ProcessorConfig{}, TelemetryProcessor::kDefaultImuTimeoutMs, Voter::kLanePersistence,
                                 &hook);
    TelemetryProcessor single;
    std::vector<FaultEvent> events;
    for (int i = 0; i < 200; i++) {
        const TelemetryFrame checked = session.next_checked();
        const TelemetryFrame expected = single.process(checked);
        const TelemetryFrame voted = redundant.process(checked);
        EXPECT_TRUE(equal_but_voter_status(voted, expected)) << "frame " << i;
        EXPECT_EQ(std::memcmp(&hook.outputs()[0], &expected, sizeof expected), 0) << "lane 1, frame " << i;
        EXPECT_EQ(std::memcmp(&hook.outputs()[2], &expected, sizeof expected), 0) << "lane 3, frame " << i;
        EXPECT_EQ(voted.voter_status, i <= 52 ? 0x0001 : 0x0005) << "frame " << i;
        for (const FaultEvent& e : redundant.take_events()) {
            events.push_back(e);
        }
    }
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].type, FaultType::LANE_DISAGREEMENT);
    EXPECT_EQ(events[0].timestamp_ms, 50 * kDtMs);
    EXPECT_EQ(events[1].type, FaultType::LANE_FAULTED);
    EXPECT_EQ(events[1].value, 2.0f);
    EXPECT_EQ(events[1].timestamp_ms, 52 * kDtMs);
}

// REQ-RED-006 through the interface: lane 3 given the previous cycle's
// timestamp comes out with it, a stale output, the harness's model of a
// lane that did not finish in time: its onset is LANE_OVERRUN.
TEST(RedundantProcessorTest, StaleInputIsAnOverrun) {
    FaultySession session;
    ScriptedLaneFaults hook;
    hook.on_input = [](int cycle, int lane, TelemetryFrame& in) {
        if (cycle == 30 && lane == 3) {
            in.timestamp_ms -= kDtMs;
            in.sequence -= 1;
        }
    };
    RedundantProcessor redundant(ProcessorConfig{}, TelemetryProcessor::kDefaultImuTimeoutMs, Voter::kLanePersistence,
                                 &hook);
    std::vector<FaultEvent> events;
    for (int i = 0; i < 40; i++) {
        redundant.process(session.next_checked());
        for (const FaultEvent& e : redundant.take_events()) {
            events.push_back(e);
        }
    }
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0].type, FaultType::LANE_OVERRUN);
    EXPECT_EQ(events[0].value, 3.0f);
    EXPECT_EQ(events[0].timestamp_ms, 30 * kDtMs);
}

// REQ-RED-005: the interface is called for every lane on every cycle, the
// input before the output, lanes in order 1, 2, 3.
TEST(RedundantProcessorTest, HookSeesEveryLaneEveryCycle) {
    FaultySession session;
    ScriptedLaneFaults hook;
    std::vector<int> order;
    hook.on_input = [&order](int, int lane, TelemetryFrame&) { order.push_back(lane); };
    hook.on_output = [&order](int, int lane, TelemetryFrame&) { order.push_back(-lane); };
    RedundantProcessor redundant(ProcessorConfig{}, TelemetryProcessor::kDefaultImuTimeoutMs, Voter::kLanePersistence,
                                 &hook);
    for (int i = 0; i < 10; i++) {
        redundant.process(session.next_checked());
    }
    EXPECT_EQ(hook.input_calls, 30);
    EXPECT_EQ(hook.output_calls, 30);
    ASSERT_EQ(order.size(), 60u);
    const std::vector<int> one_cycle{1, -1, 2, -2, 3, -3};
    for (std::size_t i = 0; i < order.size(); i++) {
        EXPECT_EQ(order[i], one_cycle[i % 6]) << "call " << i;
    }
}

// TC-034 step 3, REQ-PROC-008: on a frame flagged NEW_VEHICLE every lane is
// reborn alike, so the lanes still agree and the voted frame is the single
// lane's: the fused altitude restarts at the barometric with zero vertical
// speed (the 1 Hz GPS is not fresh on frame 120, 2,400 ms).
TEST(RedundantProcessorTest, NewVehicleRestartsEveryLane) {
    SimulatedSource source(kDtMs, 42);
    FaultDetector detector;
    ScriptedLaneFaults watch;
    RedundantProcessor redundant(ProcessorConfig{}, TelemetryProcessor::kDefaultImuTimeoutMs,
                                 Voter::kLanePersistence, &watch);
    TelemetryProcessor single;
    for (int i = 0; i < 200; i++) {
        TelemetryFrame raw = source.read_frame();
        if (i == 120) {
            set_flag(raw, FrameFlag::NEW_VEHICLE);
        }
        const TelemetryFrame checked = detector.check(raw);
        const TelemetryFrame voted = redundant.process(checked);
        ASSERT_TRUE(equal_but_voter_status(voted, single.process(checked))) << "frame " << i;
        if (i == 120) {
            EXPECT_EQ(voted.fused_altitude_m, voted.baro_altitude_m);
            EXPECT_EQ(voted.vertical_speed_mps, 0.0f);
        }
    }
    EXPECT_EQ(watch.cycles_lanes_differed, 0);
    EXPECT_TRUE(redundant.take_events().empty());
}

// The trap of a default-built lane array: every lane must run the tuning it
// was given. Non-default constants change the single lane's output, and
// every lane's output equals that single lane's on every frame.
TEST(RedundantProcessorTest, TuningReachesEveryLane) {
    ProcessorConfig tuning;
    tuning.baro_measurement_variance_m2 = 0.5f;
    tuning.accel_noise_variance = 4.0f;
    tuning.complementary_alpha = 0.9f;
    constexpr uint64_t kImuTimeoutMs = 300;
    SimulatedSource source(kDtMs, 42, kDtMs);
    FaultDetector detector;
    ScriptedLaneFaults watch;
    RedundantProcessor redundant(tuning, kImuTimeoutMs, Voter::kLanePersistence, &watch);
    TelemetryProcessor tuned(tuning, kImuTimeoutMs);
    TelemetryProcessor baseline;
    bool tuning_mattered = false;
    for (int i = 0; i < 100; i++) {
        const TelemetryFrame checked = detector.check(source.read_frame());
        const TelemetryFrame voted = redundant.process(checked);
        const TelemetryFrame expected = tuned.process(checked);
        const TelemetryFrame default_out = baseline.process(checked);
        for (std::size_t lane = 0; lane < kLaneCount; lane++) {
            ASSERT_EQ(std::memcmp(&watch.outputs()[lane], &expected, sizeof expected), 0)
                << "lane " << lane + 1 << " frame " << i;
        }
        ASSERT_TRUE(equal_but_voter_status(voted, expected)) << "frame " << i;
        tuning_mattered = tuning_mattered || std::memcmp(&expected, &default_out, sizeof expected) != 0;
    }
    EXPECT_TRUE(tuning_mattered) << "the non-default tuning must change the output, or this test proves nothing";
}
