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

std::string header_problem(const LogFileHeader& header) {
    // Checked in the order a reader would notice them: a file that is not
    // ours at all, then a layout this build does not know, then the sizes
    // and the byte order that pin the layout it does.
    if (std::memcmp(header.magic, "FTLG", 4) != 0) {
        return "bad magic: not a telemetry log";
    }
    if (header.version < kOldestReadableLogFormatVersion || header.version > kLogFormatVersion) {
        return "format version " + std::to_string(header.version) + "; this build reads "
             + std::to_string(kOldestReadableLogFormatVersion) + " to " + std::to_string(kLogFormatVersion);
    }
    if (header.frame_size != sizeof(TelemetryFrame)) {
        return "frame size " + std::to_string(header.frame_size) + " in the header, "
             + std::to_string(sizeof(TelemetryFrame)) + " in this build";
    }
    if (header.event_size != sizeof(FaultEvent)) {
        return "event size " + std::to_string(header.event_size) + " in the header, "
             + std::to_string(sizeof(FaultEvent)) + " in this build";
    }
    if (header.endian_marker != kEndianMarker) {
        return "endianness marker does not match: written on a host with the other byte order";
    }
    return "";
}

bool header_is_valid(const LogFileHeader& header) {
    return header_problem(header).empty();
}

std::array<uint8_t, kFrameMessageBytes> frame_message(const TelemetryFrame& frame) {
    std::array<uint8_t, kFrameMessageBytes> out{};
    const LogFileHeader header = make_log_header();
    std::memcpy(out.data(), &header, sizeof(header));
    out[sizeof(header)] = static_cast<uint8_t>(RecordType::FRAME);
    std::memcpy(out.data() + sizeof(header) + 1, &frame, sizeof(frame));
    return out;
}
