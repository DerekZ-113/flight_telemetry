// Unit tests for src/processing/kalman_filter.cpp and its use in
// TelemetryProcessor (REQ-PROC-003). The first four tests pin the filter's
// algebra with hand-picked numbers; the last two run the real pipeline to
// prove the filter smooths noise and stays deterministic.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "drivers/simulated_source.h"
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

// End-to-end: the simulator adds sigma 0.2 hPa (about 1.7 m) of pressure
// noise. The fused output must scatter far less than the raw barometric
// altitude, or the filter is not filtering. The 0.5 factor is a loose
// bound; the measured ratio at the current tuning is about 0.15.
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
// (REQ-PROC-003, REQ-FAULT-004)
TEST(KalmanTest, DegradedGpsIsIgnored) {
    SimulatedSource source(20, 42);
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
