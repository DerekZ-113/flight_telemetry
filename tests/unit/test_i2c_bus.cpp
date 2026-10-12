// Tests for src/drivers/i2c_bus (the I2C seam of REQ-SENS-001 through
// REQ-SENS-004) and for the fake the driver tests stand on. Test card
// TC-014, steps 1 through 4.
//
// The Linux implementation can be proven only as far as a machine without
// a bus allows: that it refuses a missing device file and that a file
// which is not an I2C device fails the transaction, not the program. The
// transaction paths that succeed are exercised on the Raspberry Pi
// (TC-014 Hardware steps) and listed in verification_results.md.

#include <gtest/gtest.h>

#include <type_traits>

#include "drivers/i2c_bus.h"
#include "fake_i2c_bus.h"

namespace {

constexpr uint8_t kBaro = 0x76;
constexpr uint8_t kImu = 0x68;

}  // namespace

// TC-014 step 1. A burst read returns consecutive registers from the
// device's file and is logged with its length.
TEST(FakeI2cBusTest, BurstReadReturnsConsecutiveRegisters) {
    FakeI2cBus bus;
    bus.load(kBaro, 0x88, {0x70, 0x6B, 0x43, 0x67});

    uint8_t buffer[4] = {0, 0, 0, 0};
    ASSERT_TRUE(bus.read_registers(kBaro, 0x88, buffer, sizeof buffer));
    EXPECT_EQ(buffer[0], 0x70);
    EXPECT_EQ(buffer[1], 0x6B);
    EXPECT_EQ(buffer[2], 0x43);
    EXPECT_EQ(buffer[3], 0x67);

    ASSERT_EQ(bus.transactions().size(), 1u);
    EXPECT_EQ(bus.transactions()[0].address, kBaro);
    EXPECT_EQ(bus.transactions()[0].reg, 0x88);
    EXPECT_FALSE(bus.transactions()[0].write);
    EXPECT_EQ(bus.transactions()[0].length, 4u);
}

// TC-014 step 2. A write lands in the register file and in the log, and
// only on the device it was addressed to.
TEST(FakeI2cBusTest, WriteLandsInRegisterFileAndLog) {
    FakeI2cBus bus;
    ASSERT_TRUE(bus.write_register(kImu, 0x6B, 0x01));

    EXPECT_EQ(bus.registers(kImu)[0x6B], 0x01);
    EXPECT_EQ(bus.registers(kBaro)[0x6B], 0x00);   // the other device is untouched

    ASSERT_EQ(bus.transactions().size(), 1u);
    EXPECT_EQ(bus.transactions()[0].address, kImu);
    EXPECT_EQ(bus.transactions()[0].reg, 0x6B);
    EXPECT_TRUE(bus.transactions()[0].write);
    EXPECT_EQ(bus.transactions()[0].value, 0x01);
    EXPECT_EQ(bus.transactions()[0].length, 1u);
}

// TC-014 step 3. Failure is per register and per device, so one
// transaction in a driver's init() sequence can be made to fail while
// its neighbours succeed, and it can be lifted again.
TEST(FakeI2cBusTest, FailureIsPerRegisterAndCanBeLifted) {
    FakeI2cBus bus;
    uint8_t byte = 0;

    bus.fail_reads_at(kBaro, 0xF7);
    EXPECT_FALSE(bus.read_registers(kBaro, 0xF7, &byte, 1));
    EXPECT_TRUE(bus.read_registers(kBaro, 0xD0, &byte, 1));    // another register
    EXPECT_TRUE(bus.read_registers(kImu, 0xF7, &byte, 1));     // another device

    bus.fail_writes_at(kBaro, 0xF4);
    EXPECT_FALSE(bus.write_register(kBaro, 0xF4, 0x2F));
    EXPECT_EQ(bus.registers(kBaro)[0xF4], 0x00);               // a refused write changes nothing
    EXPECT_TRUE(bus.write_register(kBaro, 0xF5, 0x00));

    bus.fail_reads_at(kBaro, 0xF7, false);
    bus.fail_writes_at(kBaro, 0xF4, false);
    EXPECT_TRUE(bus.read_registers(kBaro, 0xF7, &byte, 1));
    EXPECT_TRUE(bus.write_register(kBaro, 0xF4, 0x2F));

    EXPECT_EQ(bus.transactions().size(), 7u);   // refused transactions are logged too
    bus.clear_transactions();
    EXPECT_TRUE(bus.transactions().empty());
}

// A burst that would run past the end of the register file is refused
// rather than wrapped; it is a test bug, not a chip behavior.
TEST(FakeI2cBusTest, BurstPastEndOfFileIsRefused) {
    FakeI2cBus bus;
    uint8_t buffer[4];
    EXPECT_FALSE(bus.read_registers(kBaro, 0xFE, buffer, sizeof buffer));
    EXPECT_TRUE(bus.read_registers(kBaro, 0xFC, buffer, sizeof buffer));   // exactly to the end
}

// TC-014 step 4. A device file that does not exist leaves the bus not ok
// with the path in the message, and every transaction on it fails with an
// errno rather than touching a closed descriptor.
TEST(LinuxI2cBusTest, MissingDeviceFileIsNotOk) {
    LinuxI2cBus bus("/dev/i2c-99");
    EXPECT_FALSE(bus.ok());
    EXPECT_NE(bus.error().find("/dev/i2c-99"), std::string::npos);

    uint8_t byte = 0;
    EXPECT_FALSE(bus.read_registers(kBaro, 0xD0, &byte, 1));
    EXPECT_FALSE(bus.write_register(kBaro, 0xF4, 0x2F));
    EXPECT_NE(bus.last_errno(), 0);
}

// A file that opens but is not an I2C device fails the transaction, not
// the program: on Linux the open succeeds and ioctl(I2C_SLAVE) answers
// ENOTTY; on a platform without i2c-dev the stub answers ENOTSUP. The
// test asserts only what holds on both.
TEST(LinuxI2cBusTest, NonI2cDeviceFileFailsTheTransaction) {
    LinuxI2cBus bus("/dev/null");
    uint8_t byte = 0;
    EXPECT_FALSE(bus.read_registers(kBaro, 0xD0, &byte, 1));
    EXPECT_FALSE(bus.write_register(kImu, 0x6B, 0x01));
    EXPECT_NE(bus.last_errno(), 0);
}

// RAII on the descriptor: no two objects may ever close the same one, so
// the class is neither copyable nor movable (declaring the copy
// constructor deleted suppresses the implicit move).
TEST(LinuxI2cBusTest, DescriptorOwnershipCannotBeDuplicated) {
    static_assert(!std::is_copy_constructible<LinuxI2cBus>::value, "copying would double-close the descriptor");
    static_assert(!std::is_move_constructible<LinuxI2cBus>::value, "moving is not provided either");
    static_assert(!std::is_copy_assignable<LinuxI2cBus>::value, "");
    SUCCEED();
}
