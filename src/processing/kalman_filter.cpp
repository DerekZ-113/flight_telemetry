#include "processing/kalman_filter.h"

KalmanFilter1D::KalmanFilter1D(float initial_altitude_m,
                               float initial_velocity_mps,
                               float initial_altitude_variance,
                               float initial_velocity_variance,
                               float accel_noise_variance)
    : altitude_m_(initial_altitude_m),
      velocity_mps_(initial_velocity_mps),
      p00_(initial_altitude_variance),
      p01_(0.0f),   // no reason to assume altitude and velocity errors are related yet
      p11_(initial_velocity_variance),
      accel_noise_variance_(accel_noise_variance)
{
}

void KalmanFilter1D::predict(float dt) {
    if (dt <= 0.0f) {
        return;
    }

    // State prediction: x = F * x, with F = [[1, dt], [0, 1]].
    // Written out, that is the constant-velocity model: altitude moves by
    // velocity * dt, velocity is assumed unchanged. Anything that actually
    // changes velocity (climb, descent, turbulence) is not modelled here;
    // it is what Q accounts for below.
    altitude_m_ += velocity_mps_ * dt;

    // Process noise Q for a constant-velocity model driven by white
    // acceleration noise of variance sigma_a^2 over one step of length dt:
    //
    //   Q = sigma_a^2 * [ dt^4/4   dt^3/2 ]
    //                   [ dt^3/2   dt^2   ]
    //
    // The shape comes from integrating an unknown acceleration a twice:
    // it adds a*dt to velocity and a*dt^2/2 to altitude, and Q is the
    // outer product of that vector [dt^2/2, dt] scaled by sigma_a^2.
    const float dt2 = dt * dt;
    const float dt3 = dt2 * dt;
    const float dt4 = dt3 * dt;
    const float q00 = accel_noise_variance_ * dt4 / 4.0f;
    const float q01 = accel_noise_variance_ * dt3 / 2.0f;
    const float q11 = accel_noise_variance_ * dt2;

    // Covariance prediction: P = F * P * F^T + Q.
    // Multiplying out F * P * F^T for F = [[1, dt], [0, 1]] gives:
    //   p00' = p00 + 2*dt*p01 + dt^2*p11
    //   p01' = p01 + dt*p11
    //   p11' = p11
    // Every term is additive: prediction can only grow uncertainty.
    // Velocity uncertainty (p11) leaks into altitude uncertainty (p00)
    // through the dt^2 term, which is why altitude confidence decays
    // faster when the filter is unsure about velocity.
    const float p00_new = p00_ + 2.0f * dt * p01_ + dt2 * p11_ + q00;
    const float p01_new = p01_ + dt * p11_ + q01;
    const float p11_new = p11_ + q11;

    p00_ = p00_new;
    p01_ = p01_new;
    p11_ = p11_new;
}

void KalmanFilter1D::update(float measured_altitude_m, float measurement_noise_variance) {
    // Measurement matrix H = [1, 0]: the sensor reports altitude directly
    // and says nothing about velocity. With H this simple, H * x is just
    // altitude_m_ and H * P * H^T is just p00_, so no matrix products are
    // written out.

    // Innovation y: how far the measurement is from what we predicted.
    // Positive means the sensor sees us higher than we thought.
    const float y = measured_altitude_m - altitude_m_;

    // Innovation covariance S = H * P * H^T + R. Total uncertainty of the
    // disagreement: our own altitude uncertainty plus the sensor's.
    const float s = p00_ + measurement_noise_variance;

    // Kalman gain K = P * H^T / S, a 2x1 vector. Each element is
    // "how much of the innovation to apply to that state":
    //   k0 = p00 / S  -> fraction of y applied to altitude (0..1)
    //   k1 = p01 / S  -> fraction of y applied to velocity
    // If R >> p00, S is dominated by sensor noise, k0 -> 0 and the
    // measurement is nearly ignored. If R << p00, k0 -> 1 and the state
    // snaps to the measurement. k1 is how velocity gets corrected even
    // though nothing measures velocity: the off-diagonal p01, built up by
    // predict(), says that an altitude error implies a velocity error.
    const float k0 = p00_ / s;
    const float k1 = p01_ / s;

    // State correction: x = x + K * y.
    altitude_m_ += k0 * y;
    velocity_mps_ += k1 * y;

    // Covariance correction: P = (I - K * H) * P.
    // With H = [1, 0], (I - K*H) = [[1 - k0, 0], [-k1, 1]], so:
    //   p00' = (1 - k0) * p00
    //   p01' = (1 - k0) * p01
    //   p11' = p11 - k1 * p01
    // Every term shrinks P: a measurement can only add certainty.
    // p01 must be read before it is overwritten because p11' uses the
    // old value.
    const float p00_new = (1.0f - k0) * p00_;
    const float p01_new = (1.0f - k0) * p01_;
    const float p11_new = p11_ - k1 * p01_;

    p00_ = p00_new;
    p01_ = p01_new;
    p11_ = p11_new;
}
