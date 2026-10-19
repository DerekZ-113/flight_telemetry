// Tests for tests/fault_injection/prediction.cpp (REQ-TEST-006): the
// formulas pinned to FTS-FI-001 Section 5's numbers, and the two
// coincidences a hold's prediction reads from its own frames. Test card
// TC-035 step 1.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "campaign_run.h"
#include "fault_catalogue.h"
#include "prediction.h"
#include "run_outcome.h"
#include "run_schedule.h"

namespace {

const CampaignTiming kTiming{};

// A barometer hold from 3,000 ms to 4,000 ms over frames whose pressure
// steps every frame, except where a test makes two equal.
RunSchedule hold() {
    RunSchedule s;
    for (std::size_t k = 0; k < kinds().size(); k++) {
        if (std::string(kinds()[k].id) == "D05") {
            s.kind = k;
        }
    }
    s.sensor.mode = SensorMode::HOLD_CHANNEL;
    s.sensor.channels = {Channel::BARO};
    s.sensor.onset_ms = 3000;
    s.sensor.removal_ms = 4000;
    s.end_ms = 5000;
    return s;
}

Timeline stepping(uint64_t end_ms) {
    Timeline tl;
    for (uint64_t t = 0; t < end_ms; t += kTiming.period_ms) {
        TimelineFrame f;
        f.t_ms = t;
        f.raw[0] = 1000.0f + static_cast<float>(t) * 0.001f;
        tl.frames.push_back(f);
    }
    return tl;
}

}  // namespace

// Every number FTS-FI-001 Section 5 predicts, from the shipped
// configuration's thresholds (the struct defaults, equal to the file).
TEST(PredictionTest, ShippedConfigurationGivesTheReportsPredictions) {
    EXPECT_EQ(i2c_timeout_latency(kTiming), 500);
    EXPECT_EQ(timeout_event_value(500, kTiming), 520);
    EXPECT_EQ(gps_event_value(kTiming), 2020);
    EXPECT_EQ(gps_fix_latency(kTiming), 1020);
    EXPECT_EQ(gps_declared_at(3000, kTiming) - 3000, 1020);   // onset on a fix frame
    EXPECT_EQ(gps_declared_at(3020, kTiming) - 3020, 2000);   // one frame after one
    EXPECT_EQ(gps_declared_at(3980, kTiming) - 3980, 1040);
    EXPECT_EQ(gps_recovery(8000, kTiming), 4000);
    EXPECT_EQ(gps_recovery(8020, kTiming), 4980);
    EXPECT_EQ(stuck_latency(1, kTiming), 160);
    EXPECT_EQ(stuck_latency(2, kTiming), 140);
    EXPECT_EQ(recovery_time(0, kTiming), 80);
    EXPECT_EQ(recovery_time(1, kTiming), 100);
    EXPECT_EQ(isolation_latency(1, kTiming), 0);
    EXPECT_EQ(isolation_latency(3, kTiming), 40);
    EXPECT_EQ(isolation_latency(8, kTiming), 140);
    // The thresholds below which a fault must never be declared (T01-T03).
    EXPECT_EQ(kTiming.i2c_timeout_ms / kTiming.period_ms, 25u);
    EXPECT_EQ(kTiming.gps_interval_ms / kTiming.period_ms, 50u);
    EXPECT_EQ(kTiming.stuck_count - 2, 8);
}

TEST(PredictionTest, PriorRunCountsTheReadingsThatRepeatedBeforeOnset) {
    const RunSchedule s = hold();
    Timeline tl = stepping(5000);
    EXPECT_EQ(prior_run(s, tl, kTiming), 1);
    tl.frames[148].raw[0] = tl.frames[149].raw[0];   // the frame before onset repeated its predecessor
    EXPECT_EQ(prior_run(s, tl, kTiming), 2);
    EXPECT_EQ(stuck_latency(prior_run(s, tl, kTiming), kTiming), 140);
}

TEST(PredictionTest, RepeatsAtRemovalCountTheFramesStillEqualToTheHeldValue) {
    const RunSchedule s = hold();
    Timeline tl = stepping(5000);
    for (std::size_t i = 150; i < 200; i++) {
        tl.frames[i].raw[0] = tl.frames[149].raw[0];   // the hold
    }
    EXPECT_EQ(repeats_at_removal(s, tl, kTiming), 0);
    tl.frames[200].raw[0] = tl.frames[149].raw[0];      // the removal frame's live value equals the held one
    EXPECT_EQ(repeats_at_removal(s, tl, kTiming), 1);
    EXPECT_EQ(recovery_time(repeats_at_removal(s, tl, kTiming), kTiming), 100);
}

// The judge predicts a coincidence from the run's own frames, so a run
// that shows it is as predicted: a hold whose reading before onset repeated
// declared at 140 ms, the removal frame repeating the held value recovered
// at 100 ms.
TEST(PredictionTest, TheJudgePredictsTheCoincidencesItReads) {
    const RunSchedule s = hold();
    Timeline tl = stepping(5000);
    tl.frames[148].raw[0] = tl.frames[149].raw[0];
    for (std::size_t i = 150; i < 200; i++) {
        tl.frames[i].raw[0] = tl.frames[149].raw[0];
    }
    tl.frames[200].raw[0] = tl.frames[149].raw[0];
    for (TimelineFrame& f : tl.frames) {
        f.status = {ChannelStatus::NOMINAL, ChannelStatus::NOMINAL, ChannelStatus::NOMINAL};
        f.read_ok = {true, true, f.t_ms % 1000 == 0};
        f.voter_status = 0x0001;
        f.validity = {Validity::NORMAL, Validity::NORMAL, Validity::NORMAL, Validity::NORMAL, Validity::NORMAL};
    }
    tl.events.push_back(TimelineEvent{3140, Channel::BARO, FaultType::STUCK, tl.frames[149].raw[0]});
    tl.events.push_back(TimelineEvent{4100, Channel::BARO, FaultType::RECOVERY, 960.0f});
    const RunOutcome o = classify(s, tl, kTiming);
    const Verdict v = judge(s, tl, o, kTiming);
    EXPECT_TRUE(v.as_predicted) << v.mismatch;
    tl.events[0].t_ms = 3160;   // the coincidence ignored: 160 ms is now the departure
    const Verdict late = judge(s, tl, classify(s, tl, kTiming), kTiming);
    EXPECT_FALSE(late.as_predicted);
    EXPECT_NE(late.mismatch.find("latency: 160, predicted 140"), std::string::npos) << late.mismatch;
}
