// Tests for src/drivers/mpu6050 (REQ-SENS-003, REQ-SENS-004). Test card
// TC-013. Every expected value is arithmetic from the register map's
// sensitivity table (16384 LSB/g, 131 LSB per deg/s) or its sample-rate
// formula, not a number copied from a run.

#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <vector>

#include "drivers/mpu6050.h"
#include "fake_i2c_bus.h"

namespace {

constexpr uint8_t kAddress = 0x68;

// A fake with a real MPU6050's identity at WHO_AM_I.
FakeI2cBus bus_with_chip() {
    FakeI2cBus bus;
    bus.registers(kAddress)[Mpu6050::kRegWhoAmI] = Mpu6050::kWhoAmI;
    return bus;
}

// Loads the fourteen output registers from six values, high byte first,
// with a recognisable temperature pair in the middle.
void load_sample(FakeI2cBus& bus, int16_t ax, int16_t ay, int16_t az,
                 int16_t gx, int16_t gy, int16_t gz) {
    std::vector<uint8_t> bytes;
    for (int16_t v : {ax, ay, az}) {
        const uint16_t u = static_cast<uint16_t>(v);
        bytes.push_back(static_cast<uint8_t>(u >> 8));
        bytes.push_back(static_cast<uint8_t>(u & 0xFF));
    }
    bytes.push_back(0x12);   // TEMP_OUT_H, ignored
    bytes.push_back(0x34);   // TEMP_OUT_L, ignored
    for (int16_t v : {gx, gy, gz}) {
        const uint16_t u = static_cast<uint16_t>(v);
        bytes.push_back(static_cast<uint8_t>(u >> 8));
        bytes.push_back(static_cast<uint8_t>(u & 0xFF));
    }
    bus.load(kAddress, Mpu6050::kRegAccelXoutH, bytes);
}

}  // namespace

// TC-013 step 1. init() reads WHO_AM_I once, then writes the five
// configuration registers in order with the decided values: wake with the
// gyro PLL clock, divider 9 for 100 Hz, DLPF 4, ±250 deg/s, ±2 g.
TEST(Mpu6050Test, InitVerifiesIdentityAndWritesConfiguration) {
    FakeI2cBus bus = bus_with_chip();
    Mpu6050 imu(bus, kAddress);
    ASSERT_TRUE(imu.init()) << imu.error();
    EXPECT_TRUE(imu.error().empty());

    const std::vector<FakeI2cBus::Transaction>& t = bus.transactions();
    ASSERT_EQ(t.size(), 6u);
    EXPECT_FALSE(t[0].write);
    EXPECT_EQ(t[0].reg, Mpu6050::kRegWhoAmI);

    const uint8_t expected_regs[] = {0x6B, 0x19, 0x1A, 0x1B, 0x1C};
    const uint8_t expected_values[] = {0x01, 9, 0x04, 0x00, 0x00};
    for (size_t i = 0; i < 5; i++) {
        EXPECT_TRUE(t[i + 1].write);
        EXPECT_EQ(t[i + 1].address, kAddress);
        EXPECT_EQ(t[i + 1].reg, expected_regs[i]) << "write " << i;
        EXPECT_EQ(t[i + 1].value, expected_values[i]) << "write " << i;
        EXPECT_EQ(bus.registers(kAddress)[expected_regs[i]], expected_values[i]);
    }
}

// TC-013 step 2. The MPU-6500's answer, 0x70, is refused with both bytes
// in the message, and nothing is written to a chip of unknown identity.
TEST(Mpu6050Test, WrongWhoAmIRefusesInit) {
    FakeI2cBus bus;
    bus.registers(kAddress)[Mpu6050::kRegWhoAmI] = 0x70;
    Mpu6050 imu(bus, kAddress);

    EXPECT_FALSE(imu.init());
    EXPECT_NE(imu.error().find("0x70"), std::string::npos) << imu.error();
    EXPECT_NE(imu.error().find("0x68"), std::string::npos) << imu.error();
    EXPECT_EQ(bus.transactions().size(), 1u);   // the identity read only
    EXPECT_FALSE(imu.read().has_value());
}

// A chip that does not answer at all (a NACK on the identity read) is
// refused too, with a different message.
TEST(Mpu6050Test, FailedIdentityReadRefusesInit) {
    FakeI2cBus bus = bus_with_chip();
    bus.fail_reads_at(kAddress, Mpu6050::kRegWhoAmI);
    Mpu6050 imu(bus, kAddress);

    EXPECT_FALSE(imu.init());
    EXPECT_NE(imu.error().find("WHO_AM_I read failed"), std::string::npos) << imu.error();
}

// A configuration write that fails anywhere in the sequence refuses
// init, names the register, stops the sequence there, and leaves the
// driver unusable: read() stays empty.
TEST(Mpu6050Test, FailedConfigurationWriteRefusesInit) {
    const uint8_t regs[] = {0x6B, 0x19, 0x1A, 0x1B, 0x1C};
    const char* const names[] = {"0x6B", "0x19", "0x1A", "0x1B", "0x1C"};
    for (size_t i = 0; i < 5; i++) {
        FakeI2cBus bus = bus_with_chip();
        bus.fail_writes_at(kAddress, regs[i]);
        Mpu6050 imu(bus, kAddress);

        EXPECT_FALSE(imu.init()) << names[i];
        EXPECT_NE(imu.error().find(names[i]), std::string::npos) << imu.error();
        EXPECT_EQ(bus.transactions().size(), 2 + i) << names[i];   // the identity read, then writes up to the failed one
        load_sample(bus, 0x4000, 0, 0x4000, 0, 0, 0);
        EXPECT_FALSE(imu.read().has_value()) << names[i];
    }
}

// TC-013 step 3. Full-scale fractions scale to the frame's units: 0x4000
// is 16384 LSB, one g, 9.81 m/s^2; 0x0083 is 131 LSB, one degree per
// second. Negative values come out negative through the int16 cast, but
// for the y rate, which the axis mapping negates (step 8).
TEST(Mpu6050Test, ScalesRawCountsToFrameUnits) {
    FakeI2cBus bus = bus_with_chip();
    Mpu6050 imu(bus, kAddress);
    ASSERT_TRUE(imu.init());
    load_sample(bus, 0x4000, -0x4000, 0x2000, 0x0083, -0x0083, 0);

    const std::optional<ImuReading> r = imu.read();
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->accel_x, 9.81f, 1e-4f);
    EXPECT_NEAR(r->accel_y, -9.81f, 1e-4f);
    EXPECT_NEAR(r->accel_z, 4.905f, 1e-4f);
    EXPECT_NEAR(r->gyro_x, 1.0f, 1e-4f);
    EXPECT_NEAR(r->gyro_y, 1.0f, 1e-4f);
    EXPECT_NEAR(r->gyro_z, 0.0f, 1e-6f);
}

// TC-013 step 4. The high byte comes first. The same two bytes in the
// other order are a different number: 0x0040 is 64 LSB, 0.038 m/s^2, not
// one g; and the swapped bytes of -1 deg/s (0xFF7D) read as 0x7DFF, a
// large positive rate.
TEST(Mpu6050Test, ByteOrderIsHighByteFirst) {
    FakeI2cBus bus = bus_with_chip();
    Mpu6050 imu(bus, kAddress);
    ASSERT_TRUE(imu.init());

    bus.load(kAddress, Mpu6050::kRegAccelXoutH,
             {0x00, 0x40,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x7D, 0xFF,  0x00, 0x00,  0x00, 0x00});
    const std::optional<ImuReading> r = imu.read();
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->accel_x, 64.0f / 16384.0f * 9.81f, 1e-5f);
    EXPECT_NEAR(r->gyro_x, 32255.0f / 131.0f, 1e-3f);
}

// TC-013 step 5. The divider formula, rate = 1 kHz / (1 + n): exact
// divisors of 1000 within 8 bits are accepted, everything else refused.
TEST(Mpu6050Test, SampleRateDividerArithmetic) {
    EXPECT_EQ(Mpu6050::sample_rate_divider(100), std::optional<uint8_t>(9));
    EXPECT_EQ(Mpu6050::sample_rate_divider(50), std::optional<uint8_t>(19));
    EXPECT_EQ(Mpu6050::sample_rate_divider(4), std::optional<uint8_t>(249));
    EXPECT_EQ(Mpu6050::sample_rate_divider(1000), std::optional<uint8_t>(0));
    EXPECT_EQ(Mpu6050::sample_rate_divider(8), std::optional<uint8_t>(124));
    EXPECT_FALSE(Mpu6050::sample_rate_divider(1).has_value());      // n would be 999
    EXPECT_FALSE(Mpu6050::sample_rate_divider(3).has_value());      // n would be 332
    EXPECT_FALSE(Mpu6050::sample_rate_divider(30).has_value());     // 1000 / 30 is not whole
    EXPECT_FALSE(Mpu6050::sample_rate_divider(0).has_value());
    EXPECT_FALSE(Mpu6050::sample_rate_divider(-50).has_value());
    EXPECT_FALSE(Mpu6050::sample_rate_divider(2000).has_value());
}

// A rate the chip cannot produce is refused before the bus is touched:
// a configuration error is not a hardware error.
TEST(Mpu6050Test, UnreachableRateRefusesInit) {
    FakeI2cBus bus = bus_with_chip();
    Mpu6050 imu(bus, kAddress, 1);

    EXPECT_FALSE(imu.init());
    EXPECT_NE(imu.error().find("1 Hz"), std::string::npos) << imu.error();
    EXPECT_TRUE(bus.transactions().empty());
}

// TC-013 step 6. A failed burst is an empty reading for that cycle, and
// the next successful burst is a reading again: nothing is cached and
// nothing is thrown.
TEST(Mpu6050Test, FailedBurstReturnsEmpty) {
    FakeI2cBus bus = bus_with_chip();
    Mpu6050 imu(bus, kAddress);
    ASSERT_TRUE(imu.init());
    load_sample(bus, 0, 0, 0x4000, 0, 0, 0);

    bus.fail_reads_at(kAddress, Mpu6050::kRegAccelXoutH);
    EXPECT_FALSE(imu.read().has_value());
    bus.fail_reads_at(kAddress, Mpu6050::kRegAccelXoutH, false);
    const std::optional<ImuReading> r = imu.read();
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->accel_z, 9.81f, 1e-4f);
}

// TC-013 step 7. The two extreme codes through read() are the rails the
// fault detector compares against (FAULT-010), bit for bit: the constants
// are the same three numbers in the same order as the conversion in
// read(). The range is asymmetric: +32767 falls one count short of 2 g,
// which is where the 0.3.0 limit of 2 x 9.81 sat, so positive saturation
// never tripped (FTS-VR-001 Entry 8 section 8.7).
TEST(Mpu6050Test, ExtremeCountsAreTheRails) {
    FakeI2cBus bus = bus_with_chip();
    Mpu6050 imu(bus, kAddress);
    ASSERT_TRUE(imu.init());
    load_sample(bus, INT16_MAX, INT16_MIN, 0, 0, 0, 0);

    const std::optional<ImuReading> r = imu.read();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->accel_x, Mpu6050::kAccelPositiveRailMps2);
    EXPECT_EQ(r->accel_y, Mpu6050::kAccelNegativeRailMps2);
    EXPECT_LT(Mpu6050::kAccelPositiveRailMps2, 2.0f * Mpu6050::kGravityMps2);    // one count short of 2 g
    EXPECT_EQ(Mpu6050::kAccelNegativeRailMps2, -2.0f * Mpu6050::kGravityMps2);   // exactly -2 g
    EXPECT_NEAR(2.0f * Mpu6050::kGravityMps2 - Mpu6050::kAccelPositiveRailMps2,
                Mpu6050::kGravityMps2 / Mpu6050::kAccelLsbPerG, 1e-6f);          // the gap is one count
}

// TC-013 step 8. The axis mapping (design.md section 6). The frame's axes
// are the chip's as the GY-521 is mounted, x forward, y left, z up, but a
// nose-up rotation is a negative rate about the chip's y, so the driver
// negates the y rate: the frame's gyro_y is the pitch rate the filter
// integrates, positive nose up. The other five channels pass through.
// The count is negated before the conversion, so INT16_MIN's rate is
// representable and a zero count stays +0.0 in the log.
TEST(Mpu6050Test, AxisMappingNegatesOnlyTheYRate) {
    FakeI2cBus bus = bus_with_chip();
    Mpu6050 imu(bus, kAddress);
    ASSERT_TRUE(imu.init());
    load_sample(bus, 0x2000, 0x2000, 0x2000, 0x0083, 0x0083, 0x0083);

    std::optional<ImuReading> r = imu.read();
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->accel_x, 4.905f, 1e-4f);
    EXPECT_NEAR(r->accel_y, 4.905f, 1e-4f);
    EXPECT_NEAR(r->accel_z, 4.905f, 1e-4f);
    EXPECT_NEAR(r->gyro_x, 1.0f, 1e-4f);
    EXPECT_NEAR(r->gyro_y, -1.0f, 1e-4f);
    EXPECT_NEAR(r->gyro_z, 1.0f, 1e-4f);

    load_sample(bus, 0, 0, 0, 0, INT16_MIN, 0);
    r = imu.read();
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->gyro_y, 32768.0f / 131.0f, 1e-3f);

    load_sample(bus, 0, 0, 0, 0, 0, 0);
    r = imu.read();
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(std::signbit(r->gyro_y));
}

// read() before a successful init() is empty and touches the bus not at
// all: the registers would be whatever the chip powered up with.
TEST(Mpu6050Test, ReadBeforeInitReturnsEmpty) {
    FakeI2cBus bus = bus_with_chip();
    Mpu6050 imu(bus, kAddress);
    EXPECT_FALSE(imu.read().has_value());
    EXPECT_TRUE(bus.transactions().empty());
}
