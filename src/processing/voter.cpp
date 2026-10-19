#include "processing/voter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <sstream>

namespace {

static_assert(sizeof(float) == sizeof(uint32_t), "the comparison reads a float's bits as 32 bits");

// A float's bit pattern. memcpy is the defined way to read an object's
// bytes as another type: reading the float through a reinterpret_cast'd
// pointer would break the strict-aliasing rule, and std::bit_cast is
// C++20. The compiler turns this into a register move.
uint32_t bits(float value) {
    uint32_t out = 0;
    std::memcpy(&out, &value, sizeof out);
    return out;
}

// Two floats agree when their bits are equal and they are not NaN. == is
// not identity on floats: NaN is unequal to itself, so two lanes holding
// one would never agree by ==, but they would by bits, and the voter must
// not vote a NaN through; and -0.0 == +0.0, a difference identical lanes
// cannot produce. One NaN is enough to refuse: equal bits mean both are.
bool floats_agree(float a, float b) {
    return !std::isnan(a) && bits(a) == bits(b);
}

// The lane's number in the events and the bits: 1 to 3 for array index
// 0 to 2. Exact in a float, which is the event's value field.
float lane_number(std::size_t index) {
    return static_cast<float>(index + 1);
}

// Whether a lane's output is for the frame it was given (REQ-RED-006): a
// computer that overruns hands its voter the output it had, the previous
// cycle's. The timestamp and the sequence together, because the frames of
// a version-1 or version-2 log all carry sequence 0.
bool for_this_frame(const TelemetryFrame& lane, const TelemetryFrame& checked) {
    return lane.timestamp_ms == checked.timestamp_ms && lane.sequence == checked.sequence;
}

// LANE_1_FAULTED is 0x0002, and lanes 2 and 3 follow it bit by bit.
uint16_t lane_bit(std::size_t index) {
    return static_cast<uint16_t>(static_cast<uint16_t>(VoterStatus::LANE_1_FAULTED) << index);
}

// The pairs tried for a majority, in this order, so the representative is
// always the lower-numbered lane of the first pair that agrees.
constexpr std::array<std::array<std::size_t, 2>, 3> kPairs{{{0, 1}, {0, 2}, {1, 2}}};

// The representative of the majority: the lower lane of the first pair of
// eligible lanes that agrees, or none. Without NaN, bit equality is an
// equivalence (reflexive, symmetric, transitive), so whichever pair is
// found first, the lanes agreeing with its representative are the one
// majority there is.
std::optional<std::size_t> find_representative(const std::array<TelemetryFrame, kLaneCount>& lanes,
                                               const std::array<bool, kLaneCount>& eligible) {
    const auto agreeing = std::find_if(kPairs.begin(), kPairs.end(), [&](const std::array<std::size_t, 2>& pair) {
        return eligible[pair[0]] && eligible[pair[1]] && lanes_agree(lanes[pair[0]], lanes[pair[1]]);
    });
    if (agreeing == kPairs.end()) {
        return std::nullopt;
    }
    return (*agreeing)[0];
}

// The ten fields a lane writes: the five computed values and their
// validities. Everything else in the voter's output is the checked frame's.
void copy_computed(const TelemetryFrame& from, TelemetryFrame& to) {
    to.baro_altitude_m = from.baro_altitude_m;
    to.pitch_deg = from.pitch_deg;
    to.roll_deg = from.roll_deg;
    to.fused_altitude_m = from.fused_altitude_m;
    to.vertical_speed_mps = from.vertical_speed_mps;
    to.baro_altitude_validity = from.baro_altitude_validity;
    to.pitch_validity = from.pitch_validity;
    to.roll_validity = from.roll_validity;
    to.fused_altitude_validity = from.fused_altitude_validity;
    to.vertical_speed_validity = from.vertical_speed_validity;
}

// No majority (REQ-RED-004): a held verdict is never better than
// FAILURE_WARNING. Validity is ordered worst first (telemetry_frame.h), so
// min() is worst-wins: NORMAL becomes FAILURE_WARNING over the held value,
// and NO_COMPUTED_DATA stays, so a flag is never raised over a number
// nobody voted.
void degrade(TelemetryFrame& frame) {
    frame.baro_altitude_validity = std::min(frame.baro_altitude_validity, Validity::FAILURE_WARNING);
    frame.pitch_validity = std::min(frame.pitch_validity, Validity::FAILURE_WARNING);
    frame.roll_validity = std::min(frame.roll_validity, Validity::FAILURE_WARNING);
    frame.fused_altitude_validity = std::min(frame.fused_altitude_validity, Validity::FAILURE_WARNING);
    frame.vertical_speed_validity = std::min(frame.vertical_speed_validity, Validity::FAILURE_WARNING);
}

}  // namespace

bool lanes_agree(const TelemetryFrame& a, const TelemetryFrame& b) {
    // The five computed values, their five validities, and the three
    // channel statuses the lanes carry from the detector: the thirteen
    // fields of REQ-RED-002, one per line.
    return floats_agree(a.baro_altitude_m, b.baro_altitude_m) &&
           floats_agree(a.pitch_deg, b.pitch_deg) &&
           floats_agree(a.roll_deg, b.roll_deg) &&
           floats_agree(a.fused_altitude_m, b.fused_altitude_m) &&
           floats_agree(a.vertical_speed_mps, b.vertical_speed_mps) &&
           a.baro_altitude_validity == b.baro_altitude_validity &&
           a.pitch_validity == b.pitch_validity &&
           a.roll_validity == b.roll_validity &&
           a.fused_altitude_validity == b.fused_altitude_validity &&
           a.vertical_speed_validity == b.vertical_speed_validity &&
           a.baro_status == b.baro_status &&
           a.imu_status == b.imu_status &&
           a.gps_status == b.gps_status;
}

Voter::Voter(int persistence)
    : persistence_(persistence)
{
    clear_held();
}

TelemetryFrame Voter::vote(const TelemetryFrame& checked, const std::array<TelemetryFrame, kLaneCount>& lanes) {
    // A new vehicle (REQ-PROC-008): every lane is reborn on this frame, so
    // the held vote is the previous vehicle's and goes; the exclusions
    // stay, a lane's health being no more the vehicle's than a channel's.
    if (has_flag(checked, FrameFlag::NEW_VEHICLE)) {
        clear_held();
    }

    // The bits name the lanes excluded before this cycle, so a lane
    // excluded on this cycle is named from the next one: the bit says the
    // lane "was excluded from the vote" (telemetry_frame.h), and on this
    // cycle it was in the vote, outside the majority. A lane is eligible
    // when it is in the vote and its output is for this frame.
    uint16_t status = static_cast<uint16_t>(VoterStatus::VOTED);
    std::array<bool, kLaneCount> stale{};
    std::array<bool, kLaneCount> eligible{};
    for (std::size_t k = 0; k < kLaneCount; k++) {
        if (lanes_[k].faulted) {
            status = static_cast<uint16_t>(status | lane_bit(k));
        }
        stale[k] = !lanes_[k].faulted && !for_this_frame(lanes[k], checked);
        eligible[k] = !lanes_[k].faulted && !stale[k];
    }

    // The raw fields, the statuses, the flags and the tail are the checked
    // frame's: no lane can change them.
    TelemetryFrame out = checked;
    const std::optional<std::size_t> representative = find_representative(lanes, eligible);
    if (representative.has_value()) {
        copy_computed(lanes[*representative], out);
        copy_computed(lanes[*representative], held_);
        in_no_majority_ = false;
        count(lanes, stale, *representative, checked.timestamp_ms);
    } else {
        // No majority (REQ-RED-004): the last vote held and degraded, one
        // event when the condition begins, and no lane's count moves.
        status = static_cast<uint16_t>(status | static_cast<uint16_t>(VoterStatus::NO_MAJORITY));
        copy_computed(held_, out);
        degrade(out);
        if (!in_no_majority_) {
            in_no_majority_ = true;
            const auto lanes_in = std::count_if(lanes_.begin(), lanes_.end(),
                                                [](const LaneState& lane) { return !lane.faulted; });
            record_event(checked.timestamp_ms, FaultType::NO_MAJORITY, static_cast<float>(lanes_in));
        }
    }
    out.voter_status = status;
    return out;
}

void Voter::count(const std::array<TelemetryFrame, kLaneCount>& lanes,
                  const std::array<bool, kLaneCount>& stale,
                  std::size_t representative, uint64_t now_ms) {
    // REQ-RED-003, on a cycle that has a majority: a lane in it starts its
    // count again; a lane outside it, disagreeing or stale (REQ-RED-006),
    // adds one, its onset logged at the first, as an overrun when staleness
    // was the reason, and its exclusion at the persistence count. An
    // excluded lane is out for good. (On a cycle with no majority this is
    // not called: nothing says which lane is wrong, so no count moves, and a
    // lane is excluded only beside a majority it is outside of, which keeps
    // at least two lanes in the vote.)
    for (std::size_t k = 0; k < kLaneCount; k++) {
        LaneState& lane = lanes_[k];
        if (lane.faulted) {
            continue;
        }
        if (!stale[k] && lanes_agree(lanes[k], lanes[representative])) {
            lane.misses = 0;
            continue;
        }
        lane.misses++;
        if (lane.misses == 1) {
            const FaultType onset = stale[k] ? FaultType::LANE_OVERRUN : FaultType::LANE_DISAGREEMENT;
            record_event(now_ms, onset, lane_number(k));
        }
        if (lane.misses >= persistence_) {
            lane.faulted = true;
            record_event(now_ms, FaultType::LANE_FAULTED, lane_number(k));
        }
    }
}

std::vector<FaultEvent> Voter::take_events() {
    std::vector<FaultEvent> out = std::move(events_);
    events_.clear();
    return out;
}

void Voter::clear_held() {
    held_ = TelemetryFrame{};
    held_.baro_altitude_validity = Validity::NO_COMPUTED_DATA;
    held_.pitch_validity = Validity::NO_COMPUTED_DATA;
    held_.roll_validity = Validity::NO_COMPUTED_DATA;
    held_.fused_altitude_validity = Validity::NO_COMPUTED_DATA;
    held_.vertical_speed_validity = Validity::NO_COMPUTED_DATA;
}

void Voter::record_event(uint64_t now_ms, FaultType type, float value) {
    FaultEvent event{};
    event.timestamp_ms = now_ms;
    event.channel = Channel::VOTER;
    event.type = type;
    event.value = value;
    events_.push_back(event);
}

void VoterTally::account(const TelemetryFrame& frame) {
    frames++;
    const bool no_majority = (frame.voter_status & static_cast<uint16_t>(VoterStatus::NO_MAJORITY)) != 0;
    if (no_majority) {
        no_majority_frames++;
    }
    last_status = frame.voter_status;
}

void VoterTally::account_events(std::size_t count) {
    events += count;
}

std::string VoterTally::summary() const {
    std::ostringstream out;
    out << "Voter:     " << frames << " frames";
    if (frames == 0) {
        return out.str();
    }
    out << ", lanes in the vote";
    for (std::size_t k = 0; k < kLaneCount; k++) {
        // The call on a line of its own: a comparison beside a call, in a
        // function holding a stream, shows GCC's exception edges.
        const uint16_t bit = lane_bit(k);
        const bool excluded = (last_status & bit) != 0;
        if (!excluded) {
            out << ' ' << k + 1;
        }
    }
    out << ", no majority on " << no_majority_frames << " frames, voter events " << events;
    return out.str();
}
