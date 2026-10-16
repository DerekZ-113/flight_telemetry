#pragma once

#include <cstdint>
#include <string>

#include "telemetry_frame.h"
#include "transport/publisher.h"

// Publishes processed frames as UDP datagrams (REQ-TRANS-002) in a way that
// can never hold up the processing loop (REQ-TRANS-003).
//
// One frame is one datagram, laid out as the same one-record log file the
// ZeroMQ publisher sends (frame_message, log_format.h): 121 bytes, under
// any MTU, so a frame is never fragmented and arrives whole or not at all.
// A datagram socket has no connection, no acknowledgement, and no
// retransmission: a frame that could only be delivered after the next one
// is due is stale, which is why telemetry downlinks use datagrams, and why
// loss is measured at the receiver from frame timestamps, never here.
//
// The socket is connected to its one unicast destination at construction.
// connect() on a datagram socket sends nothing: it records the destination
// and the route, so send() needs no address, and it makes the kernel
// deliver this destination's ICMP errors to this socket. What the kernel
// can refuse, and when: a full send buffer (EAGAIN, never on loopback); a
// port nobody listens on, learned from an ICMP unreachable after the fact
// and reported on a later send as ECONNREFUSED, one report per ICMP, that
// datagram not sent; a non-unicast destination (EACCES). A receiver whose
// own buffer is full drops on its side and this end sees nothing, as a PUB
// socket at its high-water mark. Every refusal is counted with its errno
// and never retried (design.md section 9).
//
// RAII: the descriptor is closed in the destructor. No platform split:
// these are the same POSIX calls on Linux and macOS.

class UdpPublisher : public Publisher {
public:
    // `host` is an IPv4 dotted quad, `port` 1 to 65535. Anything else, a
    // non-unicast address (0.0.0.0, 255.255.255.255), a socket the process
    // cannot create, or a destination the kernel cannot route leaves ok()
    // false with the reason in error(); nothing throws. The destination is
    // the configuration file's `transport.udp` keys, or the --udp argument
    // for one run (REQ-CFG-001); main hands it in either way.
    UdpPublisher(const std::string& host, int port);
    ~UdpPublisher() override;

    // The descriptor has one owner; a copy would close it twice.
    UdpPublisher(const UdpPublisher&) = delete;
    UdpPublisher& operator=(const UdpPublisher&) = delete;

    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

    // Hands one frame to the kernel and returns at once. false means the
    // frame was not sent; stats() says how often that has happened.
    bool publish(const TelemetryFrame& frame) override;

    // The nineteen-byte session status (REQ-TRANS-004) as one datagram,
    // counted apart from frames.
    bool publish_status(RunMode mode, StreamState state, const SessionIdentity& identity) override;

    const PublishStats& stats() const override { return stats_; }

private:
    // One datagram of `size` bytes, sent without waiting; false when the
    // kernel refused, with the errno kept.
    bool send_bytes(const uint8_t* data, size_t size);

    int fd_ = -1;
    bool ok_ = false;
    std::string error_;
    PublishStats stats_;
};
