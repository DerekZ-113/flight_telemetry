// Tests for tests/fault_injection/run_outcome.cpp (REQ-TEST-005): the
// classifier on timelines built by hand, one bucket at a time. Test card
// TC-035 step 3.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "campaign_run.h"
#include "run_outcome.h"
#include "run_schedule.h"

namespace {

const CampaignTiming kTiming{};
constexpr uint16_t kVoted = 0x0001;
constexpr uint16_t kNoMajority = 0x0010;

// A healthy timeline: every channel NOMINAL, the barometer and IMU fresh
// every frame and the GPS once a second, every value NORMAL and voted, the
// reference equal to the voted values.
Timeline healthy(uint64_t end_ms) {
    Timeline tl;
    for (uint64_t t = 0; t < end_ms; t += kTiming.period_ms) {
        TimelineFrame f;
        f.t_ms = t;
        f.status = {ChannelStatus::NOMINAL, ChannelStatus::NOMINAL, ChannelStatus::NOMINAL};
        f.read_ok = {true, true, t % kTiming.gps_interval_ms == 0};
        f.voter_status = kVoted;
        f.voted = {2.0f, 0.1f, 0.2f, 2.0f, 0.0f};
        f.reference = f.voted;
        f.validity = {Validity::NORMAL, Validity::NORMAL, Validity::NORMAL, Validity::NORMAL, Validity::NORMAL};
        tl.frames.push_back(f);
    }
    return tl;
}

void event(Timeline& tl, uint64_t t, Channel c, FaultType type, float value = 0.0f) {
    tl.events.push_back(TimelineEvent{t, c, type, value});
}

RunSchedule dropout(Channel c, uint64_t onset, uint64_t removal, uint64_t end) {
    RunSchedule s;
    s.sensor.mode = SensorMode::FAIL_READS;
    s.sensor.channels = {c};
    s.sensor.onset_ms = onset;
    s.sensor.removal_ms = removal;
    s.end_ms = end;
    return s;
}

}  // namespace

TEST(OutcomeTest, TheFirstDeclarationInTheWindowIsTheDetectionAndALaterOneARepeat) {
    const RunSchedule s = dropout(Channel::BARO, 3000, 5000, 6000);
    Timeline tl = healthy(6000);
    event(tl, 3500, Channel::BARO, FaultType::COMM_TIMEOUT, 520.0f);
    event(tl, 4500, Channel::BARO, FaultType::STUCK, 1.0f);
    const RunOutcome o = classify(s, tl, kTiming);
    EXPECT_TRUE(o.channel[0].declared);
    EXPECT_EQ(o.channel[0].latency_ms, 500);
    EXPECT_EQ(o.channel[0].type, FaultType::COMM_TIMEOUT);
    EXPECT_EQ(o.channel[0].value, 520.0f);
    EXPECT_EQ(o.channel[0].repeats, 1);
    EXPECT_EQ(o.channel[0].false_alarms, 0);
    EXPECT_EQ(o.declarations, 2);
}

TEST(OutcomeTest, NoDeclarationInTheWindowIsAMiss) {
    const RunSchedule s = dropout(Channel::IMU, 3000, 5000, 6000);
    Timeline tl = healthy(6000);
    event(tl, 5500, Channel::IMU, FaultType::COMM_TIMEOUT);   // after the window: not a detection
    const RunOutcome o = classify(s, tl, kTiming);
    EXPECT_FALSE(o.channel[1].declared);
    EXPECT_EQ(o.channel[1].latency_ms, -1);
    EXPECT_EQ(o.channel[1].false_alarms, 1);
}

TEST(OutcomeTest, ADeclarationBeforeOnsetIsAFalseAlarm) {
    const RunSchedule s = dropout(Channel::BARO, 3000, 5000, 6000);
    Timeline tl = healthy(6000);
    event(tl, 1000, Channel::BARO, FaultType::STUCK);
    event(tl, 3500, Channel::BARO, FaultType::COMM_TIMEOUT);
    const RunOutcome o = classify(s, tl, kTiming);
    EXPECT_EQ(o.channel[0].false_alarms, 1);
    EXPECT_TRUE(o.channel[0].declared);
}

TEST(OutcomeTest, AnotherChannelDeclaredDuringTheFaultIsWrongChannel) {
    const RunSchedule s = dropout(Channel::BARO, 3000, 5000, 6000);
    Timeline tl = healthy(6000);
    event(tl, 1000, Channel::IMU, FaultType::STUCK);          // before the fault: a false alarm
    event(tl, 3200, Channel::IMU, FaultType::COMM_TIMEOUT);   // during it: the wrong channel
    const RunOutcome o = classify(s, tl, kTiming);
    EXPECT_EQ(o.channel[1].wrong_channel, 1);
    EXPECT_EQ(o.channel[1].false_alarms, 1);
    EXPECT_FALSE(o.channel[1].injected);
}

TEST(OutcomeTest, ADeclarationOnACleanRunIsAFalseAlarm) {
    RunSchedule s;
    s.end_ms = 60000;
    Timeline tl = healthy(60000);
    event(tl, 30000, Channel::GPS, FaultType::COMM_TIMEOUT);
    const RunOutcome o = classify(s, tl, kTiming);
    EXPECT_EQ(o.channel[2].false_alarms, 1);
    EXPECT_EQ(o.channel[2].wrong_channel, 0);
    EXPECT_EQ(o.declarations, 1);
}

TEST(OutcomeTest, RecoveryIsMeasuredFromRemovalAndItsAbsenceKept) {
    const RunSchedule s = dropout(Channel::BARO, 3000, 5000, 6000);
    Timeline tl = healthy(6000);
    event(tl, 3500, Channel::BARO, FaultType::COMM_TIMEOUT);
    event(tl, 5080, Channel::BARO, FaultType::RECOVERY, 1580.0f);
    RunOutcome o = classify(s, tl, kTiming);
    EXPECT_TRUE(o.channel[0].recovered);
    EXPECT_EQ(o.channel[0].recovery_ms, 80);
    tl.events.pop_back();
    o = classify(s, tl, kTiming);
    EXPECT_FALSE(o.channel[0].recovered);
    EXPECT_EQ(o.channel[0].recovery_ms, -1);
}

// A GPS dropout's window runs to the first fix after removal: a timeout on
// the removal frame or just after it is a late detection, not a false alarm.
TEST(OutcomeTest, AGpsTimeoutJustAfterRemovalIsStillADetection) {
    const RunSchedule s = dropout(Channel::GPS, 3020, 5020, 12000);
    Timeline tl = healthy(12000);
    for (TimelineFrame& f : tl.frames) {
        if (f.t_ms >= 3020 && f.t_ms < 5020) {
            f.read_ok[2] = false;
        }
    }
    EXPECT_EQ(window_end(s, tl, Channel::GPS), 6000u);
    event(tl, 5040, Channel::GPS, FaultType::COMM_TIMEOUT, 2020.0f);
    const RunOutcome o = classify(s, tl, kTiming);
    EXPECT_TRUE(o.channel[2].declared);
    EXPECT_EQ(o.channel[2].latency_ms, 2020);
    EXPECT_EQ(o.channel[2].false_alarms, 0);
}

TEST(OutcomeTest, ExposureCountsTheTimeWithNothingInjected) {
    const RunSchedule s = dropout(Channel::GPS, 4000, 8000, 14000);
    const RunOutcome o = classify(s, healthy(14000), kTiming);
    EXPECT_EQ(o.channel[0].exposure_ms, 14000u);
    EXPECT_EQ(o.channel[1].exposure_ms, 14000u);
    EXPECT_EQ(o.channel[2].exposure_ms, 4000u);
}

TEST(OutcomeTest, TheVotersOutcomesAreCounted) {
    RunSchedule s;
    s.end_ms = 6000;
    LaneFault f;
    f.lane = 2;
    f.onset_ms = 3000;
    s.lane_faults = {f};
    Timeline tl = healthy(6000);
    event(tl, 3000, Channel::VOTER, FaultType::LANE_DISAGREEMENT, 2.0f);
    event(tl, 3040, Channel::VOTER, FaultType::LANE_FAULTED, 2.0f);
    event(tl, 4000, Channel::VOTER, FaultType::LANE_FAULTED, 3.0f);   // lane 3 was never altered
    event(tl, 4500, Channel::VOTER, FaultType::NO_MAJORITY, 2.0f);
    event(tl, 4500, Channel::VOTER, FaultType::LANE_FAULT_INJECTED, 2.0f);   // a record, not a voter event
    for (TimelineFrame& fr : tl.frames) {
        if (fr.t_ms >= 4500 && fr.t_ms < 4600) {
            fr.voter_status = kVoted | kNoMajority;
        }
        if (fr.t_ms >= 5000) {
            fr.voted[3] = 3.0f;   // voted, with a majority, and not the healthy lane's value
        }
    }
    const RunOutcome o = classify(s, tl, kTiming);
    EXPECT_EQ(o.voter_events, 4);
    EXPECT_EQ(o.onset_events, 1);
    EXPECT_EQ(o.exclusions, 2);
    EXPECT_EQ(o.excluded_lane, 2);
    EXPECT_EQ(o.isolation_ms, 40);
    EXPECT_EQ(o.healthy_excluded, 1);
    EXPECT_EQ(o.nm_episodes, 1);
    EXPECT_EQ(o.nm_edges, 1);
    EXPECT_EQ(o.nm_frames, 5);
    EXPECT_EQ(o.nm_first_ms, 4500);
    EXPECT_EQ(o.nm_lanes, 2.0f);
    EXPECT_EQ(o.wrong_output_frames, 50);
    EXPECT_EQ(o.invariant_violations, 0);
}

// Each invariant broken once is counted once: VOTED clear, two lane bits,
// a NaN voted, and a no-majority bit whose edges outnumber its events.
TEST(OutcomeTest, EachInvariantBrokenOnceIsCounted) {
    RunSchedule s;
    s.end_ms = 1000;
    Timeline tl = healthy(1000);
    tl.frames[10].voter_status = 0;
    tl.frames[20].voter_status = kVoted | 0x0002 | 0x0004;
    tl.frames[30].voted[1] = std::nanf("");
    tl.frames[40].voter_status = kVoted | kNoMajority;
    const RunOutcome o = classify(s, tl, kTiming);
    EXPECT_EQ(o.invariant_violations, 4);
}

// The precondition: a channel not NOMINAL, or a value not NORMAL, on the
// frame before onset makes the run invalid.
TEST(OutcomeTest, ARunNotNominalBeforeOnsetIsInvalid) {
    const RunSchedule s = dropout(Channel::BARO, 3000, 5000, 6000);
    Timeline tl = healthy(6000);
    EXPECT_TRUE(classify(s, tl, kTiming).valid);
    tl.frames[149].validity[3] = Validity::FAILURE_WARNING;
    EXPECT_FALSE(classify(s, tl, kTiming).valid);
}
