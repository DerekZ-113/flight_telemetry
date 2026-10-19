#include "run_schedule.h"

#include <algorithm>

Channel channel_of(RawField field) {
    switch (field) {
        case RawField::PRESSURE:
        case RawField::TEMPERATURE:
            return Channel::BARO;
        case RawField::GPS_ALTITUDE:
            return Channel::GPS;
        case RawField::ACCEL_X:
        case RawField::ACCEL_Y:
        case RawField::ACCEL_Z:
        case RawField::GYRO_X:
        case RawField::GYRO_Y:
        case RawField::GYRO_Z:
            break;
    }
    return Channel::IMU;
}

std::vector<Channel> channels_touched(const SensorFault& fault) {
    switch (fault.mode) {
        case SensorMode::NONE:
            return {};
        case SensorMode::FAIL_READS:
        case SensorMode::HOLD_CHANNEL:
            return fault.channels;
        case SensorMode::HOLD_FIELD:
        case SensorMode::OVERRIDE_FIELD:
        case SensorMode::BIAS_FIELD:
            break;
    }
    return {channel_of(fault.field)};
}

int reference_lane(const RunSchedule& schedule) {
    for (int lane = 1; lane <= static_cast<int>(kLaneCount); lane++) {
        const bool altered = std::any_of(schedule.lane_faults.begin(), schedule.lane_faults.end(),
                                         [lane](const LaneFault& f) { return f.lane == lane; });
        if (!altered) {
            return lane;
        }
    }
    return 0;
}

uint64_t lane_onset(const RunSchedule& schedule, int lane) {
    uint64_t onset = kNever;
    for (const LaneFault& f : schedule.lane_faults) {
        if (f.lane == lane) {
            onset = std::min(onset, f.onset_ms);
        }
    }
    return onset;
}
