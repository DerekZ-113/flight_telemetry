// Tests for src/processing/lane_fault_injector.cpp (REQ-RED-005,
// REQ-RED-007, REQ-TEST-005): the one LaneFaultHook the fault-injection
// campaign and the demonstration option share. Test card TC-037 steps 1
// to 3; the campaign's lane cells (TC-035) rest on these kinds.

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "drivers/fault_injecting_source.h"
#include "drivers/simulated_source.h"
#include "processing/fault_detector.h"
#include "processing/lane_fault_injector.h"
#include "processing/redundant_processor.h"
#include "processing/voter.h"
#include "telemetry_frame.h"

namespace {

constexpr uint64_t kDtMs = 20;

uint32_t bits(float value) {
    uint32_t out = 0;
    std::memcpy(&out, &value, sizeof out);
    return out;
}

// A plausible processed frame at a time: the computed values a lane would
// write, distinct per field so a change to the wrong one shows.
TelemetryFrame lane_output(uint64_t ts) {
    TelemetryFrame f{};
    f.timestamp_ms = ts;
    f.sequence = static_cast<uint32_t>(ts / kDtMs);
    f.pressure_hpa = 1013.0f;
    f.accel_z = 9.81f;
    f.baro_altitude_m = 2.1f;
    f.pitch_deg = 0.3f;
    f.roll_deg = -0.2f;
    f.fused_altitude_m = 2.05f;
    f.vertical_speed_mps = 0.01f;
    return f;
}

// One cycle as RedundantProcessor runs it: each lane's input hook, a lane
// that copies its input's identity onto a fixed output, its output hook.
std::array<TelemetryFrame, kLaneCount> cycle(LaneFaultInjector& injector, uint64_t ts,
                                             std::array<TelemetryFrame, kLaneCount>* inputs = nullptr) {
    std::array<TelemetryFrame, kLaneCount> out{};
    for (int lane = 1; lane <= static_cast<int>(kLaneCount); lane++) {
        TelemetryFrame input = lane_output(ts);
        injector.perturb_input(lane, input);
        if (inputs != nullptr) {
            (*inputs)[static_cast<std::size_t>(lane - 1)] = input;
        }
        TelemetryFrame output = lane_output(ts);
        injector.perturb_output(lane, output);
        out[static_cast<std::size_t>(lane - 1)] = output;
    }
    return out;
}

LaneFault fault(int lane, LaneFaultKind kind, uint64_t onset, uint64_t end, LaneField field, uint8_t bit = 0) {
    LaneFault f;
    f.lane = lane;
    f.kind = kind;
    f.onset_ms = onset;
    f.end_ms = end;
    f.field = field;
    f.bit = bit;
    return f;
}

}  // namespace

// An output bit flip inverts exactly the named bit of the named value of
// the named lane: one single-event upset, nothing else touched.
TEST(LaneFaultInjectorTest, OutputBitFlipInvertsOneBitOfOneValueOfOneLane) {
    LaneFaultInjector injector({fault(2, LaneFaultKind::OUTPUT_BIT_FLIP, 0, LaneFault::kPersistent,
                                      LaneField::FUSED_ALTITUDE, 22)});
    const auto out = cycle(injector, 1000);
    const TelemetryFrame clean = lane_output(1000);
    EXPECT_EQ(bits(out[1].fused_altitude_m) ^ bits(clean.fused_altitude_m), 1u << 22);
    EXPECT_EQ(out[1].baro_altitude_m, clean.baro_altitude_m);
    EXPECT_EQ(out[1].vertical_speed_mps, clean.vertical_speed_mps);
    EXPECT_EQ(std::memcmp(&out[0], &clean, sizeof clean), 0);
    EXPECT_EQ(std::memcmp(&out[2], &clean, sizeof clean), 0);
}

TEST(LaneFaultInjectorTest, OutputNanReplacesTheValue) {
    LaneFaultInjector injector({fault(3, LaneFaultKind::OUTPUT_NAN, 0, LaneFault::kPersistent, LaneField::PITCH)});
    const auto out = cycle(injector, 40);
    EXPECT_TRUE(std::isnan(out[2].pitch_deg));
    EXPECT_EQ(out[2].roll_deg, lane_output(40).roll_deg);
    EXPECT_FALSE(std::isnan(out[0].pitch_deg));
}

// A stale output is the lane's own previous real output: the previous
// cycle's timestamp and sequence, which REQ-RED-006 reads as an overrun.
// The lane's real output of this cycle is kept, so the next stale cycle
// hands back this one: a computer always one cycle late.
TEST(LaneFaultInjectorTest, OutputStaleHandsTheVoterThePreviousRealOutput) {
    LaneFaultInjector injector({fault(1, LaneFaultKind::OUTPUT_STALE, 40, LaneFault::kPersistent, LaneField::PITCH)});
    cycle(injector, 20);
    const auto stale = cycle(injector, 40);
    EXPECT_EQ(stale[0].timestamp_ms, 20u);
    EXPECT_EQ(stale[0].sequence, 1u);
    EXPECT_EQ(injector.real_outputs()[0].timestamp_ms, 40u);
    EXPECT_EQ(stale[1].timestamp_ms, 40u);
    EXPECT_EQ(cycle(injector, 60)[0].timestamp_ms, 40u);
}

// A stale output from the very first cycle has no previous output to hand
// over, so that cycle passes the lane's own; from the next it is a cycle
// late. Found missing by FTS-VR-001 Entry 21's coverage preview.
TEST(LaneFaultInjectorTest, OutputStaleOnTheFirstCyclePassesTheLanesOwn) {
    LaneFaultInjector injector({fault(3, LaneFaultKind::OUTPUT_STALE, 0, LaneFault::kPersistent, LaneField::PITCH)});
    const auto first = cycle(injector, 0);
    EXPECT_EQ(first[2].timestamp_ms, 0u);
    EXPECT_EQ(std::memcmp(&first[2], &injector.real_outputs()[2], sizeof first[2]), 0);
    EXPECT_EQ(cycle(injector, 20)[2].timestamp_ms, 0u);
}

// An input flip alters only that lane's input, before the lane runs; its
// output hook leaves the output as the lane made it.
TEST(LaneFaultInjectorTest, InputBitFlipAltersOnlyThatLanesInput) {
    LaneFaultInjector injector({fault(2, LaneFaultKind::INPUT_BIT_FLIP, 0, LaneFault::kPersistent,
                                      LaneField::PRESSURE, 0)});
    std::array<TelemetryFrame, kLaneCount> inputs{};
    const auto out = cycle(injector, 20, &inputs);
    const TelemetryFrame clean = lane_output(20);
    EXPECT_EQ(bits(inputs[1].pressure_hpa) ^ bits(clean.pressure_hpa), 1u);
    EXPECT_EQ(inputs[0].pressure_hpa, clean.pressure_hpa);
    EXPECT_EQ(inputs[2].pressure_hpa, clean.pressure_hpa);
    EXPECT_EQ(std::memcmp(&out[1], &clean, sizeof clean), 0);
}

// A fault alters the frames from the first at or after its onset up to the
// frame before its end: one cycle when the end is one frame on.
TEST(LaneFaultInjectorTest, FaultIsActiveFromOnsetToTheFrameBeforeItsEnd) {
    LaneFaultInjector injector({fault(1, LaneFaultKind::OUTPUT_NAN, 30, 70, LaneField::ROLL)});
    EXPECT_FALSE(std::isnan(cycle(injector, 20)[0].roll_deg));
    EXPECT_TRUE(std::isnan(cycle(injector, 40)[0].roll_deg));
    EXPECT_TRUE(std::isnan(cycle(injector, 60)[0].roll_deg));
    EXPECT_FALSE(std::isnan(cycle(injector, 80)[0].roll_deg));
}

// The real outputs are the lanes' own, before any alteration: the
// campaign's healthy reference and the stale fault's source.
TEST(LaneFaultInjectorTest, RealOutputsAreTheLanesOwn) {
    LaneFaultInjector injector({fault(2, LaneFaultKind::OUTPUT_NAN, 0, LaneFault::kPersistent, LaneField::ROLL)});
    cycle(injector, 20);
    const TelemetryFrame clean = lane_output(20);
    for (const TelemetryFrame& real : injector.real_outputs()) {
        EXPECT_EQ(std::memcmp(&real, &clean, sizeof clean), 0);
    }
}

// REQ-RED-007: a recorded fault logs one LANE_FAULT_INJECTED, at its first
// altered frame, on channel VOTER with the lane as its value and its
// reserved bytes zero; a fault that is not recorded logs nothing.
TEST(LaneFaultInjectorTest, RecordedFaultLogsOneEventAtItsFirstAlteredFrame) {
    LaneFault recorded = fault(3, LaneFaultKind::OUTPUT_BIT_FLIP, 40, LaneFault::kPersistent,
                               LaneField::FUSED_ALTITUDE, 22);
    recorded.recorded = true;
    LaneFaultInjector injector({recorded, fault(1, LaneFaultKind::OUTPUT_NAN, 0, LaneFault::kPersistent,
                                                LaneField::PITCH)});
    cycle(injector, 20);
    EXPECT_TRUE(injector.take_events().empty());
    cycle(injector, 40);
    const std::vector<FaultEvent> events = injector.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].timestamp_ms, 40u);
    EXPECT_EQ(events[0].channel, Channel::VOTER);
    EXPECT_EQ(events[0].type, FaultType::LANE_FAULT_INJECTED);
    EXPECT_EQ(events[0].reserved, 0u);
    EXPECT_EQ(events[0].value, 3.0f);
    cycle(injector, 60);
    EXPECT_TRUE(injector.take_events().empty());
}

// The demonstration's fault: bit 22 of the fused altitude, persistent,
// recorded (REQ-RED-007).
TEST(LaneFaultInjectorTest, DemoFaultIsBit22OfTheFusedAltitudeOnEveryCycle) {
    const LaneFault demo = demo_lane_fault(2, 60000);
    EXPECT_EQ(demo.lane, 2);
    EXPECT_EQ(demo.kind, LaneFaultKind::OUTPUT_BIT_FLIP);
    EXPECT_EQ(demo.onset_ms, 60000u);
    EXPECT_EQ(demo.end_ms, LaneFault::kPersistent);
    EXPECT_EQ(demo.field, LaneField::FUSED_ALTITUDE);
    EXPECT_EQ(demo.bit, kDemoBit);
    EXPECT_EQ(kDemoBit, 22);
    EXPECT_TRUE(demo.recorded);
}

// TC-037 step 1: the option's value is <lane>@<seconds>, the lane 1 to 3
// and the seconds a finite, non-negative time from the first frame; every
// other text is refused with one message naming the option and its form.
// "inf" and "nan" fail the shape check before the number is read; 1e999
// overflows to infinity and is refused by the finiteness check.
TEST(LaneFaultInjectorTest, ParsesLaneAtSecondsAndRefusesEverythingElse) {
    std::string error;
    const auto two = parse_lane_fault("2@60", error);
    ASSERT_TRUE(two.has_value()) << error;
    EXPECT_EQ(two->lane, 2);
    EXPECT_EQ(two->onset_ms, 60000u);
    EXPECT_TRUE(two->recorded);
    const auto half = parse_lane_fault("1@0.5", error);
    ASSERT_TRUE(half.has_value());
    EXPECT_EQ(half->onset_ms, 500u);
    const auto point_first = parse_lane_fault("1@.5", error);   // the number may begin with its point
    ASSERT_TRUE(point_first.has_value()) << error;
    EXPECT_EQ(point_first->onset_ms, 500u);
    const auto zero = parse_lane_fault("3@0", error);
    ASSERT_TRUE(zero.has_value());
    EXPECT_EQ(zero->onset_ms, 0u);
    for (const std::string bad : {"", "2", "2@", "@1", "0@1", "4@1", "22@1", "x@1", "2@-1", "2@x", "2@1x",
                                  "2@inf", "2@nan", "2@ 1", "2@1e99", "2@1e999"}) {
        error.clear();
        EXPECT_FALSE(parse_lane_fault(bad, error).has_value()) << bad;
        EXPECT_NE(error.find("--lane-fault"), std::string::npos) << bad;
        EXPECT_NE(error.find("<lane>@<seconds>"), std::string::npos) << bad;
    }
}

// The line main prints: the lane, the onset, the alteration, and whether
// the option or the replay's input put it there.
TEST(LaneFaultInjectorTest, DescribeNamesTheLaneTheOnsetAndTheCause) {
    EXPECT_EQ(describe_lane_fault(demo_lane_fault(2, 60000), false),
              "lane 2 from 60000 ms, bit 22 of its fused altitude flipped on every cycle (--lane-fault, REQ-RED-007)");
    EXPECT_EQ(describe_lane_fault(demo_lane_fault(1, 500), true),
              "lane 1 from 500 ms, bit 22 of its fused altitude flipped on every cycle (re-applied: the replay's input "
              "records it, REQ-RED-007)");
}

// TC-037 step 2: the demonstration fault through the three lanes and the
// voter on the simulator. The voter outvotes lane 2 on its first altered
// frame and excludes it two cycles later, at the persistence count of
// three; every voted frame equals an unaltered processor's but for the
// voter's status, which names lane 2 from the frame after the exclusion.
TEST(LaneFaultInjectorTest, DemoFaultThroughTheVoter) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42));
    FaultDetector detector;
    LaneFaultInjector injector({demo_lane_fault(2, 1000)});
    RedundantProcessor altered(ProcessorConfig{}, TelemetryProcessor::kDefaultImuTimeoutMs,
                               Voter::kLanePersistence, &injector);
    RedundantProcessor clean;
    std::vector<FaultEvent> injected;
    std::vector<FaultEvent> voter;
    for (int i = 0; i < 200; i++) {
        const TelemetryFrame checked = detector.check(source.read_frame());
        TelemetryFrame a = altered.process(checked);
        const TelemetryFrame b = clean.process(checked);
        const uint16_t expected_status = checked.timestamp_ms >= 1060 ? 0x0005 : 0x0001;
        ASSERT_EQ(a.voter_status, expected_status) << "t=" << checked.timestamp_ms;
        a.voter_status = b.voter_status;
        ASSERT_EQ(std::memcmp(&a, &b, sizeof a), 0) << "t=" << checked.timestamp_ms;
        for (const FaultEvent& e : injector.take_events()) {
            injected.push_back(e);
        }
        for (const FaultEvent& e : altered.take_events()) {
            voter.push_back(e);
        }
    }
    ASSERT_EQ(injected.size(), 1u);
    EXPECT_EQ(injected[0].timestamp_ms, 1000u);
    EXPECT_EQ(injected[0].value, 2.0f);
    ASSERT_EQ(voter.size(), 2u);
    EXPECT_EQ(voter[0].type, FaultType::LANE_DISAGREEMENT);
    EXPECT_EQ(voter[0].timestamp_ms, 1000u);
    EXPECT_EQ(voter[0].value, 2.0f);
    EXPECT_EQ(voter[1].type, FaultType::LANE_FAULTED);
    EXPECT_EQ(voter[1].timestamp_ms, 1040u);
    EXPECT_EQ(voter[1].value, 2.0f);
}
