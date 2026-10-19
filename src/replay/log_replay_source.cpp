#include "replay/log_replay_source.h"

LogReplaySource::LogReplaySource(std::vector<std::filesystem::path> files)
    : files_(std::move(files))
{
}

bool LogReplaySource::open_next_file() {
    // Skip files that fail header validation rather than stop: a session
    // with one unreadable rotated file still replays the rest, and the
    // reader's error is available for diagnostics.
    while (next_file_ < files_.size()) {
        reader_.emplace(files_[next_file_]);
        next_file_++;
        if (reader_->ok()) {
            return true;
        }
    }
    reader_.reset();
    return false;
}

void LogReplaySource::strip_to_raw(TelemetryFrame& frame) {
    frame.baro_altitude_m = 0.0f;
    frame.pitch_deg = 0.0f;
    frame.roll_deg = 0.0f;
    frame.fused_altitude_m = 0.0f;
    frame.vertical_speed_mps = 0.0f;
    // The statuses the detector judged go back to NOMINAL for it to judge
    // again; the one a source asserts, NOT_AVAILABLE, stays, so a replay
    // reproduces the availability the source wrote (REQ-FAULT-007).
    frame.baro_status = frame.baro_status == ChannelStatus::NOT_AVAILABLE ? ChannelStatus::NOT_AVAILABLE : ChannelStatus::NOMINAL;
    frame.imu_status = frame.imu_status == ChannelStatus::NOT_AVAILABLE ? ChannelStatus::NOT_AVAILABLE : ChannelStatus::NOMINAL;
    frame.gps_status = frame.gps_status == ChannelStatus::NOT_AVAILABLE ? ChannelStatus::NOT_AVAILABLE : ChannelStatus::NOMINAL;
    // The pipeline's judgments go too (REQ-PROC-007): a source hands out
    // NOT_JUDGED and the processor writes every validity, and the voter's
    // status is a verdict the replay's voter recomputes (telemetry_frame.h).
    // The sequence, the flags and the tail's raw fields stay: a source
    // wrote them, and a replay reproduces what the source said
    // (REQ-LOG-008, REQ-PROC-008). The reserved field is zero by
    // definition (log_format.h).
    frame.baro_altitude_validity = Validity::NOT_JUDGED;
    frame.pitch_validity = Validity::NOT_JUDGED;
    frame.roll_validity = Validity::NOT_JUDGED;
    frame.fused_altitude_validity = Validity::NOT_JUDGED;
    frame.vertical_speed_validity = Validity::NOT_JUDGED;
    frame.voter_status = 0;
    frame.reserved_d = 0;
}

bool LogReplaySource::has_next() {
    // Fill the look-ahead from the current file, or the next readable one,
    // until it holds a frame or the files run out. Nothing is consumed
    // here: read_frame() hands the buffered frame out.
    while (!next_.has_value() && !exhausted_) {
        if (!reader_.has_value()) {
            // The call on a line of its own, the decision on the next
            // (FTS-VR-001 Entry 23 §23.4).
            const bool opened = open_next_file();
            if (!opened) {
                exhausted_ = true;
                break;
            }
        }
        next_ = reader_->next_frame();
        if (!next_.has_value()) {
            reader_.reset();   // this file is done; try the next
        }
    }
    return next_.has_value();
}

TelemetryFrame LogReplaySource::read_frame() {
    if (has_next()) {
        strip_to_raw(*next_);
        last_ = *next_;
        next_.reset();
        frames_read_++;
    }
    return last_;
}
