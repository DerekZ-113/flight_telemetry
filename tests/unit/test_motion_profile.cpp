// Unit tests for src/drivers/motion_profile.cpp (REQ-SENS-007): the scripted
// flight the simulator follows under --motion. Test card: TC-026 steps 2 and
// 4 in docs/test_plan.md; the keyframes are the table in FTS-DS-001 §7, and
// every expected number below is read off that table, not off the code.

#include <gtest/gtest.h>

#include <cstdint>

#include "drivers/motion_profile.h"

namespace {

MotionProfile demo() {
    const std::optional<MotionProfile> profile = MotionProfile::named("demo");
    EXPECT_TRUE(profile.has_value());
    return *profile;
}

void expect_state(const MotionState& s, float altitude_m, float pitch_deg, float roll_deg,
                  float track_deg, float ground_speed_mps, uint64_t t_ms) {
    EXPECT_NEAR(s.altitude_m, altitude_m, 1e-3f) << "t=" << t_ms;
    EXPECT_NEAR(s.pitch_deg, pitch_deg, 1e-3f) << "t=" << t_ms;
    EXPECT_NEAR(s.roll_deg, roll_deg, 1e-3f) << "t=" << t_ms;
    EXPECT_NEAR(s.track_deg, track_deg, 1e-3f) << "t=" << t_ms;
    EXPECT_NEAR(s.ground_speed_mps, ground_speed_mps, 1e-3f) << "t=" << t_ms;
}

void expect_rates(const MotionState& s, float pitch_rate, float roll_rate, float track_rate, uint64_t t_ms) {
    EXPECT_NEAR(s.pitch_rate_dps, pitch_rate, 1e-3f) << "t=" << t_ms;
    EXPECT_NEAR(s.roll_rate_dps, roll_rate, 1e-3f) << "t=" << t_ms;
    EXPECT_NEAR(s.track_rate_dps, track_rate, 1e-3f) << "t=" << t_ms;
}

}  // namespace

// TC-026 step 2: the first five seconds are parked on the ground, heading
// east, nothing moving, so every rate is zero. A gyroscope reads the rates,
// so a parked profile must read as a still board.
TEST(MotionProfileTest, DemoStartsParked) {
    const MotionProfile profile = demo();
    for (uint64_t t : {uint64_t{0}, uint64_t{4999}}) {
        const MotionState s = profile.state_at(t);
        expect_state(s, 2.0f, 0.0f, 0.0f, 90.0f, 0.0f, t);
        expect_rates(s, 0.0f, 0.0f, 0.0f, t);
    }
}

// TC-026 step 2: linear between keyframes. At 36 s the climb (17 s to 57 s,
// 2 m to 122 m) is 19 s in, so the altitude is 2 + 120 * 19 / 40 = 59 m with
// the climb's pitch and speed; at 77 s the turn (62 s to 92 s, 090 to 270)
// is half done: track 180, roll 20, pitch 2, altitude held at 122.
TEST(MotionProfileTest, InterpolatesWithinASegment) {
    const MotionProfile profile = demo();
    expect_state(profile.state_at(36000), 59.0f, 8.0f, 0.0f, 90.0f, 30.0f, 36000);
    expect_state(profile.state_at(77000), 122.0f, 2.0f, 20.0f, 180.0f, 30.0f, 77000);
}

// TC-026 step 2: each rate is its segment's slope, what a gyroscope on the
// scripted aircraft would read. Rotation (15 s to 17 s) pitches 0 to 8 at
// 4 deg/s; rolling in (57 s to 62 s) rolls 0 to 20 at 4 deg/s while the
// pitch eases 8 to 2 at -1.2 deg/s; the turn sweeps 180 degrees of track in
// 30 s, 6 deg/s, at constant bank; a steady climb has no rate at all.
TEST(MotionProfileTest, RatesAreTheSegmentSlopes) {
    const MotionProfile profile = demo();
    expect_rates(profile.state_at(16000), 4.0f, 0.0f, 0.0f, 16000);
    expect_rates(profile.state_at(60000), -1.2f, 4.0f, 0.0f, 60000);
    expect_rates(profile.state_at(77000), 0.0f, 0.0f, 6.0f, 77000);
    expect_rates(profile.state_at(36000), 0.0f, 0.0f, 0.0f, 36000);
}

// TC-026 step 2: past the last keyframe the state holds with zero rates, so
// a run longer than the script (--cycles past 7,500) stays a still aircraft
// at the script's end rather than reading past the table.
TEST(MotionProfileTest, HoldsAfterTheLastKeyframe) {
    const MotionProfile profile = demo();
    EXPECT_EQ(profile.duration_ms(), 150000u);
    for (uint64_t t : {uint64_t{150000}, uint64_t{200000}}) {
        const MotionState s = profile.state_at(t);
        expect_state(s, 62.0f, 0.0f, 0.0f, 270.0f, 20.0f, t);
        expect_rates(s, 0.0f, 0.0f, 0.0f, t);
    }
}

// TC-026 steps 2 and 7: a profile is looked up by name, and a name nobody
// scripted is nullopt, which main turns into a refusal that lists the
// known names.
TEST(MotionProfileTest, UnknownNameIsNullopt) {
    EXPECT_FALSE(MotionProfile::named("nope").has_value());
    EXPECT_FALSE(MotionProfile::named("").has_value());
    EXPECT_EQ(demo().name(), "demo");
    EXPECT_STREQ(MotionProfile::known_names(), "demo");
}
