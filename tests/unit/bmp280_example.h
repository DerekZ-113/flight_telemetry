#pragma once

#include <cstdint>
#include <vector>

#include "drivers/bmp280.h"
#include "fake_i2c_bus.h"

// The worked example on page 23 of BMP280 datasheet revision 1.14
// (BST-BMP280-DS001-11): a trimming set, two raw readings, and the
// answers the datasheet's own code produces. Shared by the barometer
// test (TC-012) and the live-source test (TC-014).
//
// The example's measurement table swaps the UT and UP register addresses
// (confirmed by Bosch, uncorrected because the chip is discontinued). The
// bytes are served in the chip's real order: pressure at 0xF7 to 0xF9,
// temperature at 0xFA to 0xFC.
namespace bmp280_example {

const Bmp280Calibration kCalibration = {
    27504, 26435, -1000,
    36477, -10685, 3024, 2855, 140, -7, 15500, -14600, 6000,
};
constexpr int32_t kAdcT = 519888;   // UT
constexpr int32_t kAdcP = 415148;   // UP
constexpr int32_t kTFine = 128422;
constexpr int32_t kTemperatureCenti = 2508;     // 25.08 °C, exact from the int32 code
constexpr uint32_t kPressureQ24_8 = 25767233;   // what the int64 code yields; the page prints 25767236
constexpr double kPressurePa = 100653.27;       // the datasheet's double-precision result
constexpr float kPressureHpa = 1006.5325f;      // 25767233 / 25600
constexpr float kTemperatureC = 25.08f;

// The twelve words as the chip stores them: little-endian, Table 17 order.
// `swapped` serves every word high byte first, which is what a driver
// with the wrong byte order would read as correct.
inline std::vector<uint8_t> calibration_bytes(const Bmp280Calibration& c, bool swapped = false) {
    const uint16_t words[12] = {
        c.dig_T1, static_cast<uint16_t>(c.dig_T2), static_cast<uint16_t>(c.dig_T3),
        c.dig_P1, static_cast<uint16_t>(c.dig_P2), static_cast<uint16_t>(c.dig_P3),
        static_cast<uint16_t>(c.dig_P4), static_cast<uint16_t>(c.dig_P5), static_cast<uint16_t>(c.dig_P6),
        static_cast<uint16_t>(c.dig_P7), static_cast<uint16_t>(c.dig_P8), static_cast<uint16_t>(c.dig_P9),
    };
    std::vector<uint8_t> bytes;
    for (uint16_t w : words) {
        const uint8_t lo = static_cast<uint8_t>(w & 0xFF);
        const uint8_t hi = static_cast<uint8_t>(w >> 8);
        bytes.push_back(swapped ? hi : lo);
        bytes.push_back(swapped ? lo : hi);
    }
    return bytes;
}

// The six data registers for two 20-bit readings: msb, lsb, xlsb[7:4].
inline std::vector<uint8_t> data_bytes(int32_t adc_p, int32_t adc_t) {
    std::vector<uint8_t> bytes;
    for (int32_t v : {adc_p, adc_t}) {
        bytes.push_back(static_cast<uint8_t>((v >> 12) & 0xFF));
        bytes.push_back(static_cast<uint8_t>((v >> 4) & 0xFF));
        bytes.push_back(static_cast<uint8_t>((v & 0x0F) << 4));
    }
    return bytes;
}

// Puts a chip with the example's trimming set and readings on the fake.
inline void load_chip(FakeI2cBus& bus, uint8_t address, uint8_t chip_id = Bmp280::kChipIdBmp280) {
    bus.registers(address)[Bmp280::kRegId] = chip_id;
    bus.load(address, Bmp280::kRegCalibration, calibration_bytes(kCalibration));
    bus.load(address, Bmp280::kRegData, data_bytes(kAdcP, kAdcT));
}

}  // namespace bmp280_example
