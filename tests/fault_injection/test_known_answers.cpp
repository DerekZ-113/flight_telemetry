// Known answers for the whole harness (REQ-TEST-005, REQ-TEST-006): hand-
// made schedules through the real pipeline, the classifier and the judge,
// each number stated here. Test card TC-035 steps 4 and 6. A verification
// tool is trusted because it gives the answers it must on inputs whose
// answers are known; these are those inputs.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "campaign_run.h"
#include "fault_catalogue.h"
#include "prediction.h"
#include "run_outcome.h"
#include "run_schedule.h"

namespace {

const PipelineSettings kSettings{};
const CampaignTiming& kTiming = kSettings.timing;

std::size_t kind_named(const std::string& id) {
    for (std::size_t k = 0; k < kinds().size(); k++) {
        if (id == kinds()[k].id) {
            return k;
        }
    }
    ADD_FAILURE() << "no kind " << id;
    return 0;
}

RunSchedule schedule_for(const std::string& id, int persistence = 3) {
    RunSchedule s;
    s.kind = kind_named(id);
    s.persistence = persistence;
    s.sim_seed = 4242;
    return s;
}

LaneFault lane_fault(int lane, LaneFaultKind kind, uint64_t onset, uint64_t end, LaneField field, uint8_t bit) {
    LaneFault f;
    f.lane = lane;
    f.kind = kind;
    f.onset_ms = onset;
    f.end_ms = end;
    f.field = field;
    f.bit = bit;
    return f;
}

struct Result {
    Timeline timeline;
    RunOutcome outcome;
    Verdict verdict;
};

Result run(const RunSchedule& s) {
    Result r;
    r.timeline = run_schedule(s, kSettings);
    r.outcome = classify(s, r.timeline, kTiming);
    r.verdict = judge(s, r.timeline, r.outcome, kTiming);
    return r;
}

const ChannelOutcome& baro(const RunOutcome& o) { return o.channel[0]; }
const ChannelOutcome& gps(const RunOutcome& o) { return o.channel[2]; }

}  // namespace

// D01: declared on the 26th failed frame, 500 ms after onset, the event
// carrying the 520 ms since the last good read; NOMINAL again on the fifth
// valid frame, 80 ms after removal.
TEST(KnownAnswerTest, BarometerDropoutIsDeclaredAt500AndRecoversAt80) {
    RunSchedule s = schedule_for("D01");
    s.sensor.mode = SensorMode::FAIL_READS;
    s.sensor.channels = {Channel::BARO};
    s.sensor.onset_ms = 3000;
    s.sensor.removal_ms = 5000;
    s.end_ms = 6000;
    const Result r = run(s);
    EXPECT_TRUE(baro(r.outcome).declared);
    EXPECT_EQ(baro(r.outcome).type, FaultType::COMM_TIMEOUT);
    EXPECT_EQ(baro(r.outcome).latency_ms, 500);
    EXPECT_EQ(baro(r.outcome).value, 520.0f);
    EXPECT_EQ(baro(r.outcome).recovery_ms, 80);
    EXPECT_EQ(r.outcome.declarations, 1);
    EXPECT_EQ(r.outcome.voter_events, 0);
    EXPECT_TRUE(r.verdict.as_predicted) << r.verdict.mismatch;
}

// D03: the latency depends on where in the fix second the dropout starts.
// On a fix frame the last delivered fix is a second old: 1,020 ms; one
// frame later it is 20 ms old: 2,000 ms. From the first suppressed fix,
// 1,020 ms both times; the event's value 2,020 both times.
TEST(KnownAnswerTest, GpsDropoutLatencyDependsOnTheOnsetsPlaceInTheSecond) {
    for (const auto& [onset, expected] : {std::pair<uint64_t, int64_t>{3000, 1020}, {3020, 2000}}) {
        RunSchedule s = schedule_for("D03");
        s.sensor.mode = SensorMode::FAIL_READS;
        s.sensor.channels = {Channel::GPS};
        s.sensor.onset_ms = onset;
        s.sensor.removal_ms = onset + 5000;
        s.end_ms = s.sensor.removal_ms + 6000;
        const Result r = run(s);
        EXPECT_EQ(gps(r.outcome).latency_ms, expected) << onset;
        EXPECT_EQ(r.outcome.gps_fix_latency_ms, 1020) << onset;
        EXPECT_EQ(gps(r.outcome).value, 2020.0f) << onset;
        EXPECT_TRUE(r.verdict.as_predicted) << r.verdict.mismatch;
    }
}

// D03's recovery: five fixes, the first at or after removal. A removal on
// a fix frame recovers 4,000 ms later; one frame after it, 4,980 ms.
TEST(KnownAnswerTest, GpsRecoveryCountsFiveFixesFromTheFirstAfterRemoval) {
    for (const auto& [removal, expected] : {std::pair<uint64_t, int64_t>{8000, 4000}, {8020, 4980}}) {
        RunSchedule s = schedule_for("D03");
        s.sensor.mode = SensorMode::FAIL_READS;
        s.sensor.channels = {Channel::GPS};
        s.sensor.onset_ms = 3000;
        s.sensor.removal_ms = removal;
        s.end_ms = removal + 6000;
        const Result r = run(s);
        EXPECT_EQ(gps(r.outcome).recovery_ms, expected) << removal;
        EXPECT_TRUE(r.verdict.as_predicted) << r.verdict.mismatch;
    }
}

// D05: the ninth held frame is the tenth identical reading, the last good
// one counted: 160 ms after onset; this seed's reading before onset does
// not repeat its predecessor.
TEST(KnownAnswerTest, BarometerHoldIsDeclaredAt160) {
    RunSchedule s = schedule_for("D05");
    s.sensor.mode = SensorMode::HOLD_CHANNEL;
    s.sensor.channels = {Channel::BARO};
    s.sensor.onset_ms = 3000;
    s.sensor.removal_ms = 4000;
    s.end_ms = 5000;
    const Result r = run(s);
    EXPECT_EQ(prior_run(s, r.timeline, kTiming), 1);
    EXPECT_EQ(baro(r.outcome).type, FaultType::STUCK);
    EXPECT_EQ(baro(r.outcome).latency_ms, 160);
    EXPECT_EQ(baro(r.outcome).recovery_ms, 80);
    EXPECT_TRUE(r.verdict.as_predicted) << r.verdict.mismatch;
}

// D08: out of range on the onset frame itself, the event carrying the value.
TEST(KnownAnswerTest, PressureOutOfRangeIsDeclaredOnTheOnsetFrame) {
    RunSchedule s = schedule_for("D08");
    s.sensor.mode = SensorMode::OVERRIDE_FIELD;
    s.sensor.field = RawField::PRESSURE;
    s.sensor.value = 250.0f;
    s.sensor.onset_ms = 3000;
    s.sensor.removal_ms = 4000;
    s.end_ms = 5000;
    const Result r = run(s);
    EXPECT_EQ(baro(r.outcome).type, FaultType::OUT_OF_RANGE);
    EXPECT_EQ(baro(r.outcome).latency_ms, 0);
    EXPECT_EQ(baro(r.outcome).value, 250.0f);
    EXPECT_EQ(baro(r.outcome).recovery_ms, 80);
    EXPECT_TRUE(r.verdict.as_predicted) << r.verdict.mismatch;
}

// C00: a clean minute raises nothing, on any channel or in the voter, and
// all of it is exposure.
TEST(KnownAnswerTest, ACleanRunRaisesNoAlarm) {
    RunSchedule s = schedule_for("C00");
    s.end_ms = 60000;
    const Result r = run(s);
    EXPECT_EQ(r.outcome.declarations, 0);
    EXPECT_EQ(r.outcome.voter_events, 0);
    for (const ChannelOutcome& ch : r.outcome.channel) {
        EXPECT_EQ(ch.false_alarms, 0);
        EXPECT_EQ(ch.exposure_ms, 60000u);
    }
    EXPECT_EQ(r.outcome.invariant_violations, 0);
    EXPECT_TRUE(r.verdict.as_predicted) << r.verdict.mismatch;
}

// V01 at the shipped count: the onset event on the onset frame, the
// exclusion two cycles later, 40 ms; the output a healthy lane's throughout.
TEST(KnownAnswerTest, PersistentLaneValueIsIsolatedAt40) {
    RunSchedule s = schedule_for("V01", 3);
    s.lane_faults = {lane_fault(2, LaneFaultKind::OUTPUT_BIT_FLIP, 3000, LaneFault::kPersistent,
                                LaneField::FUSED_ALTITUDE, 22)};
    s.end_ms = 6000;
    const Result r = run(s);
    EXPECT_EQ(r.outcome.excluded_lane, 2);
    EXPECT_EQ(r.outcome.excluded_ms, 3040);
    EXPECT_EQ(r.outcome.isolation_ms, 40);
    EXPECT_EQ(r.outcome.onset_events, 1);
    EXPECT_EQ(r.outcome.healthy_excluded, 0);
    EXPECT_EQ(r.outcome.wrong_output_frames, 0);
    EXPECT_TRUE(r.verdict.as_predicted) << r.verdict.mismatch;
}

// V06: a one-cycle overrun is an onset; it excludes the lane only when one
// miss is enough.
TEST(KnownAnswerTest, OneCycleOverrunIsExcludedOnlyAtCountOne) {
    for (int p : {1, 2}) {
        RunSchedule s = schedule_for("V06", p);
        s.lane_faults = {lane_fault(3, LaneFaultKind::OUTPUT_STALE, 3000, 3020, LaneField::PITCH, 0)};
        s.end_ms = 6000;
        const Result r = run(s);
        EXPECT_EQ(r.outcome.overrun_onsets, 1) << p;
        EXPECT_EQ(r.outcome.exclusions, p == 1 ? 1 : 0) << p;
        if (p == 1) {
            EXPECT_EQ(r.outcome.excluded_ms, 3000);
        }
        EXPECT_TRUE(r.verdict.as_predicted) << r.verdict.mismatch;
    }
}

// V11: two different faults leave no majority at every count. Inside the
// count (g = 1 < 3) lane 1 is never excluded and three lanes stay in the
// vote; past it (g = 5) lane 1 is excluded first and two remain.
TEST(KnownAnswerTest, TwoDifferentFaultsLeaveNoMajorityEitherSideOfTheCount) {
    for (int g : {1, 5}) {
        RunSchedule s = schedule_for("V11", 3);
        s.gap_cycles = g;
        s.lane_faults = {lane_fault(1, LaneFaultKind::OUTPUT_BIT_FLIP, 3000, LaneFault::kPersistent, LaneField::PITCH, 3),
                         lane_fault(2, LaneFaultKind::OUTPUT_BIT_FLIP, 3000 + 20u * static_cast<uint64_t>(g),
                                    LaneFault::kPersistent, LaneField::ROLL, 4)};
        s.end_ms = 6000;
        const Result r = run(s);
        EXPECT_EQ(r.outcome.nm_episodes, 1) << g;
        EXPECT_EQ(r.outcome.nm_first_ms, 3000 + 20 * g) << g;
        EXPECT_EQ(r.outcome.nm_lanes, g >= 3 ? 2.0f : 3.0f) << g;
        EXPECT_EQ(r.outcome.exclusions, g >= 3 ? 1 : 0) << g;
        EXPECT_EQ(r.outcome.wrong_output_frames, 0) << g;
        EXPECT_TRUE(r.verdict.as_predicted) << r.verdict.mismatch;
    }
}

// V12: the same fault arriving on a second lane while the first is still
// in the vote forms a wrong majority, which throws out the healthy lane and
// votes the wrong value; arriving after the first's exclusion it leaves no
// majority, which is safe.
TEST(KnownAnswerTest, TwoIdenticalFaultsOutvoteTheHealthyLaneInsideTheCount) {
    for (int g : {1, 5}) {
        RunSchedule s = schedule_for("V12", 3);
        s.gap_cycles = g;
        s.lane_faults = {lane_fault(1, LaneFaultKind::OUTPUT_BIT_FLIP, 3000, LaneFault::kPersistent,
                                    LaneField::FUSED_ALTITUDE, 22),
                         lane_fault(3, LaneFaultKind::OUTPUT_BIT_FLIP, 3000 + 20u * static_cast<uint64_t>(g),
                                    LaneFault::kPersistent, LaneField::FUSED_ALTITUDE, 22)};
        s.end_ms = 6000;
        const Result r = run(s);
        if (g < 3) {
            EXPECT_EQ(r.outcome.excluded_lane, 2);
            EXPECT_EQ(r.outcome.excluded_ms, 3000 + 20 * g + 40);
            EXPECT_EQ(r.outcome.healthy_excluded, 1);
            EXPECT_EQ(r.outcome.wrong_output_frames, (6000 - (3000 + 20 * g)) / 20);
            EXPECT_EQ(r.outcome.nm_episodes, 0);
        } else {
            EXPECT_EQ(r.outcome.excluded_lane, 1);
            EXPECT_EQ(r.outcome.healthy_excluded, 0);
            EXPECT_EQ(r.outcome.wrong_output_frames, 0);
            EXPECT_EQ(r.outcome.nm_lanes, 2.0f);
        }
        EXPECT_TRUE(r.verdict.as_predicted) << r.verdict.mismatch;
    }
}

// U06: one NaN pressure passes every check, poisons every lane's Kalman
// state and leaves no majority to the end: one event, three lanes in the
// vote, every frame from onset held, none NaN.
TEST(KnownAnswerTest, NanPressureLeavesNoMajorityToTheEnd) {
    RunSchedule s = schedule_for("U06");
    s.twin = true;
    s.sensor.mode = SensorMode::OVERRIDE_FIELD;
    s.sensor.field = RawField::PRESSURE;
    s.sensor.value = std::nanf("");
    s.sensor.onset_ms = 3000;
    s.sensor.removal_ms = 3020;
    s.end_ms = 8000;
    const Result r = run(s);
    EXPECT_EQ(r.outcome.declarations, 0);
    EXPECT_EQ(r.outcome.nm_episodes, 1);
    EXPECT_EQ(r.outcome.nm_first_ms, 3000);
    EXPECT_EQ(r.outcome.nm_lanes, 3.0f);
    EXPECT_EQ(r.outcome.nm_frames, 250);
    EXPECT_EQ(r.outcome.invariant_violations, 0);
    EXPECT_TRUE(r.verdict.as_predicted) << r.verdict.mismatch;
}

// U07: one infinite rotation rate makes the roll +inf in every lane; the
// lanes agree on it, and it is voted with validity NORMAL on every frame.
TEST(KnownAnswerTest, InfiniteRateIsVotedNormal) {
    RunSchedule s = schedule_for("U07");
    s.twin = true;
    s.sensor.mode = SensorMode::OVERRIDE_FIELD;
    s.sensor.field = RawField::GYRO_X;
    s.sensor.value = INFINITY;
    s.sensor.onset_ms = 3000;
    s.sensor.removal_ms = 3020;
    s.end_ms = 6000;
    const Result r = run(s);
    EXPECT_EQ(r.outcome.declarations, 0);
    EXPECT_EQ(r.outcome.voter_events, 0);
    EXPECT_TRUE(r.outcome.roll_infinite_normal);
    EXPECT_TRUE(std::isinf(r.outcome.max_error[2]));
    EXPECT_TRUE(r.verdict.as_predicted) << r.verdict.mismatch;
}

// TC-035 step 6: the twin is the run's exact counterfactual. Before onset
// the faulted run's voted values equal the twin's bit for bit; after a
// bias step they do not.
TEST(KnownAnswerTest, TheTwinEqualsTheRunBeforeOnset) {
    RunSchedule s = schedule_for("U01");
    s.twin = true;
    s.sensor.mode = SensorMode::BIAS_FIELD;
    s.sensor.field = RawField::PRESSURE;
    s.sensor.value = 2.0f;
    s.sensor.onset_ms = 3000;
    s.end_ms = 8000;
    const Result r = run(s);
    for (const TimelineFrame& f : r.timeline.frames) {
        const bool same = std::memcmp(f.voted.data(), f.reference.data(), sizeof(float) * kComputedCount) == 0;
        if (f.t_ms < 3000) {
            ASSERT_TRUE(same) << f.t_ms;
        }
    }
    EXPECT_GT(r.outcome.max_error[0], 15.0);   // 2 hPa of bias is about 16.6 m of barometric altitude
    EXPECT_LT(r.outcome.max_error[0], 18.0);
    EXPECT_GT(r.outcome.normal_error_frames, 0);
    EXPECT_TRUE(r.verdict.as_predicted) << r.verdict.mismatch;
}

// The judge names the first departure: a doctored latency fails with the
// channel and both numbers in the message.
TEST(KnownAnswerTest, AJudgeNamesTheFirstDeparture) {
    RunSchedule s = schedule_for("D01");
    s.sensor.mode = SensorMode::FAIL_READS;
    s.sensor.channels = {Channel::BARO};
    s.sensor.onset_ms = 3000;
    s.sensor.removal_ms = 5000;
    s.end_ms = 6000;
    Result r = run(s);
    r.outcome.channel[0].latency_ms = 480;
    const Verdict v = judge(s, r.timeline, r.outcome, kTiming);
    EXPECT_FALSE(v.as_predicted);
    EXPECT_NE(v.mismatch.find("BARO latency: 480, predicted 500"), std::string::npos) << v.mismatch;
}
