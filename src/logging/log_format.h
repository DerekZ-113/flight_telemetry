#pragma once

#include <cstdint>
#include <type_traits>

#include "processing/fault_detector.h"   // FaultEvent
#include "telemetry_frame.h"

// Binary log format (REQ-LOG-001). One file is:
//
//   LogFileHeader
//   { RecordType tag, record bytes } ...
//
// Records are the in-memory structs written verbatim. That is legal only
// because both structs are trivially copyable (asserted below): their
// bytes are their value, with no pointers or owning members to lose.
//
// The format is tied to one platform and one build: struct layout depends
// on the compiler's padding and the host's byte order. That is the scope
// design.md section 8 already sets for replay. The header records
// everything needed to detect a mismatch on read instead of producing
// garbage: a magic tag, a format version, both record sizes, and the
// host's endianness.
//
// Rule: any change to TelemetryFrame or FaultEvent changes the format.
// Bump kLogFormatVersion in the same commit, or an old log will be read
// with the wrong sizes and rejected with a confusing message rather than
// a clear "version 1, expected 2".

constexpr uint16_t kLogFormatVersion = 1;
constexpr uint32_t kEndianMarker = 0x01020304u;

struct LogFileHeader {
    char magic[4];            // "FTLG"
    uint16_t version;         // kLogFormatVersion
    uint16_t frame_size;      // sizeof(TelemetryFrame) when written
    uint16_t event_size;      // sizeof(FaultEvent) when written
    uint16_t reserved;        // zero; keeps the header at 16 bytes
    uint32_t endian_marker;   // kEndianMarker as this host stores it
};

enum class RecordType : uint8_t {
    FRAME = 1,
    FAULT_EVENT = 2,
};

LogFileHeader make_log_header();

// Magic, version, both sizes, and byte order all match this build.
bool header_is_valid(const LogFileHeader& header);

static_assert(std::is_trivially_copyable<TelemetryFrame>::value,
              "TelemetryFrame is written to the log as raw bytes; it must stay trivially copyable");
static_assert(std::is_trivially_copyable<FaultEvent>::value,
              "FaultEvent is written to the log as raw bytes; it must stay trivially copyable");
static_assert(std::is_trivially_copyable<LogFileHeader>::value, "header is written as raw bytes");
static_assert(sizeof(LogFileHeader) == 16, "header layout changed; bump kLogFormatVersion");
