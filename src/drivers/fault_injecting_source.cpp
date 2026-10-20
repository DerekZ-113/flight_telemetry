#include "drivers/fault_injecting_source.h"

namespace {

// The bits that say the GPS channel's optional fields are present
// (FrameFlag): they describe the reading, so they are held with it.
// NEW_VEHICLE is a fact about the frame, not the channel, and passes
// through like the sequence number.
constexpr uint8_t kGpsPresenceBits = static_cast<uint8_t>(
    static_cast<uint8_t>(FrameFlag::VERTICAL_RATE) | static_cast<uint8_t>(FrameFlag::PRESSURE_ALTITUDE) |
    static_cast<uint8_t>(FrameFlag::FIX_QUALITY));

// Each RawField's member of the frame, in the enumeration's order: one
// table instead of a switch per mode. A pointer to member names a field
// of the type, and `frame.*member` is that field of one frame.
constexpr std::array<float TelemetryFrame::*, kRawFieldCount> kFieldMember{
    &TelemetryFrame::pressure_hpa, &TelemetryFrame::temperature_c,
    &TelemetryFrame::accel_x, &TelemetryFrame::accel_y, &TelemetryFrame::accel_z,
    &TelemetryFrame::gyro_x, &TelemetryFrame::gyro_y, &TelemetryFrame::gyro_z,
    &TelemetryFrame::gps_altitude_m,
};

std::size_t index_of(RawField field) {
    return static_cast<std::size_t>(field);
}

}  // namespace

FaultInjectingSource::FaultInjectingSource(std::unique_ptr<DataSource> inner)
    : inner_(std::move(inner))
{
}

bool FaultInjectingSource::has_next() {
    return inner_->has_next();
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
            // The whole reading, since format version 3 the receiver's
            // judgment of its fix and the ADS-B fields with it, so a held
            // GPS is held as the live source holds its last fix
            // (REQ-SENS-005, REQ-SENS-008).
            to.latitude = from.latitude;
            to.longitude = from.longitude;
            to.gps_altitude_m = from.gps_altitude_m;
            to.ground_speed_mps = from.ground_speed_mps;
            to.course_deg = from.course_deg;
            to.gps_vertical_rate_mps = from.gps_vertical_rate_mps;
            to.gps_pressure_altitude_m = from.gps_pressure_altitude_m;
            to.gps_fix_quality = from.gps_fix_quality;
            to.gps_satellites = from.gps_satellites;
            to.flags = static_cast<uint8_t>((to.flags & ~kGpsPresenceBits) | (from.flags & kGpsPresenceBits));
            break;
        case Channel::VOTER:   // LCOV_EXCL_LINE: read_frame() copies the three sensor channels only
            break;             // LCOV_EXCL_LINE
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

    apply_field_modes(frame);
    previous_ = frame;
    have_previous_ = true;
    return frame;
}

void FaultInjectingSource::apply_field_modes(TelemetryFrame& frame) {
    for (std::size_t i = 0; i < kRawFieldCount; i++) {
        float& value = frame.*kFieldMember[i];
        FieldHold& hold = holds_[i];
        if (hold.enabled) {
            if (!hold.captured) {
                // The frame before the hold, or with none before it this
                // frame's own value: a reading that freezes keeps the last
                // value it showed.
                hold.value = have_previous_ ? previous_.*kFieldMember[i] : value;
                hold.captured = true;
            }
            value = hold.value;
        }
        FieldBias& bias = biases_[i];
        if (bias.enabled) {
            if (!bias.started) {
                bias.start_ms = frame.timestamp_ms;
                bias.started = true;
            }
            const double elapsed_s = static_cast<double>(frame.timestamp_ms - bias.start_ms) / 1000.0;
            value = static_cast<float>(static_cast<double>(value) + bias.offset + bias.rate_per_s * elapsed_s);
        }
        if (overrides_[i].has_value()) {
            value = *overrides_[i];
        }
    }
}

void FaultInjectingSource::fail_reads(Channel channel, bool enable) {
    switch (channel) {  // LCOV_EXCL_BR_LINE: exhaustive over enum class, no-match branch unreachable
        case Channel::BARO: fail_baro_ = enable; break;
        case Channel::IMU:  fail_imu_ = enable; break;
        case Channel::GPS:  fail_gps_ = enable; break;
        case Channel::VOTER: break;   // not a sensor channel: nothing to fail
    }
}

void FaultInjectingSource::hold_values(Channel channel, bool enable) {
    switch (channel) {  // LCOV_EXCL_BR_LINE: exhaustive over enum class, no-match branch unreachable
        case Channel::BARO: hold_baro_ = enable; break;
        case Channel::IMU:  hold_imu_ = enable; break;
        case Channel::GPS:  hold_gps_ = enable; break;
        case Channel::VOTER: break;   // not a sensor channel: nothing to hold
    }
}

void FaultInjectingSource::override_pressure(float hpa) {
    override_field(RawField::PRESSURE, hpa);
}

void FaultInjectingSource::clear_pressure_override() {
    clear_override(RawField::PRESSURE);
}

void FaultInjectingSource::hold_field(RawField field, bool enable) {
    FieldHold& hold = holds_[index_of(field)];
    if (enable && !hold.enabled) {
        hold.captured = false;
    }
    hold.enabled = enable;
}

void FaultInjectingSource::override_field(RawField field, float value) {
    overrides_[index_of(field)] = value;
}

void FaultInjectingSource::clear_override(RawField field) {
    overrides_[index_of(field)].reset();
}

void FaultInjectingSource::bias_field(RawField field, float offset, float rate_per_s) {
    FieldBias& bias = biases_[index_of(field)];
    bias.enabled = true;
    bias.started = false;
    bias.offset = offset;
    bias.rate_per_s = rate_per_s;
}

void FaultInjectingSource::clear_bias(RawField field) {
    biases_[index_of(field)] = FieldBias{};
}
