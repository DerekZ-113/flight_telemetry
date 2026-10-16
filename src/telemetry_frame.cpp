#include "telemetry_frame.h"

const char* status_name(ChannelStatus status) {
    switch (status) {
        case ChannelStatus::NOMINAL:  return "NOMINAL";
        case ChannelStatus::DEGRADED: return "DEGRADED";
        case ChannelStatus::NO_DATA:  return "NO_DATA";
        case ChannelStatus::NOT_AVAILABLE: return "NOT_AVAILABLE";
    }
    // A byte outside the enumeration: a corrupt log, never the detector.
    return "UNKNOWN";
}

const char* validity_name(Validity validity) {
    switch (validity) {
        case Validity::NOT_JUDGED:       return "NOT_JUDGED";
        case Validity::NO_COMPUTED_DATA: return "NO_COMPUTED_DATA";
        case Validity::FAILURE_WARNING:  return "FAILURE_WARNING";
        case Validity::NORMAL:           return "NORMAL";
    }
    return "UNKNOWN";
}

const char* flag_name(FrameFlag flag) {
    switch (flag) {
        case FrameFlag::NEW_VEHICLE:       return "NEW_VEHICLE";
        case FrameFlag::VERTICAL_RATE:     return "VERTICAL_RATE";
        case FrameFlag::PRESSURE_ALTITUDE: return "PRESSURE_ALTITUDE";
        case FrameFlag::FIX_QUALITY:       return "FIX_QUALITY";
    }
    // A bit no version has named: zero by definition, so a corrupt log.
    return "UNKNOWN";
}

const char* voter_status_name(VoterStatus bit) {
    switch (bit) {
        case VoterStatus::VOTED:          return "VOTED";
        case VoterStatus::LANE_1_FAULTED: return "LANE_1_FAULTED";
        case VoterStatus::LANE_2_FAULTED: return "LANE_2_FAULTED";
        case VoterStatus::LANE_3_FAULTED: return "LANE_3_FAULTED";
        case VoterStatus::NO_MAJORITY:    return "NO_MAJORITY";
    }
    // A bit no voter decision has named: zero by definition, so a corrupt log.
    return "UNKNOWN";
}

bool has_flag(const TelemetryFrame& frame, FrameFlag flag) {
    return (frame.flags & static_cast<uint8_t>(flag)) != 0;
}

void set_flag(TelemetryFrame& frame, FrameFlag flag) {
    frame.flags = static_cast<uint8_t>(frame.flags | static_cast<uint8_t>(flag));
}
