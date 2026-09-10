#pragma once

// Complementary filter for pitch and roll (REQ-PROC-002).
//
// Two sensors each answer "which way is down" badly on their own:
//
//   Accelerometer: measures gravity, so it gives an absolute tilt angle
//   with no drift. But it is noisy, and any real acceleration (a bump,
//   a turn, vibration) is indistinguishable from gravity for that sample.
//
//   Gyroscope: measures angular rate cleanly, and integrating rate gives
//   angle. But every sample carries a small bias, and integrating a bias
//   produces an error that grows without bound. Left alone, a gyro-only
//   attitude walks away in seconds.
//
// The complementary filter combines them with one weight, alpha:
//
//   angle = alpha * (angle + gyro_rate * dt) + (1 - alpha) * accel_angle
//
// The gyro path is trusted for short-term changes (high-pass), the accel
// path for the long-term average (low-pass), and the two weights sum to
// one, which is where the name comes from. Unlike the Kalman filter it
// needs no noise statistics, only alpha.
//
// alpha only means something together with dt. The time constant of the
// accel correction is roughly alpha * dt / (1 - alpha): at 0.98 and 50 Hz
// that is about one second. A steady gyro bias b settles to an error of
// alpha * b * dt / (1 - alpha) instead of growing forever.
//
// Angles are kept in radians because that is what atan2 returns and what
// the trig functions consume. Degrees exist only at the getters, so no
// unit mix-up can happen inside the filter.
class ComplementaryFilter {
public:
    // Weight on the gyro path. Placeholder until config (REQ-CFG-001).
    static constexpr float kDefaultAlpha = 0.98f;

    // explicit: a bare float must never silently become a filter.
    explicit ComplementaryFilter(float alpha = kDefaultAlpha);

    // Accelerometer in m/s^2, gyroscope in deg/s (as the MPU6050 driver
    // and TelemetryFrame report them), dt in seconds.
    //
    // Axis convention (right-handed body frame, x forward, y right, z up):
    //   pitch is rotation about y, driven by gyro_y_dps
    //   roll  is rotation about x, driven by gyro_x_dps
    //   gyro_z_dps (yaw rate) is accepted but unused; heading comes from GPS.
    // The signs assume the accelerometer and gyroscope agree on the
    // frame. Whether the mounted MPU6050 does is verified on hardware.
    //
    // The first call seeds both angles from the accelerometer alone and
    // ignores the gyro and dt, because no previous frame exists to
    // integrate from.
    void update(float accel_x, float accel_y, float accel_z,
                float gyro_x_dps, float gyro_y_dps, float gyro_z_dps,
                float dt);

    float pitch_deg() const;
    float roll_deg() const;

private:
    float alpha_;
    float pitch_rad_ = 0.0f;
    float roll_rad_ = 0.0f;
    bool initialized_ = false;
};
