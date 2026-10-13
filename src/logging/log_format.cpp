#include "logging/log_format.h"

#include <cstring>

LogFileHeader make_log_header() {
    LogFileHeader h{};
    std::memcpy(h.magic, "FTLG", 4);
    h.version = kLogFormatVersion;
    h.frame_size = static_cast<uint16_t>(sizeof(TelemetryFrame));
    h.event_size = static_cast<uint16_t>(sizeof(FaultEvent));
    h.reserved = 0;
    h.endian_marker = kEndianMarker;
    return h;
}

bool header_is_valid(const LogFileHeader& header) {
    return std::memcmp(header.magic, "FTLG", 4) == 0
        && header.version == kLogFormatVersion
        && header.frame_size == sizeof(TelemetryFrame)
        && header.event_size == sizeof(FaultEvent)
        && header.endian_marker == kEndianMarker;
}

std::array<uint8_t, kFrameMessageBytes> frame_message(const TelemetryFrame& frame) {
    std::array<uint8_t, kFrameMessageBytes> out{};
    const LogFileHeader header = make_log_header();
    std::memcpy(out.data(), &header, sizeof(header));
    out[sizeof(header)] = static_cast<uint8_t>(RecordType::FRAME);
    std::memcpy(out.data() + sizeof(header) + 1, &frame, sizeof(frame));
    return out;
}
