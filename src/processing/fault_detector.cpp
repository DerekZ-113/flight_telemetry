#include "processing/fault_detector.h"

#include <algorithm>

#include "drivers/mpu6050.h"

FaultDetector::FaultDetector(const FaultDetectorConfig& config)
    : config_(config)
{
}

bool FaultDetector::StuckTracker::push(float value, int n) {
    // Exact float comparison on purpose. FAULT-006 asks for raw register
    // comparison because a live sensor at rest still shows LSB noise; a
    // stuck register converts to a bit-identical float every time, so
    // exact equality on the float is the same test. A tolerance here
    // would turn normal quiet readings into false stuck faults.
    if (primed && value == last) {
        run++;
    } else {
        run = 1;
        primed = true;
    }
    last = value;
    return run >= n;
}

bool FaultDetector::comm_timed_out(ChannelState& state, bool read_ok, uint64_t now_ms,
                                   uint64_t timeout_ms, float& elapsed_out) {
    if (read_ok) {
        state.last_ok_ms = now_ms;
        state.has_last_ok = true;
        elapsed_out = 0.0f;
        return false;
    }
    if (!state.has_last_ok) {
        // Never had a good read. Start the clock from this frame so a
        // sensor that is dead from power-on still times out.
        state.last_ok_ms = now_ms;
        state.has_last_ok = true;
    }
    const uint64_t elapsed = now_ms - state.last_ok_ms;
    elapsed_out = static_cast<float>(elapsed);
    // Strictly greater. With the last success at t-20 and frames every
    // 20 ms, `>` declares the fault 500 ms after the first failed read;
    // `>=` would declare it at 480 ms, which TC-002 treats as a false
    // alarm on a single transient error.
    return elapsed > timeout_ms;
}

FaultDetector::Observation FaultDetector::observe_baro(const TelemetryFrame& raw, uint64_t now_ms) {
    Observation obs;
    obs.read_ok = raw.baro_read_ok;

    float elapsed = 0.0f;
    if (comm_timed_out(baro_, obs.read_ok, now_ms, config_.i2c_timeout_ms, elapsed)) {
        obs.faulted = true;
        obs.type = FaultType::COMM_TIMEOUT;
        obs.value = elapsed;
        return obs;
    }
    if (!obs.read_ok) {
        return obs;  // stale fields: nothing else to judge this frame
    }

    // FAULT-008, FAULT-009. Temperature out of range degrades the whole
    // channel because the BMP280's pressure compensation uses it.
    if (raw.pressure_hpa < config_.pressure_min_hpa || raw.pressure_hpa > config_.pressure_max_hpa) {
        obs.faulted = true;
        obs.type = FaultType::OUT_OF_RANGE;
        obs.value = raw.pressure_hpa;
        return obs;
    }
    if (raw.temperature_c < config_.temperature_min_c || raw.temperature_c > config_.temperature_max_c) {
        obs.faulted = true;
        obs.type = FaultType::OUT_OF_RANGE;
        obs.value = raw.temperature_c;
        return obs;
    }

    // FAULT-005. Fed only on successful reads so a value repeated because
    // the driver held it during a dropout counts as a timeout, not a
    // stuck sensor.
    if (baro_pressure_.push(raw.pressure_hpa, config_.stuck_count)) {
        obs.faulted = true;
        obs.type = FaultType::STUCK;
        obs.value = raw.pressure_hpa;
    }
    return obs;
}

FaultDetector::Observation FaultDetector::observe_imu(const TelemetryFrame& raw, uint64_t now_ms) {
    Observation obs;
    obs.read_ok = raw.imu_read_ok;

    float elapsed = 0.0f;
    if (comm_timed_out(imu_, obs.read_ok, now_ms, config_.i2c_timeout_ms, elapsed)) {
        obs.faulted = true;
        obs.type = FaultType::COMM_TIMEOUT;
        obs.value = elapsed;
        return obs;
    }
    if (!obs.read_ok) {
        return obs;
    }

    // FAULT-010. An axis at either rail is a clipped reading. The rails are
    // the driver's: the two extreme 16-bit codes through its own scaling
    // (mpu6050.h), so the threshold is what the sensor can produce, never
    // a stated 2 g. Two signed tests, not a magnitude test: the range is
    // asymmetric, the positive rail one count short of 2 g and the count
    // one inside the negative rail equal to minus the positive rail, so a
    // fabs() against either rail misses one case or flags a valid reading.
    // std::find_if names the intent: locate the first saturated axis.
    const float accel[3] = {raw.accel_x, raw.accel_y, raw.accel_z};
    const float* saturated = std::find_if(std::begin(accel), std::end(accel),  // LCOV_EXCL_EXCEPTION_BR_LINE: exception-unwind edges only; the decision itself is still counted
                                          [](float a) {
                                              return a >= Mpu6050::kAccelPositiveRailMps2 ||
                                                     a <= Mpu6050::kAccelNegativeRailMps2;
                                          });
    if (saturated != std::end(accel)) {
        obs.faulted = true;
        obs.type = FaultType::OUT_OF_RANGE;
        obs.value = *saturated;
        return obs;
    }

    // FAULT-006, FAULT-007. Six independent trackers: one stuck axis is a
    // fault even while the other five vary. All six are pushed every
    // frame so their run counts stay in step; the first to trip reports.
    const float axes[6] = {raw.accel_x, raw.accel_y, raw.accel_z,
                           raw.gyro_x, raw.gyro_y, raw.gyro_z};
    bool stuck = false;
    float stuck_value = 0.0f;
    for (size_t i = 0; i < imu_axes_.size(); i++) {
        if (imu_axes_[i].push(axes[i], config_.stuck_count) && !stuck) {
            stuck = true;
            stuck_value = axes[i];
        }
    }
    if (stuck) {
        obs.faulted = true;
        obs.type = FaultType::STUCK;
        obs.value = stuck_value;
    }
    return obs;
}

FaultDetector::Observation FaultDetector::observe_gps(const TelemetryFrame& raw, uint64_t now_ms) {
    Observation obs;
    obs.read_ok = raw.gps_read_ok;

    // FAULT-003, the timeout: both of its conditions reach the detector as
    // one, because the live source withholds a no-fix epoch; the field for
    // (b)'s own event is parked (design.md section 11.2). No stuck check:
    // no FAULT entry covers GPS, and a stationary receiver legitimately
    // repeats its position.
    float elapsed = 0.0f;
    if (comm_timed_out(gps_, obs.read_ok, now_ms, config_.uart_timeout_ms, elapsed)) {
        obs.faulted = true;
        obs.type = FaultType::COMM_TIMEOUT;
        obs.value = elapsed;
    }
    return obs;
}

void FaultDetector::transition(Channel channel, ChannelState& state, const Observation& obs,
                               uint64_t now_ms) {
    if (obs.faulted) {
        // Any fault resets the recovery count: M valid readings must be
        // consecutive (REQ-FAULT-005).
        state.valid_run = 0;
        // Promotion from NO_DATA or NOMINAL alike (REQ-FAULT-006): a fault
        // on a channel's first-ever reading, or the timeout of a channel
        // dead from power-on, is DEGRADED with its event. One event per
        // transition, not per faulty frame: a sensor that stays broken for
        // a minute logs one fault, not 3000.
        if (state.status != ChannelStatus::DEGRADED) {
            state.status = ChannelStatus::DEGRADED;
            state.degraded_since_ms = now_ms;
            record_event(now_ms, channel, obs.type, obs.value);
        }
        return;
    }

    if (state.status == ChannelStatus::NO_DATA) {
        // The first successful reading: a start, not a recovery, so no
        // event and no run count (REQ-FAULT-006). A frame with no reading
        // inside the timeout leaves the channel NO_DATA.
        if (obs.read_ok) {
            state.status = ChannelStatus::NOMINAL;
        }
        return;
    }

    if (state.status == ChannelStatus::DEGRADED && obs.read_ok) {
        // A frame with no fault and a fresh reading counts toward
        // recovery. A frame with no reading proves nothing either way.
        state.valid_run++;
        if (state.valid_run >= config_.recovery_count) {
            state.status = ChannelStatus::NOMINAL;
            state.valid_run = 0;
            const float degraded_ms = static_cast<float>(now_ms - state.degraded_since_ms);
            record_event(now_ms, channel, FaultType::RECOVERY, degraded_ms);
        }
    }
}

void FaultDetector::record_event(uint64_t now_ms, Channel channel, FaultType type, float value) {
    // Value-initialized, then assigned. The two bytes between `type` and
    // `value` were implicit padding in format version 1, and a braced
    // temporary (`push_back({now, channel, type, value})`) was not required
    // to zero them; GCC did not, and CI run 34442203740 found live and
    // replayed events differing only there. Version 2 names them `reserved`,
    // which value-initialization zeroes like every other field; events are
    // written to the log as raw bytes, so that zero is part of the record.
    // Same rule as frames in the simulator (design.md section 9);
    // FaultDetectionTest.EventReservedIsZero is the guard.
    FaultEvent event{};
    event.timestamp_ms = now_ms;
    event.channel = channel;
    event.type = type;
    event.value = value;
    events_.push_back(event);
}

TelemetryFrame FaultDetector::check(const TelemetryFrame& raw) {
    const uint64_t now_ms = raw.timestamp_ms;

    // A channel the source reports NOT_AVAILABLE (REQ-FAULT-007) is a fact,
    // not a reading: no observation, no transition, no event, and the
    // status passes through as the source set it. The channel's own state
    // is not touched, so a source that provides it later starts at NO_DATA.
    const bool baro_absent = raw.baro_status == ChannelStatus::NOT_AVAILABLE;
    const bool imu_absent = raw.imu_status == ChannelStatus::NOT_AVAILABLE;
    const bool gps_absent = raw.gps_status == ChannelStatus::NOT_AVAILABLE;
    if (!baro_absent) {
        transition(Channel::BARO, baro_, observe_baro(raw, now_ms), now_ms);
    }
    if (!imu_absent) {
        transition(Channel::IMU, imu_, observe_imu(raw, now_ms), now_ms);
    }
    if (!gps_absent) {
        transition(Channel::GPS, gps_, observe_gps(raw, now_ms), now_ms);
    }

    TelemetryFrame out = raw;
    out.baro_status = baro_absent ? ChannelStatus::NOT_AVAILABLE : baro_.status;
    out.imu_status = imu_absent ? ChannelStatus::NOT_AVAILABLE : imu_.status;
    out.gps_status = gps_absent ? ChannelStatus::NOT_AVAILABLE : gps_.status;
    return out;
}

std::vector<FaultEvent> FaultDetector::take_events() {
    // Move the buffer out and leave an empty one behind. The caller owns
    // the returned vector; nothing is copied element by element.
    std::vector<FaultEvent> out = std::move(events_);
    events_.clear();
    return out;
}

const char* channel_name(Channel channel) {
    switch (channel) {  // LCOV_EXCL_BR_LINE: exhaustive over enum class, no-match branch unreachable
        case Channel::BARO: return "BARO";
        case Channel::IMU:  return "IMU";
        case Channel::GPS:  return "GPS";
    }
    // Unreachable: the switch covers every enumerator. Exists only to
    // satisfy -Wreturn-type, so it is excluded from coverage.
    return "UNKNOWN";  // LCOV_EXCL_LINE
}

const char* fault_type_name(FaultType type) {
    switch (type) {  // LCOV_EXCL_BR_LINE: exhaustive over enum class, no-match branch unreachable
        case FaultType::COMM_TIMEOUT: return "COMM_TIMEOUT";
        case FaultType::STUCK:        return "STUCK";
        case FaultType::OUT_OF_RANGE: return "OUT_OF_RANGE";
        case FaultType::RECOVERY:     return "RECOVERY";
    }
    return "UNKNOWN";  // LCOV_EXCL_LINE (unreachable, see channel_name)
}
