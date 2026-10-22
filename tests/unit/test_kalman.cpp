// Unit tests for src/processing/kalman_filter.cpp and its use in
// TelemetryProcessor (REQ-PROC-003). The first four tests pin the filter's
// algebra with hand-picked numbers; the last two run the real pipeline to
// prove the filter smooths noise and stays deterministic.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "drivers/simulated_source.h"
#include "processing/altitude.h"
#include "processing/kalman_filter.h"
#include "processing/processor.h"
#include "telemetry_frame.h"

namespace {

// Shared starting point: altitude 100 m, at rest, moderately uncertain.
// (altitude, velocity, altitude variance, velocity variance, accel noise variance)
KalmanFilter1D make_filter(float altitude, float velocity) {
    return KalmanFilter1D(altitude, velocity, 1.0f, 1.0f, 1.0f);
}

// Population standard deviation: sqrt of the mean squared distance from
// the mean. Two passes over the data, mean first, keeps the arithmetic
// obvious at the cost of a second loop.
float standard_deviation(const std::vector<float>& values) {
    double sum = 0.0;
    for (float v : values) {
        sum += v;
    }
    const double mean = sum / static_cast<double>(values.size());

    double sum_sq = 0.0;
    for (float v : values) {
        const double d = v - mean;
        sum_sq += d * d;
    }
    return static_cast<float>(std::sqrt(sum_sq / static_cast<double>(values.size())));
}

}  // namespace

// With zero velocity the constant-velocity model predicts no motion, but
// time passing still adds process noise, so uncertainty must grow.
// (REQ-PROC-003)
TEST(KalmanTest, PredictZeroVelocityPreservesAltitude) {
    KalmanFilter1D filter = make_filter(100.0f, 0.0f);
    const float variance_before = filter.altitude_variance();

    filter.predict(0.1f);

    EXPECT_NEAR(filter.altitude(), 100.0f, 1e-4f);
    EXPECT_GT(filter.altitude_variance(), variance_before);
}

// altitude' = altitude + velocity * dt. 100 + 5 * 1 = 105. (REQ-PROC-003)
TEST(KalmanTest, PredictAdvancesAltitudeByVelocity) {
    KalmanFilter1D filter = make_filter(100.0f, 5.0f);

    filter.predict(1.0f);

    EXPECT_NEAR(filter.altitude(), 105.0f, 1e-4f);
    EXPECT_NEAR(filter.vertical_velocity(), 5.0f, 1e-4f);
}

// A measurement can only add information, so P must shrink on update.
// (REQ-PROC-003)
TEST(KalmanTest, UpdateShrinksCovariance) {
    KalmanFilter1D filter = make_filter(100.0f, 0.0f);
    filter.predict(0.1f);
    const float variance_after_predict = filter.altitude_variance();

    filter.update(100.0f, 3.0f);

    EXPECT_LT(filter.altitude_variance(), variance_after_predict);
}

// Two identical filters, same measurement, different trust. The one told
// the sensor is precise (R = 1) must move further toward it than the one
// told the sensor is noisy (R = 100). (REQ-PROC-003)
TEST(KalmanTest, LargeRMovesStateLess) {
    KalmanFilter1D trusting = make_filter(100.0f, 0.0f);
    KalmanFilter1D skeptical = make_filter(100.0f, 0.0f);
    trusting.predict(0.1f);
    skeptical.predict(0.1f);

    trusting.update(110.0f, 1.0f);
    skeptical.update(110.0f, 100.0f);

    const float trusting_error = std::fabs(110.0f - trusting.altitude());
    const float skeptical_error = std::fabs(110.0f - skeptical.altitude());
    EXPECT_LT(trusting_error, skeptical_error);

    // Neither may overshoot: the gain is bounded in [0, 1].
    EXPECT_GT(trusting.altitude(), 100.0f);
    EXPECT_LE(trusting.altitude(), 110.0f);
    EXPECT_GT(skeptical.altitude(), 100.0f);
    EXPECT_LE(skeptical.altitude(), 110.0f);
}

// End-to-end: the simulator adds sigma 0.0282 hPa (about 0.23 m) of
// pressure noise, the sensor's own (FTS-NP-001). The fused output must
// scatter far less than the raw barometric altitude, or the filter is not
// filtering. The 0.5 factor is a loose bound; the measured ratio at the
// current tuning is about 0.21 (0.13 at the placeholder tuning, 0.15
// before the simulator's GPS went to 1 Hz).
// (REQ-PROC-003)
TEST(KalmanTest, ConvergenceReducesNoise) {
    SimulatedSource source(20, 42);
    TelemetryProcessor processor;

    std::vector<float> baro_altitudes;
    std::vector<float> fused_altitudes;
    for (int i = 0; i < 500; i++) {
        const TelemetryFrame frame = processor.process(source.read_frame());
        baro_altitudes.push_back(frame.baro_altitude_m);
        fused_altitudes.push_back(frame.fused_altitude_m);
    }

    const float baro_sigma = standard_deviation(baro_altitudes);
    const float fused_sigma = standard_deviation(fused_altitudes);

    EXPECT_LT(fused_sigma, 0.5f * baro_sigma)
        << "baro sigma " << baro_sigma << " m, fused sigma " << fused_sigma << " m";
}

// Same seed, same interval, same code path: every fused altitude must be
// bit-identical between two runs. EXPECT_EQ on floats is deliberate here.
// A tolerance would hide exactly the kind of drift that breaks deterministic
// replay (REQ-LOG-003). (REQ-PROC-003)
TEST(KalmanTest, DeterministicOutput) {
    std::vector<float> first_run;
    std::vector<float> second_run;

    for (std::vector<float>* run : {&first_run, &second_run}) {
        SimulatedSource source(20, 42);
        TelemetryProcessor processor;
        for (int i = 0; i < 100; i++) {
            run->push_back(processor.process(source.read_frame()).fused_altitude_m);
        }
    }

    ASSERT_EQ(first_run.size(), second_run.size());
    for (size_t i = 0; i < first_run.size(); i++) {
        EXPECT_EQ(first_run[i], second_run[i]) << "frame " << i;
    }
}

// dt of zero (or negative, from a repeated timestamp) is a no-op: no time
// passed, so neither the state nor the covariance may change. (REQ-PROC-003)
TEST(KalmanTest, PredictWithZeroDtIsNoOp) {
    KalmanFilter1D filter = make_filter(100.0f, 5.0f);
    const float variance_before = filter.altitude_variance();
    filter.predict(0.0f);
    filter.predict(-0.02f);
    EXPECT_EQ(filter.altitude(), 100.0f);
    EXPECT_EQ(filter.altitude_variance(), variance_before);
}

// A DEGRADED GPS channel contributes nothing: the fused altitude must be
// identical whether the frame carries a sane GPS altitude or garbage.
// GPS on every frame (third argument), so the status gate is exercised on
// all 20 frames rather than on the one fix a 1 Hz schedule would give.
// (REQ-PROC-003, REQ-FAULT-004)
TEST(KalmanTest, DegradedGpsIsIgnored) {
    SimulatedSource source(20, 42, 20);
    TelemetryProcessor a;
    TelemetryProcessor b;
    for (int i = 0; i < 20; i++) {
        TelemetryFrame f = source.read_frame();
        f.gps_status = ChannelStatus::DEGRADED;
        TelemetryFrame g = f;
        g.gps_altitude_m = 9999.0f;
        EXPECT_EQ(a.process(f).fused_altitude_m, b.process(g).fused_altitude_m) << "frame " << i;
    }
}

// ---------------------------------------------------------------------------
// REQ-PROC-006: predict every cycle, update only on a fresh NOMINAL reading.
// Test card TC-011. "Twin" means a second processor fed the same frames
// with one field changed; equality is exact, because the claim is that
// the computation is identical, not close.
// ---------------------------------------------------------------------------

namespace {

// A fresh, healthy frame at `ts`, built by hand so each test controls
// exactly which channel is fresh. Pressure wobbles slightly so the
// barometric altitude is not constant.
TelemetryFrame fresh_frame(uint64_t ts) {
    TelemetryFrame f{};
    f.timestamp_ms = ts;
    f.pressure_hpa = 1013.0f + static_cast<float>(ts % 5) * 0.01f;
    f.temperature_c = 21.0f;
    f.accel_z = 9.81f;
    f.gps_altitude_m = 3.0f;
    f.baro_status = ChannelStatus::NOMINAL;
    f.imu_status = ChannelStatus::NOMINAL;
    f.gps_status = ChannelStatus::NOMINAL;
    f.baro_read_ok = true;
    f.imu_read_ok = true;
    f.gps_read_ok = true;
    return f;
}

}  // namespace

// TC-011 step 1. A failed barometer read inside the timeout window leaves
// the channel NOMINAL and the pressure field stale. The filter must coast
// exactly as it would if the channel were DEGRADED: predict only, no
// update from a reading nobody took this cycle. (REQ-PROC-006, REQ-PROC-003)
TEST(KalmanTest, StaleBaroInsideTimeoutIsPredictOnly) {
    TelemetryProcessor stale;      // sees baro_read_ok false, status NOMINAL
    TelemetryProcessor degraded;   // sees the same frames with baro_status DEGRADED
    uint64_t ts = 0;
    for (int i = 0; i < 20; i++, ts += 20) {
        const TelemetryFrame f = fresh_frame(ts);
        stale.process(f);
        degraded.process(f);
    }
    const float held_pressure = fresh_frame(ts - 20).pressure_hpa;
    for (int i = 0; i < 10; i++, ts += 20) {
        TelemetryFrame f = fresh_frame(ts);
        f.pressure_hpa = held_pressure;   // a driver that got no reply holds the field
        f.baro_read_ok = false;
        f.gps_read_ok = false;            // between GPS fixes, so prediction is all that remains
        TelemetryFrame d = f;
        d.baro_status = ChannelStatus::DEGRADED;
        const TelemetryFrame a = stale.process(f);
        const TelemetryFrame b = degraded.process(d);
        EXPECT_EQ(a.fused_altitude_m, b.fused_altitude_m) << "frame " << i;
        EXPECT_EQ(a.vertical_speed_mps, b.vertical_speed_mps) << "frame " << i;
        // Same numbers, different verdicts: the stale NOMINAL frame is a
        // current estimate inside the timeout (REQ-PROC-007).
        EXPECT_EQ(a.fused_altitude_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_EQ(a.baro_altitude_validity, Validity::NORMAL) << "frame " << i;
        EXPECT_EQ(b.baro_altitude_validity, Validity::FAILURE_WARNING) << "frame " << i;
    }
}

// TC-011 step 2. Between fixes the GPS fields are stale. A stale frame's
// GPS altitude must not reach the filter, whatever value it holds: the
// output is identical whether the stale frame carries the held fix or
// garbage. (REQ-PROC-006, REQ-PROC-003)
TEST(KalmanTest, StaleGpsIsNotApplied) {
    SimulatedSource source(20, 42, 20);   // GPS on every frame for the warm-up
    TelemetryProcessor sane;
    TelemetryProcessor garbage;
    for (int i = 0; i < 20; i++) {
        const TelemetryFrame f = source.read_frame();
        sane.process(f);
        garbage.process(f);
    }
    for (int i = 0; i < 20; i++) {
        TelemetryFrame f = source.read_frame();
        f.gps_read_ok = false;            // no fix this cycle; the fields are stale
        TelemetryFrame g = f;
        g.gps_altitude_m = 9999.0f;
        EXPECT_EQ(sane.process(f).fused_altitude_m, garbage.process(g).fused_altitude_m) << "frame " << i;
    }
}

// TC-011 step 3. With the simulator's 1 Hz GPS in a 50 Hz stream, the
// filter applies GPS on the fix frames and nowhere else: its output is
// identical to a twin told the GPS is DEGRADED on every non-fix frame,
// and it parts from a twin told the GPS is DEGRADED after the first fix
// exactly at the second fix, which proves that fix was applied once and
// the frames between were not. (REQ-PROC-006, REQ-PROC-003, REQ-SENS-006)
TEST(KalmanTest, OneGpsUpdatePerFix) {
    SimulatedSource source(20, 42);       // 1 Hz GPS: fixes at 0 and 1000 ms
    TelemetryProcessor gated;
    TelemetryProcessor explicit_schedule;
    TelemetryProcessor first_fix_only;
    for (int i = 0; i < 100; i++) {
        const TelemetryFrame f = source.read_frame();
        TelemetryFrame e = f;
        if (!f.gps_read_ok) {
            e.gps_status = ChannelStatus::DEGRADED;
        }
        TelemetryFrame n = f;
        if (f.timestamp_ms > 0) {
            n.gps_status = ChannelStatus::DEGRADED;
        }
        const float a = gated.process(f).fused_altitude_m;
        const float b = explicit_schedule.process(e).fused_altitude_m;
        const float d = first_fix_only.process(n).fused_altitude_m;
        EXPECT_EQ(a, b) << "frame " << i;
        if (f.timestamp_ms < 1000) {
            EXPECT_EQ(a, d) << "frame " << i << ": no GPS update is due";
        } else if (f.timestamp_ms == 1000) {
            EXPECT_NE(a, d) << "the second fix must be applied";
        }
    }
}

// TC-011 step 4, and step 9 (a). A driver reports read_ok false until its
// first successful transaction. The filter must not be born from a
// pressure nobody measured: the fused outputs keep the "not computed" zero
// until the first fresh altitude reading, and with the GPS stale that is
// the barometer's, where the filter starts. (REQ-PROC-006, REQ-PROC-003)
TEST(KalmanTest, FilterWaitsForFirstFreshBaro) {
    TelemetryProcessor processor;
    TelemetryFrame f0 = fresh_frame(0);
    f0.baro_read_ok = false;
    f0.gps_read_ok = false;
    const TelemetryFrame out0 = processor.process(f0);
    EXPECT_EQ(out0.fused_altitude_m, 0.0f);
    EXPECT_EQ(out0.vertical_speed_mps, 0.0f);
    EXPECT_EQ(out0.fused_altitude_validity, Validity::NO_COMPUTED_DATA);   // TC-011 step 4 (REQ-PROC-007)
    EXPECT_EQ(out0.vertical_speed_validity, Validity::NO_COMPUTED_DATA);

    TelemetryFrame f1 = fresh_frame(20);
    f1.gps_read_ok = false;               // no GPS, so the first estimate is the baro altitude exactly
    const TelemetryFrame out1 = processor.process(f1);
    EXPECT_EQ(out1.fused_altitude_m, out1.baro_altitude_m);
    EXPECT_EQ(out1.vertical_speed_mps, 0.0f);
    EXPECT_EQ(out1.fused_altitude_validity, Validity::NORMAL);
    EXPECT_EQ(out1.vertical_speed_validity, Validity::NORMAL);
}

// TC-011 step 9 (b). A session whose barometer never reads, the ADS-B
// source's, is born from the first fresh GPS reading: at the GPS altitude,
// at rest, with the GPS variance as its uncertainty, NORMAL from that frame,
// held NORMAL between fixes and moved by the next fix. Until 0.12.0 the
// fused fields stayed NO_COMPUTED_DATA for the whole of such a session.
// (REQ-PROC-003, REQ-PROC-004, REQ-PROC-006, REQ-PROC-007)
TEST(KalmanTest, BornFromTheGpsWhenTheBarometerNeverReads) {
    TelemetryProcessor processor;
    uint64_t ts = 0;
    for (int i = 0; i < 49; i++, ts += 20) {
        TelemetryFrame f = fresh_frame(ts);
        f.baro_status = ChannelStatus::NO_DATA;
        f.baro_read_ok = false;
        f.gps_status = ChannelStatus::NO_DATA;
        f.gps_read_ok = false;
        const TelemetryFrame out = processor.process(f);
        EXPECT_EQ(out.fused_altitude_m, 0.0f) << "frame " << i;
        EXPECT_EQ(out.fused_altitude_validity, Validity::NO_COMPUTED_DATA) << "frame " << i;
    }

    TelemetryFrame fix = fresh_frame(ts);
    fix.baro_status = ChannelStatus::NO_DATA;
    fix.baro_read_ok = false;
    fix.gps_altitude_m = 120.0f;
    const TelemetryFrame born = processor.process(fix);
    EXPECT_EQ(born.fused_altitude_m, 120.0f);   // born at the reading, no innovation on its own frame
    EXPECT_EQ(born.vertical_speed_mps, 0.0f);
    EXPECT_EQ(born.fused_altitude_validity, Validity::NORMAL);
    EXPECT_EQ(born.vertical_speed_validity, Validity::NORMAL);
    EXPECT_EQ(born.baro_altitude_validity, Validity::NO_COMPUTED_DATA);
    ts += 20;

    for (int i = 0; i < 49; i++, ts += 20) {
        TelemetryFrame f = fresh_frame(ts);
        f.baro_status = ChannelStatus::NO_DATA;
        f.baro_read_ok = false;
        f.gps_altitude_m = 120.0f;
        f.gps_read_ok = false;   // held between fixes: predict only
        const TelemetryFrame out = processor.process(f);
        EXPECT_EQ(out.fused_altitude_m, 120.0f) << "frame " << i;   // at rest, prediction moves nothing
        EXPECT_EQ(out.fused_altitude_validity, Validity::NORMAL) << "frame " << i;
    }

    TelemetryFrame next = fresh_frame(ts);
    next.baro_status = ChannelStatus::NO_DATA;
    next.baro_read_ok = false;
    next.gps_altitude_m = 130.0f;
    const TelemetryFrame moved = processor.process(next);
    EXPECT_GT(moved.fused_altitude_m, 120.0f);
    EXPECT_LT(moved.fused_altitude_m, 130.0f);
    EXPECT_EQ(moved.fused_altitude_validity, Validity::NORMAL);
}

// TC-031 step 9 (REQ-PROC-008). A frame flagged NEW_VEHICLE ends the
// previous vehicle's estimate. With a fresh GPS reading 500 m below it the
// filter is reborn at that reading with zero velocity, NORMAL on that
// frame, where the same reading taken as an update would have read a
// descent nobody flew; with no fresh reading the fused fields are 0 and
// NO_COMPUTED_DATA, and the first reading after births the filter as at
// power-on. The descent's size follows the initial velocity variance:
// -0.49 m/s at the shipped tuning, against the bound's -0.1 (-4.66 m/s
// at the placeholder 1 (m/s)^2, when the bound was -1.0).
TEST(KalmanTest, NewVehicleRebirthsTheFilterAtItsFirstReading) {
    TelemetryProcessor flagged;
    TelemetryProcessor unflagged;
    // An aircraft holding 600 m, its fix fresh once in five frames; the
    // barometer is not the source's, as in ADS-B mode.
    const auto aircraft = [](uint64_t ts, float altitude_m, bool fix) {
        TelemetryFrame f = fresh_frame(ts);
        f.baro_status = ChannelStatus::NOT_AVAILABLE;
        f.baro_read_ok = false;
        f.gps_altitude_m = altitude_m;
        f.gps_read_ok = fix;
        return f;
    };
    uint64_t ts = 0;
    for (int i = 0; i < 20; i++, ts += 20) {
        flagged.process(aircraft(ts, 600.0f, i % 5 == 0));
        unflagged.process(aircraft(ts, 600.0f, i % 5 == 0));
    }

    TelemetryFrame switched = aircraft(ts, 100.0f, true);
    const TelemetryFrame update = unflagged.process(switched);
    EXPECT_GT(update.fused_altitude_m, 100.0f);         // without the flag: an update toward the step
    EXPECT_LT(update.vertical_speed_mps, -0.1f);        // and a descent nobody flew: -0.49 m/s (above)
    set_flag(switched, FrameFlag::NEW_VEHICLE);
    const TelemetryFrame reborn = flagged.process(switched);
    EXPECT_EQ(reborn.fused_altitude_m, 100.0f);         // born at the new vehicle's reading
    EXPECT_EQ(reborn.vertical_speed_mps, 0.0f);
    EXPECT_EQ(reborn.fused_altitude_validity, Validity::NORMAL);
    EXPECT_EQ(reborn.vertical_speed_validity, Validity::NORMAL);
    ts += 20;

    // A flag with nothing fresh: nothing to begin from yet.
    TelemetryFrame quiet = aircraft(ts, 100.0f, false);
    set_flag(quiet, FrameFlag::NEW_VEHICLE);
    const TelemetryFrame empty = flagged.process(quiet);
    EXPECT_EQ(empty.fused_altitude_m, 0.0f);
    EXPECT_EQ(empty.vertical_speed_mps, 0.0f);
    EXPECT_EQ(empty.fused_altitude_validity, Validity::NO_COMPUTED_DATA);
    EXPECT_EQ(empty.vertical_speed_validity, Validity::NO_COMPUTED_DATA);
    ts += 20;
    const TelemetryFrame born = flagged.process(aircraft(ts, 250.0f, true));
    EXPECT_EQ(born.fused_altitude_m, 250.0f);
    EXPECT_EQ(born.vertical_speed_mps, 0.0f);
    EXPECT_EQ(born.fused_altitude_validity, Validity::NORMAL);
}

// TC-011 step 9 (c). Both readings fresh on the birth cycle: the filter is
// born from the barometer, at the barometric variance, and the GPS reading
// is then an update on the same frame. The gain arithmetic pins the order:
// after the barometric update P00 is (1 - R/(R+R)) x R = R/2, so the GPS
// moves the estimate by (R/2)/(R/2 + 25) of the disagreement, a small
// fraction of the way. A GPS-born filter would sit at the GPS altitude
// instead. This is every session that reads a barometer on its first
// cycle, so the order keeps their bytes as they were before 0.12.0. The
// arithmetic follows the shipped variances. (REQ-PROC-003, REQ-PROC-004)
TEST(KalmanTest, BarometerWinsWhenBothAreFreshOnTheBirthCycle) {
    TelemetryProcessor processor;
    TelemetryFrame f = fresh_frame(0);
    f.gps_altitude_m = 100.0f;
    const TelemetryFrame out = processor.process(f);
    const float baro = out.baro_altitude_m;
    const ProcessorConfig tuning{};
    const float r = tuning.baro_measurement_variance_m2;
    const float p00 = (1.0f - r / (r + r)) * r;
    const float gps_gain = p00 / (p00 + tuning.gps_measurement_variance_m2);
    EXPECT_NEAR(out.fused_altitude_m, baro + gps_gain * (100.0f - baro), 1e-3f);
    EXPECT_LT(std::fabs(out.fused_altitude_m - baro), 0.1f * std::fabs(100.0f - baro));
    EXPECT_EQ(out.fused_altitude_validity, Validity::NORMAL);
}

// TC-011 step 9 (d). The GPS fresh first and the barometer arriving twenty
// cycles later: the barometer's first reading is an update, not a second
// birth. The estimate moves from the GPS-born value toward the barometric
// altitude and stops between the two; barometric altitude is NORMAL from
// that frame. (REQ-PROC-003, REQ-PROC-006, REQ-PROC-007)
TEST(KalmanTest, BarometerArrivingAfterAGpsBirthIsAnUpdate) {
    TelemetryProcessor processor;
    uint64_t ts = 0;
    TelemetryFrame before{};
    for (int i = 0; i < 20; i++, ts += 20) {
        TelemetryFrame f = fresh_frame(ts);
        f.baro_status = ChannelStatus::NO_DATA;
        f.baro_read_ok = false;
        f.gps_altitude_m = 100.0f;
        f.gps_read_ok = (i == 0);   // one fix, then held
        before = processor.process(f);
        EXPECT_EQ(before.baro_altitude_validity, Validity::NO_COMPUTED_DATA) << "frame " << i;
    }
    EXPECT_EQ(before.fused_altitude_m, 100.0f);

    TelemetryFrame f = fresh_frame(ts);   // the barometer's first fresh reading; the GPS held
    f.gps_altitude_m = 100.0f;
    f.gps_read_ok = false;
    const TelemetryFrame after = processor.process(f);
    EXPECT_EQ(after.baro_altitude_validity, Validity::NORMAL);
    EXPECT_GT(after.fused_altitude_m, after.baro_altitude_m);    // not re-born at the barometric altitude
    EXPECT_LT(after.fused_altitude_m, before.fused_altitude_m);  // moved toward it
    EXPECT_EQ(after.fused_altitude_validity, Validity::NORMAL);
}

// TC-016 step 10. The Kalman tuning reaches the filter: a processor told
// the GPS is a hundred times noisier (variance 2500 against 25) moves
// less toward the first fix. Both start from the same barometric
// altitude, so the GPS innovation is the same and only the gain differs.
// (REQ-PROC-003, REQ-CFG-001)
TEST(KalmanTest, TuningIsHonored) {
    SimulatedSource source(20, 42, 20);   // a fix on every frame
    ProcessorConfig skeptical_tuning;
    skeptical_tuning.gps_measurement_variance_m2 = 2500.0f;
    TelemetryProcessor trusting;
    TelemetryProcessor skeptical(skeptical_tuning);

    const TelemetryFrame raw = source.read_frame();
    const TelemetryFrame a = trusting.process(raw);
    const TelemetryFrame b = skeptical.process(raw);
    EXPECT_EQ(a.baro_altitude_m, b.baro_altitude_m);
    EXPECT_NE(a.fused_altitude_m, b.fused_altitude_m);
    EXPECT_LT(std::fabs(b.fused_altitude_m - b.baro_altitude_m),
              std::fabs(a.fused_altitude_m - a.baro_altitude_m));
}

// TC-016 step 10. The sea-level reference reaches the hypsometric
// formula: with the reference 10 hPa above the measured pressure the
// barometric altitude is about 83 m (TC-001's 8.3 m per hPa near sea
// level), exactly what the formula gives for that reference.
// (REQ-PROC-001, REQ-CFG-001)
TEST(KalmanTest, SeaLevelReferenceIsHonored) {
    ProcessorConfig tuning;
    tuning.sea_level_pressure_hpa = 1023.25f;
    TelemetryProcessor processor(tuning);
    TelemetryFrame f = fresh_frame(0);
    f.pressure_hpa = 1013.25f;
    const TelemetryFrame out = processor.process(f);
    EXPECT_EQ(out.baro_altitude_m, pressure_to_altitude(1013.25f, 1023.25f));
    EXPECT_NEAR(out.baro_altitude_m, 83.0f, 2.0f);
}
