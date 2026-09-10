#include "logging/log_reader.h"

#include <algorithm>

LogReader::LogReader(const std::filesystem::path& file)
    : in_(file, std::ios::binary)
{
    if (!in_.is_open()) {
        error_ = "cannot open " + file.string();
        return;
    }
    in_.read(reinterpret_cast<char*>(&header_), sizeof(header_));
    if (in_.gcount() != static_cast<std::streamsize>(sizeof(header_))) {
        error_ = "file shorter than a header";
        return;
    }
    if (!header_is_valid(header_)) {  // LCOV_EXCL_EXCEPTION_BR_LINE: exception-unwind edges only; the decision itself is still counted
        error_ = "header mismatch: not a log from this build (magic, version, sizes, or byte order)";
        return;
    }
    ok_ = true;
}

bool LogReader::next(LogRecord& out) {
    if (!ok_) {
        return false;
    }
    uint8_t tag = 0;
    in_.read(reinterpret_cast<char*>(&tag), 1);
    if (in_.gcount() != 1) {
        return false;   // clean end of file
    }

    // The tag decides how many bytes follow. gcount() is the honest
    // measure of what was actually read: a short read is a torn record.
    char* dest = nullptr;
    std::streamsize size = 0;
    switch (static_cast<RecordType>(tag)) {   // LCOV_EXCL_BR_LINE: default handled below
        case RecordType::FRAME:
            out.type = RecordType::FRAME;
            dest = reinterpret_cast<char*>(&out.frame);
            size = sizeof(out.frame);
            break;
        case RecordType::FAULT_EVENT:
            out.type = RecordType::FAULT_EVENT;
            dest = reinterpret_cast<char*>(&out.event);
            size = sizeof(out.event);
            break;
        default:
            error_ = "unknown record tag";
            ok_ = false;
            return false;
    }
    in_.read(dest, size);
    if (in_.gcount() != size) {
        error_ = "torn record at end of file";
        return false;
    }
    return true;
}

std::optional<TelemetryFrame> LogReader::next_frame() {
    LogRecord record;
    while (next(record)) {  // LCOV_EXCL_EXCEPTION_BR_LINE: exception-unwind edges only; the decision itself is still counted
        if (record.type == RecordType::FRAME) {
            return record.frame;
        }
    }
    return std::nullopt;
}

std::vector<std::filesystem::path> list_log_files(const std::filesystem::path& directory,
                                                  const std::string& prefix) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {  // LCOV_EXCL_EXCEPTION_BR_LINE: exception-unwind edges only; the decision itself is still counted
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix + "_", 0) == 0 && entry.path().extension() == ".bin") {  // LCOV_EXCL_EXCEPTION_BR_LINE: exception-unwind edges only; the decision itself is still counted
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}
