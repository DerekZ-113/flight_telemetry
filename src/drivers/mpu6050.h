#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "drivers/i2c_bus.h"

// One IMU sample in the frame's units (REQ-SENS-003, REQ-SENS-004).
struct ImuReading {
    float accel_x, accel_y, accel_z;   // m/s^2
    float gyro_x, gyro_y, gyro_z;      // deg/s
};

// MPU6050 driver (REQ-SENS-003, REQ-SENS-004). Register map: InvenSense
// RM-MPU-6000A-00, "MPU-6000 and MPU-6050 Register Map and Descriptions",
// revision 4.2; section numbers below refer to it.
//
// What the chip is: a three-axis accelerometer and a three-axis gyroscope
// sampled behind one digital low-pass filter and one sample-rate divider,
// delivering 16-bit two's-complement results in big-endian register pairs.
// One divider for both sensors is why REQ-SENS-004 ties the gyroscope rate
// to the accelerometer's.
//
// What this class does: init() checks the chip's identity and writes five
// configuration registers; read() burst-reads the fourteen output
// registers and scales them. Nothing is thrown: a failed transaction is
// nullopt, which the live source turns into imu_read_ok = false and the
// fault detector judges over time (REQ-FAULT-001).
class Mpu6050 {
public:
    // Register addresses (section 3, register map table).
    static constexpr uint8_t kRegSampleRateDivider = 0x19;   // SMPLRT_DIV
    static constexpr uint8_t kRegConfig = 0x1A;              // CONFIG, DLPF_CFG in bits 2:0
    static constexpr uint8_t kRegGyroConfig = 0x1B;          // GYRO_CONFIG, FS_SEL in bits 4:3
    static constexpr uint8_t kRegAccelConfig = 0x1C;         // ACCEL_CONFIG, AFS_SEL in bits 4:3
    static constexpr uint8_t kRegAccelXoutH = 0x3B;          // first of the 14 output registers (4.17 to 4.19)
    static constexpr uint8_t kRegPowerManagement1 = 0x6B;    // PWR_MGMT_1 (4.28)
    static constexpr uint8_t kRegWhoAmI = 0x75;              // WHO_AM_I (4.32)

    // WHO_AM_I holds the upper six bits of the I2C address and reads 0x68
    // whatever AD0 is. An MPU-6500 answers 0x70; some clones answer other
    // values, and the driver refuses all of them.
    static constexpr uint8_t kWhoAmI = 0x68;

    // Configuration (the reasoning is in design.md section 3).
    static constexpr uint8_t kPowerManagement1 = 0x01;   // SLEEP off; CLKSEL 1, PLL with the X gyro as reference (4.28)
    static constexpr uint8_t kConfigDlpf20Hz = 0x04;     // DLPF_CFG 4: 21 Hz accel, 20 Hz gyro, 8.5 ms delay (4.3)
    static constexpr uint8_t kGyroConfig250Dps = 0x00;   // FS_SEL 0: ±250 deg/s (4.4)
    static constexpr uint8_t kAccelConfig2G = 0x00;      // AFS_SEL 0: ±2 g (4.5)

    // Chip sample rate: the default a bare driver and the unit tests get;
    // the binary's value is `sensors.imu.sample_rate_hz` (REQ-CFG-001),
    // handed down by LiveSensorSource. 100 Hz is twice the loop rate, so
    // every 20 ms poll sees a new sample; the configuration's validation
    // keeps the rate at or above the loop rate for that reason.
    static constexpr int kDefaultSampleRateHz = 100;

    // Sensitivity at the ranges above (4.17 and 4.19). g is the project's
    // 9.81, which the fault detector's limit and the simulator also use.
    static constexpr float kAccelLsbPerG = 16384.0f;
    static constexpr float kGyroLsbPerDps = 131.0f;
    static constexpr float kGravityMps2 = 9.81f;

    // The SMPLRT_DIV value for a sample rate, or nullopt if the chip cannot
    // produce that rate exactly. With the DLPF on, rate = 1 kHz / (1 + n)
    // (4.2), so 1000 must be divisible by the rate, and n is 8 bits, so
    // 4 Hz is the floor. REQ-SENS-003 promises 4 to 100 Hz.
    static std::optional<uint8_t> sample_rate_divider(int sample_rate_hz);

    // `bus` must outlive this object; the live source owns both.
    Mpu6050(I2cBus& bus, uint8_t address, int sample_rate_hz = kDefaultSampleRateHz);

    // Verifies WHO_AM_I, wakes the chip, and writes the divider, filter,
    // and ranges. False with error() set if the rate is unreachable, the
    // chip answered with the wrong identity, or a transaction failed.
    bool init();
    const std::string& error() const;

    // One burst of the fourteen output registers, scaled. nullopt before a
    // successful init() or when the transaction failed.
    std::optional<ImuReading> read();

private:
    // One configuration write; false with error() naming the register.
    bool write(uint8_t reg, uint8_t value);

    I2cBus& bus_;
    uint8_t address_;
    int sample_rate_hz_;
    bool initialized_ = false;
    std::string error_;
};
