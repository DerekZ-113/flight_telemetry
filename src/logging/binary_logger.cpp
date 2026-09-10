#include "logging/binary_logger.h"

#include <iomanip>
#include <sstream>

std::filesystem::path log_file_path(const std::filesystem::path& directory,
                                    const std::string& prefix, int index) {
    // ostringstream with setw/setfill rather than snprintf: no buffer to
    // size, and it composes with std::string without a cast.
    std::ostringstream name;
    name << prefix << '_' << std::setw(3) << std::setfill('0') << index << ".bin";
    return directory / name.str();
}

BinaryLogger::BinaryLogger(std::filesystem::path directory, std::string prefix,
                           uint64_t max_file_bytes)
    : directory_(std::move(directory)),
      prefix_(std::move(prefix)),
      max_file_bytes_(max_file_bytes)
{
    // A file must be able to hold its header and at least one frame, or
    // every write would rotate and the frame would never land.
    const uint64_t minimum = sizeof(LogFileHeader) + 1 + sizeof(TelemetryFrame);
    if (max_file_bytes_ < minimum) {
        max_file_bytes_ = minimum;
    }

    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);
    open_next_file();
}

void BinaryLogger::open_next_file() {
    file_index_++;
    // Assigning a fresh stream closes the previous file: the old
    // ofstream's destructor runs as part of the move-assignment.
    out_ = std::ofstream(log_file_path(directory_, prefix_, file_index_),
                         std::ios::binary | std::ios::trunc);
    ok_ = out_.good();
    bytes_written_ = 0;
    file_has_record_ = false;
    if (!ok_) {
        return;
    }
    const LogFileHeader header = make_log_header();
    // reinterpret_cast to bytes is safe here and only here: the header is
    // trivially copyable (static_assert in log_format.h).
    out_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    bytes_written_ += sizeof(header);
}

void BinaryLogger::write_record(RecordType type, const void* data, size_t size) {
    if (!ok_) {
        return;
    }
    const uint64_t record_bytes = 1 + size;
    // Rotate before, not after: the decision is "would this record
    // overflow", and it is only made once the file holds something, so a
    // limit smaller than one record still produces one record per file.
    if (file_has_record_ && bytes_written_ + record_bytes > max_file_bytes_) {
        open_next_file();
        if (!ok_) {
            return;
        }
    }
    const uint8_t tag = static_cast<uint8_t>(type);
    out_.write(reinterpret_cast<const char*>(&tag), 1);
    out_.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    bytes_written_ += record_bytes;
    file_has_record_ = true;
    ok_ = out_.good();
}

void BinaryLogger::log_frame(const TelemetryFrame& frame) {
    write_record(RecordType::FRAME, &frame, sizeof(frame));
}

void BinaryLogger::log_event(const FaultEvent& event) {
    write_record(RecordType::FAULT_EVENT, &event, sizeof(event));
}

std::filesystem::path BinaryLogger::current_path() const {
    return log_file_path(directory_, prefix_, file_index_);
}
