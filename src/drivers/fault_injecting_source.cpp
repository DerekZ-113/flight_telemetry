#include "drivers/fault_injecting_source.h"

FaultInjectingSource::FaultInjectingSource(std::unique_ptr<DataSource> inner)
    : inner_(std::move(inner))
{
}

void FaultInjectingSource::copy_channel(Channel channel, const TelemetryFrame& from,
                                        TelemetryFrame& to) {
    switch (channel) {  // LCOV_EXCL_BR_LINE: exhaustive over enum class, no-match branch unreachable
        case Channel::BARO:
            to.pressure_hpa = from.pressure_hpa;
            to.temperature_c = from.temperature_c;
            break;
        case Channel::IMU:
            to.accel_x = from.accel_x; to.accel_y = from.accel_y; to.accel_z = from.accel_z;
            to.gyro_x = from.gyro_x;   to.gyro_y = from.gyro_y;   to.gyro_z = from.gyro_z;
            break;
        case Channel::GPS:
            to.latitude = from.latitude;
            to.longitude = from.longitude;
            to.gps_altitude_m = from.gps_altitude_m;
            to.ground_speed_mps = from.ground_speed_mps;
            to.heading_deg = from.heading_deg;
            break;
    }
}

TelemetryFrame FaultInjectingSource::read_frame() {
    // Always pull from the inner source so its own state (timestamps,
    // random sequence) advances exactly as it would without injection.
    TelemetryFrame frame = inner_->read_frame();

    if (!have_last_good_) {
        last_good_ = frame;
        have_last_good_ = true;
    }

    // Order matters: a held or failed channel keeps the previous good
    // values, so last_good_ is updated only for channels passing through.
    if (fail_baro_) {
        copy_channel(Channel::BARO, last_good_, frame);
        frame.baro_read_ok = false;
    } else if (hold_baro_) {
        copy_channel(Channel::BARO, last_good_, frame);
    } else {
        copy_channel(Channel::BARO, frame, last_good_);
    }

    if (fail_imu_) {
        copy_channel(Channel::IMU, last_good_, frame);
        frame.imu_read_ok = false;
    } else if (hold_imu_) {
        copy_channel(Channel::IMU, last_good_, frame);
    } else {
        copy_channel(Channel::IMU, frame, last_good_);
    }

    if (fail_gps_) {
        copy_channel(Channel::GPS, last_good_, frame);
        frame.gps_read_ok = false;
    } else if (hold_gps_) {
        copy_channel(Channel::GPS, last_good_, frame);
    } else {
        copy_channel(Channel::GPS, frame, last_good_);
    }

    if (pressure_override_.has_value()) {
        frame.pressure_hpa = *pressure_override_;
    }

    return frame;
}

void FaultInjectingSource::fail_reads(Channel channel, bool enable) {
    switch (channel) {  // LCOV_EXCL_BR_LINE: exhaustive over enum class, no-match branch unreachable
        case Channel::BARO: fail_baro_ = enable; break;
        case Channel::IMU:  fail_imu_ = enable; break;
        case Channel::GPS:  fail_gps_ = enable; break;
    }
}

void FaultInjectingSource::hold_values(Channel channel, bool enable) {
    switch (channel) {  // LCOV_EXCL_BR_LINE: exhaustive over enum class, no-match branch unreachable
        case Channel::BARO: hold_baro_ = enable; break;
        case Channel::IMU:  hold_imu_ = enable; break;
        case Channel::GPS:  hold_gps_ = enable; break;
    }
}

void FaultInjectingSource::override_pressure(float hpa) {
    pressure_override_ = hpa;
}

void FaultInjectingSource::clear_pressure_override() {
    pressure_override_.reset();
}
