// Tests for src/drivers/bmp280 (REQ-SENS-001, REQ-SENS-002). Test card
// TC-012. The primary oracle is the worked example on page 23 of BMP280
// datasheet revision 1.14 (BST-BMP280-DS001-11): a trimming set, two raw
// readings, and the answers the datasheet's own code produces. The oracle
// is independent of the code under test, which is the point: a driver
// that agreed only with itself would prove nothing.
//
// The example's bytes come from bmp280_example.h, served in the chip's
// real register order (the page's table has UT and UP swapped), so the
// example tests register order as well as arithmetic.

#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <vector>

#include "bmp280_example.h"
#include "drivers/bmp280.h"
#include "fake_i2c_bus.h"

namespace {

constexpr uint8_t kAddress = 0x76;
using bmp280_example::kCalibration;
using bmp280_example::kAdcT;
using bmp280_example::kAdcP;
using bmp280_example::calibration_bytes;
using bmp280_example::data_bytes;

// A fake holding a chip with the example's trimming set and readings.
FakeI2cBus bus_with_chip(uint8_t chip_id = Bmp280::kChipIdBmp280) {
    FakeI2cBus bus;
    bmp280_example::load_chip(bus, kAddress, chip_id);
    return bus;
}

}  // namespace

// TC-012 step 1. The compensation functions reproduce the datasheet's
// worked example: temperature to the hundredth of a degree exactly, and
// pressure within 0.1 Pa of the double-precision figure, with the int64
// code's own result pinned to the count so a change in the arithmetic
// (a shift turned into a division, say) cannot hide inside the tolerance.
TEST(Bmp280Test, DatasheetExampleCompensatesExactly) {
    int32_t t_fine = 0;
    const int32_t temperature = Bmp280::compensate_temperature(kCalibration, kAdcT, t_fine);
    EXPECT_EQ(temperature, bmp280_example::kTemperatureCenti);
    EXPECT_EQ(t_fine, bmp280_example::kTFine);

    const uint32_t pressure = Bmp280::compensate_pressure(kCalibration, kAdcP, t_fine);
    EXPECT_EQ(pressure, bmp280_example::kPressureQ24_8);
    EXPECT_NEAR(pressure / 256.0, bmp280_example::kPressurePa, 0.1);
}

// TC-012 step 2. The same example through the register protocol: the
// driver reads the chip ID, the 24 calibration bytes little-endian, and
// the six data bytes big-endian with pressure first, and reports hPa and
// degrees. 25767233 / 25600 = 1006.5325 hPa.
TEST(Bmp280Test, ReadsTheExampleThroughTheRegisters) {
    FakeI2cBus bus = bus_with_chip();
    Bmp280 baro(bus, kAddress);
    ASSERT_TRUE(baro.init()) << baro.error();
    EXPECT_EQ(baro.chip_id(), 0x58);

    const std::optional<BaroReading> r = baro.read();
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->pressure_hpa, 1006.5325f, 0.001f);
    EXPECT_NEAR(r->temperature_c, 25.08f, 0.001f);
}

// TC-012 step 3. Both Bosch identities are accepted and reported; a
// BMP180's 0x55 is refused with the byte read and both expected bytes in
// the message, and nothing further is sent to the unknown chip.
TEST(Bmp280Test, InitAcceptsBothChipIdsAndRefusesOthers) {
    {
        FakeI2cBus bus = bus_with_chip(Bmp280::kChipIdBme280);
        Bmp280 baro(bus, kAddress);
        EXPECT_TRUE(baro.init()) << baro.error();
        EXPECT_EQ(baro.chip_id(), 0x60);
        EXPECT_TRUE(baro.read().has_value());
    }
    {
        FakeI2cBus bus = bus_with_chip(0x55);
        Bmp280 baro(bus, kAddress);
        EXPECT_FALSE(baro.init());
        EXPECT_NE(baro.error().find("0x55"), std::string::npos) << baro.error();
        EXPECT_NE(baro.error().find("0x58"), std::string::npos) << baro.error();
        EXPECT_NE(baro.error().find("0x60"), std::string::npos) << baro.error();
        EXPECT_EQ(baro.chip_id(), 0);
        EXPECT_EQ(bus.transactions().size(), 1u);
        EXPECT_FALSE(baro.read().has_value());
    }
}

// TC-012 step 4. The configuration sequence and its order: sleep, config,
// normal. A config written in normal mode may be ignored, so the mode
// register is written twice, and the chip ends in normal mode with the
// filter off and the shortest standby.
TEST(Bmp280Test, InitWritesSleepConfigNormalInOrder) {
    FakeI2cBus bus = bus_with_chip();
    Bmp280 baro(bus, kAddress);
    ASSERT_TRUE(baro.init()) << baro.error();

    const std::vector<FakeI2cBus::Transaction>& t = bus.transactions();
    ASSERT_EQ(t.size(), 5u);
    EXPECT_FALSE(t[0].write);
    EXPECT_EQ(t[0].reg, Bmp280::kRegId);
    EXPECT_FALSE(t[1].write);
    EXPECT_EQ(t[1].reg, Bmp280::kRegCalibration);
    EXPECT_EQ(t[1].length, 24u);
    EXPECT_TRUE(t[2].write);
    EXPECT_EQ(t[2].reg, 0xF4);
    EXPECT_EQ(t[2].value, 0x00);
    EXPECT_TRUE(t[3].write);
    EXPECT_EQ(t[3].reg, 0xF5);
    EXPECT_EQ(t[3].value, 0x00);
    EXPECT_TRUE(t[4].write);
    EXPECT_EQ(t[4].reg, 0xF4);
    EXPECT_EQ(t[4].value, 0x2F);
    EXPECT_EQ(bus.registers(kAddress)[0xF4], 0x2F);
    EXPECT_EQ(bus.registers(kAddress)[0xF5], 0x00);
}

// TC-012 step 5. Every transaction in init() can fail, and each failure
// refuses init with a message naming the cause and leaves read() empty.
TEST(Bmp280Test, FailedTransactionsRefuseInit) {
    {
        FakeI2cBus bus = bus_with_chip();
        bus.fail_reads_at(kAddress, Bmp280::kRegId);
        Bmp280 baro(bus, kAddress);
        EXPECT_FALSE(baro.init());
        EXPECT_NE(baro.error().find("chip id read failed"), std::string::npos) << baro.error();
    }
    {
        FakeI2cBus bus = bus_with_chip();
        bus.fail_reads_at(kAddress, Bmp280::kRegCalibration);
        Bmp280 baro(bus, kAddress);
        EXPECT_FALSE(baro.init());
        EXPECT_NE(baro.error().find("calibration read failed"), std::string::npos) << baro.error();
        EXPECT_FALSE(baro.read().has_value());
    }
    {
        FakeI2cBus bus = bus_with_chip();
        bus.fail_writes_at(kAddress, Bmp280::kRegCtrlMeas);   // the sleep write, first of three
        Bmp280 baro(bus, kAddress);
        EXPECT_FALSE(baro.init());
        EXPECT_NE(baro.error().find("0xF4"), std::string::npos) << baro.error();
        EXPECT_EQ(bus.transactions().size(), 3u);
    }
    {
        FakeI2cBus bus = bus_with_chip();
        bus.fail_writes_at(kAddress, Bmp280::kRegConfig);     // second of three
        Bmp280 baro(bus, kAddress);
        EXPECT_FALSE(baro.init());
        EXPECT_NE(baro.error().find("0xF5"), std::string::npos) << baro.error();
        EXPECT_EQ(bus.transactions().size(), 4u);
    }
    {
        FakeI2cBus bus = bus_with_chip();
        bus.fail_write_of(kAddress, Bmp280::kRegCtrlMeas, Bmp280::kCtrlMeasNormal);   // third of three only
        Bmp280 baro(bus, kAddress);
        EXPECT_FALSE(baro.init());
        EXPECT_NE(baro.error().find("0xF4"), std::string::npos) << baro.error();
        EXPECT_EQ(bus.transactions().size(), 5u);
        EXPECT_EQ(baro.chip_id(), 0);
        EXPECT_FALSE(baro.read().has_value());
    }
}

// TC-012 step 6. A failed burst is an empty reading, and so is a data
// field still at its reset value 0x80000 (no conversion has completed, or
// the measurement was skipped): compensating it would invent a pressure.
TEST(Bmp280Test, FailedBurstAndSkippedMeasurementAreEmpty) {
    FakeI2cBus bus = bus_with_chip();
    Bmp280 baro(bus, kAddress);
    ASSERT_TRUE(baro.init()) << baro.error();

    bus.fail_reads_at(kAddress, Bmp280::kRegData);
    EXPECT_FALSE(baro.read().has_value());
    bus.fail_reads_at(kAddress, Bmp280::kRegData, false);

    bus.load(kAddress, Bmp280::kRegData, data_bytes(Bmp280::kSkippedMeasurement, kAdcT));
    EXPECT_FALSE(baro.read().has_value());
    bus.load(kAddress, Bmp280::kRegData, data_bytes(kAdcP, Bmp280::kSkippedMeasurement));
    EXPECT_FALSE(baro.read().has_value());

    bus.load(kAddress, Bmp280::kRegData, data_bytes(kAdcP, kAdcT));
    EXPECT_TRUE(baro.read().has_value());
}

// TC-012 step 7. The calibration words served high byte first give a
// different answer, so this suite can tell the byte order apart; a driver
// reading the words backwards would fail step 2, not pass it by accident.
TEST(Bmp280Test, ByteSwappedCalibrationGivesAnotherAnswer) {
    FakeI2cBus bus = bus_with_chip();
    bus.load(kAddress, Bmp280::kRegCalibration, calibration_bytes(kCalibration, true));
    Bmp280 baro(bus, kAddress);
    ASSERT_TRUE(baro.init()) << baro.error();

    const std::optional<BaroReading> r = baro.read();
    ASSERT_TRUE(r.has_value());
    EXPECT_GT(std::abs(r->temperature_c - 25.08f), 0.01f);
    EXPECT_GT(std::abs(r->pressure_hpa - 1006.5325f), 0.01f);
}

// The one guard in the datasheet's pressure code: var1 is zero only when
// dig_P1 is zero (an all-zero calibration), and the formula would then
// divide by it. The datasheet returns 0; so does the driver, and the
// guard is reached rather than left to a coverage exclusion.
TEST(Bmp280Test, ZeroCalibrationDoesNotDivideByZero) {
    const Bmp280Calibration zero{};
    int32_t t_fine = 0;
    Bmp280::compensate_temperature(zero, kAdcT, t_fine);
    EXPECT_EQ(Bmp280::compensate_pressure(zero, kAdcP, t_fine), 0u);
}

// read() before a successful init() is empty and touches the bus not at
// all: there is no calibration to compensate with.
TEST(Bmp280Test, ReadBeforeInitIsEmpty) {
    FakeI2cBus bus = bus_with_chip();
    Bmp280 baro(bus, kAddress);
    EXPECT_FALSE(baro.read().has_value());
    EXPECT_TRUE(bus.transactions().empty());
}
