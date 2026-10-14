#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "logging/log_format.h"

// One record from a log file. `type` says which of the two payloads is
// meaningful; the other is left zeroed.
struct LogRecord {
    RecordType type = RecordType::FRAME;
    TelemetryFrame frame{};
    FaultEvent event{};
};

// Reads one binary log file written by BinaryLogger.
//
// The constructor reads and validates the header. A file from another
// build, another platform, a format version this build does not read, or
// a different frame layout is refused with ok() false and the mismatch
// named in error() (REQ-LOG-005); it is never partially read. A version-1
// file is read, its frames' validity and reserved bytes set to what
// version 2 means by them (log_reader.cpp).
//
// next() returns false at a clean end of file and also at a torn record
// (a tag with fewer payload bytes behind it than its type requires), so
// a file cut off mid-write yields every complete record and then stops.
class LogReader {
public:
    explicit LogReader(const std::filesystem::path& file);

    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }
    const LogFileHeader& header() const { return header_; }

    bool next(LogRecord& out);

    // Frame records only; event records are skipped. Empty at end.
    std::optional<TelemetryFrame> next_frame();

private:
    std::ifstream in_;
    LogFileHeader header_{};
    bool ok_ = false;
    std::string error_;
};

// Every <prefix>_NNN.bin in `directory`, sorted by name so rotated files
// come back in the order they were written.
std::vector<std::filesystem::path> list_log_files(const std::filesystem::path& directory,
                                                  const std::string& prefix);
