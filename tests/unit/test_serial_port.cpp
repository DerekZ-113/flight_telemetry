#include <gtest/gtest.h>

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

#include "drivers/serial_port.h"
#include "nmea_examples.h"

// TC-018 steps 6 through 9: the real port on a pseudo-terminal. A pty
// slave is a tty in every way the port cares about (termios, O_NONBLOCK,
// tcflush), so the success path of PosixSerialPort runs on the Mac and on
// the CI runner, which the I2C bus's never can. The test holds the master
// and writes to it as a receiver would; the port opens the slave.

namespace {

struct Pty {
    int master = -1;
    std::string slave_path;

    Pty() {
        master = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (master >= 0) {
            const int granted = ::grantpt(master);
            const int unlocked = ::unlockpt(master);
            const char* name = ::ptsname(master);   // a static buffer: copied at once
            if (granted == 0 && unlocked == 0 && name != nullptr) {
                slave_path = name;
            }
        }
    }
    ~Pty() {
        if (master >= 0) {
            ::close(master);
        }
    }
    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;

    bool ok() const { return master >= 0 && !slave_path.empty(); }

    // What a receiver sends: every byte, as is.
    void send(const std::string& bytes) const {
        const ssize_t written = ::write(master, bytes.data(), bytes.size());
        EXPECT_EQ(written, static_cast<ssize_t>(bytes.size()));
    }
};

// Reads until `expected` bytes have arrived. A pty may deliver a write in
// pieces and may take a moment after the master's write, so an empty read
// is retried a bounded number of times; the usual case is one read.
std::string read_all(SerialPort& port, size_t expected) {
    std::string out;
    uint8_t buffer[256];
    for (int attempt = 0; attempt < 200 && out.size() < expected; ++attempt) {
        const std::optional<size_t> got = port.read(buffer, sizeof buffer);
        if (!got.has_value()) {
            break;
        }
        if (*got == 0) {
            ::usleep(1000);
            continue;
        }
        out.append(reinterpret_cast<const char*>(buffer), *got);
    }
    return out;
}

}  // namespace

TEST(PosixSerialPortTest, PtyRoundTripDeliversEveryByte) {
    Pty pty;
    ASSERT_TRUE(pty.ok());
    PosixSerialPort port(pty.slave_path, 9600);
    ASSERT_TRUE(port.ok()) << port.error();
    const std::string sentence = std::string(nmea_examples::kGga) + "\r\n";
    pty.send(sentence);
    EXPECT_EQ(read_all(port, sentence.size()), sentence);
    uint8_t byte = 0;
    const std::optional<size_t> more = port.read(&byte, 1);
    ASSERT_TRUE(more.has_value());
    EXPECT_EQ(*more, 0u);   // nothing waiting is 0, not an error
}

TEST(PosixSerialPortTest, ControlBytesPassUnaltered) {
    // Raw mode: no CR to NL translation, no XON/XOFF swallowing 0x11 and
    // 0x13, no interrupt on 0x03, NUL kept.
    Pty pty;
    ASSERT_TRUE(pty.ok());
    PosixSerialPort port(pty.slave_path, 9600);
    ASSERT_TRUE(port.ok()) << port.error();
    const std::string bytes("\r\n\x11\x13\x03\x00" "a", 7);
    pty.send(bytes);
    EXPECT_EQ(read_all(port, bytes.size()), bytes);
}

// The settings the port found at open are back when it closes
// (REQ-SENS-005, 0.20.0). A terminal's settings outlive its descriptors,
// and the ones the port's non-blocking reads need, VMIN and VTIME zero,
// left behind on the bench made every later blocking reader see an end of
// file at once (FTS-VR-001 Entry 13 §13.10). A second descriptor reads the
// slave's settings as another program would: before, held, and after.
TEST(PosixSerialPortTest, CloseRestoresTheSettingsFoundAtOpen) {
    Pty pty;
    ASSERT_TRUE(pty.ok());
    const int watcher = ::open(pty.slave_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    ASSERT_GE(watcher, 0);
    termios before{};
    ASSERT_EQ(::tcgetattr(watcher, &before), 0);
    before.c_lflag |= ICANON;   // a line discipline the port turns off
    before.c_cc[VMIN] = 1;      // what a blocking reader such as cat needs
    before.c_cc[VTIME] = 0;
    ASSERT_EQ(::tcsetattr(watcher, TCSANOW, &before), 0);
    ASSERT_EQ(::tcgetattr(watcher, &before), 0);   // as the kernel holds them

    {
        PosixSerialPort port(pty.slave_path, 9600);
        ASSERT_TRUE(port.ok()) << port.error();
        termios held{};
        ASSERT_EQ(::tcgetattr(watcher, &held), 0);
        EXPECT_EQ(held.c_cc[VMIN], 0);
        EXPECT_EQ(held.c_lflag & ICANON, static_cast<tcflag_t>(0));
    }

    termios after{};
    ASSERT_EQ(::tcgetattr(watcher, &after), 0);
    EXPECT_EQ(after.c_iflag, before.c_iflag);
    EXPECT_EQ(after.c_oflag, before.c_oflag);
    EXPECT_EQ(after.c_cflag, before.c_cflag);
    // PENDIN is the kernel's state, not a setting: a BSD kernel sets it
    // when a line returns to canonical mode, to reprocess pending input.
    EXPECT_EQ(after.c_lflag & ~static_cast<tcflag_t>(PENDIN), before.c_lflag & ~static_cast<tcflag_t>(PENDIN));
    for (int i = 0; i < NCCS; ++i) {
        EXPECT_EQ(after.c_cc[i], before.c_cc[i]) << "c_cc[" << i << "]";
    }
    EXPECT_EQ(::cfgetispeed(&after), ::cfgetispeed(&before));
    EXPECT_EQ(::cfgetospeed(&after), ::cfgetospeed(&before));
    ::close(watcher);
}

TEST(PosixSerialPortTest, BytesWrittenBeforeOpenAreFlushed) {
    Pty pty;
    ASSERT_TRUE(pty.ok());
    // Hold the slave open so the stale bytes have a queue to land in, and
    // wait until they are readable there: then the constructor's flush is
    // the only thing that can make them vanish.
    const int holder = ::open(pty.slave_path.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    ASSERT_GE(holder, 0);
    pty.send("$STALE,1*00\r\n");
    pollfd waiting{};
    waiting.fd = holder;
    waiting.events = POLLIN;
    const int ready = ::poll(&waiting, 1, 1000);
    ASSERT_EQ(ready, 1);

    PosixSerialPort port(pty.slave_path, 9600);
    ASSERT_TRUE(port.ok()) << port.error();
    uint8_t buffer[64];
    const std::optional<size_t> got = port.read(buffer, sizeof buffer);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, 0u);
    const std::string fresh = "$FRESH,1*00\r\n";
    pty.send(fresh);
    EXPECT_EQ(read_all(port, fresh.size()), fresh);
    ::close(holder);
}

// A receiver that is gone reads 0 bytes forever, exactly like one that is
// idle; poll(2) tells them apart, reporting the hang-up on both kernels.
// Closing the master is the pty's way of unplugging the device.
TEST(PosixSerialPortTest, HungUpDeviceIsAnError) {
    Pty pty;
    ASSERT_TRUE(pty.ok());
    PosixSerialPort port(pty.slave_path, 9600);
    ASSERT_TRUE(port.ok()) << port.error();
    uint8_t buffer[64];
    const std::optional<size_t> before = port.read(buffer, sizeof buffer);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(*before, 0u);
    ::close(pty.master);
    pty.master = -1;
    const std::optional<size_t> after = port.read(buffer, sizeof buffer);
    EXPECT_FALSE(after.has_value());
    EXPECT_NE(port.last_errno(), 0);
    EXPECT_FALSE(port.read(buffer, sizeof buffer).has_value());   // and it stays gone
}

TEST(PosixSerialPortTest, EachStandardRateOpensThePty) {
    Pty pty;
    ASSERT_TRUE(pty.ok());
    for (int rate : {4800, 9600, 19200, 38400, 57600, 115200}) {
        PosixSerialPort port(pty.slave_path, rate);
        EXPECT_TRUE(port.ok()) << rate << ": " << port.error();
    }
}

TEST(PosixSerialPortTest, MissingDeviceIsNotOk) {
    PosixSerialPort port("/dev/tty-does-not-exist-99", 9600);
    EXPECT_FALSE(port.ok());
    EXPECT_NE(port.error().find("/dev/tty-does-not-exist-99"), std::string::npos);
    EXPECT_EQ(port.last_errno(), ENOENT);
    uint8_t byte = 0;
    EXPECT_FALSE(port.read(&byte, 1).has_value());
    EXPECT_EQ(port.last_errno(), EBADF);
}

TEST(PosixSerialPortTest, NonTerminalFileIsNotOk) {
    // /dev/null opens, but it is no terminal, so tcgetattr refuses it:
    // ENOTTY on Linux, ENODEV on macOS. The test asserts only what holds
    // on both, as the I2C bus test does for the same file.
    PosixSerialPort port("/dev/null", 9600);
    EXPECT_FALSE(port.ok());
    EXPECT_NE(port.last_errno(), 0);
    EXPECT_NE(port.last_errno(), ENOENT);   // the file exists; it is the terminal check that failed
    EXPECT_NE(port.error().find("/dev/null"), std::string::npos);
}

TEST(PosixSerialPortTest, UnsupportedBaudRateIsRefusedBeforeOpen) {
    PosixSerialPort port("/dev/tty-does-not-exist-99", 1234);
    EXPECT_FALSE(port.ok());
    EXPECT_EQ(port.last_errno(), EINVAL);   // not ENOENT: the rate was refused before the path was tried
    EXPECT_NE(port.error().find("1234"), std::string::npos);
    EXPECT_NE(port.error().find("4800, 9600, 19200, 38400, 57600, 115200"), std::string::npos);
}

TEST(PosixSerialPortTest, SupportsExactlyTheSixStandardRates) {
    for (int rate : {4800, 9600, 19200, 38400, 57600, 115200}) {
        EXPECT_TRUE(PosixSerialPort::supports_baud_rate(rate)) << rate;
    }
    for (int rate : {0, 1200, 1234, 9601, 230400, -9600}) {
        EXPECT_FALSE(PosixSerialPort::supports_baud_rate(rate)) << rate;
    }
}

TEST(PosixSerialPortTest, DescriptorOwnershipCannotBeDuplicated) {
    static_assert(!std::is_copy_constructible<PosixSerialPort>::value, "a copy would close the descriptor twice");
    static_assert(!std::is_move_constructible<PosixSerialPort>::value, "a declared copy constructor suppresses the move");
    static_assert(!std::is_copy_assignable<PosixSerialPort>::value, "assignment would leak one descriptor and double-close another");
    static_assert(!std::is_move_assignable<PosixSerialPort>::value, "no move assignment either");
}
