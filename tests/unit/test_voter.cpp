// Tests for the voter (REQ-RED-002 to REQ-RED-004, REQ-RED-006): the one
// comparison between two lanes' outputs, bit for bit over the thirteen
// fields a lane writes or carries from the detector (TC-032 steps 1 and
// 2); the majority, the count and the exclusion (TC-032 steps 3 to 7);
// no majority (TC-033 steps 1 to 7); the overrun (TC-032 step 8); the
// new vehicle; the exit line's tally. Lane outputs are real processor
// outputs over the simulator, so every compared field holds a value a lane
// can produce; a wrong lane is a healthy output with one field changed,
// which is what the voter is built from directly, as the fault-injection
// harness will build it.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

#include "drivers/simulated_source.h"
#include "processing/fault_detector.h"
#include "processing/processor.h"
#include "processing/voter.h"
#include "telemetry_frame.h"

namespace {

constexpr uint64_t kDtMs = 20;

// A lane's output after `n` frames of the seed-42 simulator: every channel
// NOMINAL, every validity NORMAL, every computed value non-zero.
TelemetryFrame lane_output(int n = 10) {
    SimulatedSource source(kDtMs, 42, kDtMs);
    FaultDetector detector;
    TelemetryProcessor processor;
    TelemetryFrame out{};
    for (int i = 0; i < n; i++) {
        out = processor.process(detector.check(source.read_frame()));
    }
    return out;
}

// The same value with its lowest bit flipped: the smallest difference a
// float can have, which a tolerance would swallow and the voter must not.
float flip_low_bit(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    bits ^= 1u;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

Validity other(Validity v) {
    return v == Validity::NORMAL ? Validity::FAILURE_WARNING : Validity::NORMAL;
}

ChannelStatus other(ChannelStatus s) {
    return s == ChannelStatus::NOMINAL ? ChannelStatus::DEGRADED : ChannelStatus::NOMINAL;
}

// The thirteen compared fields, each as a change to one of them.
struct FieldChange {
    const char* name;
    std::function<void(TelemetryFrame&)> change;
};

std::vector<FieldChange> compared_fields() {
    return {
        {"baro_altitude_m", [](TelemetryFrame& f) { f.baro_altitude_m = flip_low_bit(f.baro_altitude_m); }},
        {"pitch_deg", [](TelemetryFrame& f) { f.pitch_deg = flip_low_bit(f.pitch_deg); }},
        {"roll_deg", [](TelemetryFrame& f) { f.roll_deg = flip_low_bit(f.roll_deg); }},
        {"fused_altitude_m", [](TelemetryFrame& f) { f.fused_altitude_m = flip_low_bit(f.fused_altitude_m); }},
        {"vertical_speed_mps", [](TelemetryFrame& f) { f.vertical_speed_mps = flip_low_bit(f.vertical_speed_mps); }},
        {"baro_altitude_validity", [](TelemetryFrame& f) { f.baro_altitude_validity = other(f.baro_altitude_validity); }},
        {"pitch_validity", [](TelemetryFrame& f) { f.pitch_validity = other(f.pitch_validity); }},
        {"roll_validity", [](TelemetryFrame& f) { f.roll_validity = other(f.roll_validity); }},
        {"fused_altitude_validity", [](TelemetryFrame& f) { f.fused_altitude_validity = other(f.fused_altitude_validity); }},
        {"vertical_speed_validity", [](TelemetryFrame& f) { f.vertical_speed_validity = other(f.vertical_speed_validity); }},
        {"baro_status", [](TelemetryFrame& f) { f.baro_status = other(f.baro_status); }},
        {"imu_status", [](TelemetryFrame& f) { f.imu_status = other(f.imu_status); }},
        {"gps_status", [](TelemetryFrame& f) { f.gps_status = other(f.gps_status); }},
    };
}

// Field indexes into compared_fields() the tests below corrupt by name.
constexpr std::size_t kBaroAltitude = 0;
constexpr std::size_t kPitch = 1;
constexpr std::size_t kFusedAltitude = 3;

TelemetryFrame corrupted(const TelemetryFrame& healthy, std::size_t field) {
    TelemetryFrame out = healthy;
    compared_fields()[field].change(out);
    return out;
}

// One cycle's input to the voter: the frame the detector checked, and a
// healthy lane's output for it.
struct Cycle {
    TelemetryFrame checked;
    TelemetryFrame healthy;
};

// The seed-42 simulator through one detector and one processor: the
// cycles a healthy lane sees, one after another.
class Stream {
public:
    Cycle next() {
        Cycle cycle;
        cycle.checked = detector_.check(source_.read_frame());
        cycle.healthy = processor_.process(cycle.checked);
        return cycle;
    }

private:
    SimulatedSource source_{kDtMs, 42, kDtMs};
    FaultDetector detector_;
    TelemetryProcessor processor_;
};

std::array<TelemetryFrame, kLaneCount> three(const TelemetryFrame& frame) {
    return {frame, frame, frame};
}

// Three lanes that pairwise disagree: each healthy output changed in a
// different field.
std::array<TelemetryFrame, kLaneCount> pairwise_different(const TelemetryFrame& healthy) {
    return {corrupted(healthy, kFusedAltitude), corrupted(healthy, kPitch), corrupted(healthy, kBaroAltitude)};
}

// The voter's output against what one healthy lane would have emitted:
// every byte equal but the voter's status.
void expect_single_lane_output(const TelemetryFrame& out, const TelemetryFrame& healthy, const char* where) {
    TelemetryFrame copy = out;
    copy.voter_status = healthy.voter_status;
    EXPECT_EQ(std::memcmp(&copy, &healthy, sizeof copy), 0) << where;
}

void expect_float_bits(float actual, float expected, const char* what) {
    uint32_t a = 0;
    uint32_t e = 0;
    std::memcpy(&a, &actual, sizeof a);
    std::memcpy(&e, &expected, sizeof e);
    EXPECT_EQ(a, e) << what;
}

using EventSpec = std::pair<FaultType, float>;

// The events since the last call, as (type, value) in order, each on the
// voter's channel and stamped with the cycle's timestamp.
void expect_events(Voter& voter, const std::vector<EventSpec>& expected, uint64_t timestamp_ms, const char* where) {
    const std::vector<FaultEvent> events = voter.take_events();
    ASSERT_EQ(events.size(), expected.size()) << where;
    for (std::size_t i = 0; i < events.size(); i++) {
        EXPECT_EQ(events[i].channel, Channel::VOTER) << where << " event " << i;
        EXPECT_EQ(events[i].type, expected[i].first) << where << " event " << i;
        EXPECT_EQ(events[i].value, expected[i].second) << where << " event " << i;
        EXPECT_EQ(events[i].timestamp_ms, timestamp_ms) << where << " event " << i;
    }
}

constexpr uint16_t kVoted = 0x0001;
constexpr uint16_t kNoMajority = 0x0010;

uint16_t lane_faulted_bit(int lane) {   // lane 1 to 3
    return static_cast<uint16_t>(0x0002 << (lane - 1));
}

// Excludes lane `lane` (1 to 3) with three corrupted cycles, checking the
// events on the way.
void exclude(Voter& voter, Stream& stream, int lane) {
    const std::size_t index = static_cast<std::size_t>(lane - 1);
    for (int i = 0; i < Voter::kLanePersistence; i++) {
        const Cycle c = stream.next();
        std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
        lanes[index] = corrupted(c.healthy, kFusedAltitude);
        voter.vote(c.checked, lanes);
    }
    voter.take_events();
}

}  // namespace

// TC-032 step 1: two lanes given the same input agree.
TEST(LanesAgreeTest, IdenticalFramesAgree) {
    const TelemetryFrame a = lane_output();
    const TelemetryFrame b = lane_output();
    EXPECT_TRUE(lanes_agree(a, b));
    EXPECT_TRUE(lanes_agree(a, a));
}

// TC-032 step 1: one bit in any of the thirteen compared fields breaks
// agreement, in either order (REQ-RED-002: bit for bit, no tolerance).
TEST(LanesAgreeTest, EveryComparedFieldBreaksAgreement) {
    const TelemetryFrame a = lane_output();
    const std::vector<FieldChange> fields = compared_fields();
    ASSERT_EQ(fields.size(), 13u);
    for (const FieldChange& field : fields) {
        TelemetryFrame b = a;
        field.change(b);
        EXPECT_NE(std::memcmp(&a, &b, sizeof a), 0) << field.name << " did not change";
        EXPECT_FALSE(lanes_agree(a, b)) << field.name;
        EXPECT_FALSE(lanes_agree(b, a)) << field.name;
    }
}

// TC-032 step 1: the fields the voter takes from the checked frame, not
// from a lane, take no part: the raw readings, the timestamp and sequence
// (the overrun's check, REQ-RED-006), the flags, the tail, the voter's own
// status.
TEST(LanesAgreeTest, UncomparedFieldsDoNotTakePart) {
    const TelemetryFrame a = lane_output();
    TelemetryFrame b = a;
    b.timestamp_ms += 20;
    b.sequence += 1;
    b.pressure_hpa += 1.0f;
    b.temperature_c += 1.0f;
    b.accel_x += 1.0f;
    b.gyro_z += 1.0f;
    b.latitude += 0.001;
    b.longitude += 0.001;
    b.gps_altitude_m += 1.0f;
    b.ground_speed_mps += 1.0f;
    b.course_deg += 1.0f;
    b.baro_read_ok = !b.baro_read_ok;
    b.imu_read_ok = !b.imu_read_ok;
    b.gps_read_ok = !b.gps_read_ok;
    b.flags = 0xFF;
    b.gps_vertical_rate_mps = 3.0f;
    b.gps_pressure_altitude_m = 300.0f;
    b.gps_fix_quality = 2;
    b.gps_satellites = 12;
    b.voter_status = 0x0011;
    b.reserved_d = 7;
    EXPECT_TRUE(lanes_agree(a, b));
}

// TC-032 step 2: -0.0 == +0.0 under ==, but their bits differ, and
// identical lanes cannot produce the two; the voter compares bits.
TEST(LanesAgreeTest, NegativeZeroDisagreesWithZero) {
    TelemetryFrame a = lane_output();
    TelemetryFrame b = a;
    a.vertical_speed_mps = 0.0f;
    b.vertical_speed_mps = -0.0f;
    ASSERT_TRUE(a.vertical_speed_mps == b.vertical_speed_mps);   // what == would have said
    EXPECT_FALSE(lanes_agree(a, b));
}

// TC-033 step 4's comparison half: a NaN agrees with nothing, not even the
// same NaN bits, so two lanes holding one cannot form a majority; a number
// beside a NaN disagrees in either order. The processor never emits NaN
// (REQ-PROC-007), so a NaN is a fault wherever it appears.
TEST(LanesAgreeTest, NaNAgreesWithNothing) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    TelemetryFrame a = lane_output();
    TelemetryFrame b = a;
    a.pitch_deg = nan;
    b.pitch_deg = nan;
    ASSERT_EQ(std::memcmp(&a, &b, sizeof a), 0);   // the same bits
    EXPECT_FALSE(lanes_agree(a, b));
    const TelemetryFrame number = lane_output();
    EXPECT_FALSE(lanes_agree(a, number));
    EXPECT_FALSE(lanes_agree(number, a));
}

// TC-032 step 3: three identical lanes are voted, frame after frame, with
// no event; the output is the single lane's in every byte but VOTED.
TEST(VoterTest, HealthyLanesVoteWithNoEvent) {
    Stream stream;
    Voter voter;
    for (int i = 0; i < 10; i++) {
        const Cycle c = stream.next();
        const TelemetryFrame out = voter.vote(c.checked, three(c.healthy));
        EXPECT_EQ(out.voter_status, kVoted) << "frame " << i;
        expect_single_lane_output(out, c.healthy, "healthy");
        EXPECT_TRUE(voter.take_events().empty()) << "frame " << i;
    }
}

// REQ-RED-002: the output is the checked frame carrying the majority's
// computed fields and validities. Lanes that agree with each other on raw
// fields, statuses or the tail that differ from the checked frame (only an
// altered output could) change nothing of them: the voter never writes a
// status.
TEST(VoterTest, OutputIsTheCheckedFrameWithTheMajoritysComputedFields) {
    Stream stream;
    Voter voter;
    const Cycle c = stream.next();
    TelemetryFrame lane = c.healthy;
    lane.fused_altitude_m = 1234.5f;
    lane.fused_altitude_validity = Validity::FAILURE_WARNING;
    lane.pressure_hpa += 5.0f;
    lane.baro_status = ChannelStatus::DEGRADED;
    lane.flags = 0x0F;
    lane.gps_fix_quality = 9;
    const TelemetryFrame out = voter.vote(c.checked, three(lane));
    EXPECT_EQ(out.fused_altitude_m, 1234.5f);
    EXPECT_EQ(out.fused_altitude_validity, Validity::FAILURE_WARNING);
    EXPECT_EQ(out.pressure_hpa, c.checked.pressure_hpa);
    EXPECT_EQ(out.baro_status, c.checked.baro_status);
    EXPECT_EQ(out.flags, c.checked.flags);
    EXPECT_EQ(out.gps_fix_quality, c.checked.gps_fix_quality);
    EXPECT_EQ(out.voter_status, kVoted);
}

// TC-032 step 4: one wrong lane, any lane, any of the thirteen fields, on
// one frame among healthy ones: outvoted, the output the healthy lane's,
// one onset event naming the lane, and back to no event the next frame.
TEST(VoterTest, OneBadLaneIsOutvotedInEveryField) {
    for (std::size_t lane = 0; lane < kLaneCount; lane++) {
        for (std::size_t field = 0; field < compared_fields().size(); field++) {
            Stream stream;
            Voter voter;
            for (int i = 0; i < 5; i++) {
                const Cycle c = stream.next();
                voter.vote(c.checked, three(c.healthy));
            }
            const Cycle bad = stream.next();
            std::array<TelemetryFrame, kLaneCount> lanes = three(bad.healthy);
            lanes[lane] = corrupted(bad.healthy, field);
            const TelemetryFrame out = voter.vote(bad.checked, lanes);
            const char* name = compared_fields()[field].name;
            expect_single_lane_output(out, bad.healthy, name);
            EXPECT_EQ(out.voter_status, kVoted) << name;
            expect_events(voter, {{FaultType::LANE_DISAGREEMENT, static_cast<float>(lane + 1)}}, bad.checked.timestamp_ms, name);

            const Cycle after = stream.next();
            EXPECT_EQ(voter.vote(after.checked, three(after.healthy)).voter_status, kVoted) << name;
            EXPECT_TRUE(voter.take_events().empty()) << name;
        }
    }
}

// TC-032 step 5: two misses, an agreeing frame, two more: the count starts
// again at the agreement, so the lane is never excluded; each run of
// misses logs its onset.
TEST(VoterTest, TransientDisagreementResetsTheCount) {
    Stream stream;
    Voter voter;
    const std::array<bool, 5> wrong{true, true, false, true, true};
    for (std::size_t i = 0; i < wrong.size(); i++) {
        const Cycle c = stream.next();
        std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
        if (wrong[i]) {
            lanes[1] = corrupted(c.healthy, kPitch);
        }
        const TelemetryFrame out = voter.vote(c.checked, lanes);
        EXPECT_EQ(out.voter_status, kVoted) << "frame " << i;
        const std::vector<FaultEvent> events = voter.take_events();
        const bool onset = (i == 0 || i == 3);
        ASSERT_EQ(events.size(), onset ? 1u : 0u) << "frame " << i;
        if (onset) {
            EXPECT_EQ(events[0].type, FaultType::LANE_DISAGREEMENT);
            EXPECT_EQ(events[0].value, 2.0f);
        }
    }
}

// TC-032 step 6: three consecutive misses exclude the lane on the third,
// which still shows the vote it was outside of; every later frame names
// it, with no further event.
TEST(VoterTest, ThreeConsecutiveDisagreementsFaultTheLane) {
    Stream stream;
    Voter voter;
    for (int i = 0; i < 3; i++) {
        const Cycle c = stream.next();
        std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
        lanes[2] = corrupted(c.healthy, kFusedAltitude);
        const TelemetryFrame out = voter.vote(c.checked, lanes);
        EXPECT_EQ(out.voter_status, kVoted) << "frame " << i;
        expect_single_lane_output(out, c.healthy, "while counting");
        std::vector<EventSpec> expected;
        if (i == 0) {
            expected.emplace_back(FaultType::LANE_DISAGREEMENT, 3.0f);
        }
        if (i == 2) {
            expected.emplace_back(FaultType::LANE_FAULTED, 3.0f);
        }
        expect_events(voter, expected, c.checked.timestamp_ms, "counting");
    }
    for (int i = 0; i < 100; i++) {
        const Cycle c = stream.next();
        std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
        lanes[2] = corrupted(c.healthy, kFusedAltitude);
        const TelemetryFrame out = voter.vote(c.checked, lanes);
        EXPECT_EQ(out.voter_status, kVoted | lane_faulted_bit(3)) << "frame " << i;
        expect_single_lane_output(out, c.healthy, "after the exclusion");
    }
    EXPECT_TRUE(voter.take_events().empty());
}

// REQ-RED-003: excluded until restart. A lane that agrees again is not
// readmitted, and an excluded lane's disagreement logs nothing.
TEST(VoterTest, FaultedLaneStaysOutUntilRestart) {
    Stream stream;
    Voter voter;
    exclude(voter, stream, 3);
    for (int i = 0; i < 100; i++) {
        const Cycle c = stream.next();
        EXPECT_EQ(voter.vote(c.checked, three(c.healthy)).voter_status, kVoted | lane_faulted_bit(3)) << "frame " << i;
    }
    const Cycle c = stream.next();
    std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
    lanes[2] = corrupted(c.healthy, kPitch);
    EXPECT_EQ(voter.vote(c.checked, lanes).voter_status, kVoted | lane_faulted_bit(3));
    EXPECT_TRUE(voter.take_events().empty());
}

// TC-032 step 7: the persistence count is the constructor's, so the
// harness can sweep it: 1 excludes on the first miss, the onset and the
// exclusion on that frame in that order; 5 excludes on the fifth.
TEST(VoterTest, PersistenceIsAParameter) {
    {
        Stream stream;
        Voter voter(1);
        const Cycle c = stream.next();
        std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
        lanes[0] = corrupted(c.healthy, kPitch);
        EXPECT_EQ(voter.vote(c.checked, lanes).voter_status, kVoted);
        expect_events(voter, {{FaultType::LANE_DISAGREEMENT, 1.0f}, {FaultType::LANE_FAULTED, 1.0f}},
                      c.checked.timestamp_ms, "persistence 1");
        const Cycle next = stream.next();
        EXPECT_EQ(voter.vote(next.checked, three(next.healthy)).voter_status, kVoted | lane_faulted_bit(1));
    }
    {
        Stream stream;
        Voter voter(5);
        for (int i = 0; i < 5; i++) {
            const Cycle c = stream.next();
            std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
            lanes[1] = corrupted(c.healthy, kPitch);
            voter.vote(c.checked, lanes);
            const std::vector<FaultEvent> events = voter.take_events();
            const bool excluded = std::any_of(events.begin(), events.end(),
                                              [](const FaultEvent& e) { return e.type == FaultType::LANE_FAULTED; });
            EXPECT_EQ(excluded, i == 4) << "miss " << i + 1;
        }
        const Cycle next = stream.next();
        EXPECT_EQ(voter.vote(next.checked, three(next.healthy)).voter_status, kVoted | lane_faulted_bit(2));
    }
}

// TC-033 step 1, the first half of REQ-RED-004: with one lane excluded the
// two left carry on while they agree, whatever the excluded one says.
TEST(VoterTest, TwoAgreeingLanesContinue) {
    Stream stream;
    Voter voter;
    exclude(voter, stream, 3);
    for (int i = 0; i < 5; i++) {
        const Cycle c = stream.next();
        std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
        lanes[2] = corrupted(c.healthy, kBaroAltitude);
        const TelemetryFrame out = voter.vote(c.checked, lanes);
        EXPECT_EQ(out.voter_status, kVoted | lane_faulted_bit(3));
        expect_single_lane_output(out, c.healthy, "two lanes");
    }
    EXPECT_TRUE(voter.take_events().empty());
}

// TC-033 step 1, the second half: the two left disagree, so no majority:
// the frame holds the last voted values under FAILURE_WARNING, keeps the
// detector's statuses and raw fields, logs one event naming the two lanes
// in the vote, and carries on; agreement again votes again, silently.
TEST(VoterTest, TwoDisagreeingLanesHaveNoMajority) {
    Stream stream;
    Voter voter;
    exclude(voter, stream, 3);
    TelemetryFrame last_voted{};
    for (int i = 0; i < 5; i++) {
        const Cycle c = stream.next();
        last_voted = voter.vote(c.checked, three(c.healthy));
    }
    ASSERT_EQ(last_voted.fused_altitude_validity, Validity::NORMAL);
    for (int i = 0; i < 5; i++) {
        const Cycle c = stream.next();
        std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
        lanes[0] = corrupted(c.healthy, kFusedAltitude);
        lanes[1] = corrupted(c.healthy, kPitch);
        const TelemetryFrame out = voter.vote(c.checked, lanes);
        EXPECT_EQ(out.voter_status, kVoted | kNoMajority | lane_faulted_bit(3)) << "frame " << i;
        expect_float_bits(out.fused_altitude_m, last_voted.fused_altitude_m, "fused altitude held");
        expect_float_bits(out.pitch_deg, last_voted.pitch_deg, "pitch held");
        expect_float_bits(out.baro_altitude_m, last_voted.baro_altitude_m, "barometric altitude held");
        expect_float_bits(out.roll_deg, last_voted.roll_deg, "roll held");
        expect_float_bits(out.vertical_speed_mps, last_voted.vertical_speed_mps, "vertical speed held");
        EXPECT_EQ(out.fused_altitude_validity, Validity::FAILURE_WARNING);
        EXPECT_EQ(out.vertical_speed_validity, Validity::FAILURE_WARNING);
        EXPECT_EQ(out.pitch_validity, Validity::FAILURE_WARNING);
        EXPECT_EQ(out.roll_validity, Validity::FAILURE_WARNING);
        EXPECT_EQ(out.baro_altitude_validity, Validity::FAILURE_WARNING);
        EXPECT_EQ(out.baro_status, c.checked.baro_status);
        EXPECT_EQ(out.pressure_hpa, c.checked.pressure_hpa);
        EXPECT_EQ(out.timestamp_ms, c.checked.timestamp_ms);
        if (i == 0) {
            expect_events(voter, {{FaultType::NO_MAJORITY, 2.0f}}, c.checked.timestamp_ms, "no majority begins");
        } else {
            EXPECT_TRUE(voter.take_events().empty()) << "frame " << i;
        }
    }
    for (int i = 0; i < 5; i++) {
        const Cycle c = stream.next();
        const TelemetryFrame out = voter.vote(c.checked, three(c.healthy));
        EXPECT_EQ(out.voter_status, kVoted | lane_faulted_bit(3));
        expect_single_lane_output(out, c.healthy, "voting again");
    }
    EXPECT_TRUE(voter.take_events().empty());
}

// TC-033 step 2: no two of three agree on the very first frame, before any
// vote: every computed field 0 and NO_COMPUTED_DATA, never a flag over a
// number nobody voted; one event naming three lanes.
TEST(VoterTest, NoMajorityBeforeAnyVoteIsNoComputedData) {
    Stream stream;
    Voter voter;
    const Cycle c = stream.next();
    const TelemetryFrame out = voter.vote(c.checked, pairwise_different(c.healthy));
    EXPECT_EQ(out.voter_status, kVoted | kNoMajority);
    EXPECT_EQ(out.baro_altitude_m, 0.0f);
    EXPECT_EQ(out.pitch_deg, 0.0f);
    EXPECT_EQ(out.roll_deg, 0.0f);
    EXPECT_EQ(out.fused_altitude_m, 0.0f);
    EXPECT_EQ(out.vertical_speed_mps, 0.0f);
    EXPECT_EQ(out.baro_altitude_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.pitch_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.roll_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.fused_altitude_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.vertical_speed_validity, Validity::NO_COMPUTED_DATA);
    expect_events(voter, {{FaultType::NO_MAJORITY, 3.0f}}, c.checked.timestamp_ms, "first frame");
}

// TC-033 step 2, after votes: three lanes pairwise different hold the last
// vote under FAILURE_WARNING.
TEST(VoterTest, NoMajorityAmongThreeLanes) {
    Stream stream;
    Voter voter;
    TelemetryFrame last_voted{};
    for (int i = 0; i < 10; i++) {
        const Cycle c = stream.next();
        last_voted = voter.vote(c.checked, three(c.healthy));
    }
    const Cycle c = stream.next();
    const TelemetryFrame out = voter.vote(c.checked, pairwise_different(c.healthy));
    EXPECT_EQ(out.voter_status, kVoted | kNoMajority);
    expect_float_bits(out.fused_altitude_m, last_voted.fused_altitude_m, "held");
    EXPECT_EQ(out.fused_altitude_validity, Validity::FAILURE_WARNING);
    expect_events(voter, {{FaultType::NO_MAJORITY, 3.0f}}, c.checked.timestamp_ms, "after votes");
}

// TC-033 step 3: a field the last vote held as NO_COMPUTED_DATA stays so
// under no majority (REQ-PROC-007's precedence: the ADS-B source's absent
// IMU keeps its attitude dashes); the others degrade to FAILURE_WARNING.
TEST(VoterTest, NoMajorityKeepsNoComputedDataWhereNothingWasVoted) {
    Stream stream;
    Voter voter;
    const Cycle voted = stream.next();
    TelemetryFrame lane = voted.healthy;
    lane.pitch_deg = 0.0f;
    lane.roll_deg = 0.0f;
    lane.pitch_validity = Validity::NO_COMPUTED_DATA;
    lane.roll_validity = Validity::NO_COMPUTED_DATA;
    voter.vote(voted.checked, three(lane));
    const Cycle c = stream.next();
    const TelemetryFrame out = voter.vote(c.checked, pairwise_different(c.healthy));
    EXPECT_EQ(out.pitch_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.roll_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.pitch_deg, 0.0f);
    EXPECT_EQ(out.fused_altitude_validity, Validity::FAILURE_WARNING);
    EXPECT_EQ(out.baro_altitude_validity, Validity::FAILURE_WARNING);
}

// TC-033 step 3: a majority's verdict passes through unchanged; two lanes
// agreeing on FAILURE_WARNING give FAILURE_WARNING, not the dashes the
// format-version-2 rule (fewer than two NORMAL lanes) would have shown.
TEST(VoterTest, AgreedFailureWarningStaysFailureWarning) {
    Stream stream;
    Voter voter;
    const Cycle c = stream.next();
    TelemetryFrame lane = c.healthy;
    lane.fused_altitude_validity = Validity::FAILURE_WARNING;
    lane.vertical_speed_validity = Validity::FAILURE_WARNING;
    std::array<TelemetryFrame, kLaneCount> lanes = three(lane);
    lanes[2] = corrupted(lane, kPitch);
    const TelemetryFrame out = voter.vote(c.checked, lanes);
    EXPECT_EQ(out.fused_altitude_validity, Validity::FAILURE_WARNING);
    EXPECT_EQ(out.vertical_speed_validity, Validity::FAILURE_WARNING);
    EXPECT_EQ(out.voter_status, kVoted);
}

// TC-033 step 5: no count moves without a majority. Two misses, five
// no-majority frames, then one more miss beside a majority: that is the
// third, and the lane is excluded, with no second onset.
TEST(VoterTest, NoMajorityHoldsTheCounters) {
    Stream stream;
    Voter voter;
    for (int i = 0; i < 2; i++) {
        const Cycle c = stream.next();
        std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
        lanes[1] = corrupted(c.healthy, kPitch);
        voter.vote(c.checked, lanes);
    }
    for (int i = 0; i < 5; i++) {
        const Cycle c = stream.next();
        voter.vote(c.checked, pairwise_different(c.healthy));
    }
    const Cycle c = stream.next();
    std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
    lanes[1] = corrupted(c.healthy, kPitch);
    voter.vote(c.checked, lanes);
    const std::vector<FaultEvent> events = voter.take_events();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].type, FaultType::LANE_DISAGREEMENT);
    EXPECT_EQ(events[1].type, FaultType::NO_MAJORITY);
    EXPECT_EQ(events[2].type, FaultType::LANE_FAULTED);
    EXPECT_EQ(events[2].value, 2.0f);
    EXPECT_EQ(events[2].timestamp_ms, c.checked.timestamp_ms);
}

// One NO_MAJORITY event per run of no-majority frames, not per frame: two
// runs with a vote between them log two.
TEST(VoterTest, NoMajorityEventOncePerRun) {
    Stream stream;
    Voter voter;
    const std::array<bool, 7> split{true, true, true, false, true, true, false};
    int events = 0;
    for (bool no_majority : split) {
        const Cycle c = stream.next();
        voter.vote(c.checked, no_majority ? pairwise_different(c.healthy) : three(c.healthy));
        events += static_cast<int>(voter.take_events().size());
    }
    EXPECT_EQ(events, 2);
}

// TC-033 step 7: corruptions scripted on every lane in turn, singly and in
// pairs, for 200 frames: at most one lane is ever excluded, so the vote
// never has fewer than two lanes.
TEST(VoterTest, NeverBelowTwoLanes) {
    Stream stream;
    Voter voter;
    for (int i = 0; i < 200; i++) {
        const Cycle c = stream.next();
        std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
        const std::size_t first = static_cast<std::size_t>(i / 7) % kLaneCount;
        lanes[first] = corrupted(c.healthy, static_cast<std::size_t>(i) % 13);
        if (i % 3 == 0) {
            lanes[(first + 1) % kLaneCount] = corrupted(c.healthy, static_cast<std::size_t>(i + 5) % 13);
        }
        const TelemetryFrame out = voter.vote(c.checked, lanes);
        const std::bitset<16> excluded(out.voter_status & 0x000E);
        EXPECT_LE(excluded.count(), 1u) << "frame " << i;
    }
}

// TC-033 step 4: a lane holding a NaN is outvoted like any wrong lane.
TEST(VoterTest, NaNLaneIsOutvoted) {
    Stream stream;
    Voter voter;
    const Cycle c = stream.next();
    std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
    lanes[0].pitch_deg = std::numeric_limits<float>::quiet_NaN();
    const TelemetryFrame out = voter.vote(c.checked, lanes);
    expect_single_lane_output(out, c.healthy, "NaN lane");
    expect_events(voter, {{FaultType::LANE_DISAGREEMENT, 1.0f}}, c.checked.timestamp_ms, "NaN lane");
}

// TC-033 step 4: two lanes holding the same NaN are two faults, not a
// majority: no majority, and the frame holds no NaN.
TEST(VoterTest, TwoNaNLanesNeverFormAMajority) {
    Stream stream;
    Voter voter;
    const Cycle voted = stream.next();
    voter.vote(voted.checked, three(voted.healthy));
    const Cycle c = stream.next();
    std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
    lanes[0].pitch_deg = std::numeric_limits<float>::quiet_NaN();
    lanes[1].pitch_deg = std::numeric_limits<float>::quiet_NaN();
    const TelemetryFrame out = voter.vote(c.checked, lanes);
    EXPECT_EQ(out.voter_status, kVoted | kNoMajority);
    EXPECT_FALSE(std::isnan(out.pitch_deg));
    EXPECT_EQ(out.pitch_validity, Validity::FAILURE_WARNING);
}

// TC-032 and TC-033, the voter's status in each case, as format version 3
// defines its bits (telemetry_frame.h).
TEST(VoterTest, VoterStatusBitsInEveryCase) {
    struct Case {
        const char* name;
        int excluded;            // 0 for none, else the lane 1 to 3
        bool no_majority;
        uint16_t expected;
    };
    const std::array<Case, 6> cases{{
        {"healthy", 0, false, 0x0001},
        {"lane 1 excluded", 1, false, 0x0003},
        {"lane 2 excluded", 2, false, 0x0005},
        {"lane 3 excluded", 3, false, 0x0009},
        {"no majority of three", 0, true, 0x0011},
        {"lane 2 excluded, no majority of two", 2, true, 0x0015},
    }};
    for (const Case& test : cases) {
        Stream stream;
        Voter voter;
        if (test.excluded != 0) {
            exclude(voter, stream, test.excluded);
        }
        const Cycle c = stream.next();
        const std::array<TelemetryFrame, kLaneCount> lanes =
            test.no_majority ? pairwise_different(c.healthy) : three(c.healthy);
        EXPECT_EQ(voter.vote(c.checked, lanes).voter_status, test.expected) << test.name;
    }
}

// Every voter event is value-initialized, then assigned, as the detector's
// are: the reserved bytes are zero, so the log's bytes are a function of
// the fields alone (design.md section 9).
TEST(VoterTest, EventsAreValueInitialized) {
    Stream stream;
    Voter voter(1);
    const Cycle c = stream.next();
    std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
    lanes[2] = corrupted(c.healthy, kPitch);
    voter.vote(c.checked, lanes);                                   // onset and exclusion
    const Cycle d = stream.next();
    lanes = three(d.healthy);
    lanes[0] = corrupted(d.healthy, kFusedAltitude);
    voter.vote(d.checked, lanes);                                   // no majority of two
    const std::vector<FaultEvent> events = voter.take_events();
    ASSERT_EQ(events.size(), 3u);
    for (const FaultEvent& event : events) {
        EXPECT_EQ(event.channel, Channel::VOTER);
        EXPECT_EQ(event.reserved, 0u);
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&event);
        for (std::size_t i = offsetof(FaultEvent, reserved); i < offsetof(FaultEvent, value); i++) {
            EXPECT_EQ(bytes[i], 0) << "byte " << i;
        }
    }
}

// TC-032 step 8: a lane handing back the previous cycle's output has
// overrun: it takes no part in the vote, so the output is the current
// lanes', and its onset is LANE_OVERRUN. A stale timestamp alone and a
// stale sequence alone are each enough; frames of a version-1 or version-2
// log carry sequence 0 throughout, and the timestamp still tells them apart.
TEST(VoterTest, StaleLaneIsAnOverrun) {
    struct Case {
        const char* name;
        bool old_timestamp;
        bool old_sequence;
    };
    const std::array<Case, 3> cases{{{"previous output", true, true}, {"timestamp only", true, false},
                                     {"sequence only", false, true}}};
    for (const Case& test : cases) {
        Stream stream;
        Voter voter;
        const Cycle previous = stream.next();
        voter.vote(previous.checked, three(previous.healthy));
        const Cycle c = stream.next();
        std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
        if (test.old_timestamp) {
            lanes[1].timestamp_ms = previous.healthy.timestamp_ms;
        }
        if (test.old_sequence) {
            lanes[1].sequence = previous.healthy.sequence;
        }
        const TelemetryFrame out = voter.vote(c.checked, lanes);
        expect_single_lane_output(out, c.healthy, test.name);
        EXPECT_EQ(out.voter_status, kVoted) << test.name;
        expect_events(voter, {{FaultType::LANE_OVERRUN, 2.0f}}, c.checked.timestamp_ms, test.name);
    }
}

// TC-032 step 8: two lanes that both handed back the previous output agree
// with each other, but neither is eligible, so they never outvote the one
// current lane: no majority, and no onset, since no count moves without a
// majority.
TEST(VoterTest, TwoStaleLanesNeverOutvoteACurrentOne) {
    Stream stream;
    Voter voter;
    const Cycle previous = stream.next();
    voter.vote(previous.checked, three(previous.healthy));
    const Cycle c = stream.next();
    const std::array<TelemetryFrame, kLaneCount> lanes{previous.healthy, previous.healthy, c.healthy};
    const TelemetryFrame out = voter.vote(c.checked, lanes);
    EXPECT_EQ(out.voter_status, kVoted | kNoMajority);
    expect_float_bits(out.fused_altitude_m, previous.healthy.fused_altitude_m, "the last vote held");
    expect_events(voter, {{FaultType::NO_MAJORITY, 3.0f}}, c.checked.timestamp_ms, "two stale lanes");
}

// TC-032 step 8, the reviewer's case: stale on one cycle and disagreeing on
// the next two is three consecutive misses, excluded on the third, with one
// onset, the overrun's.
TEST(VoterTest, StaleThenDisagreeingCountsOnce) {
    Stream stream;
    Voter voter;
    const Cycle previous = stream.next();
    voter.vote(previous.checked, three(previous.healthy));
    const Cycle first = stream.next();
    std::array<TelemetryFrame, kLaneCount> lanes = three(first.healthy);
    lanes[2] = previous.healthy;
    voter.vote(first.checked, lanes);
    expect_events(voter, {{FaultType::LANE_OVERRUN, 3.0f}}, first.checked.timestamp_ms, "stale");
    for (int i = 0; i < 2; i++) {
        const Cycle c = stream.next();
        lanes = three(c.healthy);
        lanes[2] = corrupted(c.healthy, kPitch);
        voter.vote(c.checked, lanes);
        std::vector<EventSpec> expected;
        if (i == 1) {
            expected.emplace_back(FaultType::LANE_FAULTED, 3.0f);
        }
        expect_events(voter, expected, c.checked.timestamp_ms, "disagreeing");
    }
    const Cycle after = stream.next();
    EXPECT_EQ(voter.vote(after.checked, three(after.healthy)).voter_status, kVoted | lane_faulted_bit(3));
}

// TC-033 step 6 (REQ-PROC-008): on a frame flagged NEW_VEHICLE the held
// vote is the previous vehicle's and goes, so a no-majority frame then
// reads 0 and NO_COMPUTED_DATA; the exclusion stays. A flagged frame with
// a majority votes as any other.
TEST(VoterTest, NewVehicleClearsHeldValuesNotLatches) {
    Stream stream;
    Voter voter;
    exclude(voter, stream, 3);
    for (int i = 0; i < 5; i++) {
        const Cycle c = stream.next();
        voter.vote(c.checked, three(c.healthy));
    }
    Cycle c = stream.next();
    c.checked.flags = static_cast<uint8_t>(c.checked.flags | static_cast<uint8_t>(FrameFlag::NEW_VEHICLE));
    std::array<TelemetryFrame, kLaneCount> lanes = three(c.healthy);
    lanes[0] = corrupted(c.healthy, kFusedAltitude);
    lanes[1] = corrupted(c.healthy, kPitch);
    const TelemetryFrame out = voter.vote(c.checked, lanes);
    EXPECT_EQ(out.voter_status, kVoted | kNoMajority | lane_faulted_bit(3));
    EXPECT_EQ(out.baro_altitude_m, 0.0f);
    EXPECT_EQ(out.pitch_deg, 0.0f);
    EXPECT_EQ(out.roll_deg, 0.0f);
    EXPECT_EQ(out.fused_altitude_m, 0.0f);
    EXPECT_EQ(out.vertical_speed_mps, 0.0f);
    EXPECT_EQ(out.baro_altitude_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.pitch_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.roll_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.fused_altitude_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.vertical_speed_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_TRUE(has_flag(out, FrameFlag::NEW_VEHICLE));

    Cycle d = stream.next();
    d.checked.flags = static_cast<uint8_t>(d.checked.flags | static_cast<uint8_t>(FrameFlag::NEW_VEHICLE));
    const TelemetryFrame voted = voter.vote(d.checked, three(d.healthy));
    EXPECT_EQ(voted.voter_status, kVoted | lane_faulted_bit(3));
    EXPECT_EQ(voted.fused_altitude_m, d.healthy.fused_altitude_m);
}

// The exit line (REQ-RED-001): the frames counted, the lanes the last frame
// left in the vote, the no-majority frames, the voter's events; a run of
// no frames says so and no more.
TEST(VoterTest, VoterTallyCounts) {
    VoterTally empty;
    EXPECT_EQ(empty.summary(), "Voter:     0 frames");

    VoterTally healthy;
    TelemetryFrame frame{};
    frame.voter_status = 0x0001;
    for (int i = 0; i < 500; i++) {
        healthy.account(frame);
    }
    EXPECT_EQ(healthy.summary(), "Voter:     500 frames, lanes in the vote 1 2 3, no majority on 0 frames, voter events 0");

    VoterTally faulted;
    frame.voter_status = 0x0001;
    faulted.account(frame);
    frame.voter_status = 0x0015;
    faulted.account(frame);
    faulted.account(frame);
    frame.voter_status = 0x0005;
    faulted.account(frame);
    faulted.account_events(2);
    faulted.account_events(1);
    EXPECT_EQ(faulted.frames, 4);
    EXPECT_EQ(faulted.no_majority_frames, 2);
    EXPECT_EQ(faulted.summary(), "Voter:     4 frames, lanes in the vote 1 3, no majority on 2 frames, voter events 3");
}
