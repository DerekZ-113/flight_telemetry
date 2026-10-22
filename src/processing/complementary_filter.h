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
    // Weight on the gyro path: the default a bare filter and the unit
    // tests get. The binary's value is `processing.complementary_alpha`
    // (REQ-CFG-001), handed in by TelemetryProcessor.
    static constexpr float kDefaultAlpha = 0.98f;

    // explicit: a bare float must never silently become a filter.
    explicit ComplementaryFilter(float alpha = kDefaultAlpha);

    // Accelerometer in m/s^2, gyroscope in deg/s (as the MPU6050 driver
    // and TelemetryFrame report them), dt in seconds.
    //
    // Axis convention (design.md section 6): the chip's axes as mounted,
    // right-handed, x forward, y left, z up.
    //   pitch, positive nose up, is driven by gyro_y_dps, the nose-up pitch
    //         rate: the MPU6050 driver negates the chip's y rate for it
    //   roll, positive for a right roll, is driven by gyro_x_dps
    //   gyro_z_dps (yaw rate) is accepted but unused; heading comes from GPS.
    // The bench's tilt test settled the signs (TC-014 step 13).
    //
    // The first call seeds both angles from the accelerometer alone and
    // ignores the gyro and dt, because no previous frame exists to
    // integrate from.
    void update(float accel_x, float accel_y, float accel_z,
                float gyro_x_dps, float gyro_y_dps, float gyro_z_dps,
                float dt);

    // Forget the seed, keep the angles. The next update with a usable
    // accelerometer adopts its tilt outright, as the first update did;
    // until then the last angles stand, as they do on a DEGRADED frame.
    // The processor calls this after an IMU gap longer than the
    // communication timeout (REQ-PROC-002): one rate sample integrated
    // over an outage is not a measurement of the rotation during it.
    void reset();

    float pitch_deg() const;
    float roll_deg() const;

    // Whether the angles rest on an accelerometer tilt: false until the
    // first usable sample, and again after reset() until the next one.
    // The processor reads it to judge the attitude's validity
    // (REQ-PROC-007); the filter itself never acts on it from outside.
    bool seeded() const { return initialized_; }

private:
    float alpha_;
    float pitch_rad_ = 0.0f;
    float roll_rad_ = 0.0f;
    bool initialized_ = false;
};
