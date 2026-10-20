#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include "processing/fault_detector.h"   // FaultEvent
#include "run_mode.h"
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
// a clear "format version 4; this build reads 1 to 3".
//
// Versions (REQ-LOG-005). kLogFormatVersion is the one this build writes;
// kOldestReadableLogFormatVersion the oldest it reads. Version 1 (through
// 2026-10-13) had a 104-byte frame with four compiler padding bytes before
// latitude and six after gps_read_ok, and a 16-byte event with two after
// `type`. Version 2 named every one of those bytes (reserved_a, the five
// Validity fields, reserved_b, FaultEvent::reserved) and moved no other
// byte. Version 3 (2026-10-15) kept every version-2 field where it was,
// spent the two reserved fields (the sequence number, the flags byte),
// renamed heading_deg course_deg, and appended a 16-byte tail: 120 bytes.
// So one layout serves every version, the prefix rule: a frame of an
// older file is read into a value-initialized version-3 frame and lands
// in its first 104 bytes, where every one of its fields still lives, the
// tail staying zero and the sequence 0; a version-1 frame's validity and
// former reserved bytes are set besides to what version 2 meant by them,
// since an old file may carry stack contents there. A change that keeps
// the raw bytes in place may extend the readable range this way; one that
// moves them may not. The header's version implies the frame size
// (frame_size_for), which the header must agree with.
//
// The header also records the run mode of the session that wrote the file
// and the source mode its frames were produced in (REQ-LOG-007), in the
// two bytes that were one reserved field until version 3: a run property
// belongs in the header, written once, not in fifty frames a second, and
// a reader takes it from there and never infers it from the frames. A
// header older than version 3 reads UNKNOWN for both.

constexpr uint16_t kLogFormatVersion = 3;
constexpr uint16_t kOldestReadableLogFormatVersion = 1;
constexpr uint32_t kEndianMarker = 0x01020304u;

// The frame of versions 1 and 2: the first 104 bytes of today's.
constexpr uint16_t kVersion2FrameBytes = 104;

// The frame size a version's layout implies; 0 for a version this build
// does not define, which header_problem has refused before asking.
constexpr uint16_t frame_size_for(uint16_t version) {
    return version == 1 || version == 2 ? kVersion2FrameBytes
         : version == kLogFormatVersion ? static_cast<uint16_t>(sizeof(TelemetryFrame)) : 0;
}

struct LogFileHeader {
    char magic[4];            // "FTLG"
    uint16_t version;         // kLogFormatVersion
    uint16_t frame_size;      // sizeof(TelemetryFrame) when written
    uint16_t event_size;      // sizeof(FaultEvent) when written
    uint8_t run_mode;         // RunMode: how this session ran (REQ-LOG-007); 0 before version 3
    uint8_t source_mode;      // RunMode: how its frames were originally produced; 0 before version 3
    uint32_t endian_marker;   // kEndianMarker as this host stores it
};

enum class RecordType : uint8_t {
    FRAME = 1,
    FAULT_EVENT = 2,
};

// The header this build writes, with the two modes main knows; the
// defaults say that nobody told them, which is what a test's logger or
// publisher means by it.
LogFileHeader make_log_header(RunMode run_mode = RunMode::UNKNOWN, RunMode source_mode = RunMode::UNKNOWN);

// Empty when the header is one this build reads: the magic, a version in
// the readable range, the frame size that version implies, the event size
// and the byte order all match. Otherwise the first mismatch, named
// ("format version 4; this build reads 1 to 3"; "frame size 112 in the
// header, 120 for version 3 in this build"), the twin of the Python
// reader's check_header. A refused file is named before any record is
// read (REQ-LOG-005).
std::string header_problem(const LogFileHeader& header);

// header_problem(header).empty(): the one-line form for callers that need
// only the verdict.
bool header_is_valid(const LogFileHeader& header);

static_assert(std::is_trivially_copyable<TelemetryFrame>::value,
              "TelemetryFrame is written to the log as raw bytes; it must stay trivially copyable");
static_assert(std::is_trivially_copyable<FaultEvent>::value,
              "FaultEvent is written to the log as raw bytes; it must stay trivially copyable");
static_assert(std::is_trivially_copyable<LogFileHeader>::value, "header is written as raw bytes");
static_assert(sizeof(LogFileHeader) == 16, "header layout changed; bump kLogFormatVersion");

// Bytes in one frame message: header, tag, frame.
constexpr size_t kFrameMessageBytes = sizeof(LogFileHeader) + 1 + sizeof(TelemetryFrame);

// A frame as a complete one-record log file: the header, the FRAME tag,
// then the frame's bytes verbatim, laid out exactly as BinaryLogger writes
// a record after the file header. This is what a transport sends
// (REQ-TRANS-001, REQ-TRANS-002; design.md section 9: the wire format is
// the log format). Both publishers call this one function with the header
// the composition root handed them, the same one the logger writes, so
// the file and the wire cannot disagree by a byte. memcpy is licensed by
// the static_asserts above.
std::array<uint8_t, kFrameMessageBytes> frame_message(const TelemetryFrame& frame, const LogFileHeader& header);
