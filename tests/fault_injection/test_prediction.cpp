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

namespace {

// Frames from 3,000 ms to 4,100 ms on which every pair agrees (healthy
// lanes), for the replayed vote's tests to break.
Timeline agreeing(uint64_t from, uint64_t to) {
    Timeline tl;
    for (uint64_t t = from; t < to; t += kTiming.period_ms) {
        TimelineFrame f;
        f.t_ms = t;
        f.pair_agree = {true, true, true};
        tl.frames.push_back(f);
    }
    return tl;
}

RunSchedule two_altered(int persistence) {
    RunSchedule s;
    s.persistence = persistence;
    LaneFault a;
    a.lane = 2;
    a.onset_ms = 3100;
    LaneFault b = a;
    b.lane = 3;
    s.lane_faults = {a, b};
    return s;
}

}  // namespace

// Seed 6900004's pattern, the finding of FTS-FI-001 Section 7: lanes 2 and 3
// carry the identical flip, which is NaN while the value is below 2 m (no
// pair agrees: no majority, one event, three lanes in the vote) and finite
// once it crosses (lanes 2 and 3 agree: a majority without lane 1, which
// counts its misses). At P = 1 lane 1 goes on that frame, at P = 3 two
// frames later; every frame of that majority votes lane 2's wrong value.
TEST(PredictionTest, ReplayedVoteFollowsAFlipThatIsNaNThenFinite) {
    for (int p : {1, 3}) {
        Timeline tl = agreeing(3000, 4100);
        for (TimelineFrame& f : tl.frames) {
            if (f.t_ms >= 3100 && f.t_ms < 3960) {
                f.pair_agree = {false, false, false};
            } else if (f.t_ms >= 3960) {
                f.pair_agree = {false, false, true};
            }
        }
        const ReplayedVote vote = replay_vote(two_altered(p), tl);
        ASSERT_EQ(vote.events.size(), 3u) << p;
        EXPECT_EQ(vote.events[0].type, FaultType::NO_MAJORITY);
        EXPECT_EQ(vote.events[0].t_ms, 3100u);
        EXPECT_EQ(vote.events[0].value, 3.0f);
        EXPECT_EQ(vote.events[1].type, FaultType::LANE_DISAGREEMENT);
        EXPECT_EQ(vote.events[1].t_ms, 3960u);
        EXPECT_EQ(vote.events[1].value, 1.0f);
        EXPECT_EQ(vote.events[2].type, FaultType::LANE_FAULTED);
        EXPECT_EQ(vote.events[2].t_ms, p == 1 ? 3960u : 4000u);
        EXPECT_EQ(vote.events[2].value, 1.0f);
        EXPECT_EQ(vote.altered_representative_frames, (4100 - 3960) / 20) << p;
    }
}

// A stale lane takes no part in the vote and counts a miss, its onset an
// overrun; once it is out, the two left disagreeing is no majority with two
// lanes in the vote.
TEST(PredictionTest, ReplayedVoteCountsAnOverrunAndThenNoMajorityOnTwo) {
    RunSchedule s;
    s.persistence = 2;
    LaneFault stale;
    stale.lane = 1;
    stale.kind = LaneFaultKind::OUTPUT_STALE;
    stale.onset_ms = 3000;
    s.lane_faults = {stale};
    Timeline tl = agreeing(3000, 3200);
    for (TimelineFrame& f : tl.frames) {
        f.stale[0] = true;
        if (f.t_ms >= 3100) {
            f.pair_agree = {false, false, false};   // lanes 2 and 3 now disagree too
        }
    }
    const ReplayedVote vote = replay_vote(s, tl);
    ASSERT_EQ(vote.events.size(), 3u);
    EXPECT_EQ(vote.events[0].type, FaultType::LANE_OVERRUN);
    EXPECT_EQ(vote.events[0].t_ms, 3000u);
    EXPECT_EQ(vote.events[1].type, FaultType::LANE_FAULTED);
    EXPECT_EQ(vote.events[1].t_ms, 3020u);
    EXPECT_EQ(vote.events[2].type, FaultType::NO_MAJORITY);
    EXPECT_EQ(vote.events[2].t_ms, 3100u);
    EXPECT_EQ(vote.events[2].value, 2.0f);
}
