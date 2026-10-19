// Unit tests for src/processing/fault_detector.cpp (REQ-FAULT-001..005)
// and src/drivers/fault_injecting_source.cpp. Test cards TC-002 and
// TC-003 in docs/test_plan.md; fault entries FAULT-001..011 in
// docs/fault_model.md. Frames are built by hand so every raw value and
// timestamp is controlled exactly. Time is frame.timestamp_ms; there is
// no clock to fake.

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "drivers/fault_injecting_source.h"
#include "drivers/mpu6050.h"
#include "drivers/simulated_source.h"
#include "processing/fault_detector.h"
#include "processing/processor.h"
#include "telemetry_frame.h"

namespace {

constexpr uint64_t kDtMs = 20;   // 50 Hz

// A healthy, plausible frame at a given time. Each call varies every
// axis slightly so nothing looks stuck unless a test makes it so.
TelemetryFrame healthy_frame(uint64_t ts) {
    TelemetryFrame f{};
    f.timestamp_ms = ts;
    const float wobble = static_cast<float>(ts % 7) * 0.001f;
    f.pressure_hpa = 1013.0f + wobble;
    f.temperature_c = 21.0f + wobble;
    f.accel_x = 0.01f + wobble;
    f.accel_y = -0.01f + wobble;
    f.accel_z = 9.81f + wobble;
    f.gyro_x = 0.02f + wobble;
    f.gyro_y = -0.02f + wobble;
    f.gyro_z = 0.03f + wobble;
    f.latitude = 37.5585;
    f.longitude = -122.2711;
    f.gps_altitude_m = 3.0f;
    f.baro_status = ChannelStatus::NOMINAL;
    f.imu_status = ChannelStatus::NOMINAL;
    f.gps_status = ChannelStatus::NOMINAL;
    f.baro_read_ok = true;
    f.imu_read_ok = true;
    f.gps_read_ok = true;
    return f;
}

// Feed `n` healthy frames starting at `ts`; returns the next timestamp.
uint64_t feed_healthy(FaultDetector& d, uint64_t ts, int n) {
    for (int i = 0; i < n; i++) {
        d.check(healthy_frame(ts));
        ts += kDtMs;
    }
    return ts;
}

ChannelStatus status_of(const TelemetryFrame& f, Channel ch) {
    switch (ch) {
        case Channel::BARO: return f.baro_status;
        case Channel::IMU:  return f.imu_status;
        case Channel::GPS:  return f.gps_status;
        case Channel::VOTER: break;   // not a sensor channel
    }
    return ChannelStatus::NOMINAL;
}

void set_read_ok(TelemetryFrame& f, Channel ch, bool ok) {
    switch (ch) {
        case Channel::BARO: f.baro_read_ok = ok; break;
        case Channel::IMU:  f.imu_read_ok = ok; break;
        case Channel::GPS:  f.gps_read_ok = ok; break;
        case Channel::VOTER: break;   // not a sensor channel
    }
}

// TC-002 core: after 10 healthy frames, fail reads on `ch` and return the
// elapsed ms from the first failed read to the first DEGRADED frame.
uint64_t time_to_degraded(FaultDetector& d, Channel ch, uint64_t max_ms) {
    uint64_t ts = feed_healthy(d, 0, 10);
    const uint64_t first_fail = ts;
    while (ts - first_fail <= max_ms + 200) {
        TelemetryFrame f = healthy_frame(ts);
        set_read_ok(f, ch, false);
        if (status_of(d.check(f), ch) == ChannelStatus::DEGRADED) {
            return ts - first_fail;
        }
        ts += kDtMs;
    }
    return UINT64_MAX;
}

}  // namespace

// ---------------------------------------------------------------------------
// TC-002: communication timeout (REQ-FAULT-001, REQ-FAULT-004)
// ---------------------------------------------------------------------------

// FAULT-001. Detection must land in (480, 500] ms after the first failed
// read: late enough to ignore one transient error, no later than the bound.
TEST(FaultDetectionTest, BaroTimeoutDetectedWithinBound) {
    FaultDetector d;
    const uint64_t t = time_to_degraded(d, Channel::BARO, 500);
    EXPECT_GT(t, 480u);
    EXPECT_LE(t, 500u);

    const auto events = d.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].channel, Channel::BARO);
    EXPECT_EQ(events[0].type, FaultType::COMM_TIMEOUT);
}

// FAULT-002. Same bound for the second I2C device.
TEST(FaultDetectionTest, ImuTimeoutDetectedWithinBound) {
    FaultDetector d;
    const uint64_t t = time_to_degraded(d, Channel::IMU, 500);
    EXPECT_GT(t, 480u);
    EXPECT_LE(t, 500u);
    const auto events = d.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].channel, Channel::IMU);
}

// FAULT-003 (a). GPS uses the longer UART bound because NMEA arrives at 1 Hz;
// a 500 ms bound would false-alarm every second.
TEST(FaultDetectionTest, GpsTimeoutUsesUartBound) {
    FaultDetector d;
    const uint64_t t = time_to_degraded(d, Channel::GPS, 2000);
    EXPECT_GT(t, 1980u);
    EXPECT_LE(t, 2000u);
    const auto events = d.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].channel, Channel::GPS);
}

// A couple of dropped reads followed by success is normal bus behavior,
// not a fault. No status change, no event.
TEST(FaultDetectionTest, TransientReadErrorDoesNotFault) {
    FaultDetector d;
    uint64_t ts = feed_healthy(d, 0, 5);
    for (int i = 0; i < 2; i++) {
        TelemetryFrame f = healthy_frame(ts);
        f.baro_read_ok = false;
        EXPECT_EQ(d.check(f).baro_status, ChannelStatus::NOMINAL);
        ts += kDtMs;
    }
    EXPECT_EQ(d.check(healthy_frame(ts)).baro_status, ChannelStatus::NOMINAL);
    EXPECT_TRUE(d.take_events().empty());
}

// Healthy channels are untouched while another channel is timing out.
TEST(FaultDetectionTest, OtherChannelsStayNominalDuringTimeout) {
    FaultDetector d;
    uint64_t ts = feed_healthy(d, 0, 10);
    TelemetryFrame last{};
    for (int i = 0; i < 40; i++) {
        TelemetryFrame f = healthy_frame(ts);
        f.baro_read_ok = false;
        last = d.check(f);
        ts += kDtMs;
    }
    EXPECT_EQ(last.baro_status, ChannelStatus::DEGRADED);
    EXPECT_EQ(last.imu_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(last.gps_status, ChannelStatus::NOMINAL);
}

// FAULT-004. Both I2C devices failing together degrade both channels on
// the same frame. A single bus-level event is deferred to the live driver,
// which can tell a bus fault from two device faults; today it is two events.
TEST(FaultDetectionTest, BusFailureDegradesBothI2cChannels) {
    FaultDetector d;
    uint64_t ts = feed_healthy(d, 0, 10);
    TelemetryFrame last{};
    uint64_t baro_at = 0, imu_at = 0;
    for (int i = 0; i < 40; i++) {
        TelemetryFrame f = healthy_frame(ts);
        f.baro_read_ok = false;
        f.imu_read_ok = false;
        last = d.check(f);
        if (baro_at == 0 && last.baro_status == ChannelStatus::DEGRADED) baro_at = ts;
        if (imu_at == 0 && last.imu_status == ChannelStatus::DEGRADED) imu_at = ts;
        ts += kDtMs;
    }
    EXPECT_NE(baro_at, 0u);
    EXPECT_EQ(baro_at, imu_at);
    EXPECT_EQ(last.gps_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(d.take_events().size(), 2u);
}

// The injector wrapping the real simulator produces the same detection as
// hand-built frames. Proves the DataSource seam carries read_ok through.
TEST(FaultDetectionTest, TimeoutViaInjector) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42));
    FaultDetector d;

    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(d.check(source.read_frame()).baro_status, ChannelStatus::NOMINAL);
    }
    source.fail_reads(Channel::BARO, true);

    uint64_t first_fail = 0, degraded_at = 0;
    for (int i = 0; i < 40 && degraded_at == 0; i++) {
        TelemetryFrame f = source.read_frame();
        if (first_fail == 0) first_fail = f.timestamp_ms;
        EXPECT_FALSE(f.baro_read_ok);
        if (d.check(f).baro_status == ChannelStatus::DEGRADED) degraded_at = f.timestamp_ms;
    }
    ASSERT_NE(degraded_at, 0u);
    EXPECT_GT(degraded_at - first_fail, 480u);
    EXPECT_LE(degraded_at - first_fail, 500u);
}

// ---------------------------------------------------------------------------
// TC-003: stuck sensor (REQ-FAULT-002, REQ-FAULT-004)
// ---------------------------------------------------------------------------

// FAULT-005, TC-003 steps 1 and 2. Nine identical is fine; the tenth trips.
TEST(FaultDetectionTest, TenthIdenticalPressureIsStuck) {
    FaultDetector d;
    uint64_t ts = 0;
    TelemetryFrame last{};
    for (int i = 0; i < 9; i++) {
        TelemetryFrame f = healthy_frame(ts);
        f.pressure_hpa = 1013.0f;
        last = d.check(f);
        EXPECT_EQ(last.baro_status, ChannelStatus::NOMINAL) << "frame " << i;
        ts += kDtMs;
    }
    TelemetryFrame f = healthy_frame(ts);
    f.pressure_hpa = 1013.0f;
    last = d.check(f);
    EXPECT_EQ(last.baro_status, ChannelStatus::DEGRADED);

    const auto events = d.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, FaultType::STUCK);
    EXPECT_EQ(events[0].value, 1013.0f);
}

// FAULT-006, TC-003 steps 3 and 4. One stuck axis is a fault even while
// the other five vary every frame.
TEST(FaultDetectionTest, StuckAccelXWithOtherAxesVarying) {
    FaultDetector d;
    uint64_t ts = 0;
    for (int i = 0; i < 9; i++) {
        TelemetryFrame f = healthy_frame(ts);
        f.accel_x = 0.05f;
        EXPECT_EQ(d.check(f).imu_status, ChannelStatus::NOMINAL) << "frame " << i;
        ts += kDtMs;
    }
    TelemetryFrame f = healthy_frame(ts);
    f.accel_x = 0.05f;
    EXPECT_EQ(d.check(f).imu_status, ChannelStatus::DEGRADED);
}

// TC-003 step 5. Runs of two are normal quantization, not a stuck sensor.
TEST(FaultDetectionTest, RepeatsOfTwoAreNotStuck) {
    FaultDetector d;
    uint64_t ts = 0;
    for (int i = 0; i < 20; i++) {
        TelemetryFrame f = healthy_frame(ts);
        const float v = static_cast<float>(i / 2) * 0.01f;   // 0,0,1,1,2,2,...
        f.accel_x = v; f.accel_y = v; f.accel_z = 9.81f + v;
        f.gyro_x = v;  f.gyro_y = v;  f.gyro_z = v;
        f.pressure_hpa = 1013.0f + v;
        const TelemetryFrame out = d.check(f);
        EXPECT_EQ(out.imu_status, ChannelStatus::NOMINAL) << "frame " << i;
        EXPECT_EQ(out.baro_status, ChannelStatus::NOMINAL) << "frame " << i;
        ts += kDtMs;
    }
}

// FAULT-007, TC-003 step 6. One differing value must reset the run count.
TEST(FaultDetectionTest, DifferingValueResetsRun) {
    FaultDetector d;
    uint64_t ts = 0;
    auto feed = [&](float gz, int n) {
        for (int i = 0; i < n; i++) {
            TelemetryFrame f = healthy_frame(ts);
            f.gyro_z = gz;
            EXPECT_EQ(d.check(f).imu_status, ChannelStatus::NOMINAL) << "t=" << ts;
            ts += kDtMs;
        }
    };
    feed(0.5f, 9);
    feed(0.6f, 1);
    feed(0.5f, 9);
    EXPECT_TRUE(d.take_events().empty());
}

// FAULT-007. Gyro axes are tracked exactly like accel axes.
TEST(FaultDetectionTest, StuckGyroZ) {
    FaultDetector d;
    uint64_t ts = 0;
    TelemetryFrame last{};
    for (int i = 0; i < 10; i++) {
        TelemetryFrame f = healthy_frame(ts);
        f.gyro_z = 0.5f;
        last = d.check(f);
        ts += kDtMs;
    }
    EXPECT_EQ(last.imu_status, ChannelStatus::DEGRADED);
}

// A value that repeats while the read failed must not count as stuck; the
// driver held it because it got nothing new. That is a timeout story.
TEST(FaultDetectionTest, StaleValuesDuringDropoutAreNotStuck) {
    FaultDetector d;
    uint64_t ts = feed_healthy(d, 0, 5);
    for (int i = 0; i < 12; i++) {
        TelemetryFrame f = healthy_frame(ts);
        f.pressure_hpa = 1013.0f;
        f.baro_read_ok = false;
        d.check(f);
        ts += kDtMs;
    }
    // 240 ms of dropout: under the 500 ms bound, and 12 repeats exceed N.
    EXPECT_EQ(d.check(healthy_frame(ts)).baro_status, ChannelStatus::NOMINAL);
    EXPECT_TRUE(d.take_events().empty());
}

// ---------------------------------------------------------------------------
// Out of range (REQ-FAULT-003, REQ-FAULT-004)
// ---------------------------------------------------------------------------

// FAULT-008.
TEST(FaultDetectionTest, PressureBelowRange) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.pressure_hpa = 200.0f;
    EXPECT_EQ(d.check(f).baro_status, ChannelStatus::DEGRADED);
    const auto events = d.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, FaultType::OUT_OF_RANGE);
    EXPECT_EQ(events[0].value, 200.0f);
}

TEST(FaultDetectionTest, PressureAboveRange) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.pressure_hpa = 1200.0f;
    EXPECT_EQ(d.check(f).baro_status, ChannelStatus::DEGRADED);
}

// FAULT-009. Temperature out of range degrades the BARO channel, because
// the BMP280's pressure compensation depends on its own temperature.
TEST(FaultDetectionTest, TemperatureOutOfRangeDegradesBaro) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.temperature_c = 90.0f;
    const TelemetryFrame out = d.check(f);
    EXPECT_EQ(out.baro_status, ChannelStatus::DEGRADED);
    EXPECT_EQ(out.imu_status, ChannelStatus::NOMINAL);
}

// FAULT-010. The positive rail: count +32767 through the driver's scaling,
// 19.6194 m/s^2, one count short of 2 g. The 0.3.0 detector compared the
// magnitude against a limit of 19.62 and never saw this value (FTS-VR-001
// Entry 8 section 8.7); its only saturation test fed 2 x 9.81 itself, a
// number the driver cannot produce. This test was red against it.
TEST(FaultDetectionTest, AccelAtPositiveRailDegradesImu) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.accel_x = Mpu6050::kAccelPositiveRailMps2;
    EXPECT_EQ(d.check(f).imu_status, ChannelStatus::DEGRADED);
    const auto events = d.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].channel, Channel::IMU);
    EXPECT_EQ(events[0].type, FaultType::OUT_OF_RANGE);
    EXPECT_EQ(events[0].value, Mpu6050::kAccelPositiveRailMps2);
}

// FAULT-010. The negative rail: count -32768, exactly -2 g, on another
// axis so the sign and the axis search are both exercised.
TEST(FaultDetectionTest, AccelAtNegativeRailDegradesImu) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.accel_z = Mpu6050::kAccelNegativeRailMps2;
    EXPECT_EQ(d.check(f).imu_status, ChannelStatus::DEGRADED);
    const auto events = d.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].channel, Channel::IMU);
    EXPECT_EQ(events[0].type, FaultType::OUT_OF_RANGE);
    EXPECT_EQ(events[0].value, Mpu6050::kAccelNegativeRailMps2);
}

// FAULT-010. One count inside either rail is a reading the sensor
// represented, not a clipped one: +32766, and -32767, which converts to
// the negation of the positive rail and is exactly what a magnitude test
// against that rail would flag. Each value on each axis in turn.
TEST(FaultDetectionTest, AccelOneCountInsideEitherRailIsValid) {
    FaultDetector d;
    const float inside_positive = static_cast<float>(INT16_MAX - 1) / Mpu6050::kAccelLsbPerG * Mpu6050::kGravityMps2;
    const float inside_negative = static_cast<float>(INT16_MIN + 1) / Mpu6050::kAccelLsbPerG * Mpu6050::kGravityMps2;
    ASSERT_EQ(inside_negative, -Mpu6050::kAccelPositiveRailMps2);   // the asymmetry this test guards against

    uint64_t ts = 0;
    for (float value : {inside_positive, inside_negative}) {
        for (int axis = 0; axis < 3; axis++) {
            TelemetryFrame f = healthy_frame(ts);
            if (axis == 0) f.accel_x = value;
            if (axis == 1) f.accel_y = value;
            if (axis == 2) f.accel_z = value;
            EXPECT_EQ(d.check(f).imu_status, ChannelStatus::NOMINAL) << value << " on axis " << axis;
            ts += kDtMs;
        }
    }
    EXPECT_TRUE(d.take_events().empty());
}

// The datasheet limits are inclusive: a reading exactly at the edge is
// still a reading the sensor is rated to produce.
TEST(FaultDetectionTest, BoundaryValuesAreValid) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.pressure_hpa = 300.0f;
    f.temperature_c = -40.0f;
    EXPECT_EQ(d.check(f).baro_status, ChannelStatus::NOMINAL);
    f = healthy_frame(kDtMs);
    f.pressure_hpa = 1100.0f;
    f.temperature_c = 85.0f;
    EXPECT_EQ(d.check(f).baro_status, ChannelStatus::NOMINAL);
    EXPECT_TRUE(d.take_events().empty());
}

// ---------------------------------------------------------------------------
// Recovery (REQ-FAULT-005, FAULT-011)
// ---------------------------------------------------------------------------

// M = 5 consecutive valid readings restore NOMINAL; four do not. The
// RECOVERY event carries how long the channel was down.
TEST(FaultDetectionTest, RecoveryAfterMValidReadings) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.pressure_hpa = 200.0f;
    EXPECT_EQ(d.check(f).baro_status, ChannelStatus::DEGRADED);
    d.take_events();

    uint64_t ts = kDtMs;
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(d.check(healthy_frame(ts)).baro_status, ChannelStatus::DEGRADED) << "valid " << i + 1;
        ts += kDtMs;
    }
    EXPECT_EQ(d.check(healthy_frame(ts)).baro_status, ChannelStatus::NOMINAL);

    const auto events = d.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, FaultType::RECOVERY);
    EXPECT_EQ(events[0].channel, Channel::BARO);
    EXPECT_EQ(events[0].value, static_cast<float>(ts));   // degraded since t=0
}

// One bad reading in the middle restarts the count from zero.
TEST(FaultDetectionTest, BadReadingResetsRecoveryCount) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.pressure_hpa = 200.0f;
    d.check(f);
    uint64_t ts = kDtMs;
    ts = feed_healthy(d, ts, 4);
    f = healthy_frame(ts);
    f.pressure_hpa = 200.0f;
    d.check(f);
    ts += kDtMs;
    TelemetryFrame last{};
    for (int i = 0; i < 4; i++) {
        last = d.check(healthy_frame(ts));
        ts += kDtMs;
    }
    EXPECT_EQ(last.baro_status, ChannelStatus::DEGRADED);
}

// A stuck fault also recovers once fresh, varying readings resume.
TEST(FaultDetectionTest, StuckSensorRecovers) {
    FaultDetector d;
    uint64_t ts = 0;
    for (int i = 0; i < 10; i++) {
        TelemetryFrame f = healthy_frame(ts);
        f.pressure_hpa = 1013.0f;
        d.check(f);
        ts += kDtMs;
    }
    TelemetryFrame last{};
    for (int i = 0; i < 5; i++) {
        last = d.check(healthy_frame(ts));
        ts += kDtMs;
    }
    EXPECT_EQ(last.baro_status, ChannelStatus::NOMINAL);
}

// ---------------------------------------------------------------------------
// REQ-FAULT-004: processing continues on the remaining sensors
// ---------------------------------------------------------------------------

// A DEGRADED barometer still yields a complete processed frame: the Kalman
// filter coasts on prediction plus GPS, attitude keeps running.
TEST(FaultDetectionTest, ProcessingContinuesOnDegradedChannel) {
    FaultDetector d;
    TelemetryProcessor p;
    uint64_t ts = 0;
    for (int i = 0; i < 5; i++) {
        p.process(d.check(healthy_frame(ts)));
        ts += kDtMs;
    }
    TelemetryFrame f = healthy_frame(ts);
    f.pressure_hpa = 200.0f;
    const TelemetryFrame out = p.process(d.check(f));
    EXPECT_EQ(out.baro_status, ChannelStatus::DEGRADED);
    EXPECT_GT(out.fused_altitude_m, -50.0f);   // not the -13 km a 200 hPa reading implies
    EXPECT_LT(out.fused_altitude_m, 50.0f);
    EXPECT_EQ(out.timestamp_ms, ts);
}

// ---------------------------------------------------------------------------
// FaultInjectingSource behavior
// ---------------------------------------------------------------------------

TEST(FaultInjectingSourceTest, HoldValuesRepeatsLastGood) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42));
    const TelemetryFrame a = source.read_frame();
    source.hold_values(Channel::BARO, true);
    const TelemetryFrame b = source.read_frame();
    const TelemetryFrame c = source.read_frame();
    EXPECT_EQ(b.pressure_hpa, a.pressure_hpa);
    EXPECT_EQ(c.pressure_hpa, a.pressure_hpa);
    EXPECT_TRUE(c.baro_read_ok);
    EXPECT_NE(c.accel_x, a.accel_x);          // other channels still vary
    EXPECT_EQ(c.timestamp_ms, a.timestamp_ms + 2 * kDtMs);   // inner source still advances
}

TEST(FaultInjectingSourceTest, PressureOverrideAndClear) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42));
    source.override_pressure(200.0f);
    EXPECT_EQ(source.read_frame().pressure_hpa, 200.0f);
    source.clear_pressure_override();
    EXPECT_NE(source.read_frame().pressure_hpa, 200.0f);
}

// ---------------------------------------------------------------------------
// Branch-coverage additions (REQ-TEST-003): paths the first coverage run
// showed untaken. Each still traces to the fault requirement it exercises.
// ---------------------------------------------------------------------------

// FAULT-001. A sensor that never answers from power-on has no "last good
// read" to measure from; the detector starts its clock at the first frame
// so the channel still times out. (REQ-FAULT-001)
TEST(FaultDetectionTest, DeadFromPowerOnStillTimesOut) {
    FaultDetector d;
    uint64_t ts = 0;
    TelemetryFrame last{};
    for (int i = 0; i < 30; i++) {
        TelemetryFrame f = healthy_frame(ts);
        f.baro_read_ok = false;
        last = d.check(f);
        if (last.baro_status == ChannelStatus::DEGRADED) break;
        EXPECT_EQ(last.baro_status, ChannelStatus::NO_DATA) << "t=" << ts;   // never read: NO_DATA until the timeout (REQ-FAULT-006)
        ts += kDtMs;
    }
    EXPECT_EQ(last.baro_status, ChannelStatus::DEGRADED);
    EXPECT_GT(ts, 480u);
    EXPECT_LE(ts, 520u);
    const auto events = d.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, FaultType::COMM_TIMEOUT);
}

// ---------------------------------------------------------------------------
// REQ-FAULT-006: NO_DATA until a channel's first reading. TC-017 step 10.
// ---------------------------------------------------------------------------

// Every channel is NO_DATA from start-up while nothing has been read, with
// no event: the raw fields are value-initialized zeros, not readings.
TEST(FaultDetectionTest, NoDataUntilFirstReading) {
    FaultDetector d;
    uint64_t ts = 0;
    for (int i = 0; i < 5; i++, ts += kDtMs) {
        TelemetryFrame f = healthy_frame(ts);
        f.baro_read_ok = false;
        f.imu_read_ok = false;
        f.gps_read_ok = false;
        const TelemetryFrame out = d.check(f);
        EXPECT_EQ(out.baro_status, ChannelStatus::NO_DATA) << "frame " << i;
        EXPECT_EQ(out.imu_status, ChannelStatus::NO_DATA) << "frame " << i;
        EXPECT_EQ(out.gps_status, ChannelStatus::NO_DATA) << "frame " << i;
    }
    EXPECT_TRUE(d.take_events().empty());
}

// The first successful reading makes a channel NOMINAL on that frame, with
// no event: a start is not a recovery. The other channels stay NO_DATA.
TEST(FaultDetectionTest, FirstReadingMakesNominalWithoutEvent) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.imu_read_ok = false;
    f.gps_read_ok = false;
    const TelemetryFrame out = d.check(f);
    EXPECT_EQ(out.baro_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(out.imu_status, ChannelStatus::NO_DATA);
    EXPECT_EQ(out.gps_status, ChannelStatus::NO_DATA);
    EXPECT_TRUE(d.take_events().empty());
    EXPECT_EQ(d.check(healthy_frame(kDtMs)).gps_status, ChannelStatus::NOMINAL);
    EXPECT_TRUE(d.take_events().empty());
}

// A fault on a channel's first-ever reading is DEGRADED with its event, as
// from NOMINAL: the promotion rule reads "not yet DEGRADED", so NO_DATA
// cannot swallow a fault. Both out-of-range kinds, on both I2C channels.
TEST(FaultDetectionTest, FaultOnFirstReadingDegradesFromNoData) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.pressure_hpa = 200.0f;
    f.accel_x = Mpu6050::kAccelPositiveRailMps2;   // a value the driver produces, not the nominal 2 g
    const TelemetryFrame out = d.check(f);
    EXPECT_EQ(out.baro_status, ChannelStatus::DEGRADED);
    EXPECT_EQ(out.imu_status, ChannelStatus::DEGRADED);
    EXPECT_EQ(out.gps_status, ChannelStatus::NOMINAL);
    const auto events = d.take_events();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].channel, Channel::BARO);
    EXPECT_EQ(events[0].type, FaultType::OUT_OF_RANGE);
    EXPECT_EQ(events[1].channel, Channel::IMU);
    EXPECT_EQ(events[1].type, FaultType::OUT_OF_RANGE);
    // Recovery from there is the ordinary M-reading rule.
    uint64_t ts = feed_healthy(d, kDtMs, 4);
    const TelemetryFrame recovered = d.check(healthy_frame(ts));
    EXPECT_EQ(recovered.baro_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(recovered.imu_status, ChannelStatus::NOMINAL);
}

// FAULT-009, lower limit. (REQ-FAULT-003)
TEST(FaultDetectionTest, TemperatureBelowRangeDegradesBaro) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.temperature_c = -45.0f;
    EXPECT_EQ(d.check(f).baro_status, ChannelStatus::DEGRADED);
}

// FAULT-006/007. Every IMU axis stuck at once reports one fault carrying
// the first axis's value, not six faults. (REQ-FAULT-002, REQ-FAULT-004)
TEST(FaultDetectionTest, AllAxesStuckReportsOnce) {
    FaultDetector d;
    uint64_t ts = 0;
    TelemetryFrame last{};
    for (int i = 0; i < 10; i++) {
        TelemetryFrame f = healthy_frame(ts);
        f.accel_x = 0.1f; f.accel_y = 0.2f; f.accel_z = 9.8f;
        f.gyro_x = 0.3f;  f.gyro_y = 0.4f;  f.gyro_z = 0.5f;
        last = d.check(f);
        ts += kDtMs;
    }
    EXPECT_EQ(last.imu_status, ChannelStatus::DEGRADED);
    const auto events = d.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].value, 0.1f);
}

// FAULT-011. A frame with no reading proves nothing about recovery: it
// neither counts toward M nor resets the count. (REQ-FAULT-005)
TEST(FaultDetectionTest, MissingReadDuringRecoveryNeitherCountsNorResets) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.pressure_hpa = 200.0f;
    d.check(f);
    uint64_t ts = feed_healthy(d, kDtMs, 3);          // 3 valid
    f = healthy_frame(ts);
    f.baro_read_ok = false;                            // no reading
    EXPECT_EQ(d.check(f).baro_status, ChannelStatus::DEGRADED);
    ts += kDtMs;
    EXPECT_EQ(d.check(healthy_frame(ts)).baro_status, ChannelStatus::DEGRADED);  // 4th valid
    ts += kDtMs;
    EXPECT_EQ(d.check(healthy_frame(ts)).baro_status, ChannelStatus::NOMINAL);   // 5th valid
}

// Injector paths for the IMU and GPS channels mirror the barometer. The
// inner simulator fixes GPS on every frame (third argument) so the
// gps_read_ok assertions are about the injector, not the 1 Hz schedule.
TEST(FaultInjectingSourceTest, FailReadsImuAndGps) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42, kDtMs));
    const TelemetryFrame a = source.read_frame();
    source.fail_reads(Channel::IMU, true);
    source.fail_reads(Channel::GPS, true);
    const TelemetryFrame b = source.read_frame();
    EXPECT_FALSE(b.imu_read_ok);
    EXPECT_FALSE(b.gps_read_ok);
    EXPECT_TRUE(b.baro_read_ok);
    EXPECT_EQ(b.accel_x, a.accel_x);
    EXPECT_EQ(b.latitude, a.latitude);
    EXPECT_NE(b.pressure_hpa, a.pressure_hpa);
    // The inner source's count passes through: a fault is in a channel,
    // not in the stream (REQ-LOG-008, TC-031 step 5).
    EXPECT_EQ(a.sequence, 0u);
    EXPECT_EQ(b.sequence, 1u);
}

// Channel::VOTER names the voter's events (REQ-RED-003; FTS-DD-001
// Section 13), not a sensor: failing or holding it changes no frame, byte
// for byte, against the same simulator with nothing injected.
TEST(FaultInjectingSourceTest, VoterIsNotASensorChannel) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42, kDtMs));
    SimulatedSource plain(kDtMs, 42, kDtMs);
    source.fail_reads(Channel::VOTER, true);
    source.hold_values(Channel::VOTER, true);
    for (int i = 0; i < 60; i++) {
        const TelemetryFrame injected = source.read_frame();
        const TelemetryFrame expected = plain.read_frame();
        EXPECT_EQ(std::memcmp(&injected, &expected, sizeof(TelemetryFrame)), 0) << "frame " << i;
    }
}

namespace {

// A source that hands out the frames it was given, in order: the injector
// on fields the simulator never varies.
class FrameList : public DataSource {
public:
    explicit FrameList(std::vector<TelemetryFrame> frames) : frames_(std::move(frames)) {}
    TelemetryFrame read_frame() override { return frames_.at(next_++); }

private:
    std::vector<TelemetryFrame> frames_;
    size_t next_ = 0;
};

}  // namespace

// The GPS channel's reading is the position, the speed and the course and,
// since format version 3, the receiver's fix quality and satellite count
// and the ADS-B vertical rate and pressure altitude, with the bits that say
// each is present (REQ-SENS-005, REQ-SENS-008): a held GPS is held whole,
// as the live source holds its last fix. What the frame says of itself
// passes through: the source's count (REQ-LOG-008) and NEW_VEHICLE
// (REQ-PROC-008).
TEST(FaultInjectingSourceTest, HeldGpsIsTheWholeReading) {
    TelemetryFrame first{};
    first.latitude = 37.5;
    first.course_deg = 90.0f;
    first.gps_vertical_rate_mps = -3.5f;
    first.gps_pressure_altitude_m = 300.0f;
    first.gps_fix_quality = 2;
    first.gps_satellites = 9;
    set_flag(first, FrameFlag::VERTICAL_RATE);
    set_flag(first, FrameFlag::PRESSURE_ALTITUDE);
    set_flag(first, FrameFlag::FIX_QUALITY);
    TelemetryFrame second{};
    second.timestamp_ms = 20;
    second.sequence = 1;
    second.latitude = 38.0;
    second.course_deg = 180.0f;
    set_flag(second, FrameFlag::NEW_VEHICLE);
    FaultInjectingSource source(std::make_unique<FrameList>(std::vector<TelemetryFrame>{first, second}));
    source.read_frame();
    source.hold_values(Channel::GPS, true);
    const TelemetryFrame held = source.read_frame();
    EXPECT_EQ(held.latitude, 37.5);
    EXPECT_EQ(held.course_deg, 90.0f);
    EXPECT_EQ(held.gps_vertical_rate_mps, -3.5f);
    EXPECT_EQ(held.gps_pressure_altitude_m, 300.0f);
    EXPECT_EQ(held.gps_fix_quality, 2u);
    EXPECT_EQ(held.gps_satellites, 9u);
    EXPECT_TRUE(has_flag(held, FrameFlag::VERTICAL_RATE));
    EXPECT_TRUE(has_flag(held, FrameFlag::PRESSURE_ALTITUDE));
    EXPECT_TRUE(has_flag(held, FrameFlag::FIX_QUALITY));
    EXPECT_TRUE(has_flag(held, FrameFlag::NEW_VEHICLE));   // the frame's own fact, through
    EXPECT_EQ(held.sequence, 1u);                           // the inner source's count, through
    EXPECT_EQ(held.timestamp_ms, 20u);
}

TEST(FaultInjectingSourceTest, HoldValuesImuAndGps) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42, kDtMs));
    const TelemetryFrame a = source.read_frame();
    source.hold_values(Channel::IMU, true);
    source.hold_values(Channel::GPS, true);
    const TelemetryFrame b = source.read_frame();
    EXPECT_TRUE(b.imu_read_ok);
    EXPECT_TRUE(b.gps_read_ok);
    EXPECT_EQ(b.gyro_z, a.gyro_z);
    EXPECT_EQ(b.gps_altitude_m, a.gps_altitude_m);
    EXPECT_NE(b.pressure_hpa, a.pressure_hpa);
}

// REQ-FAULT-004 says the logged event carries the fault type. The name
// helpers are what make that readable in a log or on a display; an enum
// printed as an integer is not a fault type anyone can act on.
TEST(FaultDetectionTest, EventNamesAreReadable) {
    EXPECT_STREQ(channel_name(Channel::BARO), "BARO");
    EXPECT_STREQ(channel_name(Channel::IMU), "IMU");
    EXPECT_STREQ(channel_name(Channel::GPS), "GPS");
    EXPECT_STREQ(fault_type_name(FaultType::COMM_TIMEOUT), "COMM_TIMEOUT");
    EXPECT_STREQ(fault_type_name(FaultType::STUCK), "STUCK");
    EXPECT_STREQ(fault_type_name(FaultType::OUT_OF_RANGE), "OUT_OF_RANGE");
    EXPECT_STREQ(fault_type_name(FaultType::RECOVERY), "RECOVERY");
    // The voter's events, new values of the same two bytes (REQ-RED-003,
    // REQ-RED-004, REQ-RED-006; FTS-FM-001 FAULT-019 to FAULT-022). The
    // numbers are pinned too: they are what a log carries and what an
    // older reader prints.
    EXPECT_STREQ(channel_name(Channel::VOTER), "VOTER");
    EXPECT_STREQ(fault_type_name(FaultType::LANE_DISAGREEMENT), "LANE_DISAGREEMENT");
    EXPECT_STREQ(fault_type_name(FaultType::LANE_FAULTED), "LANE_FAULTED");
    EXPECT_STREQ(fault_type_name(FaultType::NO_MAJORITY), "NO_MAJORITY");
    EXPECT_STREQ(fault_type_name(FaultType::LANE_OVERRUN), "LANE_OVERRUN");
    EXPECT_EQ(static_cast<int>(Channel::VOTER), 3);
    EXPECT_EQ(static_cast<int>(FaultType::LANE_DISAGREEMENT), 4);
    EXPECT_EQ(static_cast<int>(FaultType::LANE_FAULTED), 5);
    EXPECT_EQ(static_cast<int>(FaultType::NO_MAJORITY), 6);
    EXPECT_EQ(static_cast<int>(FaultType::LANE_OVERRUN), 7);
}

// Events are written to the binary log as raw bytes. The two bytes between
// `type` and `value` were implicit padding in format version 1, which a
// braced temporary leaves unspecified: GCC filled them with stack garbage
// and CI run 34442203740 saw live and replayed events differ only there.
// Version 2 names them `reserved` and value-initialization zeroes them;
// this reads the field by name and the bytes directly, so a construction
// site that stopped value-initializing would still be caught.
// (REQ-FAULT-004, REQ-LOG-003, TC-017 step 1)
TEST(FaultDetectionTest, EventReservedIsZero) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.pressure_hpa = 200.0f;                       // OUT_OF_RANGE
    d.check(f);
    uint64_t ts = feed_healthy(d, kDtMs, 5);       // RECOVERY
    for (int i = 0; i < 10; i++) {                 // STUCK
        TelemetryFrame g = healthy_frame(ts);
        g.gyro_z = 0.5f;
        d.check(g);
        ts += kDtMs;
    }
    const auto events = d.take_events();
    ASSERT_EQ(events.size(), 3u);

    for (size_t e = 0; e < events.size(); e++) {
        EXPECT_EQ(events[e].reserved, 0u) << "event " << e;
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&events[e]);
        for (size_t i = offsetof(FaultEvent, reserved); i < offsetof(FaultEvent, value); i++) {
            EXPECT_EQ(bytes[i], 0) << "event " << e << " byte " << i;
        }
    }
}

// TC-017 step 17 (REQ-FAULT-007, FAULT-018). A channel the source reports
// NOT_AVAILABLE is a fact, not a reading: the detector leaves the status as
// the source set it, runs no timeout, stuck or range check on it, and
// records no event, for ten seconds and whichever channel it is, while the
// other two are judged as before. A barometer that then goes silent beside
// an absent GPS still times out with its event: the neighbour's rules are
// untouched.
TEST(FaultDetectionTest, NotAvailableChannelIsLeftAloneWithNoEvent) {
    for (const Channel absent : {Channel::GPS, Channel::BARO, Channel::IMU}) {
        FaultDetector d;
        uint64_t ts = 0;
        for (int i = 0; i < 500; i++, ts += kDtMs) {
            TelemetryFrame f = healthy_frame(ts);
            switch (absent) {
                case Channel::GPS:
                    f.gps_status = ChannelStatus::NOT_AVAILABLE;
                    f.gps_read_ok = false;
                    f.latitude = 0.0;
                    break;
                case Channel::BARO:
                    f.baro_status = ChannelStatus::NOT_AVAILABLE;
                    f.baro_read_ok = false;
                    f.pressure_hpa = 0.0f;   // out of range, were it a reading
                    break;
                case Channel::IMU:
                    f.imu_status = ChannelStatus::NOT_AVAILABLE;
                    f.imu_read_ok = false;
                    f.accel_x = f.accel_y = f.accel_z = 0.0f;   // stuck, were they readings
                    f.gyro_x = f.gyro_y = f.gyro_z = 0.0f;
                    break;
                case Channel::VOTER:   // not in the list: not a sensor channel
                    break;
            }
            const TelemetryFrame out = d.check(f);
            EXPECT_EQ(status_of(out, absent), ChannelStatus::NOT_AVAILABLE) << channel_name(absent) << " frame " << i;
            for (const Channel other : {Channel::BARO, Channel::IMU, Channel::GPS}) {
                if (other != absent) {
                    EXPECT_EQ(status_of(out, other), ChannelStatus::NOMINAL) << channel_name(other) << " frame " << i;
                }
            }
        }
        EXPECT_TRUE(d.take_events().empty()) << channel_name(absent);
    }

    FaultDetector d;
    uint64_t ts = 0;
    for (int i = 0; i < 20; i++, ts += kDtMs) {
        TelemetryFrame f = healthy_frame(ts);
        f.gps_status = ChannelStatus::NOT_AVAILABLE;
        f.gps_read_ok = false;
        d.check(f);
    }
    TelemetryFrame last{};
    for (int i = 0; i < 30; i++, ts += kDtMs) {   // 600 ms of silence: past the 500 ms I2C timeout
        TelemetryFrame f = healthy_frame(ts);
        f.gps_status = ChannelStatus::NOT_AVAILABLE;
        f.gps_read_ok = false;
        f.baro_read_ok = false;
        last = d.check(f);
    }
    EXPECT_EQ(last.baro_status, ChannelStatus::DEGRADED);
    EXPECT_EQ(last.gps_status, ChannelStatus::NOT_AVAILABLE);
    const std::vector<FaultEvent> events = d.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].channel, Channel::BARO);
    EXPECT_EQ(events[0].type, FaultType::COMM_TIMEOUT);
}

// ---------------------------------------------------------------------------
// The field modes the fault-injection campaign needs (REQ-TEST-005): one
// raw field held, replaced or offset while the rest of its channel passes
// through. Each is checked against a twin simulator with the same seed, so
// the fields the mode must not touch are compared bit for bit.
// ---------------------------------------------------------------------------

// FAULT-006 on one axis: the held field repeats its value from the frame
// before the hold, the channel's other axes and read flag go on, and a
// release returns the live value.
TEST(FaultInjectingSourceTest, HoldFieldRepeatsTheFrameBeforeTheHold) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42));
    SimulatedSource twin(kDtMs, 42);
    const TelemetryFrame before = source.read_frame();
    twin.read_frame();
    source.hold_field(RawField::ACCEL_X, true);
    for (int i = 0; i < 5; i++) {
        const TelemetryFrame held = source.read_frame();
        const TelemetryFrame live = twin.read_frame();
        EXPECT_EQ(held.accel_x, before.accel_x);
        EXPECT_NE(held.accel_x, live.accel_x);
        EXPECT_EQ(held.accel_y, live.accel_y);
        EXPECT_EQ(held.gyro_z, live.gyro_z);
        EXPECT_TRUE(held.imu_read_ok);
        EXPECT_EQ(held.sequence, live.sequence);
    }
    source.hold_field(RawField::ACCEL_X, true);   // already held: the value is not taken again
    EXPECT_EQ(source.read_frame().accel_x, before.accel_x);
    twin.read_frame();
    source.hold_field(RawField::ACCEL_X, false);
    EXPECT_EQ(source.read_frame().accel_x, twin.read_frame().accel_x);
}

// A hold set before the first frame has no frame before it, so it holds
// the first frame's own value.
TEST(FaultInjectingSourceTest, HoldFieldBeforeTheFirstFrameHoldsThatFrame) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42));
    source.hold_field(RawField::GYRO_Z, true);
    const TelemetryFrame first = source.read_frame();
    EXPECT_EQ(source.read_frame().gyro_z, first.gyro_z);
    EXPECT_EQ(source.read_frame().gyro_z, first.gyro_z);
}

// FAULT-008 to FAULT-010 and FAULT-024: any one raw field replaced by a
// constant, an infinity and a NaN included, until cleared.
TEST(FaultInjectingSourceTest, OverrideFieldReplacesOneRawField) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42));
    SimulatedSource twin(kDtMs, 42);
    source.override_field(RawField::TEMPERATURE, 150.0f);
    source.override_field(RawField::GYRO_X, std::numeric_limits<float>::infinity());
    source.override_field(RawField::GPS_ALTITUDE, std::numeric_limits<float>::quiet_NaN());
    source.override_field(RawField::ACCEL_Z, Mpu6050::kAccelNegativeRailMps2);
    const TelemetryFrame out = source.read_frame();
    const TelemetryFrame live = twin.read_frame();
    EXPECT_EQ(out.temperature_c, 150.0f);
    EXPECT_TRUE(std::isinf(out.gyro_x));
    EXPECT_TRUE(std::isnan(out.gps_altitude_m));
    EXPECT_EQ(out.accel_z, Mpu6050::kAccelNegativeRailMps2);
    EXPECT_EQ(out.pressure_hpa, live.pressure_hpa);
    EXPECT_EQ(out.gyro_y, live.gyro_y);
    source.clear_override(RawField::TEMPERATURE);
    source.clear_override(RawField::GYRO_X);
    source.clear_override(RawField::GPS_ALTITUDE);
    source.clear_override(RawField::ACCEL_Z);
    const TelemetryFrame cleared = source.read_frame();
    const TelemetryFrame live2 = twin.read_frame();
    EXPECT_EQ(cleared.temperature_c, live2.temperature_c);
    EXPECT_EQ(cleared.gyro_x, live2.gyro_x);
    EXPECT_EQ(cleared.gps_altitude_m, live2.gps_altitude_m);
    EXPECT_EQ(cleared.accel_z, live2.accel_z);
}

// The pressure override is the field override of PRESSURE: one mechanism,
// so clearing either clears it.
TEST(FaultInjectingSourceTest, PressureOverrideIsTheFieldOverride) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42));
    source.override_pressure(200.0f);
    EXPECT_EQ(source.read_frame().pressure_hpa, 200.0f);
    source.clear_override(RawField::PRESSURE);
    EXPECT_NE(source.read_frame().pressure_hpa, 200.0f);
    source.override_field(RawField::PRESSURE, 250.0f);
    EXPECT_EQ(source.read_frame().pressure_hpa, 250.0f);
    source.clear_pressure_override();
    EXPECT_NE(source.read_frame().pressure_hpa, 250.0f);
}

// FAULT-023: a bias step adds a constant to the live value and a drift a
// rate times the time since its first frame, every reading still varying.
TEST(FaultInjectingSourceTest, BiasFieldAddsAStepOrADrift) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42));
    SimulatedSource twin(kDtMs, 42);
    source.bias_field(RawField::PRESSURE, 2.5f, 0.0f);
    source.bias_field(RawField::ACCEL_Y, 0.0f, 0.5f);
    for (int i = 0; i < 10; i++) {
        const TelemetryFrame out = source.read_frame();
        const TelemetryFrame live = twin.read_frame();
        EXPECT_NEAR(out.pressure_hpa - live.pressure_hpa, 2.5f, 1e-3f);
        EXPECT_NEAR(out.accel_y - live.accel_y, 0.5f * static_cast<float>(i) * 0.02f, 1e-6f) << "frame " << i;
        EXPECT_EQ(out.accel_x, live.accel_x);
    }
    source.clear_bias(RawField::PRESSURE);
    source.clear_bias(RawField::ACCEL_Y);
    const TelemetryFrame out = source.read_frame();
    const TelemetryFrame live = twin.read_frame();
    EXPECT_EQ(out.pressure_hpa, live.pressure_hpa);
    EXPECT_EQ(out.accel_y, live.accel_y);
}

// An override is applied last, so it wins over a hold or a bias of the
// same field; a channel's failed read still holds its fields as before.
TEST(FaultInjectingSourceTest, OverrideWinsOverHoldAndBias) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42));
    source.read_frame();
    source.hold_field(RawField::PRESSURE, true);
    source.bias_field(RawField::PRESSURE, 1.0f, 0.0f);
    source.override_field(RawField::PRESSURE, 999.0f);
    EXPECT_EQ(source.read_frame().pressure_hpa, 999.0f);
}

// The campaign's stuck axis through the detector: one held gyroscope axis,
// the axis the attitude filter ignores, trips STUCK on the IMU on the
// ninth held frame, 160 ms after the hold began (FAULT-007, REQ-FAULT-002).
TEST(FaultDetectionTest, StuckAxisViaInjector) {
    FaultInjectingSource source(std::make_unique<SimulatedSource>(kDtMs, 42));
    FaultDetector d;
    for (int i = 0; i < 100; i++) {
        d.check(source.read_frame());
    }
    source.hold_field(RawField::GYRO_Z, true);
    uint64_t onset = 0;
    uint64_t degraded_at = 0;
    for (int i = 0; i < 20 && degraded_at == 0; i++) {
        const TelemetryFrame f = source.read_frame();
        if (onset == 0) {
            onset = f.timestamp_ms;
        }
        if (d.check(f).imu_status == ChannelStatus::DEGRADED) {
            degraded_at = f.timestamp_ms;
        }
    }
    EXPECT_EQ(degraded_at - onset, 160u);
    const std::vector<FaultEvent> events = d.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].channel, Channel::IMU);
    EXPECT_EQ(events[0].type, FaultType::STUCK);
}
