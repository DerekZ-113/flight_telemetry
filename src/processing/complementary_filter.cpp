#include "processing/complementary_filter.h"

#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

// Below this the accelerometer is not measuring gravity in any usable
// way (free fall, or a dead sensor reporting zeros). atan2(0, 0) is
// defined but meaningless, so the accel term is skipped for that frame.
constexpr float kMinAccelMagnitude = 0.5f;   // m/s^2

}  // namespace

ComplementaryFilter::ComplementaryFilter(float alpha)
    : alpha_(alpha)
{
}

void ComplementaryFilter::update(float accel_x, float accel_y, float accel_z,
                                 float gyro_x_dps, float gyro_y_dps, float gyro_z_dps,
                                 float dt) {
    (void)gyro_z_dps;   // yaw rate: no yaw state in this filter

    // Tilt from gravity alone. atan2 keeps the quadrant and never divides
    // by zero. The denominator is the gravity component in the plane
    // perpendicular to the axis of interest, not just accel_z, so the
    // angle stays correct when pitch and roll are both nonzero.
    const float accel_magnitude = std::sqrt(accel_x * accel_x + accel_y * accel_y + accel_z * accel_z);
    const bool accel_usable = accel_magnitude > kMinAccelMagnitude;
    const float accel_pitch = std::atan2(accel_x, std::sqrt(accel_y * accel_y + accel_z * accel_z));
    const float accel_roll  = std::atan2(accel_y, std::sqrt(accel_x * accel_x + accel_z * accel_z));

    if (!initialized_) {
        // Starting from zero would take about one time constant (roughly
        // a second at the default alpha) to converge onto the true tilt.
        // The accelerometer already knows the answer for a stationary
        // start, so use it directly.
        if (accel_usable) {
            pitch_rad_ = accel_pitch;
            roll_rad_ = accel_roll;
            initialized_ = true;
        }
        return;
    }

    // Gyro path: integrate rate over the interval. This is the term that
    // would drift alone; the accel term below keeps pulling it back.
    const float gyro_pitch = pitch_rad_ + gyro_y_dps * kDegToRad * dt;
    const float gyro_roll  = roll_rad_  + gyro_x_dps * kDegToRad * dt;

    if (accel_usable) {
        pitch_rad_ = alpha_ * gyro_pitch + (1.0f - alpha_) * accel_pitch;
        roll_rad_  = alpha_ * gyro_roll  + (1.0f - alpha_) * accel_roll;
    } else {
        // No gravity reference this frame: coast on the gyro.
        pitch_rad_ = gyro_pitch;
        roll_rad_ = gyro_roll;
    }
}

float ComplementaryFilter::pitch_deg() const {
    return pitch_rad_ * kRadToDeg;
}

float ComplementaryFilter::roll_deg() const {
    return roll_rad_ * kRadToDeg;
}
