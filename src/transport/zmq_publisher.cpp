#include "transport/zmq_publisher.h"

#include <cstring>

namespace {

// How long context termination waits for messages the I/O thread has not
// yet written to a peer (REQ-TRANS-003 applied to exit). The library
// default is forever, which would hang shutdown on a stuck subscriber;
// zero would discard the last frames of a healthy run before they reach
// the wire. Half a second bounds the worst case and costs nothing in the
// normal one, where the queue is already empty.
constexpr int kLingerMs = 500;

}  // namespace

ZmqPublisher::ZmqPublisher(zmq::context_t& context, const std::string& endpoint,
                           int send_high_water_mark)
    : socket_(context, zmq::socket_type::pub) {
    // cppzmq reports failures by throwing zmq::error_t. This class reports
    // them as status, like BinaryLogger::ok(), so the composition root
    // decides what a failed bind means and the loop never sees an exception.
    try {
        socket_.set(zmq::sockopt::sndhwm, send_high_water_mark);
        socket_.set(zmq::sockopt::linger, kLingerMs);
        socket_.bind(endpoint);
        ok_ = true;
    } catch (const zmq::error_t& e) {
        error_ = e.what();
    }
}

bool ZmqPublisher::publish(const TelemetryFrame& frame) {
    if (!ok_) {
        stats_.send_failures++;
        return false;
    }

    // The message is laid out exactly as BinaryLogger::write_record lays a
    // record down after the file header: header, one tag byte, then the
    // frame's bytes verbatim. memcpy is licensed by the static_asserts in
    // log_format.h: both structs are trivially copyable.
    zmq::message_t message(kFrameMessageBytes);
    auto* out = static_cast<uint8_t*>(message.data());
    const LogFileHeader header = make_log_header();
    std::memcpy(out, &header, sizeof(header));
    out[sizeof(header)] = static_cast<uint8_t>(RecordType::FRAME);
    std::memcpy(out + sizeof(header) + 1, &frame, sizeof(frame));

    // dontwait states the intent. A PUB socket never waits anyway
    // (zmq_socket(3)); if the socket type ever changed, this flag is what
    // would keep "never block" true. send() returns no value only for
    // EAGAIN and throws for everything else, such as ETERM once the context
    // has been shut down. Either way the frame is counted as not sent and
    // the caller gets control back.
    bool sent = false;
    try {
        sent = socket_.send(message, zmq::send_flags::dontwait).has_value();
    } catch (const zmq::error_t& e) {
        stats_.last_errno = e.num();
    }
    if (!sent) {
        stats_.send_failures++;
        return false;
    }
    stats_.published++;
    return true;
}
