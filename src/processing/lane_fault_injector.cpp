#include "processing/lane_fault_injector.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <utility>

namespace {

// Each LaneField's member of the frame, in the enumeration's order: one
// table instead of a switch per kind (the injector's own idiom).
constexpr std::array<float TelemetryFrame::*, kLaneFieldCount> kFieldMember{
    &TelemetryFrame::baro_altitude_m, &TelemetryFrame::pitch_deg, &TelemetryFrame::roll_deg,
    &TelemetryFrame::fused_altitude_m, &TelemetryFrame::vertical_speed_mps,
    &TelemetryFrame::pressure_hpa,
    &TelemetryFrame::accel_x, &TelemetryFrame::accel_y, &TelemetryFrame::accel_z,
    &TelemetryFrame::gyro_x, &TelemetryFrame::gyro_y, &TelemetryFrame::gyro_z,
    &TelemetryFrame::gps_altitude_m,
};

float& field_of(TelemetryFrame& frame, LaneField field) {
    return frame.*kFieldMember[static_cast<std::size_t>(field)];
}

// One bit inverted through the float's bit pattern, by memcpy for the
// strict-aliasing reason voter.cpp gives; the bit taken modulo 32.
void flip(float& value, uint8_t bit) {
    uint32_t pattern = 0;
    std::memcpy(&pattern, &value, sizeof pattern);
    pattern ^= 1u << (bit % 32u);
    std::memcpy(&value, &pattern, sizeof value);
}

bool active(const LaneFault& fault, uint64_t t_ms) {
    return fault.onset_ms <= t_ms && t_ms < fault.end_ms;
}

std::size_t index_of(int lane) {
    return static_cast<std::size_t>(lane - 1);
}

// The longest onset the option takes, a little under four months: far
// beyond any run, and small enough that the milliseconds stay exact.
constexpr double kMaxOnsetSeconds = 1.0e7;

}  // namespace

LaneFaultInjector::LaneFaultInjector(const std::vector<LaneFault>& faults) {
    faults_.reserve(faults.size());
    for (std::size_t i = 0; i < faults.size(); i++) {
        Scheduled scheduled;
        scheduled.fault = faults[i];
        faults_.push_back(scheduled);
    }
}

void LaneFaultInjector::perturb_input(int lane, TelemetryFrame& lane_input) {
    for (std::size_t i = 0; i < faults_.size(); i++) {
        const LaneFault& fault = faults_[i].fault;
        // The call on a line of its own, the decision on the next: GCC
        // gives a call beside a decision an exception edge (FTS-VR-001
        // Entry 20 §20.7).
        const bool in_time = active(fault, lane_input.timestamp_ms);
        const bool applies = fault.lane == lane && fault.kind == LaneFaultKind::INPUT_BIT_FLIP && in_time;
        if (applies) {
            flip(field_of(lane_input, fault.field), fault.bit);
        }
    }
}

void LaneFaultInjector::perturb_output(int lane, TelemetryFrame& lane_output) {
    const std::size_t k = index_of(lane);
    real_[k] = lane_output;
    const uint64_t t_ms = lane_output.timestamp_ms;
    for (std::size_t i = 0; i < faults_.size(); i++) {
        Scheduled& scheduled = faults_[i];
        const LaneFault& fault = scheduled.fault;
        const bool in_time = active(fault, t_ms);
        const bool applies = fault.lane == lane && in_time;
        if (!applies) {
            continue;
        }
        if (fault.recorded && !scheduled.announced) {
            // Value-initialized, then assigned, so the reserved bytes are
            // zero (design.md section 9).
            FaultEvent event{};
            event.timestamp_ms = t_ms;
            event.channel = Channel::VOTER;
            event.type = FaultType::LANE_FAULT_INJECTED;
            event.value = static_cast<float>(lane);
            events_.push_back(event);
            scheduled.announced = true;
        }
        switch (fault.kind) {  // LCOV_EXCL_BR_LINE: exhaustive over enum class, no-match branch unreachable
            case LaneFaultKind::OUTPUT_BIT_FLIP:
                flip(field_of(lane_output, fault.field), fault.bit);
                break;
            case LaneFaultKind::OUTPUT_NAN:
                field_of(lane_output, fault.field) = std::numeric_limits<float>::quiet_NaN();
                break;
            case LaneFaultKind::OUTPUT_STALE:
                // The first cycle has no previous output, and an onset
                // after the start always has one; the lane then hands over
                // what it made.
                if (have_previous_) {
                    lane_output = previous_[k];
                }
                break;
            case LaneFaultKind::INPUT_BIT_FLIP:   // altered on the way in, perturb_input
                break;
        }
    }
    if (k + 1 == kLaneCount) {
        previous_ = real_;
        have_previous_ = true;
    }
}

std::vector<FaultEvent> LaneFaultInjector::take_events() {
    std::vector<FaultEvent> out = std::move(events_);
    events_.clear();
    return out;
}

LaneFault demo_lane_fault(int lane, uint64_t onset_ms) {
    LaneFault fault;
    fault.lane = lane;
    fault.kind = LaneFaultKind::OUTPUT_BIT_FLIP;
    fault.onset_ms = onset_ms;
    fault.end_ms = LaneFault::kPersistent;
    fault.field = LaneField::FUSED_ALTITUDE;
    fault.bit = kDemoBit;
    fault.recorded = true;
    return fault;
}

std::optional<LaneFault> parse_lane_fault(const std::string& text, std::string& error) {
    const std::string form =
        "--lane-fault needs <lane>@<seconds>: a lane from 1 to 3 and a time in seconds from the run's first frame, "
        "e.g. 2@60";
    // The shape first, on locals: one digit, '@', then a number that starts
    // with a digit or a point, so strtod's leading spaces and signs never
    // reach it.
    const std::size_t at = text.find('@');
    const bool shaped = at == 1 && text.size() > 2;
    if (!shaped) {
        error = form;
        return std::nullopt;
    }
    const char lane_char = text[0];
    const char first = text[2];
    const bool lane_ok = lane_char >= '1' && lane_char <= '3';
    const bool number_starts = (first >= '0' && first <= '9') || first == '.';
    if (!lane_ok || !number_starts) {
        error = form;
        return std::nullopt;
    }
    const char* begin = text.c_str() + 2;
    char* end = nullptr;
    const double seconds = std::strtod(begin, &end);
    const bool whole = end == text.c_str() + text.size();
    const bool finite = std::isfinite(seconds);
    if (!whole || !finite || seconds > kMaxOnsetSeconds) {
        error = form;
        return std::nullopt;
    }
    const long long onset_ms = std::llround(seconds * 1000.0);
    return demo_lane_fault(lane_char - '0', static_cast<uint64_t>(onset_ms));
}

std::string describe_lane_fault(const LaneFault& fault, bool reapplied) {
    const char* cause = reapplied ? "re-applied: the replay's input records it, REQ-RED-007" : "--lane-fault, REQ-RED-007";
    std::ostringstream out;
    out << "lane " << fault.lane << " from " << fault.onset_ms << " ms, bit " << static_cast<int>(fault.bit)
        << " of its fused altitude flipped on every cycle (" << cause << ")";
    return out.str();
}
