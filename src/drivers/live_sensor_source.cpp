#include "drivers/live_sensor_source.h"

LiveSensorSource::LiveSensorSource(std::unique_ptr<I2cBus> bus, uint8_t baro_address,
                                   uint8_t imu_address, Clock& clock, int imu_sample_rate_hz,
                                   std::unique_ptr<SerialPort> gps_port)
    // Declaration order: the bus first, so *bus_ is valid when the drivers
    // take their references to it; the port before the driver that reads it.
    : bus_(std::move(bus)),
      baro_(*bus_, baro_address),
      imu_(*bus_, imu_address, imu_sample_rate_hz),
      clock_(clock),
      gps_port_(std::move(gps_port))
{
    if (gps_port_ != nullptr) {
        gps_.emplace(*gps_port_);
    }
    // A chip that is absent or answers with the wrong identity when the
    // process starts is a wiring or configuration error, reported here
    // with the chip named; a chip that fails later is a fault for the
    // detector (design.md section 3). Both chips are initialized even
    // when the first fails, so one message can name everything wrong on
    // the bus. The calls sit on their own lines on purpose: a call that
    // may throw inside a conditional leaves an untaken unwind edge in the
    // coverage report (verification_results.md Entry 5 §5.6).
    const bool baro_ready = baro_.init();
    const bool imu_ready = imu_.init();
    if (!baro_ready) {
        error_ = "barometer: " + baro_.error();
    }
    if (!imu_ready) {
        if (!error_.empty()) {
            error_ += "; ";
        }
        error_ += "IMU: " + imu_.error();
    }
    ok_ = baro_ready && imu_ready;
}

bool LiveSensorSource::ok() const {
    return ok_;
}

const std::string& LiveSensorSource::error() const {
    return error_;
}

std::optional<GpsCounters> LiveSensorSource::gps_counters() const {
    if (!gps_.has_value()) {
        return std::nullopt;
    }
    return gps_->counters();
}

std::optional<GpsFix> LiveSensorSource::last_gps_fix() const {
    if (!gps_ever_fixed_) {
        return std::nullopt;
    }
    return last_gps_;
}

size_t LiveSensorSource::gps_startup_discarded() const {
    return gps_startup_discarded_;
}

TelemetryFrame LiveSensorSource::read_frame() {
    // Value-initialized: the logger writes the struct's raw bytes, padding
    // included, and a default-initialized frame would carry stack contents
    // into the log (verification_results.md Entries 4 and 5).
    TelemetryFrame frame{};

    // Read time, not the scheduler's deadline: the frame says when the
    // data was taken, and an overrun shows up as a longer step rather
    // than being hidden by the grid. Integer arithmetic throughout the
    // timing path; the half-million rounds to the nearest millisecond.
    const uint64_t now_ns = clock_.now_ns();
    if (!first_read_ns_.has_value()) {
        first_read_ns_ = now_ns;
    }
    frame.timestamp_ms = (now_ns - *first_read_ns_ + 500'000) / 1'000'000;

    // Each channel: a fresh reading replaces the held one and marks the
    // frame fresh; a failed read leaves the held one and does not. The
    // fields are copied either way, so the frame always carries a value
    // and read_ok says whether it is this cycle's.
    const std::optional<BaroReading> baro = baro_.read();
    if (baro.has_value()) {
        last_baro_ = *baro;
        frame.baro_read_ok = true;
    }
    frame.pressure_hpa = last_baro_.pressure_hpa;
    frame.temperature_c = last_baro_.temperature_c;

    const std::optional<ImuReading> imu = imu_.read();
    if (imu.has_value()) {
        last_imu_ = *imu;
        frame.imu_read_ok = true;
    }
    frame.accel_x = last_imu_.accel_x;
    frame.accel_y = last_imu_.accel_y;
    frame.accel_z = last_imu_.accel_z;
    frame.gyro_x = last_imu_.gyro_x;
    frame.gyro_y = last_imu_.gyro_y;
    frame.gyro_z = last_imu_.gyro_z;

    // GPS: the receiver's own rate (REQ-SENS-005). poll() returns a fix
    // only on the cycle an epoch completed, so the channel is fresh once a
    // second and the last fix is held, not fresh, on the cycles between
    // (REQ-PROC-006). Before the first fix, or without a receiver, the
    // fields are zero and the detector reports the channel (FAULT-003).
    if (gps_.has_value()) {
        const std::optional<GpsFix> fix = gps_->poll();
        if (fix.has_value()) {
            // The first second is the device's backlog (live_sensor_source.h).
            const bool backlog = frame.timestamp_ms < kGpsStartupGraceMs;
            if (backlog) {
                gps_startup_discarded_++;
            } else {
                last_gps_ = *fix;
                gps_ever_fixed_ = true;
                frame.gps_read_ok = true;
            }
        }
    }
    frame.latitude = last_gps_.latitude_deg;
    frame.longitude = last_gps_.longitude_deg;
    frame.gps_altitude_m = last_gps_.altitude_m;
    frame.ground_speed_mps = last_gps_.ground_speed_mps;
    frame.heading_deg = last_gps_.course_deg;

    // Sources report; the detector judges (telemetry_frame.h).
    frame.baro_status = ChannelStatus::NOMINAL;
    frame.imu_status = ChannelStatus::NOMINAL;
    frame.gps_status = ChannelStatus::NOMINAL;
    return frame;
}
