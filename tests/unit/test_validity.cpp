// Tests for the validity of every computed field (REQ-PROC-007), judged by
// TelemetryProcessor, and the test that every emitted frame is judged
// (REQ-PROC-005). Test card TC-017 steps 3 through 8. Frames are built by
// hand so each step controls which channel exists, is fresh, or is
// trusted; the detector is bypassed except in the last test, so the status
// a step wants is set directly, as ComplementaryTest.DegradedImuHoldsAttitude
// does.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "drivers/simulated_source.h"
#include "processing/altitude.h"
#include "processing/fault_detector.h"
#include "processing/processor.h"
#include "telemetry_frame.h"

namespace {

constexpr float kG = 9.81f;

// A fresh, healthy frame at `ts`: every channel NOMINAL and read this
// cycle, pressure wobbling so the barometric altitude is not constant.
TelemetryFrame frame(uint64_t ts) {
    TelemetryFrame f{};
    f.timestamp_ms = ts;
    f.pressure_hpa = 1013.0f + static_cast<float>(ts % 5) * 0.01f;
    f.temperature_c = 21.0f;
    f.accel_z = kG;
    f.gps_altitude_m = 3.0f;
    f.baro_status = ChannelStatus::NOMINAL;
    f.imu_status = ChannelStatus::NOMINAL;
    f.gps_status = ChannelStatus::NOMINAL;
    f.baro_read_ok = true;
    f.imu_read_ok = true;
    f.gps_read_ok = true;
    return f;
}

// REQ-PROC-005's missing assertion, applied to every frame these tests
// emit: the pipeline judged all five fields. NOT_JUDGED is the zero no
// pipeline path may leave behind.
void expect_judged(const TelemetryFrame& out, const char* where) {
    EXPECT_NE(out.baro_altitude_validity, Validity::NOT_JUDGED) << where;
    EXPECT_NE(out.pitch_validity, Validity::NOT_JUDGED) << where;
    EXPECT_NE(out.roll_validity, Validity::NOT_JUDGED) << where;
    EXPECT_NE(out.fused_altitude_validity, Validity::NOT_JUDGED) << where;
    EXPECT_NE(out.vertical_speed_validity, Validity::NOT_JUDGED) << where;
}

}  // namespace

// TC-017 step 3. Before the first fresh barometer reading nothing has been
// estimated: 0 and NO_COMPUTED_DATA on all three altitude fields, never the
// 44 km the ISA formula gives a pressure of 0. A barometer DEGRADED before
// it ever read is still "never existed", not a flag over nothing
// (precedence). The first fresh frame is NORMAL on all three, the fused
// altitude born at the barometric one. (REQ-PROC-007, REQ-PROC-001)
TEST(ProcessorValidityTest, AltitudeBeforeFirstBaroReadingIsNoComputedData) {
    TelemetryProcessor p;

    TelemetryFrame f0 = frame(0);
    f0.pressure_hpa = 0.0f;          // a driver that has not read yet leaves zero
    f0.baro_read_ok = false;
    f0.gps_read_ok = false;
    const TelemetryFrame out0 = p.process(f0);
    EXPECT_EQ(out0.baro_altitude_m, 0.0f);
    EXPECT_EQ(out0.fused_altitude_m, 0.0f);
    EXPECT_EQ(out0.vertical_speed_mps, 0.0f);
    EXPECT_EQ(out0.baro_altitude_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out0.fused_altitude_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out0.vertical_speed_validity, Validity::NO_COMPUTED_DATA);
    expect_judged(out0, "before the first reading");

    TelemetryFrame f1 = frame(20);
    f1.pressure_hpa = 200.0f;        // out of range: the detector would have marked it
    f1.baro_status = ChannelStatus::DEGRADED;
    f1.gps_read_ok = false;
    const TelemetryFrame out1 = p.process(f1);
    EXPECT_EQ(out1.baro_altitude_m, 0.0f);
    EXPECT_EQ(out1.baro_altitude_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out1.fused_altitude_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out1.vertical_speed_validity, Validity::NO_COMPUTED_DATA);
    expect_judged(out1, "degraded before any reading");

    TelemetryFrame f2 = frame(40);
    f2.gps_read_ok = false;          // no GPS, so the first estimate is the barometric altitude exactly
    const TelemetryFrame out2 = p.process(f2);
    EXPECT_EQ(out2.baro_altitude_validity, Validity::NORMAL);
    EXPECT_EQ(out2.fused_altitude_validity, Validity::NORMAL);
    EXPECT_EQ(out2.vertical_speed_validity, Validity::NORMAL);
    EXPECT_EQ(out2.fused_altitude_m, out2.baro_altitude_m);
    EXPECT_NE(out2.baro_altitude_m, 0.0f);
    expect_judged(out2, "first fresh frame");
}

// TC-017 step 4. Pitch and roll are NO_COMPUTED_DATA until the filter has
// adopted a usable accelerometer tilt: a stale IMU, an unusable
// accelerometer, and a DEGRADED IMU before any seed all keep them there,
// at 0. The first usable sample seeds them and they are NORMAL.
// (REQ-PROC-007, REQ-PROC-002)
TEST(ProcessorValidityTest, AttitudeBeforeFirstSeedIsNoComputedData) {
    TelemetryProcessor p;

    TelemetryFrame f0 = frame(0);
    f0.imu_read_ok = false;
    const TelemetryFrame out0 = p.process(f0);
    EXPECT_EQ(out0.pitch_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out0.roll_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out0.pitch_deg, 0.0f);

    TelemetryFrame f1 = frame(20);
    f1.accel_z = 0.0f;               // no usable gravity: the filter cannot seed
    const TelemetryFrame out1 = p.process(f1);
    EXPECT_EQ(out1.pitch_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out1.roll_validity, Validity::NO_COMPUTED_DATA);

    TelemetryFrame f2 = frame(40);
    f2.imu_status = ChannelStatus::DEGRADED;
    const TelemetryFrame out2 = p.process(f2);
    EXPECT_EQ(out2.pitch_validity, Validity::NO_COMPUTED_DATA);   // never existed outranks degraded
    EXPECT_EQ(out2.pitch_deg, 0.0f);

    TelemetryFrame f3 = frame(60);
    f3.accel_x = kG;                 // 45 degrees of pitch
    const TelemetryFrame out3 = p.process(f3);
    EXPECT_EQ(out3.pitch_validity, Validity::NORMAL);
    EXPECT_EQ(out3.roll_validity, Validity::NORMAL);
    EXPECT_NEAR(out3.pitch_deg, 45.0f, 0.1f);
    for (const TelemetryFrame* out : {&out0, &out1, &out2, &out3}) {
        expect_judged(*out, "attitude start-up");
    }
}

// TC-017 step 5. Under a DEGRADED IMU the angles are held bit for bit and
// flagged FAILURE_WARNING: the frame keeps a defined number, the flag is
// what the display acts on (FAULT-006). A stale NOMINAL frame inside the
// timeout holds the angles and stays NORMAL: staleness is the detector's
// call. (REQ-PROC-007, REQ-PROC-006)
TEST(ProcessorValidityTest, DegradedImuIsFailureWarningAndHeld) {
    TelemetryProcessor p;
    const TelemetryFrame first = p.process(frame(0));
    ASSERT_EQ(first.pitch_validity, Validity::NORMAL);

    TelemetryFrame degraded = frame(20);
    degraded.imu_status = ChannelStatus::DEGRADED;
    degraded.accel_x = kG;           // 45 degrees, which must not move the estimate
    const TelemetryFrame second = p.process(degraded);
    EXPECT_EQ(second.pitch_deg, first.pitch_deg);
    EXPECT_EQ(second.roll_deg, first.roll_deg);
    EXPECT_EQ(second.pitch_validity, Validity::FAILURE_WARNING);
    EXPECT_EQ(second.roll_validity, Validity::FAILURE_WARNING);

    TelemetryFrame stale = frame(40);
    stale.imu_read_ok = false;
    stale.accel_x = kG;
    const TelemetryFrame third = p.process(stale);
    EXPECT_EQ(third.pitch_deg, first.pitch_deg);
    EXPECT_EQ(third.pitch_validity, Validity::NORMAL);
    EXPECT_EQ(third.roll_validity, Validity::NORMAL);
    expect_judged(second, "degraded IMU");
    expect_judged(third, "stale IMU");
}

// TC-017 step 6. After a gap longer than the IMU timeout the filter
// re-seeds on the next accepted frame: with a usable accelerometer the
// reset and the re-seed happen on the same frame, which is NORMAL with the
// accelerometer's tilt. With an unusable one the old angles are held and
// flagged FAILURE_WARNING until a usable sample arrives.
// (REQ-PROC-007, REQ-PROC-002)
TEST(ProcessorValidityTest, ReseedAfterLongGapIsNormalUnlessAccelUnusable) {
    auto run_gap = [](TelemetryProcessor& p) {
        p.process(frame(0));
        p.process(frame(20));
        for (uint64_t ts = 40; ts <= 520; ts += 20) {
            TelemetryFrame stale = frame(ts);
            stale.imu_read_ok = false;
            p.process(stale);
        }
    };

    TelemetryProcessor usable;
    run_gap(usable);
    TelemetryFrame recovery = frame(540);
    recovery.gyro_y = 5.0f;          // not integrated across the outage
    const TelemetryFrame out = usable.process(recovery);
    EXPECT_EQ(out.pitch_validity, Validity::NORMAL);
    EXPECT_EQ(out.roll_validity, Validity::NORMAL);
    EXPECT_NEAR(out.pitch_deg, 0.0f, 0.001f);

    TelemetryProcessor unusable;
    run_gap(unusable);
    const TelemetryFrame before = unusable.process([] { TelemetryFrame s = frame(540); s.imu_read_ok = false; return s; }());
    TelemetryFrame dead = frame(560);
    dead.accel_z = 0.0f;             // no gravity to re-seed from
    dead.gyro_y = 5.0f;
    const TelemetryFrame held = unusable.process(dead);
    EXPECT_EQ(held.pitch_validity, Validity::FAILURE_WARNING);
    EXPECT_EQ(held.roll_validity, Validity::FAILURE_WARNING);
    EXPECT_EQ(held.pitch_deg, before.pitch_deg);
    EXPECT_EQ(held.roll_deg, before.roll_deg);
    const TelemetryFrame reseeded = unusable.process(frame(580));
    EXPECT_EQ(reseeded.pitch_validity, Validity::NORMAL);
    expect_judged(out, "re-seed");
    expect_judged(held, "unusable re-seed");
}

// TC-017 step 7. A DEGRADED barometer with a contributing GPS: the fused
// fields stay NORMAL (FAULT-001's "relies solely on GPS"), the barometric
// altitude is FAILURE_WARNING and holds the conversion of the last accepted
// reading while the raw pressure carries the out-of-range value
// (FAULT-008's "discard reading"). (REQ-PROC-007)
TEST(ProcessorValidityTest, BarometerDegradedWithContributingGps) {
    TelemetryProcessor p;
    TelemetryFrame last_accepted{};
    uint64_t ts = 0;
    for (int i = 0; i < 20; i++, ts += 20) {
        last_accepted = p.process(frame(ts));
    }
    ASSERT_EQ(last_accepted.baro_altitude_validity, Validity::NORMAL);

    for (int i = 0; i < 5; i++, ts += 20) {
        TelemetryFrame f = frame(ts);
        f.pressure_hpa = 200.0f;
        f.baro_status = ChannelStatus::DEGRADED;
        const TelemetryFrame out = p.process(f);
        EXPECT_EQ(out.pressure_hpa, 200.0f) << "the raw field is the source's record";
        EXPECT_EQ(out.baro_altitude_m, last_accepted.baro_altitude_m) << "frame " << i;
        EXPECT_EQ(out.baro_altitude_validity, Validity::FAILURE_WARNING) << "frame " << i;
        EXPECT_EQ(out.fused_altitude_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_EQ(out.vertical_speed_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_GT(out.fused_altitude_m, -50.0f);   // not the -13 km a 200 hPa reading implies
        EXPECT_LT(out.fused_altitude_m, 50.0f);
        expect_judged(out, "barometer degraded, GPS contributing");
    }
}

// TC-017 step 7, continued. With no contributing altitude source the
// filter coasts on prediction and both fused fields are FAILURE_WARNING at
// once: whether both channels are DEGRADED or the GPS is NOMINAL but has
// never fixed, since a channel the filter never measured contributes
// nothing. The value is the coasting prediction: identical to a twin that
// coasts for the ordinary reason, a stale GPS inside its fix period,
// which keeps its NORMAL. (REQ-PROC-007, REQ-PROC-006)
TEST(ProcessorValidityTest, NoContributingAltitudeSourceIsFailureWarning) {
    TelemetryProcessor both_degraded;
    TelemetryProcessor coasting_twin;
    uint64_t ts = 0;
    for (int i = 0; i < 20; i++, ts += 20) {
        both_degraded.process(frame(ts));
        coasting_twin.process(frame(ts));
    }
    for (int i = 0; i < 10; i++, ts += 20) {
        TelemetryFrame f = frame(ts);
        f.baro_read_ok = false;
        f.gps_read_ok = false;
        TelemetryFrame d = f;
        d.baro_status = ChannelStatus::DEGRADED;
        d.gps_status = ChannelStatus::DEGRADED;
        const TelemetryFrame a = both_degraded.process(d);
        const TelemetryFrame b = coasting_twin.process(f);
        EXPECT_EQ(a.fused_altitude_validity, Validity::FAILURE_WARNING) << "frame " << i;
        EXPECT_EQ(a.vertical_speed_validity, Validity::FAILURE_WARNING) << "frame " << i;
        EXPECT_EQ(b.fused_altitude_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_EQ(a.fused_altitude_m, b.fused_altitude_m) << "frame " << i;
        EXPECT_EQ(a.vertical_speed_mps, b.vertical_speed_mps) << "frame " << i;
        expect_judged(a, "both degraded");
    }

    // The GPS that never fixed: NOMINAL throughout, or NO_DATA, and never
    // measured. The barometer's degradation leaves nothing contributing.
    for (const ChannelStatus never_fixed : {ChannelStatus::NOMINAL, ChannelStatus::NO_DATA}) {
        TelemetryProcessor p;
        ts = 0;
        for (int i = 0; i < 20; i++, ts += 20) {
            TelemetryFrame f = frame(ts);
            f.gps_status = never_fixed;
            f.gps_read_ok = false;
            const TelemetryFrame out = p.process(f);
            EXPECT_EQ(out.fused_altitude_validity, Validity::NORMAL) << "barometer alone, frame " << i;
        }
        TelemetryFrame f = frame(ts);
        f.gps_status = never_fixed;
        f.gps_read_ok = false;
        f.baro_status = ChannelStatus::DEGRADED;
        const TelemetryFrame out = p.process(f);
        EXPECT_EQ(out.fused_altitude_validity, Validity::FAILURE_WARNING);
        EXPECT_EQ(out.vertical_speed_validity, Validity::FAILURE_WARNING);
        EXPECT_EQ(out.baro_altitude_validity, Validity::FAILURE_WARNING);
        expect_judged(out, "GPS never fixed");
    }
}

// TC-017 step 20. A filter born from the GPS (REQ-PROC-003, 0.12.0) is
// NORMAL on the GPS alone and FAILURE_WARNING the moment the GPS is
// DEGRADED, because the barometer never contributed; a barometer that is
// NOMINAL but has never read counts for nothing either, which is the
// `ever` flag on the barometer's side of the rule. The barometric altitude
// stays NO_COMPUTED_DATA throughout. (REQ-PROC-007, REQ-PROC-003)
TEST(ProcessorValidityTest, GpsBornFilterIsNormalOnTheGpsAlone) {
    for (const ChannelStatus baro_never_read : {ChannelStatus::NO_DATA, ChannelStatus::NOMINAL}) {
        TelemetryProcessor p;
        uint64_t ts = 0;
        for (int i = 0; i < 100; i++, ts += 20) {
            TelemetryFrame f = frame(ts);
            f.baro_status = baro_never_read;
            f.baro_read_ok = false;
            f.gps_read_ok = (i % 50 == 0);
            const TelemetryFrame out = p.process(f);
            EXPECT_EQ(out.baro_altitude_validity, Validity::NO_COMPUTED_DATA) << "frame " << i;
            EXPECT_EQ(out.fused_altitude_validity, Validity::NORMAL) << "frame " << i;
            EXPECT_EQ(out.vertical_speed_validity, Validity::NORMAL) << "frame " << i;
            expect_judged(out, "GPS-born, GPS contributing");
        }
        for (int i = 0; i < 5; i++, ts += 20) {
            TelemetryFrame f = frame(ts);
            f.baro_status = baro_never_read;
            f.baro_read_ok = false;
            f.gps_status = ChannelStatus::DEGRADED;
            f.gps_read_ok = false;
            const TelemetryFrame out = p.process(f);
            EXPECT_EQ(out.fused_altitude_validity, Validity::FAILURE_WARNING) << "frame " << i;
            EXPECT_EQ(out.vertical_speed_validity, Validity::FAILURE_WARNING) << "frame " << i;
            expect_judged(out, "GPS-born, GPS degraded, barometer never read");
        }
    }
}

// TC-017 step 20 (REQ-FAULT-007, REQ-SENS-011). A channel reported
// NOT_AVAILABLE is not NOMINAL, so the processor never consumes it and
// needs no rule of its own: with the barometer and the IMU not provided,
// as in ADS-B mode, barometric altitude, pitch and roll stay
// NO_COMPUTED_DATA at 0 on every frame, and the fused fields wait for the
// first fresh GPS reading and are NORMAL from it, born from the GPS.
TEST(ProcessorValidityTest, NotAvailableChannelNeverContributes) {
    TelemetryProcessor p;
    uint64_t ts = 0;
    for (int i = 0; i < 100; i++, ts += 20) {
        TelemetryFrame f = frame(ts);
        f.baro_status = ChannelStatus::NOT_AVAILABLE;
        f.baro_read_ok = false;
        f.pressure_hpa = 0.0f;
        f.imu_status = ChannelStatus::NOT_AVAILABLE;
        f.imu_read_ok = false;
        f.accel_z = 0.0f;
        const bool fixed_yet = i >= 25;   // the first fix on frame 25, one a second after it
        f.gps_status = fixed_yet ? ChannelStatus::NOMINAL : ChannelStatus::NO_DATA;
        f.gps_read_ok = fixed_yet && (i - 25) % 50 == 0;
        const TelemetryFrame out = p.process(f);
        EXPECT_EQ(out.baro_altitude_validity, Validity::NO_COMPUTED_DATA) << "frame " << i;
        EXPECT_EQ(out.pitch_validity, Validity::NO_COMPUTED_DATA) << "frame " << i;
        EXPECT_EQ(out.roll_validity, Validity::NO_COMPUTED_DATA) << "frame " << i;
        EXPECT_EQ(out.baro_altitude_m, 0.0f) << "frame " << i;
        EXPECT_EQ(out.pitch_deg, 0.0f) << "frame " << i;
        if (fixed_yet) {
            EXPECT_EQ(out.fused_altitude_validity, Validity::NORMAL) << "frame " << i;
            EXPECT_EQ(out.fused_altitude_m, 3.0f) << "frame " << i;   // the GPS altitude, at rest
        } else {
            EXPECT_EQ(out.fused_altitude_validity, Validity::NO_COMPUTED_DATA) << "frame " << i;
            EXPECT_EQ(out.fused_altitude_m, 0.0f) << "frame " << i;
        }
        expect_judged(out, "barometer and IMU not available");
    }
}

// TC-017 step 8, REQ-PROC-005's missing test: through the real detector
// and processor, on the simulator's frames, no field of any emitted frame
// is NOT_JUDGED, and on this healthy source every field is NORMAL from
// frame 0 (the simulator is fresh on every channel at t = 0).
// TC-031 step 9 (REQ-PROC-008, REQ-PROC-007). After the flag nothing of the
// previous vehicle remains. With nothing fresh the barometric altitude,
// pitch, roll and the fused fields are 0 and NO_COMPUTED_DATA, the "never
// existed" of a new vehicle, not the last vehicle's numbers held; when
// readings are fresh on the flagged frame they begin the new vehicle on
// that frame: the attitude seeded from its accelerometer, not blended
// from the 30 degrees before, the barometric altitude its pressure's, the
// fused altitude born from that barometer.
TEST(ProcessorValidityTest, NewVehicleStartsEveryFieldAgain) {
    TelemetryProcessor processor;
    uint64_t ts = 0;
    for (int i = 0; i < 20; i++, ts += 20) {
        TelemetryFrame f = frame(ts);
        f.accel_x = kG * 0.5f;            // the previous vehicle, pitched about 30 degrees
        f.accel_z = kG * 0.866f;
        const TelemetryFrame out = processor.process(f);
        ASSERT_EQ(out.pitch_validity, Validity::NORMAL);
        ASSERT_GT(std::abs(out.pitch_deg), 20.0f);
    }

    TelemetryFrame quiet = frame(ts);
    quiet.baro_read_ok = false;
    quiet.imu_read_ok = false;
    quiet.gps_read_ok = false;
    set_flag(quiet, FrameFlag::NEW_VEHICLE);
    const TelemetryFrame out = processor.process(quiet);
    expect_judged(out, "flagged, nothing fresh");
    EXPECT_EQ(out.baro_altitude_m, 0.0f);
    EXPECT_EQ(out.pitch_deg, 0.0f);
    EXPECT_EQ(out.roll_deg, 0.0f);
    EXPECT_EQ(out.fused_altitude_m, 0.0f);
    EXPECT_EQ(out.baro_altitude_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.pitch_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.roll_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.fused_altitude_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(out.vertical_speed_validity, Validity::NO_COMPUTED_DATA);
    ts += 20;

    TelemetryFrame fresh = frame(ts);     // level: the accelerometer on z alone
    fresh.gps_read_ok = false;            // so the birth is the barometer's alone
    set_flag(fresh, FrameFlag::NEW_VEHICLE);
    const TelemetryFrame again = processor.process(fresh);
    expect_judged(again, "flagged, fresh");
    EXPECT_EQ(again.pitch_deg, 0.0f);
    EXPECT_EQ(again.roll_deg, 0.0f);
    EXPECT_EQ(again.pitch_validity, Validity::NORMAL);
    EXPECT_EQ(again.baro_altitude_m, pressure_to_altitude(fresh.pressure_hpa));
    EXPECT_EQ(again.baro_altitude_validity, Validity::NORMAL);
    EXPECT_EQ(again.fused_altitude_m, again.baro_altitude_m);
    EXPECT_EQ(again.vertical_speed_mps, 0.0f);
    EXPECT_EQ(again.fused_altitude_validity, Validity::NORMAL);
}

TEST(ProcessorValidityTest, EveryEmittedFrameIsJudged) {
    SimulatedSource source(20, 42);
    FaultDetector detector;
    TelemetryProcessor processor;
    for (int i = 0; i < 200; i++) {
        const TelemetryFrame out = processor.process(detector.check(source.read_frame()));
        expect_judged(out, "simulated frame");
        EXPECT_EQ(out.baro_altitude_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_EQ(out.pitch_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_EQ(out.roll_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_EQ(out.fused_altitude_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_EQ(out.vertical_speed_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_EQ(out.baro_status, ChannelStatus::NOMINAL) << "frame " << i;
        EXPECT_EQ(out.gps_status, ChannelStatus::NOMINAL) << "frame " << i;
    }
}
