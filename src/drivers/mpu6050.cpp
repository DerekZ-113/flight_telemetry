#include "drivers/mpu6050.h"

namespace {

// ACCEL_XOUT_H (0x3B) through GYRO_ZOUT_L (0x48): accel x, y, z, then
// temperature, then gyro x, y, z, two bytes each.
constexpr size_t kBurstLength = 14;

// A 16-bit two's-complement value from two registers, high byte first
// (register map 4.17: "ACCEL_XOUT_H" holds bits 15:8). The bytes are
// combined as unsigned so no shift ever touches a signed value; the cast
// to int16_t at the end is the one place where the sign appears.
int16_t be16(uint8_t hi, uint8_t lo) {
    const uint16_t word = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
    return static_cast<int16_t>(word);
}

std::string hex_byte(uint8_t byte) {
    static const char* const kDigits = "0123456789ABCDEF";
    return std::string{kDigits[byte >> 4], kDigits[byte & 0x0F]};
}

}  // namespace

std::optional<uint8_t> Mpu6050::sample_rate_divider(int sample_rate_hz) {
    // With the DLPF enabled the gyroscope output rate is 1 kHz and the
    // accelerometer's is 1 kHz as well, so one divider sets both
    // (register map 4.2; REQ-SENS-004). Rounding to the nearest
    // producible rate would make the configured number a lie, so an
    // inexact rate is refused instead.
    constexpr int kBaseRateHz = 1000;
    constexpr int kMinRateHz = 4;   // 1000 / (1 + 255) is 3.9 Hz; 4 Hz is the lowest exact divisor the 8 bits reach
    if (sample_rate_hz < kMinRateHz || sample_rate_hz > kBaseRateHz || kBaseRateHz % sample_rate_hz != 0) {
        return std::nullopt;
    }
    return static_cast<uint8_t>(kBaseRateHz / sample_rate_hz - 1);
}

Mpu6050::Mpu6050(I2cBus& bus, uint8_t address, int sample_rate_hz)
    : bus_(bus),
      address_(address),
      sample_rate_hz_(sample_rate_hz)
{
}

bool Mpu6050::init() {
    initialized_ = false;

    // The rate is checked before the bus is touched: a configuration the
    // chip cannot honor is a configuration error, not a hardware one.
    const std::optional<uint8_t> divider = sample_rate_divider(sample_rate_hz_);
    if (!divider.has_value()) {
        error_ = "sample rate " + std::to_string(sample_rate_hz_) +
                 " Hz is not 1 kHz / (1 + n) with n at most 255";
        return false;
    }

    uint8_t who_am_i = 0;
    if (!bus_.read_registers(address_, kRegWhoAmI, &who_am_i, 1)) {
        error_ = "WHO_AM_I read failed";
        return false;
    }
    if (who_am_i != kWhoAmI) {
        error_ = "WHO_AM_I read 0x" + hex_byte(who_am_i) + ", expected 0x" + hex_byte(kWhoAmI);
        return false;
    }

    // Order matters once: the chip powers up asleep (PWR_MGMT_1 reset
    // value 0x40, 4.28) and samples nothing until woken, so the wake comes
    // first; the divider, filter, and ranges follow in register order.
    // Short-circuit: the first failed write ends the sequence with error()
    // naming its register.
    if (!write(kRegPowerManagement1, kPowerManagement1) ||
        !write(kRegSampleRateDivider, *divider) ||
        !write(kRegConfig, kConfigDlpf20Hz) ||
        !write(kRegGyroConfig, kGyroConfig250Dps) ||
        !write(kRegAccelConfig, kAccelConfig2G)) {
        return false;
    }

    initialized_ = true;
    error_.clear();
    return true;
}

bool Mpu6050::write(uint8_t reg, uint8_t value) {
    if (!bus_.write_register(address_, reg, value)) {
        error_ = "write to register 0x" + hex_byte(reg) + " failed";
        return false;
    }
    return true;
}

const std::string& Mpu6050::error() const {
    return error_;
}

std::optional<ImuReading> Mpu6050::read() {
    if (!initialized_) {
        return std::nullopt;
    }

    // One burst for all fourteen registers (4.17 to 4.19). The chip loads
    // them together at each sample, and reading them one at a time could
    // combine the high byte of one sample with the low byte of the next.
    // The temperature pair travels in the burst and is not converted: the
    // frame has no field for it.
    uint8_t raw[kBurstLength];
    if (!bus_.read_registers(address_, kRegAccelXoutH, raw, kBurstLength)) {
        return std::nullopt;
    }

    // Chip axes map to the body frame as the identity (design.md section
    // 6). If the tilt test on hardware shows a sign disagreement between
    // the gyroscope and accelerometer paths, this is the one place it
    // changes.
    ImuReading r{};
    r.accel_x = static_cast<float>(be16(raw[0], raw[1])) / kAccelLsbPerG * kGravityMps2;
    r.accel_y = static_cast<float>(be16(raw[2], raw[3])) / kAccelLsbPerG * kGravityMps2;
    r.accel_z = static_cast<float>(be16(raw[4], raw[5])) / kAccelLsbPerG * kGravityMps2;
    // raw[6], raw[7]: TEMP_OUT_H, TEMP_OUT_L
    r.gyro_x = static_cast<float>(be16(raw[8], raw[9])) / kGyroLsbPerDps;
    r.gyro_y = static_cast<float>(be16(raw[10], raw[11])) / kGyroLsbPerDps;
    r.gyro_z = static_cast<float>(be16(raw[12], raw[13])) / kGyroLsbPerDps;
    return r;
}
