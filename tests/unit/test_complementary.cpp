// Unit tests for src/processing/complementary_filter.cpp and its use in
// TelemetryProcessor (REQ-PROC-002). Test card: TC-006 in docs/test_plan.md.
// Expected values are closed-form (a tilt angle, an integrated rate, a
// steady-state bias bound), not numbers copied from a previous run.

#include <gtest/gtest.h>

#include "drivers/simulated_source.h"
#include "processing/complementary_filter.h"
#include "processing/processor.h"
#include "telemetry_frame.h"

namespace {

constexpr float kG = 9.81f;      // m/s^2
constexpr float kDt = 0.02f;     // 50 Hz

}  // namespace

// TC-006 step 1. Gravity straight down the z axis is zero tilt on both
// axes. (REQ-PROC-002)
TEST(ComplementaryTest, FlatBoardGivesZeroAngles) {
    ComplementaryFilter filter;
    filter.update(0.0f, 0.0f, kG, 0.0f, 0.0f, 0.0f, kDt);

    EXPECT_NEAR(filter.pitch_deg(), 0.0f, 0.01f);
    EXPECT_NEAR(filter.roll_deg(), 0.0f, 0.01f);
}

// TC-006 step 2. Equal gravity on x and z is a 45 degree pitch with no
// roll. (REQ-PROC-002)
TEST(ComplementaryTest, PitchFortyFiveDegrees) {
    ComplementaryFilter filter;
    filter.update(kG, 0.0f, kG, 0.0f, 0.0f, 0.0f, kDt);

    EXPECT_NEAR(filter.pitch_deg(), 45.0f, 0.1f);
    EXPECT_NEAR(filter.roll_deg(), 0.0f, 0.01f);
}

// Symmetric check: equal gravity on y and z is 45 degrees of roll. A
// swapped axis in either formula fails this while passing the pitch
// test. (REQ-PROC-002)
TEST(ComplementaryTest, RollFortyFiveDegrees) {
    ComplementaryFilter filter;
    filter.update(0.0f, kG, kG, 0.0f, 0.0f, 0.0f, kDt);

    EXPECT_NEAR(filter.roll_deg(), 45.0f, 0.1f);
    EXPECT_NEAR(filter.pitch_deg(), 0.0f, 0.01f);
}

// The first update must adopt the accelerometer angle outright, not move
// 2% of the way toward it. (REQ-PROC-002)
TEST(ComplementaryTest, FirstUpdateSeedsFromAccel) {
    ComplementaryFilter filter(0.98f);
    filter.update(kG, 0.0f, kG, 0.0f, 0.0f, 0.0f, kDt);

    EXPECT_NEAR(filter.pitch_deg(), 45.0f, 0.1f);
}

// TC-006 step 3. With alpha = 1 the accelerometer is ignored and the
// filter is a pure integrator. A steady 1 deg/s pitch-rate bias over
// 100 frames of 20 ms must read exactly 100 * 0.02 * 1 = 2 degrees,
// even though the accelerometer says the board is flat the whole time.
// (REQ-PROC-002)
TEST(ComplementaryTest, GyroOnlyDrifts) {
    ComplementaryFilter filter(1.0f);
    filter.update(0.0f, 0.0f, kG, 0.0f, 0.0f, 0.0f, kDt);   // seed flat

    for (int i = 0; i < 100; i++) {
        filter.update(0.0f, 0.0f, kG, 0.0f, 1.0f, 0.0f, kDt);
    }

    EXPECT_NEAR(filter.pitch_deg(), 2.0f, 0.01f);
    EXPECT_NEAR(filter.roll_deg(), 0.0f, 0.01f);
}

// TC-006 step 4. Same bias, alpha = 0.98. The accelerometer now pulls
// the estimate back every frame, and the error settles at the closed-form
// steady state alpha * bias * dt / (1 - alpha) = 0.98 * 1 * 0.02 / 0.02
// = 0.98 degrees instead of growing forever. 500 frames is ten time
// constants, long past settling. (REQ-PROC-002)
TEST(ComplementaryTest, AccelCorrectsGyroDrift) {
    ComplementaryFilter filter(0.98f);
    filter.update(0.0f, 0.0f, kG, 0.0f, 0.0f, 0.0f, kDt);   // seed flat

    for (int i = 0; i < 500; i++) {
        filter.update(0.0f, 0.0f, kG, 0.0f, 1.0f, 0.0f, kDt);
    }

    EXPECT_LT(filter.pitch_deg(), 1.5f);
    EXPECT_NEAR(filter.pitch_deg(), 0.98f, 0.05f);
}

// Processor-level gate. A DEGRADED IMU frame must not move the attitude,
// even when its accelerometer data says 45 degrees. Interim behavior
// until a validity marker exists (FAULT-006 wants "invalid", not "held").
// EXPECT_EQ: a hold is exact, not approximate. (REQ-PROC-002)
TEST(ComplementaryTest, DegradedImuHoldsAttitude) {
    SimulatedSource source(20, 42);
    TelemetryProcessor processor;

    const TelemetryFrame first = processor.process(source.read_frame());

    TelemetryFrame tilted = source.read_frame();
    tilted.imu_status = ChannelStatus::DEGRADED;
    tilted.accel_x = kG;
    tilted.accel_y = 0.0f;
    tilted.accel_z = kG;
    const TelemetryFrame second = processor.process(tilted);

    EXPECT_EQ(second.pitch_deg, first.pitch_deg);
    EXPECT_EQ(second.roll_deg, first.roll_deg);
}

// No usable gravity reference (free fall, or a dead sensor reporting
// zeros): the filter must coast on the gyro rather than feed atan2(0, 0)
// into the estimate. (REQ-PROC-002)
TEST(ComplementaryTest, ZeroAccelCoastsOnGyro) {
    ComplementaryFilter filter(0.98f);
    filter.update(0.0f, 0.0f, kG, 0.0f, 0.0f, 0.0f, kDt);   // seed flat
    for (int i = 0; i < 50; i++) {
        filter.update(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, kDt);   // 1 deg/s, no gravity
    }
    // Pure integration: 50 * 0.02 * 1.0 = 1.0 degree, no accel pull-back.
    EXPECT_NEAR(filter.pitch_deg(), 1.0f, 0.01f);
}

// A filter that has never seen usable gravity stays unseeded and reports
// zero until it does. (REQ-PROC-002)
TEST(ComplementaryTest, UnseededUntilAccelUsable) {
    ComplementaryFilter filter;
    filter.update(0.0f, 0.0f, 0.0f, 0.0f, 5.0f, 0.0f, kDt);
    EXPECT_EQ(filter.pitch_deg(), 0.0f);
    filter.update(kG, 0.0f, kG, 0.0f, 0.0f, 0.0f, kDt);
    EXPECT_NEAR(filter.pitch_deg(), 45.0f, 0.1f);
}
