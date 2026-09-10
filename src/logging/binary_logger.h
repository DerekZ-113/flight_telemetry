#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "logging/log_format.h"

// Writes processed frames and fault events to rotating binary files
// (REQ-LOG-001, REQ-LOG-002, and the "log a fault event" half of
// REQ-FAULT-004).
//
// Files are named <prefix>_000.bin, <prefix>_001.bin, ... so a plain sort
// is chronological. Every file starts with a LogFileHeader, so each one
// can be read on its own. Rotation happens before a record that would
// push the file past max_file_bytes; a record is never split across two
// files, because a torn record would make the tail of one file unreadable
// and replay would silently lose a frame.
//
// The std::ofstream member is the RAII handle: opened in open_next_file(),
// flushed and closed by its own destructor when the logger is destroyed.
// Nothing here calls close() by hand.
class BinaryLogger {
public:
    // Creates `directory` if needed and opens <prefix>_000.bin.
    // max_file_bytes below header + one frame record is raised to that
    // minimum, so rotation can never trigger on every write.
    BinaryLogger(std::filesystem::path directory, std::string prefix, uint64_t max_file_bytes);

    void log_frame(const TelemetryFrame& frame);
    void log_event(const FaultEvent& event);

    std::filesystem::path current_path() const;
    int file_index() const { return file_index_; }
    bool ok() const { return ok_; }

private:
    void open_next_file();
    void write_record(RecordType type, const void* data, size_t size);

    std::filesystem::path directory_;
    std::string prefix_;
    uint64_t max_file_bytes_;
    uint64_t bytes_written_ = 0;     // in the current file, header included
    bool file_has_record_ = false;
    int file_index_ = -1;
    bool ok_ = false;
    std::ofstream out_;
};

// <prefix>_NNN.bin, zero-padded to three digits.
std::filesystem::path log_file_path(const std::filesystem::path& directory,
                                    const std::string& prefix, int index);
