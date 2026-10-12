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

// ---------------------------------------------------------------------------
// REQ-PROC-006 at the processor: a reading is used only on the cycle it
// was obtained. Test card TC-011.
// ---------------------------------------------------------------------------

// TC-011 step 5. A NOMINAL IMU frame whose read failed carries the
// previous sample; here it carries 45 degrees of tilt so any use of it is
// visible. The attitude must not move. (REQ-PROC-006, REQ-PROC-002)
TEST(ComplementaryTest, StaleImuFrameHoldsAttitude) {
    SimulatedSource source(20, 42);
    TelemetryProcessor processor;
    const TelemetryFrame first = processor.process(source.read_frame());

    TelemetryFrame stale = source.read_frame();
    stale.imu_read_ok = false;
    stale.accel_x = kG;
    stale.accel_y = 0.0f;
    stale.accel_z = kG;
    const TelemetryFrame second = processor.process(stale);

    EXPECT_EQ(second.pitch_deg, first.pitch_deg);
    EXPECT_EQ(second.roll_deg, first.roll_deg);
}

// TC-011 step 6. Three stale IMU frames between two fresh ones. The fresh
// frame after the gap carries a 1 deg/s pitch rate and a flat
// accelerometer; the gyro term must integrate over the 80 ms since the
// last accepted sample, not over one 20 ms period:
// alpha * rate * gap = 0.98 * 1 * 0.080 = 0.0784 degrees.
// (REQ-PROC-006, REQ-PROC-002)
TEST(ComplementaryTest, GyroIntegratesOverStaleGap) {
    TelemetryProcessor processor;
    auto flat = [](uint64_t ts, bool fresh, float pitch_rate_dps) {
        TelemetryFrame f{};
        f.timestamp_ms = ts;
        f.pressure_hpa = 1013.0f;
        f.temperature_c = 21.0f;
        f.accel_z = kG;
        f.gyro_y = pitch_rate_dps;
        f.gps_altitude_m = 3.0f;
        f.baro_status = ChannelStatus::NOMINAL;
        f.imu_status = ChannelStatus::NOMINAL;
        f.gps_status = ChannelStatus::NOMINAL;
        f.baro_read_ok = true;
        f.imu_read_ok = fresh;
        f.gps_read_ok = true;
        return f;
    };
    processor.process(flat(0, true, 0.0f));     // seeds the attitude from the accelerometer
    processor.process(flat(20, true, 0.0f));    // first integration step, rate zero
    processor.process(flat(40, false, 0.0f));   // stale: a driver holding the last sample
    processor.process(flat(60, false, 0.0f));
    processor.process(flat(80, false, 0.0f));
    const TelemetryFrame out = processor.process(flat(100, true, 1.0f));
    EXPECT_NEAR(out.pitch_deg, 0.98f * 1.0f * 0.080f, 0.001f);
    EXPECT_NEAR(out.roll_deg, 0.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// REQ-PROC-002, second sentence: a gap longer than the IMU communication
// timeout is an outage, not rotation to account for. Test card TC-006
// steps 5 to 7. The gap rule lives in the processor, which owns the
// timestamps, so these drive TelemetryProcessor with hand-built frames.
// ---------------------------------------------------------------------------

namespace {

// A NOMINAL frame whose IMU fields are the only ones that matter here.
// `fresh` is imu_read_ok; the barometer and GPS are fresh and flat.
TelemetryFrame imu_frame(uint64_t ts, bool fresh, float pitch_rate_dps,
                         float accel_x, float accel_z) {
    TelemetryFrame f{};
    f.timestamp_ms = ts;
    f.pressure_hpa = 1013.0f;
    f.temperature_c = 21.0f;
    f.accel_x = accel_x;
    f.accel_z = accel_z;
    f.gyro_y = pitch_rate_dps;
    f.gps_altitude_m = 3.0f;
    f.baro_status = ChannelStatus::NOMINAL;
    f.imu_status = ChannelStatus::NOMINAL;
    f.gps_status = ChannelStatus::NOMINAL;
    f.baro_read_ok = true;
    f.imu_read_ok = fresh;
    f.gps_read_ok = true;
    return f;
}

// Two accepted frames, then stale frames every 20 ms through `last_stale_ms`.
void run_gap(TelemetryProcessor& processor, uint64_t last_stale_ms) {
    processor.process(imu_frame(0, true, 0.0f, 0.0f, kG));    // seeds flat
    processor.process(imu_frame(20, true, 0.0f, 0.0f, kG));   // last accepted sample
    for (uint64_t ts = 40; ts <= last_stale_ms; ts += 20) {
        processor.process(imu_frame(ts, false, 0.0f, 0.0f, kG));   // a driver holding the last sample
    }
}

}  // namespace

// TC-006 step 5. The fresh frame at 540 ms is 520 ms after the last
// accepted sample, past the 500 ms timeout. Its 5 deg/s rate must not be
// integrated over the gap (0.98 * 5 * 0.52 = 2.548 degrees of invented
// tilt); the filter re-seeds from the flat accelerometer instead.
// (REQ-PROC-002)
TEST(ComplementaryTest, LongImuGapReseedsFromAccel) {
    TelemetryProcessor processor;
    run_gap(processor, 520);
    const TelemetryFrame out = processor.process(imu_frame(540, true, 5.0f, 0.0f, kG));
    EXPECT_NEAR(out.pitch_deg, 0.0f, 0.001f);
    EXPECT_NEAR(out.roll_deg, 0.0f, 0.001f);
}

// TC-006 step 6. A gap of exactly the timeout is still rotation to
// account for: strictly greater re-seeds, equal integrates, matching the
// detector's own comparison. 0.98 * 5 deg/s * 0.5 s = 2.45 degrees.
// (REQ-PROC-002, REQ-PROC-006)
TEST(ComplementaryTest, GapEqualToTimeoutStillIntegrates) {
    TelemetryProcessor processor;
    run_gap(processor, 500);
    const TelemetryFrame out = processor.process(imu_frame(520, true, 5.0f, 0.0f, kG));
    EXPECT_NEAR(out.pitch_deg, 2.45f, 0.001f);
}

// TC-006 step 7. The board was moved during the outage: the recovery
// frame's accelerometer says 45 degrees. The re-seed adopts it outright,
// as the first frame after boot would, rather than moving 2% of the way
// toward it on top of the invented gyro tilt (about 3.4 degrees today).
// (REQ-PROC-002)
TEST(ComplementaryTest, ReseedAdoptsTiltedAccel) {
    TelemetryProcessor processor;
    run_gap(processor, 520);
    const TelemetryFrame out = processor.process(imu_frame(540, true, 5.0f, kG, kG));
    EXPECT_NEAR(out.pitch_deg, 45.0f, 0.1f);
    EXPECT_NEAR(out.roll_deg, 0.0f, 0.001f);
}
