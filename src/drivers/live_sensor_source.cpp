#include "drivers/live_sensor_source.h"

LiveSensorSource::LiveSensorSource(std::unique_ptr<I2cBus> bus, uint8_t baro_address,
                                   uint8_t imu_address, Clock& clock, int imu_sample_rate_hz)
    // Declaration order: the bus first, so *bus_ is valid when the drivers
    // take their references to it.
    : bus_(std::move(bus)),
      baro_(*bus_, baro_address),
      imu_(*bus_, imu_address, imu_sample_rate_hz),
      clock_(clock)
{
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

    // GPS: no driver yet (REQ-SENS-005). The fields stay zero and the
    // channel is never fresh, so the detector marks it DEGRADED after its
    // UART timeout, which is the truthful report of an absent sensor.
    frame.gps_read_ok = false;

    // Sources report; the detector judges (telemetry_frame.h).
    frame.baro_status = ChannelStatus::NOMINAL;
    frame.imu_status = ChannelStatus::NOMINAL;
    frame.gps_status = ChannelStatus::NOMINAL;
    return frame;
}
