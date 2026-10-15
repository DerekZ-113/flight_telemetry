#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <zmq.hpp>

#include "logging/log_format.h"
#include "telemetry_frame.h"
#include "transport/publisher.h"

// Publishes processed frames on a ZeroMQ PUB socket (REQ-TRANS-001) in a
// way that can never hold up the processing loop (REQ-TRANS-003).
//
// The wire format is the log format (FTS-DD-001 section 9). Every message
// is a complete one-record log file:
//
//   LogFileHeader (16 bytes) | RecordType tag (1 byte) | TelemetryFrame (104 bytes)
//
// so a subscriber validates the layout from its first message, whenever it
// joins, with the same parser it uses for a log on disk. Pub/sub has no
// handshake, which is why the header travels on every message rather than
// once per connection.
//
// What "never blocks" means here, from zmq_socket(3): a PUB socket's send
// never waits. When one subscriber's queue reaches the high-water mark,
// messages to that subscriber are dropped until it catches up; other
// subscribers are unaffected; with no subscriber at all every message is
// dropped on the spot. Those drops are silent at this end, so this class
// cannot count them. It counts what it can see: messages the library
// accepted, and sends the library refused. Loss is measured at the
// receiver, from gaps in frame timestamps.
//
// RAII: the zmq::socket_t member closes itself in its destructor. The
// zmq::context_t is owned by the caller (one per process; inproc:// only
// works between sockets of the same context) and must outlive this object,
// because terminating a context blocks until every socket on it is closed.

// Messages the library queues per subscriber before dropping
// (REQ-TRANS-003). 100 is two seconds of frames at 50 Hz: a subscriber
// that stalls for less loses nothing; one that falls further behind loses
// frames rather than slowing the loop. Not a configuration key: REQ-CFG-001
// lists transport addresses and ports, and the tuning stays fixed until a
// requirement asks for it (FTS-DD-001 section 12, decided 2026-10-13).
constexpr int kDefaultSendHighWaterMark = 100;

// Implements Publisher (publisher.h), the seam the loop sees; PublishStats
// lives there too, shared with the UDP publisher.
class ZmqPublisher : public Publisher {
public:
    // Binds a PUB socket to `endpoint` ("tcp://127.0.0.1:5555",
    // "inproc://name"). A failed bind (address in use, malformed endpoint)
    // leaves ok() false with the reason in error(); it does not throw.
    // Creating the socket itself fails only when the process is out of
    // descriptors or the context is already terminated; that is a
    // zmq::error_t from the member initializer and is not handled here.
    ZmqPublisher(zmq::context_t& context, const std::string& endpoint,
                 int send_high_water_mark = kDefaultSendHighWaterMark);

    // The socket is a resource with one owner; a copy would close it twice.
    ZmqPublisher(const ZmqPublisher&) = delete;
    ZmqPublisher& operator=(const ZmqPublisher&) = delete;

    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

    // Hands one frame to the library and returns at once. false means the
    // frame was not sent; stats() says how often that has happened.
    bool publish(const TelemetryFrame& frame) override;

    // The eight-byte session status (REQ-TRANS-004), through the same
    // socket, counted apart from frames.
    bool publish_status(RunMode mode, StreamState state) override;

    const PublishStats& stats() const override { return stats_; }

private:
    // One message of `size` bytes, sent without waiting; false when the
    // library refused or threw, with the errno kept. Frames and statuses
    // differ only in the bytes they hand in.
    bool send_bytes(const uint8_t* data, size_t size);

    zmq::socket_t socket_;
    bool ok_ = false;
    std::string error_;
    PublishStats stats_;
};
