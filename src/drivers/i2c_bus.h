#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The boundary to an I2C bus: the "via I2C" of REQ-SENS-001 through
// REQ-SENS-004.
//
// Same pattern as DataSource and Clock. The drivers talk to this interface
// and never to the operating system, so a driver's register protocol and
// arithmetic are tested on a machine with no bus at all, against a fake
// that serves recorded bytes (tests/unit/fake_i2c_bus.h). The one real
// implementation, LinuxI2cBus below, is the only code that knows what a
// device file is.
//
// A register transaction on both chips in this project has the same
// shape: send the register address, then read or write the bytes. Both
// chips auto-increment their register pointer, so one call reads a whole
// block (a burst read). That is what the datasheets ask for: the data
// registers are latched together, and reading them one at a time could
// mix bytes from two conversions.
//
// Errors are reported as false, never thrown. A failed transaction is an
// expected event at 50 Hz (REQ-FAULT-001 budgets 500 ms of them); the
// caller turns it into read_ok = false and the fault detector decides when
// it has gone on long enough to matter.
class I2cBus {
public:
    virtual ~I2cBus() = default;

    // Write one byte to register `reg` of the device at `address` (7-bit).
    virtual bool write_register(uint8_t address, uint8_t reg, uint8_t value) = 0;

    // Read `length` consecutive registers starting at `reg` into `buffer`
    // in one burst. False if any part of the transaction failed; the
    // buffer's contents are then unspecified.
    virtual bool read_registers(uint8_t address, uint8_t reg, uint8_t* buffer, size_t length) = 0;
};

// The real bus: a Linux i2c-dev device file such as /dev/i2c-1.
//
// One descriptor serves the whole bus. ioctl(I2C_SLAVE) sets which device
// the next read() or write() on the descriptor addresses, so it is issued
// before every transaction rather than once at open: two drivers share
// this object and alternate addresses every cycle.
//
// RAII on the descriptor. An int has no destructor, so this class gives it
// one: open() in the constructor, close() in the destructor. Copying is
// deleted so no two objects can ever close the same descriptor, and with
// the copy constructor declared the compiler generates no move either.
// The constructor never throws; a failed open leaves ok() false and
// error() saying why, the same shape as BinaryLogger and ZmqPublisher.
//
// On a platform without i2c-dev (the development Mac) the same class
// compiles to a stub whose constructor records ENOTSUP and whose
// transactions fail, so one source list builds everywhere and `--live`
// reports a clear reason instead of failing to link.
class LinuxI2cBus : public I2cBus {
public:
    explicit LinuxI2cBus(const std::string& device_path);
    ~LinuxI2cBus() override;
    LinuxI2cBus(const LinuxI2cBus&) = delete;
    LinuxI2cBus& operator=(const LinuxI2cBus&) = delete;

    // False if the device file could not be opened; error() says why.
    bool ok() const;
    const std::string& error() const;

    // errno of the most recent failed transaction, 0 if none has failed.
    int last_errno() const;

    bool write_register(uint8_t address, uint8_t reg, uint8_t value) override;
    bool read_registers(uint8_t address, uint8_t reg, uint8_t* buffer, size_t length) override;

private:
    int fd_ = -1;
    std::string error_;
    int last_errno_ = 0;
};
