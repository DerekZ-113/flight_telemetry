#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include "data_source.h"
#include "logging/log_reader.h"

// DataSource that replays a recorded session (REQ-LOG-003, REQ-LOG-004).
//
// The logger wrote *processed* frames. This source hands back *raw* ones:
// before a frame leaves read_frame(), every computed field is zeroed, every
// ChannelStatus is reset to NOMINAL and every Validity to NOT_JUDGED.
// Timestamp, sensor fields, and the read_ok flags are kept exactly as
// logged; a version-1 log's frames arrive the same way (REQ-LOG-005). That
// is the raw-only contract from data_source.h applied to replay: the
// detector recomputes status from read_ok and the processor recomputes the
// rest. A replay that passed the logged results through would let the
// pipeline be skipped entirely and still "match".
//
// Files are read in the order given, so rotated files replay as one
// session. Event records are skipped; replay reproduces them by re-running
// the detector.
//
// End of stream (REQ-LOG-006): has_next() answers from a one-frame
// look-ahead, filled from the current file or the next readable one and
// consumed only by read_frame(), so the loop can ask before every read and
// stop exactly at the log's end. exhausted() reports true once a look-ahead
// found nothing. A caller that never asks still gets the last frame again
// from read_frame(), the fallback the loop no longer relies on.
class LogReplaySource : public DataSource {
public:
    explicit LogReplaySource(std::vector<std::filesystem::path> files);

    TelemetryFrame read_frame() override;
    bool has_next() override;

    bool exhausted() const { return exhausted_; }
    size_t frames_read() const { return frames_read_; }

private:
    bool open_next_file();
    static void strip_to_raw(TelemetryFrame& frame);

    std::vector<std::filesystem::path> files_;
    size_t next_file_ = 0;
    std::optional<LogReader> reader_;
    std::optional<TelemetryFrame> next_;   // the look-ahead: what read_frame() hands out next
    TelemetryFrame last_{};
    size_t frames_read_ = 0;
    bool exhausted_ = false;
};
