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
    frame.baro_status = ChannelStatus::NOMINAL;
    frame.imu_status = ChannelStatus::NOMINAL;
    frame.gps_status = ChannelStatus::NOMINAL;
}

TelemetryFrame LogReplaySource::read_frame() {
    while (!exhausted_) {
        if (!reader_.has_value() && !open_next_file()) {  // LCOV_EXCL_EXCEPTION_BR_LINE: exception-unwind edges only; the decision itself is still counted
            exhausted_ = true;
            break;
        }
        std::optional<TelemetryFrame> frame = reader_->next_frame();
        if (frame.has_value()) {
            strip_to_raw(*frame);
            last_ = *frame;
            frames_read_++;
            return last_;
        }
        reader_.reset();   // this file is done; try the next
    }
    return last_;
}
