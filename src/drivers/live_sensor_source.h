#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "data_source.h"
#include "drivers/bmp280.h"
#include "drivers/i2c_bus.h"
#include "drivers/mpu6050.h"
#include "drivers/nmea_gps.h"
#include "drivers/serial_port.h"
#include "timing/clock.h"

// The fourth DataSource: real sensors on an I2C bus and a GPS receiver on
// a serial device (REQ-SENS-001 through REQ-SENS-005, and the "live mode"
// of REQ-LOG-004).
//
// Each read_frame() stamps the frame from the monotonic clock, reads the
// barometer and the IMU, and polls the receiver. The receiver speaks at
// its own rate: gps_read_ok is true only on the cycle a valid fix
// completed, once a second, and the fix is held with gps_read_ok false on
// the cycles between, which is exactly the simulator's 1 Hz schedule and
// the shape the freshness rule was written for (REQ-PROC-006). Without a
// receiver (sensors.gps.enabled false) the GPS fields stay zero and the
// channel is never fresh, so the detector reports it NO_DATA and then
// DEGRADED, the truthful report of an absent sensor. Raw fields and
// read_ok only, status NOMINAL: the same contract as the simulator, so the
// detector and the processor cannot tell the two apart (design.md
// section 3).
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
// life and the bus is destroyed last; the same for the receiver's port and
// its driver.
class LiveSensorSource : public DataSource {
public:
    // Takes ownership of `bus`; `clock` must outlive this object. Both
    // drivers are initialized here; a failure leaves ok() false with
    // error() naming each chip that failed and why. Never throws.
    // imu_sample_rate_hz is the chip's own rate (`sensors.imu.sample_rate_hz`,
    // REQ-SENS-003, REQ-CFG-001), handed to the MPU6050 driver, which
    // refuses a rate its divider cannot produce; the default serves the
    // unit tests.
    // `gps_port` is the receiver's serial device, or null for a bench
    // without one (REQ-SENS-005, `sensors.gps.enabled`); the source takes
    // ownership. A port that could not be opened is refused by `main`
    // before this constructor runs, so no check is made here.
    LiveSensorSource(std::unique_ptr<I2cBus> bus, uint8_t baro_address, uint8_t imu_address,
                     Clock& clock, int imu_sample_rate_hz = Mpu6050::kDefaultSampleRateHz,
                     std::unique_ptr<SerialPort> gps_port = nullptr);

    bool ok() const;
    const std::string& error() const;

    TelemetryFrame read_frame() override;

    // What the receiver's driver has seen, for the exit line; empty
    // without a receiver.
    std::optional<GpsCounters> gps_counters() const;

    // The last fix delivered, for the exit line (its quality, satellites
    // and UTC time); empty without a receiver or before the first fix.
    std::optional<GpsFix> last_gps_fix() const;

    // The first second after start belongs to the device's backlog, not
    // the sky: the kernel receives from a USB device only while the port
    // is open, so what the receiver buffered while it was closed arrives
    // after the flush at open and would parse as current. A fix completing
    // inside this window is counted here and discarded, so the first fix
    // delivered is a live one. One second: a backlog arrives at USB speed
    // within the first cycles, and a live epoch at 9600 baud is never more
    // than 1.5 s away, so at most one live epoch is lost to the rule
    // (design.md section 3).
    static constexpr uint64_t kGpsStartupGraceMs = 1000;
    size_t gps_startup_discarded() const;

private:
    std::unique_ptr<I2cBus> bus_;
    Bmp280 baro_;
    Mpu6050 imu_;
    Clock& clock_;
    std::unique_ptr<SerialPort> gps_port_;
    std::optional<NmeaGps> gps_;   // emplaced over *gps_port_ when there is one
    bool ok_ = false;
    std::string error_;
    BaroReading last_baro_{};
    ImuReading last_imu_{};
    GpsFix last_gps_{};
    bool gps_ever_fixed_ = false;
    size_t gps_startup_discarded_ = 0;
    std::optional<uint64_t> first_read_ns_;
};
