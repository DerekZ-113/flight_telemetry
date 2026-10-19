#include "logging/log_reader.h"

#include <algorithm>

namespace {

// A version-1 record has the same bytes as a version-2 one, with the
// compiler's padding where version 2 named fields (log_format.h). A log
// written before 2026-09-09 may carry stack contents there (FTS-VR-001
// Entries 4 and 5), so the bytes are set to what version 2 meant by them:
// validity NOT_JUDGED, the former reserved fields (version 3's sequence
// and flags) zero. Replay and the tests then see one defined thing. The
// Python reader keeps a file's bytes as they are, because its
// byte-identity tests compare files (REQ-LOG-005). A version-2 file needs
// nothing of this: it defined every byte.
void sanitize_version_1(LogRecord& out) {
    if (out.type == RecordType::FRAME) {
        out.frame.sequence = 0;
        out.frame.baro_altitude_validity = Validity::NOT_JUDGED;
        out.frame.pitch_validity = Validity::NOT_JUDGED;
        out.frame.roll_validity = Validity::NOT_JUDGED;
        out.frame.fused_altitude_validity = Validity::NOT_JUDGED;
        out.frame.vertical_speed_validity = Validity::NOT_JUDGED;
        out.frame.flags = 0;
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

    // The tag decides how many bytes follow: for a frame, the file's
    // version's size (the prefix rule, log_format.h), read into a
    // value-initialized frame so that an older file's shorter record
    // leaves the tail zero rather than the previous record's. gcount() is
    // the honest measure of what was actually read: a short read is a torn
    // record.
    char* dest = nullptr;
    std::streamsize size = 0;
    switch (static_cast<RecordType>(tag)) {   // LCOV_EXCL_BR_LINE: default handled below
        case RecordType::FRAME:
            out.type = RecordType::FRAME;
            out.frame = TelemetryFrame{};
            dest = reinterpret_cast<char*>(&out.frame);
            size = frame_size_for(header_.version);
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
    while (next(record)) {  // LCOV_EXCL_EXCEPTION_BR_LINE: next() is the loop's condition; its decision is tested, not measured (FTS-VR-001 §1.3)
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
    // What the filter compares is built before the loop, and each name's
    // parts on lines of their own, so the condition calls only rfind and
    // path equality, which cannot throw (FTS-VR-001 Entry 23 §23.4).
    const std::string numbered = prefix + "_";
    const std::filesystem::path log_extension(".bin");
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {  // LCOV_EXCL_EXCEPTION_BR_LINE: a range-for whose increment can throw; its decision is tested, not measured (FTS-VR-001 §1.3)
        const std::string name = entry.path().filename().string();
        const std::filesystem::path extension = entry.path().extension();
        if (name.rfind(numbered, 0) == 0 && extension == log_extension) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}
