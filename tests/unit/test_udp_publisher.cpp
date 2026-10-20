// Tests for src/transport/udp_publisher (REQ-TRANS-002, REQ-TRANS-003).
// Test cards TC-005 (unit-level steps) and TC-010 (steps 8 through 12).
//
// Every test runs over loopback. The Receiver below is the test's stand-in
// for a ground station: a datagram socket bound to 127.0.0.1 on port 0,
// the port read back from the kernel, with a 1 s receive timeout so a
// missing datagram fails in a second instead of hanging ctest. A datagram
// socket has no join, so there is no settle step: whatever is sent after
// the bind is received, in order, over loopback.
//
// Two tests depend on what the kernel reports for a port nobody listens
// on. Both kernels report the first ICMP unreachable on a fresh socket as
// ECONNREFUSED on a later send; how many they report after that differs
// (Linux's loopback is exempt from the ICMP rate limit, macOS suppresses
// repeats for about 50 ms), so the tests assert the invariant and the
// verification record keeps the counts.

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "drivers/simulated_source.h"
#include "logging/log_format.h"
#include "transport/session_message.h"
#include "transport/udp_publisher.h"

namespace {

constexpr int kRecvTimeoutMs = 1000;   // a missing datagram fails in a second, never hangs ctest
constexpr int kBurst = 1000;
const std::chrono::milliseconds kBound(2000);
const char* const kLoopback = "127.0.0.1";

class Receiver {
public:
    // receive_buffer_bytes > 0 shrinks the kernel's queue, the UDP analog of
    // a high-water mark, so a receiver that never reads drops quickly.
    explicit Receiver(int receive_buffer_bytes = 0) {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (receive_buffer_bytes > 0) {
            ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &receive_buffer_bytes, sizeof(receive_buffer_bytes));
        }
        timeval timeout{};
        timeout.tv_sec = kRecvTimeoutMs / 1000;
        timeout.tv_usec = (kRecvTimeoutMs % 1000) * 1000;
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;   // the kernel picks a free port
        ::bind(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        socklen_t length = sizeof(address);
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
    }
    ~Receiver() { ::close(fd_); }
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    int port() const { return port_; }

    // One datagram into `buffer`, or -1 after the timeout.
    ssize_t receive(std::vector<uint8_t>& buffer) {
        buffer.resize(65535);
        const ssize_t n = ::recv(fd_, buffer.data(), buffer.size(), 0);
        if (n >= 0) {
            buffer.resize(static_cast<size_t>(n));
        }
        return n;
    }

    // Reads without waiting until the queue is empty; returns the count.
    int drain() {
        std::vector<uint8_t> buffer(65535);
        int count = 0;
        while (::recv(fd_, buffer.data(), buffer.size(), MSG_DONTWAIT) >= 0) {
            count++;
        }
        return count;
    }

private:
    int fd_ = -1;
    int port_ = 0;
};

// A loopback port nobody is bound to: a receiver that existed and closed.
int dead_port() {
    Receiver gone;
    return gone.port();
}

const uint8_t* frame_bytes(const std::vector<uint8_t>& datagram) {
    return datagram.data() + sizeof(LogFileHeader) + 1;
}

}  // namespace

// A publisher to a live loopback receiver is ready to use. (REQ-TRANS-002)
TEST(UdpPublisherTest, ConnectsAndReportsOk) {
    Receiver receiver;
    UdpPublisher publisher(kLoopback, receiver.port());
    EXPECT_TRUE(publisher.ok()) << publisher.error();
    EXPECT_TRUE(publisher.error().empty());
    EXPECT_EQ(publisher.stats().published, 0u);
    EXPECT_EQ(publisher.stats().send_failures, 0u);
    EXPECT_EQ(publisher.stats().last_errno, 0);
}

// One datagram is a complete one-record log file: the header, the FRAME
// tag, the frame's bytes verbatim. (REQ-TRANS-002; TC-005 step 4)
TEST(UdpPublisherTest, DatagramIsHeaderTagAndFrameBytes) {
    Receiver receiver;
    const LogFileHeader expected = make_log_header(RunMode::LIVE, RunMode::LIVE);
    UdpPublisher publisher(kLoopback, receiver.port(), expected);
    SimulatedSource source(20, 42);
    const TelemetryFrame frame = source.read_frame();
    ASSERT_TRUE(publisher.publish(frame));

    std::vector<uint8_t> datagram;
    ASSERT_EQ(receiver.receive(datagram), static_cast<ssize_t>(kFrameMessageBytes));
    LogFileHeader header{};
    std::memcpy(&header, datagram.data(), sizeof(header));
    EXPECT_TRUE(header_is_valid(header));
    EXPECT_EQ(header.version, 3);
    EXPECT_EQ(header.run_mode, static_cast<uint8_t>(RunMode::LIVE));   // the header handed in (REQ-LOG-007)
    EXPECT_EQ(std::memcmp(&header, &expected, sizeof(header)), 0);
    EXPECT_EQ(datagram[sizeof(header)], static_cast<uint8_t>(RecordType::FRAME));
    EXPECT_EQ(std::memcmp(frame_bytes(datagram), &frame, sizeof(frame)), 0);
}

// 100 simulator frames arrive in order, each byte-identical: loopback never
// reorders, and 12 KB fits any default receive buffer. (TC-005 step 7)
TEST(UdpPublisherTest, EveryPublishedFrameArrivesInOrder) {
    Receiver receiver;
    UdpPublisher publisher(kLoopback, receiver.port());
    SimulatedSource source(20, 42);
    std::vector<TelemetryFrame> sent;
    for (int i = 0; i < 100; i++) {
        sent.push_back(source.read_frame());
        ASSERT_TRUE(publisher.publish(sent.back())) << "frame " << i;
    }
    EXPECT_EQ(publisher.stats().published, 100u);
    EXPECT_EQ(publisher.stats().send_failures, 0u);

    std::vector<uint8_t> datagram;
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(receiver.receive(datagram), static_cast<ssize_t>(kFrameMessageBytes)) << "frame " << i;
        EXPECT_EQ(std::memcmp(frame_bytes(datagram), &sent[static_cast<size_t>(i)], sizeof(TelemetryFrame)), 0)
            << "frame " << i;
    }
}

// A destination that is not an IPv4 dotted quad, or a port outside 1 to
// 65535, is refused at construction with a message naming it; a publish
// on the refused publisher is counted, not thrown. (TC-010 step 8)
TEST(UdpPublisherTest, BadAddressOrPortIsRefusedNotThrown) {
    struct Case {
        const char* host;
        int port;
        const char* names;
    };
    const Case cases[] = {
        {"localhost", 5556, "dotted-quad"},
        {"256.1.1.1", 5556, "dotted-quad"},
        {"1.2.3", 5556, "dotted-quad"},
        {"127.0.0.1", 0, "port"},
        {"127.0.0.1", 70000, "port"},
    };
    for (const Case& c : cases) {
        UdpPublisher publisher(c.host, c.port);
        EXPECT_FALSE(publisher.ok()) << c.host << ":" << c.port;
        EXPECT_NE(publisher.error().find(c.names), std::string::npos) << publisher.error();
        TelemetryFrame frame{};
        EXPECT_FALSE(publisher.publish(frame));
        EXPECT_EQ(publisher.stats().send_failures, 1u);
        EXPECT_EQ(publisher.stats().published, 0u);
    }
}

// 0.0.0.0 would mean loopback and 255.255.255.255 needs SO_BROADCAST, which
// is not set: neither is a unicast destination. (TC-010 step 8)
TEST(UdpPublisherTest, NonUnicastAddressIsRefused) {
    for (const char* host : {"0.0.0.0", "255.255.255.255"}) {
        UdpPublisher publisher(host, 5556);
        EXPECT_FALSE(publisher.ok()) << host;
        EXPECT_NE(publisher.error().find("unicast"), std::string::npos) << publisher.error();
    }
}

// socket() fails only when the process is out of descriptors. The soft
// limit is lowered to zero around the construction, so that branch is a
// test rather than a coverage exclusion, and restored before anything
// else runs. (TC-010 step 8) Under the sanitized build (TC-039) the test
// skips itself: libubsan validates an object's vtable by probing memory
// through a pipe, which the exhausted descriptor table refuses, so the
// publisher's own construction is reported as an invalid vptr although
// nothing is wrong with it (FTS-VR-001 Entry 27). The branch is measured
// on the coverage build, where the test runs.
TEST(UdpPublisherTest, SocketFailureIsReportedNotThrown) {
#ifdef TELEMETRY_SANITIZED
    GTEST_SKIP() << "under the sanitizers: libubsan probes a vtable's memory through a pipe, which the exhausted "
                    "descriptor table refuses, so the construction would read as an invalid vptr (FTS-VR-001 Entry 27); "
                    "the branch is measured on the coverage build";
#endif
    rlimit saved{};
    ASSERT_EQ(::getrlimit(RLIMIT_NOFILE, &saved), 0);
    rlimit none = saved;
    none.rlim_cur = 0;
    ASSERT_EQ(::setrlimit(RLIMIT_NOFILE, &none), 0);
    UdpPublisher publisher(kLoopback, 5556);
    ASSERT_EQ(::setrlimit(RLIMIT_NOFILE, &saved), 0);

    EXPECT_FALSE(publisher.ok());
    EXPECT_NE(publisher.error().find("socket"), std::string::npos) << publisher.error();
    TelemetryFrame frame{};
    EXPECT_FALSE(publisher.publish(frame));
    EXPECT_EQ(publisher.stats().send_failures, 1u);
}

// Nobody listening: 1000 sends return promptly. What the kernel refuses,
// the port-unreachable it learns after the fact, is counted, and the sum
// is still 1000. (REQ-TRANS-003; TC-010 step 9)
TEST(UdpPublisherTest, NoReceiverNeverBlocks) {
    UdpPublisher publisher(kLoopback, dead_port());
    ASSERT_TRUE(publisher.ok()) << publisher.error();
    TelemetryFrame frame{};
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kBurst; i++) {
        publisher.publish(frame);
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, kBound);
    const PublishStats& stats = publisher.stats();
    EXPECT_EQ(stats.published + stats.send_failures, static_cast<uint64_t>(kBurst));
    if (stats.send_failures > 0) {
        EXPECT_EQ(stats.last_errno, ECONNREFUSED);
    }
}

// The port-unreachable arrives after the datagram that caused it, so the
// kernel reports it on a later send: one publish fails with ECONNREFUSED,
// the one after it succeeds, nothing is thrown. (TC-010 step 10)
TEST(UdpPublisherTest, PortUnreachableIsCountedNotThrown) {
    UdpPublisher publisher(kLoopback, dead_port());
    ASSERT_TRUE(publisher.ok()) << publisher.error();
    TelemetryFrame frame{};
    EXPECT_TRUE(publisher.publish(frame));   // nothing to refuse yet
    bool refused = false;
    for (int attempt = 0; attempt < 100 && !refused; attempt++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        refused = !publisher.publish(frame);
    }
    ASSERT_TRUE(refused) << "the kernel never reported the unreachable port";
    EXPECT_EQ(publisher.stats().last_errno, ECONNREFUSED);
    EXPECT_EQ(publisher.stats().send_failures, 1u);
    EXPECT_TRUE(publisher.publish(frame));   // the report was consumed; this one goes out
    // That frame drew another ICMP; a status meets its report through the
    // same send (REQ-TRANS-004). macOS suppresses repeats for about 50 ms,
    // so the wait is longer than the first one.
    refused = false;
    for (int attempt = 0; attempt < 300 && !refused; attempt++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        refused = !publisher.publish_status(
            SessionStatus{RunMode::SIMULATED, RunMode::SIMULATED, StreamState::RUNNING, SessionIdentity{}});
    }
    ASSERT_TRUE(refused) << "the kernel never reported the unreachable port to the status send";
    EXPECT_EQ(publisher.stats().send_failures, 2u);
    EXPECT_EQ(publisher.stats().last_errno, ECONNREFUSED);
}

// A receiver that never reads, with a 4 KB buffer: the kernel drops on its
// side; the sender never waits and sees nothing. How many the kernel keeps
// is its business (macOS held 22, 104 and 301 of 1000 in different runs;
// Linux holds a handful), so the assertion is the step 4 range: some
// arrived, not all. (TC-010 step 11)
TEST(UdpPublisherTest, StalledReceiverDropsInsteadOfBlocking) {
    Receiver receiver(4096);
    UdpPublisher publisher(kLoopback, receiver.port());
    ASSERT_TRUE(publisher.ok()) << publisher.error();
    TelemetryFrame frame{};
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kBurst; i++) {
        publisher.publish(frame);
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, kBound);
    const PublishStats& stats = publisher.stats();
    EXPECT_EQ(stats.published + stats.send_failures, static_cast<uint64_t>(kBurst));
    const int drained = receiver.drain();
    EXPECT_GE(drained, 1);
    EXPECT_LT(drained, kBurst);
}

// A directed broadcast address without SO_BROADCAST. Linux marks
// 127.255.255.255 broadcast in its local routing table and refuses the
// connect (EACCES); macOS routes it to 127.0.0.1 like any unicast address
// and the send succeeds (measured 2026-10-12), refusing only the limited
// broadcast 255.255.255.255, which the unicast check above catches by
// name on both. Whichever the kernel does, it is status and a counter,
// never an exception; the verification record says which. (TC-010 step 12)
TEST(UdpPublisherTest, DirectedBroadcastIsRefusedOrRouted) {
    UdpPublisher publisher("127.255.255.255", 5556);
    TelemetryFrame frame{};
    if (!publisher.ok()) {
        EXPECT_FALSE(publisher.error().empty());
        EXPECT_FALSE(publisher.publish(frame));
        EXPECT_EQ(publisher.stats().send_failures, 1u);
        return;
    }
    const bool sent = publisher.publish(frame);
    const PublishStats& stats = publisher.stats();
    EXPECT_EQ(stats.published + stats.send_failures, 1u);
    if (!sent) {
        EXPECT_EQ(stats.last_errno, EACCES);
    }
}

// REQ-TRANS-004, TC-021 step 3: a status is one datagram of nineteen bytes,
// counted apart from frames.
TEST(UdpPublisherTest, StatusDatagramIsNineteenBytes) {
    Receiver receiver;
    UdpPublisher publisher(kLoopback, receiver.port());
    ASSERT_TRUE(publisher.ok());

    const SessionStatus status{RunMode::SIMULATED, RunMode::SIMULATED, StreamState::ENDED, SessionIdentity{}};
    ASSERT_TRUE(publisher.publish_status(status));

    std::vector<uint8_t> datagram;
    ASSERT_EQ(receiver.receive(datagram), static_cast<ssize_t>(kSessionMessageBytes));
    const std::array<uint8_t, kSessionMessageBytes> expected = session_message(status);
    EXPECT_TRUE(std::equal(datagram.begin(), datagram.end(), expected.begin()));
    EXPECT_EQ(publisher.stats().status_published, 1u);
    EXPECT_EQ(publisher.stats().published, 0u);
    EXPECT_EQ(publisher.stats().send_failures, 0u);
}

// TC-021 step 4.
TEST(UdpPublisherTest, NotOkCountsAStatusFailure) {
    UdpPublisher publisher("not-an-address", 5556);
    ASSERT_FALSE(publisher.ok());
    EXPECT_FALSE(publisher.publish_status(SessionStatus{RunMode::LIVE, RunMode::LIVE, StreamState::RUNNING, SessionIdentity{}}));
    EXPECT_EQ(publisher.stats().send_failures, 1u);
    EXPECT_EQ(publisher.stats().status_published, 0u);
}
