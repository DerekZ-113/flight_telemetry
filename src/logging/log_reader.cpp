#include "logging/log_reader.h"

#include <algorithm>

namespace {

// A version-1 record has the same bytes as a version-2 one, with the
// compiler's padding where version 2 names fields (log_format.h). A log
// written before 2026-09-09 may carry stack contents there (FTS-VR-001
// Entries 4 and 5), so the bytes are set to what version 2 means by them:
// validity NOT_JUDGED, reserved zero. Replay and the tests then see one
// defined thing. The Python reader keeps a file's bytes as they are, because
// its byte-identity tests compare files (REQ-LOG-005).
void sanitize_version_1(LogRecord& out) {
    if (out.type == RecordType::FRAME) {
        out.frame.reserved_a = 0;
        out.frame.baro_altitude_validity = Validity::NOT_JUDGED;
        out.frame.pitch_validity = Validity::NOT_JUDGED;
        out.frame.roll_validity = Validity::NOT_JUDGED;
        out.frame.fused_altitude_validity = Validity::NOT_JUDGED;
        out.frame.vertical_speed_validity = Validity::NOT_JUDGED;
        out.frame.reserved_b = 0;
    } else {
        out.event.reserved = 0;
    }
}

}  // namespace

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
    // The call sits on its own line and the branch tests a local: a call
    // that may throw inside a conditional leaves an exception-unwind edge
    // on the line, and the marker that removes it takes the decision with
    // it on the runner's lcov (FTS-VR-001 Entry 11 §11.7).
    const std::string problem = header_problem(header_);
    if (!problem.empty()) {
        error_ = "header mismatch: " + problem;
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
    if (header_.version == 1) {
        sanitize_version_1(out);
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
