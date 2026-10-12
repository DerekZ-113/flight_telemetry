#include "drivers/i2c_bus.h"

#include <cerrno>
#include <cstring>

bool LinuxI2cBus::ok() const {
    return fd_ >= 0;
}

const std::string& LinuxI2cBus::error() const {
    return error_;
}

int LinuxI2cBus::last_errno() const {
    return last_errno_;
}

// The second platform split in the codebase, after the clock's sleep
// primitive: both sit on an operating-system boundary and nothing above
// either of them differs between platforms.
#if defined(__linux__)

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

// I2C_SLAVE stores the 7-bit address in the descriptor's private data;
// every read() and write() after it goes to that device until the next
// call. The argument is a plain integer rather than a pointer, and the
// kernel takes it as an unsigned long, so it is cast to exactly that. The
// call fails on a descriptor that is not an I2C device file (ENOTTY) or
// with an address outside the 7-bit range.
bool select_device(int fd, uint8_t address, int& err) {
    if (ioctl(fd, I2C_SLAVE, static_cast<unsigned long>(address)) < 0) {
        err = errno;
        return false;
    }
    return true;
}

}  // namespace

LinuxI2cBus::LinuxI2cBus(const std::string& device_path)
    // O_RDWR: register writes and data reads share the descriptor. No
    // O_NONBLOCK: a transaction completes or fails within the controller's
    // own timeout, well inside one cycle, and the loop budgets for that
    // (REQ-FAULT-001).
    : fd_(open(device_path.c_str(), O_RDWR))
{
    if (fd_ < 0) {
        error_ = device_path + ": " + std::strerror(errno);
    }
}

LinuxI2cBus::~LinuxI2cBus() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool LinuxI2cBus::write_register(uint8_t address, uint8_t reg, uint8_t value) {
    if (!ok()) {
        last_errno_ = EBADF;
        return false;
    }
    if (!select_device(fd_, address, last_errno_)) {
        return false;
    }
    // One write transaction: the register address followed by the byte.
    // Both chips take exactly this form for a single-register write
    // (BMP280 datasheet section 5.2.1, MPU-6050 product specification
    // section 9.3). A short write is a failure like any other: the chip
    // did not get the byte.
    const uint8_t frame[2] = {reg, value};
    const ssize_t written = write(fd_, frame, sizeof frame);
    if (written != static_cast<ssize_t>(sizeof frame)) {
        last_errno_ = written < 0 ? errno : EIO;
        return false;
    }
    return true;
}

bool LinuxI2cBus::read_registers(uint8_t address, uint8_t reg, uint8_t* buffer, size_t length) {
    if (!ok()) {
        last_errno_ = EBADF;
        return false;
    }
    if (!select_device(fd_, address, last_errno_)) {
        return false;
    }
    // Two transactions with a stop between them: write the register
    // address, then read the bytes. Both datasheets allow the stop here
    // (BMP280 section 5.2.2: "either a stop or a repeated start"); the
    // chip's register pointer survives it. If a bus ever needs the
    // repeated start, this function becomes one ioctl(I2C_RDWR) with a
    // two-message array, and nothing above it changes.
    const ssize_t written = write(fd_, &reg, 1);
    if (written != 1) {
        last_errno_ = written < 0 ? errno : EIO;
        return false;
    }
    const ssize_t got = read(fd_, buffer, length);
    if (got != static_cast<ssize_t>(length)) {
        last_errno_ = got < 0 ? errno : EIO;
        return false;
    }
    return true;
}

#else

// No i2c-dev on this platform. The class still exists so the program
// compiles and links from one source list; its first use says why it
// cannot work. Parameters are unnamed because nothing uses them.
LinuxI2cBus::LinuxI2cBus(const std::string& device_path)
    : error_(device_path + ": I2C device files are not available on this platform"),
      last_errno_(ENOTSUP)
{
}

LinuxI2cBus::~LinuxI2cBus() = default;

bool LinuxI2cBus::write_register(uint8_t, uint8_t, uint8_t) {
    return false;
}

bool LinuxI2cBus::read_registers(uint8_t, uint8_t, uint8_t*, size_t) {
    return false;
}

#endif
