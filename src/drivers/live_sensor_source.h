#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "data_source.h"
#include "drivers/bmp280.h"
#include "drivers/i2c_bus.h"
#include "drivers/mpu6050.h"
#include "timing/clock.h"

// The fourth DataSource: real sensors on an I2C bus (REQ-SENS-001 through
// REQ-SENS-004, and the "live mode" of REQ-LOG-004).
//
// Each read_frame() stamps the frame from the monotonic clock, reads the
// barometer and the IMU, and leaves the GPS fields zero with gps_read_ok
// false until the NEO-6M driver exists. Raw fields and read_ok only,
// status NOMINAL: the same contract as the simulator, so the detector and
// the processor cannot tell the two apart (design.md section 3).
//
// A failed read holds the channel's fields at the last good values and
// clears that channel's read_ok. That is exactly what FaultInjectingSource
// does when it fails a channel, so a pulled wire and the injector produce
// the same frames and the same tests cover the consequences (REQ-PROC-006
// for the filters, REQ-FAULT-001 for the detector). Before the first good
// read the fields are the zeros of a value-initialized frame.
//
// Timestamps are the actual read time: Clock::now_ns() at the top of
// read_frame(), as milliseconds since the first frame, rounded to the
// nearest millisecond (design.md section 10). An overrun shows as a long
// step that the filters then integrate over; the jitter CSV stays the
// timing evidence.
//
// Ownership: the source owns the bus and both drivers, declared in that
// order, so the drivers' references to the bus are valid for their whole
// life and the bus is destroyed last.
class LiveSensorSource : public DataSource {
public:
    // Takes ownership of `bus`; `clock` must outlive this object. Both
    // drivers are initialized here; a failure leaves ok() false with
    // error() naming each chip that failed and why. Never throws.
    LiveSensorSource(std::unique_ptr<I2cBus> bus, uint8_t baro_address, uint8_t imu_address,
                     Clock& clock);

    bool ok() const;
    const std::string& error() const;

    TelemetryFrame read_frame() override;

private:
    std::unique_ptr<I2cBus> bus_;
    Bmp280 baro_;
    Mpu6050 imu_;
    Clock& clock_;
    bool ok_ = false;
    std::string error_;
    BaroReading last_baro_{};
    ImuReading last_imu_{};
    std::optional<uint64_t> first_read_ns_;
};
