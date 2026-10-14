#include "telemetry_frame.h"

const char* status_name(ChannelStatus status) {
    switch (status) {
        case ChannelStatus::NOMINAL:  return "NOMINAL";
        case ChannelStatus::DEGRADED: return "DEGRADED";
        case ChannelStatus::NO_DATA:  return "NO_DATA";
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
