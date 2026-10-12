#include "drivers/bmp280.h"

namespace {

// Calibration words are little-endian (datasheet Table 17: 0x88 holds
// dig_T1[7:0], 0x89 holds dig_T1[15:8]). Combined as unsigned; the
// signed variant casts once at the end, the one place the sign appears.
uint16_t le16(uint8_t lo, uint8_t hi) {
    return static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
}

int16_t le16s(uint8_t lo, uint8_t hi) {
    return static_cast<int16_t>(le16(lo, hi));
}

// Data fields are 20 bits across three registers, high byte first, with
// the last four bits in the top nibble of xlsb (section 4.3.5): the
// opposite byte order from the calibration words.
int32_t raw20(uint8_t msb, uint8_t lsb, uint8_t xlsb) {
    return (static_cast<int32_t>(msb) << 12) | (static_cast<int32_t>(lsb) << 4) | (xlsb >> 4);
}

std::string hex_byte(uint8_t byte) {
    static const char* const kDigits = "0123456789ABCDEF";
    return std::string{kDigits[byte >> 4], kDigits[byte & 0x0F]};
}

}  // namespace

int32_t Bmp280::compensate_temperature(const Bmp280Calibration& cal, int32_t adc_t, int32_t& t_fine) {
    // "bmp280_compensate_T_int32" from section 3.11.3, transliterated.
    // Fixed point: the chip's MCU-class reference has no floating point,
    // so the formula is scaled integer arithmetic with the shifts placed
    // so every intermediate fits 32 bits (the largest, the squared term,
    // is about 1.4e9 at the top of the ADC range). The two left shifts of
    // the datasheet are multiplications here: both operands are positive,
    // but one rule is easier to keep than two.
    const int32_t t1 = static_cast<int32_t>(cal.dig_T1);
    const int32_t var1 = (((adc_t >> 3) - t1 * 2) * static_cast<int32_t>(cal.dig_T2)) >> 11;
    const int32_t var2 = (((((adc_t >> 4) - t1) * ((adc_t >> 4) - t1)) >> 12) * static_cast<int32_t>(cal.dig_T3)) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8;
}

uint32_t Bmp280::compensate_pressure(const Bmp280Calibration& cal, int32_t adc_p, int32_t t_fine) {
    // "bmp280_compensate_P_int64" from section 3.11.3, transliterated with
    // one change: where the datasheet left-shifts a signed intermediate,
    // the value is multiplied by the same power of two, because shifting
    // a negative value left is undefined behavior in C++17, and it happens
    // in the datasheet's own worked example (var1 * dig_P2 is negative).
    // Right shifts of negatives stay as written: implementation-defined
    // before C++20, arithmetic on GCC and clang, mandated arithmetic from
    // C++20, and the oracle test pins the exact result on both toolchains.
    //
    // 64 bits are not optional. var1 is t_fine - 128000, about ±330,000 at
    // the temperature limits, so var1 * var1 * dig_P6 reaches 1e11 and
    // overflows int32 once |var1| exceeds 17,515: with the datasheet's
    // trimming set that is any temperature outside 21.6 to 28.4 °C. The
    // worked example at 25.08 °C (var1 = 422) would pass in int32 by
    // luck. (1048576 - adc_p) * 2^31 is about 1e15 and never fits.
    int64_t var1 = static_cast<int64_t>(t_fine) - 128000;
    int64_t var2 = var1 * var1 * cal.dig_P6;
    var2 = var2 + (var1 * cal.dig_P5) * (int64_t{1} << 17);
    var2 = var2 + static_cast<int64_t>(cal.dig_P4) * (int64_t{1} << 35);
    var1 = ((var1 * var1 * cal.dig_P3) >> 8) + (var1 * cal.dig_P2) * (int64_t{1} << 12);
    var1 = (((int64_t{1} << 47) + var1)) * cal.dig_P1 >> 33;
    if (var1 == 0) {
        return 0;   // only an all-zero calibration gets here; a division by zero otherwise
    }
    int64_t p = 1048576 - adc_p;
    p = (((p * (int64_t{1} << 31)) - var2) * 3125) / var1;
    var1 = (static_cast<int64_t>(cal.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (static_cast<int64_t>(cal.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + static_cast<int64_t>(cal.dig_P7) * 16;
    return static_cast<uint32_t>(p);   // Q24.8: the datasheet's "24 integer bits and 8 fractional bits"
}

Bmp280::Bmp280(I2cBus& bus, uint8_t address)
    : bus_(bus),
      address_(address)
{
}

bool Bmp280::init() {
    initialized_ = false;
    chip_id_ = 0;

    uint8_t id = 0;
    if (!bus_.read_registers(address_, kRegId, &id, 1)) {
        error_ = "chip id read failed";
        return false;
    }
    if (id != kChipIdBmp280 && id != kChipIdBme280) {
        error_ = "chip id 0x" + hex_byte(id) + ", expected 0x58 (BMP280) or 0x60 (BME280)";
        return false;
    }

    // All twelve words in one burst, in Table 17's order.
    uint8_t raw[kCalibrationLength];
    if (!bus_.read_registers(address_, kRegCalibration, raw, kCalibrationLength)) {
        error_ = "calibration read failed";
        return false;
    }
    calibration_.dig_T1 = le16(raw[0], raw[1]);
    calibration_.dig_T2 = le16s(raw[2], raw[3]);
    calibration_.dig_T3 = le16s(raw[4], raw[5]);
    calibration_.dig_P1 = le16(raw[6], raw[7]);
    calibration_.dig_P2 = le16s(raw[8], raw[9]);
    calibration_.dig_P3 = le16s(raw[10], raw[11]);
    calibration_.dig_P4 = le16s(raw[12], raw[13]);
    calibration_.dig_P5 = le16s(raw[14], raw[15]);
    calibration_.dig_P6 = le16s(raw[16], raw[17]);
    calibration_.dig_P7 = le16s(raw[18], raw[19]);
    calibration_.dig_P8 = le16s(raw[20], raw[21]);
    calibration_.dig_P9 = le16s(raw[22], raw[23]);

    // Sleep first: the datasheet says writes to config may be ignored in
    // normal mode and are honored in sleep mode (section 4.3.5), and a
    // chip left running by a previous process is in normal mode. Then the
    // config, then normal mode, which starts the first conversion. The
    // first failed write ends the sequence with error() naming it.
    if (!write(kRegCtrlMeas, kCtrlMeasSleep) ||
        !write(kRegConfig, kConfig) ||
        !write(kRegCtrlMeas, kCtrlMeasNormal)) {
        return false;
    }

    chip_id_ = id;
    initialized_ = true;
    error_.clear();
    return true;
}

bool Bmp280::write(uint8_t reg, uint8_t value) {
    if (!bus_.write_register(address_, reg, value)) {
        error_ = "write to register 0x" + hex_byte(reg) + " failed";
        return false;
    }
    return true;
}

const std::string& Bmp280::error() const {
    return error_;
}

uint8_t Bmp280::chip_id() const {
    return chip_id_;
}

std::optional<BaroReading> Bmp280::read() {
    if (!initialized_) {
        return std::nullopt;
    }

    // One burst for all six registers (section 3.9): the chip shadows
    // them together at the end of a conversion, and reading them one at a
    // time could combine bytes from two conversions.
    uint8_t raw[kDataLength];
    if (!bus_.read_registers(address_, kRegData, raw, kDataLength)) {
        return std::nullopt;
    }
    const int32_t adc_p = raw20(raw[0], raw[1], raw[2]);
    const int32_t adc_t = raw20(raw[3], raw[4], raw[5]);

    // The reset value means no conversion has completed yet (or the
    // measurement was skipped). Compensating it would produce a number
    // nobody measured; it is a missing reading instead.
    if (adc_p == kSkippedMeasurement || adc_t == kSkippedMeasurement) {
        return std::nullopt;
    }

    int32_t t_fine = 0;
    const int32_t temperature_centi = compensate_temperature(calibration_, adc_t, t_fine);
    const uint32_t pressure_q24_8 = compensate_pressure(calibration_, adc_p, t_fine);

    BaroReading r{};
    r.temperature_c = static_cast<float>(temperature_centi) / 100.0f;
    r.pressure_hpa = static_cast<float>(pressure_q24_8) / 25600.0f;   // 256 per Pa, 100 Pa per hPa
    return r;
}
