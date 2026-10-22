#include "drivers/serial_port.h"

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace {

// The kernel programs a divider, not a number: a rate is a named constant
// or nothing. These six are what a u-blox receiver can be set to and what
// every POSIX termios names.
std::optional<speed_t> baud_constant(int baud_rate) {
    switch (baud_rate) {
        case 4800: return B4800;
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        default: return std::nullopt;
    }
}

}  // namespace

bool PosixSerialPort::supports_baud_rate(int baud_rate) {
    return baud_constant(baud_rate).has_value();
}

PosixSerialPort::PosixSerialPort(const std::string& path, int baud_rate) {
    const std::optional<speed_t> speed = baud_constant(baud_rate);
    if (!speed.has_value()) {
        last_errno_ = EINVAL;
        error_ = path + ": baud rate " + std::to_string(baud_rate) + " is not one of " + kSupportedRates;   // the class's list
        return;
    }
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        last_errno_ = errno;
        error_ = path + ": " + std::strerror(last_errno_);
        return;
    }
    // Start from the device's current settings, then turn the line
    // discipline off. A file that is not a terminal fails here (ENOTTY),
    // which is how a wrong path that happens to exist is refused.
    termios tio{};
    const int got = ::tcgetattr(fd, &tio);
    if (got != 0) {
        last_errno_ = errno;
        error_ = path + ": " + std::strerror(last_errno_);
        ::close(fd);
        return;
    }
    // Kept to be put back at close (REQ-SENS-005): a terminal's settings
    // outlive the descriptor that set them, and VMIN and VTIME zero, which
    // these reads need, make the next blocking reader see an end of file.
    original_ = tio;
    ::cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= static_cast<tcflag_t>(~(CRTSCTS | CSTOPB));   // no flow control; one stop bit
    tio.c_iflag |= IGNBRK;                                       // a BREAK is noise, not a NUL in the stream
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    const int speed_set = ::cfsetspeed(&tio, *speed);
    const int applied = speed_set == 0 ? ::tcsetattr(fd, TCSANOW, &tio) : -1;
    const int flushed = applied == 0 ? ::tcflush(fd, TCIFLUSH) : -1;
    if (flushed != 0) {
        last_errno_ = errno;
        error_ = path + ": cannot configure the terminal: " + std::strerror(last_errno_);
        ::tcsetattr(fd, TCSANOW, &original_);   // whatever took, undone
        ::close(fd);
        return;
    }
    fd_ = fd;
}

PosixSerialPort::~PosixSerialPort() {
    if (fd_ >= 0) {
        // Unchecked: a device the kernel hung up refuses it, and there is
        // nothing left to restore.
        ::tcsetattr(fd_, TCSANOW, &original_);
        ::close(fd_);
    }
}

bool PosixSerialPort::ok() const {
    return fd_ >= 0;
}

const std::string& PosixSerialPort::error() const {
    return error_;
}

int PosixSerialPort::last_errno() const {
    return last_errno_;
}

std::optional<size_t> PosixSerialPort::read(uint8_t* buffer, size_t length) {
    if (!ok()) {
        last_errno_ = EBADF;
        return std::nullopt;
    }
    // A terminal the kernel has hung up (a USB receiver unplugged; a pty
    // whose master closed) reads 0 bytes forever, exactly like an idle
    // one. poll(2) tells them apart: it reports the hang-up on both
    // kernels, so a device that is gone is an error the driver counts
    // and the exit line shows, not a receiver that is quiet.
    pollfd waiting{};
    waiting.fd = fd_;
    waiting.events = POLLIN;
    const int ready = ::poll(&waiting, 1, 0);
    const bool hung_up = ready > 0 && (waiting.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0;
    if (hung_up) {
        last_errno_ = EIO;
        return std::nullopt;
    }
    const ssize_t got = ::read(fd_, buffer, length);
    if (got < 0) {
        // Not "nothing waiting": with VMIN and VTIME zero that is 0 bytes
        // on both kernels. A failure is a vanished driver, or on Linux
        // EAGAIN, another process reading the same device.
        last_errno_ = errno;
        return std::nullopt;
    }
    return static_cast<size_t>(got);
}
