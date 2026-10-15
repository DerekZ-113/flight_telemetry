#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// The boundary to a serial device: the "configurable serial device" of
// REQ-SENS-005, the twin of I2cBus for a receiver that talks on its own
// schedule instead of answering when asked.
//
// Same pattern as DataSource, Clock and I2cBus. The GPS driver reads bytes
// through this interface and never calls the operating system, so its
// framing and its pairing of sentences are tested against a fake that
// hands out bytes in whatever pieces a test chooses (tests/unit/
// fake_serial_port.h). The one real implementation, PosixSerialPort, is
// the only code that knows what a terminal device is.
//
// One method, because a receiver is read and never written: the bytes
// waiting on the device, at most `length`, into `buffer`. The count read,
// 0 when nothing is waiting, nullopt on an error the implementation keeps
// the reason for. It never blocks: the 50 Hz loop asks every cycle and
// takes what is there, which at 9600 baud is about nineteen bytes.
class SerialPort {
public:
    virtual ~SerialPort() = default;

    virtual std::optional<size_t> read(uint8_t* buffer, size_t length) = 0;
};

// The real port: a terminal device file such as /dev/serial0 (the Pi's
// header UART) or /dev/ttyACM0 (a receiver on USB). termios is POSIX, so
// unlike the I2C device file this class has no platform split: it builds
// and runs on macOS, where a pseudo-terminal stands in for the device in
// the unit tests.
//
// The device is opened read-only, O_NOCTTY (it must not become the
// process's controlling terminal, or a 0x03 in the byte stream could be
// an interrupt) and O_NONBLOCK (open does not wait for a modem's carrier,
// and read returns what is waiting). The line discipline is turned off
// with cfmakeraw: no line assembly, no echo, no signal characters, no
// carriage-return translation, eight bits, no parity. Nothing waits in a
// read (VMIN and VTIME zero), the receiver is enabled and modem lines are
// ignored (CREAD, CLOCAL), hardware flow control is off, and the baud rate
// is one of the six named constants a u-blox receiver speaks. After the
// settings take, the input queue is flushed once: a receiver that has
// been talking to a closed port holds seconds of old sentences, and the
// first fix the pipeline sees must be current. One stop bit, and a BREAK
// on the line ignored: cfmakeraw leaves both as it found them, and a
// receive-only line gets only noise from a BREAK.
//
// With VMIN and VTIME zero an empty read returns 0 bytes on both kernels,
// which is the answer forty-nine cycles in fifty. A device the kernel has
// hung up (a USB receiver unplugged) also reads 0 bytes, forever, so each
// read first asks poll(2), which reports the hang-up on both kernels, and
// a device that is gone is an error the driver counts; so is a read that
// fails (a vanished driver, or on Linux EAGAIN, which with these settings
// means another process is reading the same device).
//
// RAII on the descriptor, copying deleted, the constructor never throws:
// ok() false and error() saying why, the same shape as LinuxI2cBus.
class PosixSerialPort : public SerialPort {
public:
    // The rates the port accepts, as the configuration's message prints them.
    static constexpr const char* kSupportedRates = "4800, 9600, 19200, 38400, 57600, 115200";
    static bool supports_baud_rate(int baud_rate);

    PosixSerialPort(const std::string& path, int baud_rate);
    ~PosixSerialPort() override;
    PosixSerialPort(const PosixSerialPort&) = delete;
    PosixSerialPort& operator=(const PosixSerialPort&) = delete;

    // False if the device could not be opened and configured; error() says why.
    bool ok() const;
    const std::string& error() const;

    // errno of the most recent failure, 0 if none.
    int last_errno() const;

    std::optional<size_t> read(uint8_t* buffer, size_t length) override;

private:
    int fd_ = -1;
    std::string error_;
    int last_errno_ = 0;
};
