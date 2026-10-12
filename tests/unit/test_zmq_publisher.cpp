// Tests for src/transport/zmq_publisher (REQ-TRANS-001, REQ-TRANS-003).
// Test cards TC-005 (unit-level steps) and TC-010.
//
// Every test runs over inproc://, ZeroMQ's in-process transport: no port,
// no network, and the subscriber is a plain SUB socket in the same
// context, which is the only way inproc works. The fixture owns that
// context and declares it first, so it is destroyed last, after every
// socket that uses it; terminating a context with a socket still open
// would block forever.
//
// Subscribing is itself a message that travels from SUB to PUB. Until it
// lands, the publisher sees no subscriber and drops everything: the
// slow-joiner effect. settle() publishes warm-up frames until one comes
// back, bounded, so no test asserts on a frame that was sent before the
// subscription arrived.

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <zmq.hpp>

#include "drivers/simulated_source.h"
#include "logging/log_format.h"
#include "transport/zmq_publisher.h"

namespace {

constexpr int kRecvTimeoutMs = 1000;         // a missing message fails in a second, never hangs ctest
constexpr uint64_t kWarmUpStamp = ~0ull;     // marks settle()'s frames; real frames start at 0

class ZmqPublisherTest : public ::testing::Test {
protected:
    // One I/O thread is the minimum; inproc does not use it.
    zmq::context_t context_{1};

    // A SUB socket connected to `endpoint` that receives everything.
    zmq::socket_t make_subscriber(const std::string& endpoint, int receive_high_water_mark = 1000) {
        zmq::socket_t sub(context_, zmq::socket_type::sub);
        sub.set(zmq::sockopt::rcvhwm, receive_high_water_mark);
        sub.set(zmq::sockopt::rcvtimeo, kRecvTimeoutMs);
        sub.set(zmq::sockopt::subscribe, "");   // empty prefix: every message
        sub.connect(endpoint);
        return sub;
    }

    // Publishes warm-up frames until the subscriber receives one, then
    // clears any that queued behind it. Returns how many were published,
    // which the publisher's stats include. Fails the test after 100 tries.
    int settle(ZmqPublisher& publisher, zmq::socket_t& sub) {
        TelemetryFrame warm_up{};
        warm_up.timestamp_ms = kWarmUpStamp;
        for (int attempt = 1; attempt <= 100; attempt++) {
            EXPECT_TRUE(publisher.publish(warm_up));
            zmq::message_t msg;
            if (sub.recv(msg, zmq::recv_flags::dontwait).has_value()) {
                while (sub.recv(msg, zmq::recv_flags::dontwait).has_value()) {
                }
                return attempt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ADD_FAILURE() << "subscription never reached the publisher";
        return 0;
    }
};

const uint8_t* frame_bytes(const zmq::message_t& msg) {
    return static_cast<const uint8_t*>(msg.data()) + sizeof(LogFileHeader) + 1;
}

}  // namespace

// A publisher on a fresh endpoint is ready to use. (REQ-TRANS-001)
TEST_F(ZmqPublisherTest, BindsAndReportsOk) {
    ZmqPublisher publisher(context_, "inproc://binds");
    EXPECT_TRUE(publisher.ok());
    EXPECT_TRUE(publisher.error().empty());
    EXPECT_EQ(publisher.stats().published, 0u);
    EXPECT_EQ(publisher.stats().send_failures, 0u);
    EXPECT_EQ(publisher.stats().last_errno, 0);
}

// Two publishers cannot share an endpoint. The second reports it, refuses
// to publish, and counts the refusal; nothing throws. (REQ-TRANS-003)
TEST_F(ZmqPublisherTest, BindFailureIsReportedNotThrown) {
    ZmqPublisher first(context_, "inproc://taken");
    ASSERT_TRUE(first.ok());
    ZmqPublisher second(context_, "inproc://taken");
    EXPECT_FALSE(second.ok());
    EXPECT_FALSE(second.error().empty());
    EXPECT_FALSE(second.publish(TelemetryFrame{}));
    EXPECT_EQ(second.stats().send_failures, 1u);
    EXPECT_EQ(second.stats().published, 0u);
    EXPECT_TRUE(first.ok());   // the loser did not disturb the winner
}

// A string that is not an endpoint is the same kind of failure. (REQ-TRANS-003)
TEST_F(ZmqPublisherTest, InvalidEndpointIsReportedNotThrown) {
    ZmqPublisher publisher(context_, "not-an-endpoint");
    EXPECT_FALSE(publisher.ok());
    EXPECT_FALSE(publisher.error().empty());
    EXPECT_FALSE(publisher.publish(TelemetryFrame{}));
    EXPECT_EQ(publisher.stats().send_failures, 1u);
}

// One message is a one-record log file: the 16-byte header this build
// writes to disk, the FRAME tag, then the frame's bytes verbatim.
// (REQ-TRANS-001; FTS-DD-001 section 9)
TEST_F(ZmqPublisherTest, MessageIsHeaderTagAndFrameBytes) {
    ZmqPublisher publisher(context_, "inproc://layout");
    ASSERT_TRUE(publisher.ok());
    zmq::socket_t sub = make_subscriber("inproc://layout");
    settle(publisher, sub);

    SimulatedSource source(20, 42);
    const TelemetryFrame frame = source.read_frame();
    ASSERT_TRUE(publisher.publish(frame));

    zmq::message_t msg;
    ASSERT_TRUE(sub.recv(msg).has_value());
    ASSERT_EQ(msg.size(), kFrameMessageBytes);
    const auto* bytes = static_cast<const uint8_t*>(msg.data());

    LogFileHeader header{};
    std::memcpy(&header, bytes, sizeof(header));
    EXPECT_TRUE(header_is_valid(header));
    const LogFileHeader expected = make_log_header();
    EXPECT_EQ(std::memcmp(bytes, &expected, sizeof(expected)), 0);
    EXPECT_EQ(bytes[sizeof(header)], static_cast<uint8_t>(RecordType::FRAME));
    EXPECT_EQ(std::memcmp(frame_bytes(msg), &frame, sizeof(frame)), 0);
}

// Every frame handed to the publisher reaches a live subscriber, in order,
// byte for byte. (REQ-TRANS-001; TC-005 steps 5 to 7 at unit level)
TEST_F(ZmqPublisherTest, EveryPublishedFrameArrivesInOrder) {
    ZmqPublisher publisher(context_, "inproc://ordered");
    ASSERT_TRUE(publisher.ok());
    zmq::socket_t sub = make_subscriber("inproc://ordered");
    const int warm_ups = settle(publisher, sub);

    SimulatedSource source(20, 42);
    std::vector<TelemetryFrame> sent;
    for (int i = 0; i < 100; i++) {
        sent.push_back(source.read_frame());
        ASSERT_TRUE(publisher.publish(sent.back())) << "frame " << i;
    }
    EXPECT_EQ(publisher.stats().published, static_cast<uint64_t>(100 + warm_ups));
    EXPECT_EQ(publisher.stats().send_failures, 0u);

    for (int i = 0; i < 100; i++) {
        zmq::message_t msg;
        ASSERT_TRUE(sub.recv(msg).has_value()) << "frame " << i << " never arrived";
        ASSERT_EQ(msg.size(), kFrameMessageBytes);
        EXPECT_EQ(std::memcmp(frame_bytes(msg), &sent[i], sizeof(TelemetryFrame)), 0) << "frame " << i;
    }
}

// ---------------------------------------------------------------------------
// REQ-TRANS-003: nothing on the other side of the socket can delay the loop.
// The upper bounds are loose on purpose, as in TC-009: a CI runner may
// stall for a while, and a send that actually blocked would wait forever.
// ---------------------------------------------------------------------------

namespace {
constexpr int kBurst = 1000;
constexpr auto kBound = std::chrono::milliseconds(2000);
}  // namespace

// With nobody subscribed, every send is accepted and dropped at once.
TEST_F(ZmqPublisherTest, NoSubscriberNeverBlocks) {
    ZmqPublisher publisher(context_, "inproc://nobody");
    ASSERT_TRUE(publisher.ok());
    TelemetryFrame frame{};
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kBurst; i++) {
        frame.timestamp_ms = static_cast<uint64_t>(i) * 20;
        ASSERT_TRUE(publisher.publish(frame)) << "frame " << i;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, kBound);
    EXPECT_EQ(publisher.stats().published, static_cast<uint64_t>(kBurst));
    EXPECT_EQ(publisher.stats().send_failures, 0u);
}

// A subscriber that never reads fills its queue; from then on the library
// drops messages to it and the publisher neither notices nor waits. An
// inproc queue holds about the sum of both sides' high-water marks (10 + 10
// here) but never an exact number, so the test asserts "some arrived, most
// did not" rather than a count.
TEST_F(ZmqPublisherTest, StalledSubscriberDropsInsteadOfBlocking) {
    ZmqPublisher publisher(context_, "inproc://stalled", 10);
    ASSERT_TRUE(publisher.ok());
    zmq::socket_t sub = make_subscriber("inproc://stalled", 10);
    const int warm_ups = settle(publisher, sub);

    TelemetryFrame frame{};
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kBurst; i++) {
        frame.timestamp_ms = static_cast<uint64_t>(i) * 20;
        ASSERT_TRUE(publisher.publish(frame)) << "frame " << i;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, kBound);
    EXPECT_EQ(publisher.stats().published, static_cast<uint64_t>(kBurst + warm_ups));
    EXPECT_EQ(publisher.stats().send_failures, 0u);   // the drops are invisible here

    int received = 0;
    zmq::message_t msg;
    while (sub.recv(msg, zmq::recv_flags::dontwait).has_value()) {
        received++;
    }
    EXPECT_GE(received, 1);
    EXPECT_LT(received, kBurst);
}

// The one failure a test can provoke on demand: once the context is shut
// down, every socket operation fails with ETERM. The publisher counts it,
// keeps the code, and returns; no exception reaches the loop.
TEST_F(ZmqPublisherTest, SendErrorIsCountedNotThrown) {
    ZmqPublisher publisher(context_, "inproc://shutdown");
    ASSERT_TRUE(publisher.ok());
    context_.shutdown();
    TelemetryFrame frame{};
    EXPECT_FALSE(publisher.publish(frame));
    EXPECT_EQ(publisher.stats().send_failures, 1u);
    EXPECT_EQ(publisher.stats().last_errno, ETERM);
    EXPECT_EQ(publisher.stats().published, 0u);
}
