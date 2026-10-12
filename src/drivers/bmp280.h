#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "drivers/i2c_bus.h"

// One barometer sample in the frame's units (REQ-SENS-001, REQ-SENS-002).
struct BaroReading {
    float pressure_hpa;
    float temperature_c;
};

// The chip's trimming parameters, burned in at the factory and read once
// from 0x88 (BMP280 datasheet Table 17): twelve 16-bit words, little-endian,
// two of them unsigned. Pressure and temperature are meaningless without
// them; the raw registers are ADC counts.
struct Bmp280Calibration {
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
};

// Barometer driver (REQ-SENS-001, REQ-SENS-002). Datasheet: Bosch
// BST-BMP280-DS001 (section numbers below are revision 1.14's; the
// compensation code is identical in every revision and in the BME280
// datasheet, section 4.2.3).
//
// Named for the chip the documents assume. It accepts the BME280's chip ID
// as well, because the BME280's pressure and temperature control, data
// readout, calibration layout, and compensation are register-compatible
// with the BMP280 (BME280 datasheet section 5.2); the humidity it adds has
// no requirement and is not read. Which chip is on the bench is settled by
// the ID read on hardware (design.md section 12).
//
// What this class does: init() checks the chip ID, reads the trimming
// parameters, and configures continuous measurement; read() burst-reads
// the six data registers and runs the datasheet's integer compensation.
// Nothing is thrown: a failed transaction, or a data field still at its
// reset value, is nullopt, which the live source turns into
// baro_read_ok = false and the fault detector judges over time.
class Bmp280 {
public:
    // Registers (datasheet Table 18, memory map).
    static constexpr uint8_t kRegCalibration = 0x88;   // dig_T1 through dig_P9, 24 bytes
    static constexpr uint8_t kRegId = 0xD0;
    static constexpr uint8_t kRegCtrlMeas = 0xF4;      // osrs_t[7:5] osrs_p[4:2] mode[1:0]
    static constexpr uint8_t kRegConfig = 0xF5;        // t_sb[7:5] filter[4:2] spi3w_en[0]
    static constexpr uint8_t kRegData = 0xF7;          // press_msb, lsb, xlsb, temp_msb, lsb, xlsb
    static constexpr size_t kCalibrationLength = 24;
    static constexpr size_t kDataLength = 6;

    // Chip identities accepted (BMP280 section 4.3.1; BME280 section 5.4.1).
    static constexpr uint8_t kChipIdBmp280 = 0x58;
    static constexpr uint8_t kChipIdBme280 = 0x60;

    // Configuration (the reasoning is in design.md section 3).
    static constexpr uint8_t kCtrlMeasSleep = 0x00;    // mode 00: sleep, so the config write below is honored
    static constexpr uint8_t kConfig = 0x00;           // t_sb 0.5 ms, IIR filter off, 4-wire SPI bit clear
    static constexpr uint8_t kCtrlMeasNormal = 0x2F;   // osrs_t x1 (001), osrs_p x4 (011), mode 11: normal

    // A 20-bit data field at its reset value (0x80, 0x00, 0x00): the
    // datasheet's "measurement skipped" marker, and what the registers
    // hold before the first conversion completes.
    static constexpr int32_t kSkippedMeasurement = 0x80000;

    // The datasheet's compensation (section 3.11.3), integer arithmetic,
    // public and static so the test can feed the worked example straight
    // in. Temperature in 0.01 °C; pressure in Pa × 256 (Q24.8). t_fine
    // carries the temperature into the pressure formula, which is why
    // REQ-SENS-002 reads the two together.
    static int32_t compensate_temperature(const Bmp280Calibration& cal, int32_t adc_t, int32_t& t_fine);
    static uint32_t compensate_pressure(const Bmp280Calibration& cal, int32_t adc_p, int32_t t_fine);

    // `bus` must outlive this object; the live source owns both.
    Bmp280(I2cBus& bus, uint8_t address);

    // Chip ID, calibration, then sleep, config, normal mode. False with
    // error() set if the chip answered with an unknown identity or a
    // transaction failed.
    bool init();
    const std::string& error() const;

    // The identity the chip answered with; 0 before a successful init().
    uint8_t chip_id() const;

    // One burst of the six data registers, compensated. nullopt before a
    // successful init(), when the transaction failed, or when either field
    // is still at its reset value.
    std::optional<BaroReading> read();

private:
    // One configuration write; false with error() naming the register.
    bool write(uint8_t reg, uint8_t value);

    I2cBus& bus_;
    uint8_t address_;
    uint8_t chip_id_ = 0;
    Bmp280Calibration calibration_{};
    bool initialized_ = false;
    std::string error_;
};
