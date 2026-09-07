#include "telemetry_frame.h"

const char* status_name(ChannelStatus status) {
    return status == ChannelStatus::NOMINAL ? "NOMINAL" : "DEGRADED";
}
