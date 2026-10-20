#include "transport/udp_publisher.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>

#include "logging/log_format.h"

UdpPublisher::UdpPublisher(const std::string& host, int port) {
    // Refusals come in order of how cheap they are to explain: the port and
    // the address are checked before any system call, so the message names
    // what was typed, not what the kernel made of it.
    if (port < 1 || port > 65535) {
        error_ = "port must be 1 to 65535";
        return;
    }
    sockaddr_in destination{};   // value-initialized: sin_zero, and macOS's sin_len, are zero
    destination.sin_family = AF_INET;
    destination.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &destination.sin_addr) != 1) {
        error_ = "not an IPv4 dotted-quad address: " + host;
        return;
    }
    // 0.0.0.0 would be rewritten to loopback by both kernels, and
    // 255.255.255.255 needs SO_BROADCAST, which is not set: a destination
    // is one unicast host (REQ-TRANS-002).
    const uint32_t address = ntohl(destination.sin_addr.s_addr);
    if (address == INADDR_ANY || address == INADDR_BROADCAST) {
        error_ = "not a unicast destination: " + host;
        return;
    }
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        error_ = std::string("socket: ") + std::strerror(errno);
        return;
    }
    // Connecting a datagram socket sends nothing (see the header). A
    // destination with no route, or a directed broadcast on Linux, fails
    // here, which is the startup refusal REQ-TRANS-003 asks for.
    if (::connect(fd_, reinterpret_cast<const sockaddr*>(&destination), sizeof(destination)) < 0) {
        error_ = std::string("connect: ") + std::strerror(errno);
        return;
    }
    ok_ = true;
}

UdpPublisher::~UdpPublisher() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool UdpPublisher::send_bytes(const uint8_t* data, size_t size) {
    // MSG_DONTWAIT is the kernel's dontwait: a full send buffer is EAGAIN,
    // not a wait. A datagram send never returns a short count: it is the
    // whole message, or -1 with the reason in errno.
    const ssize_t sent = ::send(fd_, data, size, MSG_DONTWAIT);
    if (sent < 0) {
        stats_.last_errno = errno;
        return false;
    }
    return true;
}

bool UdpPublisher::publish(const TelemetryFrame& frame) {
    if (!ok_) {
        stats_.send_failures++;
        return false;
    }

    const std::array<uint8_t, kFrameMessageBytes> bytes = frame_message(frame);
    if (!send_bytes(bytes.data(), bytes.size())) {
        stats_.send_failures++;
        return false;
    }
    stats_.published++;
    return true;
}

bool UdpPublisher::publish_status(RunMode mode, StreamState state) {
    if (!ok_) {
        stats_.send_failures++;
        return false;
    }

    // Eight bytes under their own magic (session_message.h), one datagram
    // like a frame, never a log record.
    const std::array<uint8_t, kSessionMessageBytes> bytes = session_message(mode, state);
    if (!send_bytes(bytes.data(), bytes.size())) {
        stats_.send_failures++;
        return false;
    }
    stats_.status_published++;
    return true;
}
