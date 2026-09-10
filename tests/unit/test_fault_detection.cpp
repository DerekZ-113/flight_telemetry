// Unit tests for src/processing/fault_detector.cpp (REQ-FAULT-001..005)
// and src/drivers/fault_injecting_source.cpp. Test cards TC-002 and
// TC-003 in docs/test_plan.md; fault entries FAULT-001..011 in
// docs/fault_model.md. Frames are built by hand so every raw value and
// timestamp is controlled exactly. Time is frame.timestamp_ms; there is
// no clock to fake.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "drivers/fault_injecting_source.h"
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
    }
    return ChannelStatus::NOMINAL;
}

void set_read_ok(TelemetryFrame& f, Channel ch, bool ok) {
    switch (ch) {
        case Channel::BARO: f.baro_read_ok = ok; break;
        case Channel::IMU:  f.imu_read_ok = ok; break;
        case Channel::GPS:  f.gps_read_ok = ok; break;
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

// FAULT-003a. GPS uses the longer UART bound because NMEA arrives at 1 Hz;
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

// FAULT-010. Any axis at the ±2g power-on full scale is a clipped reading.
TEST(FaultDetectionTest, AccelSaturationDegradesImu) {
    FaultDetector d;
    TelemetryFrame f = healthy_frame(0);
    f.accel_x = 2.0f * 9.81f;
    EXPECT_EQ(d.check(f).imu_status, ChannelStatus::DEGRADED);
    const auto events = d.take_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].channel, Channel::IMU);
    EXPECT_EQ(events[0].type, FaultType::OUT_OF_RANGE);
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
