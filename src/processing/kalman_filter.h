#pragma once

// 1D Kalman filter for altitude fusion (REQ-PROC-003).
//
// "1D" means one spatial dimension (vertical). The filter still has two
// states, because tracking altitude well requires knowing how fast it is
// changing:
//
//   x = [ altitude (m), vertical velocity (m/s) ]
//
// Alongside the state the filter carries P, a 2x2 covariance matrix that
// is its own opinion of how uncertain the estimate is. The diagonal holds
// the variance of each state (sigma squared, in m^2 and (m/s)^2); the
// off-diagonal holds how altitude and velocity errors move together.
// P is symmetric (p01 == p10), so only three numbers are stored.
//
// The two operations alternate every processing cycle:
//
//   predict(dt): move the state forward in time with a physics model and
//                grow P, because time passing without a measurement makes
//                the estimate less certain.
//   update(z, R): pull the state toward a measurement z whose noise
//                variance is R, and shrink P, because new information
//                makes the estimate more certain.
//
// Everything is explicit float arithmetic. The matrices are 2x2 and the
// measurement is scalar, so a linear algebra library would hide five lines
// of math behind a dependency.
class KalmanFilter1D {
public:
    // initial_altitude_m / initial_velocity_mps: where the state starts.
    // initial_altitude_variance / initial_velocity_variance: how much to
    //   distrust those starting values. Large values let the first few
    //   measurements dominate; small values make the filter stubborn.
    // accel_noise_variance: variance of the unmodelled vertical
    //   acceleration in (m/s^2)^2. This is the single knob behind the
    //   process noise matrix Q (see predict). Larger means "the aircraft
    //   may accelerate a lot between frames", so the filter trusts its
    //   own prediction less and follows measurements faster.
    KalmanFilter1D(float initial_altitude_m,
                   float initial_velocity_mps,
                   float initial_altitude_variance,
                   float initial_velocity_variance,
                   float accel_noise_variance);

    // Advance the estimate by dt seconds under a constant-velocity model.
    // dt <= 0 is ignored: no time passed, so nothing changes.
    void predict(float dt);

    // Correct the estimate with a direct altitude measurement.
    // measurement_noise_variance is R: the sensor's noise variance in m^2.
    // A noisy sensor (large R) moves the state less than a precise one.
    void update(float measured_altitude_m, float measurement_noise_variance);

    float altitude() const { return altitude_m_; }
    float vertical_velocity() const { return velocity_mps_; }

    // Variance of the altitude estimate (P[0][0]). Diagnostics only: a
    // display or logger can show how confident the filter is right now.
    float altitude_variance() const { return p00_; }

private:
    // State vector x.
    float altitude_m_;
    float velocity_mps_;

    // Covariance matrix P, stored as its three unique elements:
    //   P = [ p00  p01 ]
    //       [ p01  p11 ]
    float p00_;
    float p01_;
    float p11_;

    // Process noise strength, held so predict() can build Q from dt.
    float accel_noise_variance_;
};
