#include "drivers/aircraft_selector.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMetersPerDegree = 6'371'000.0 * kPi / 180.0;

AircraftIdentity identity_of(const opensky_json::AircraftState& state) {
    return {state.icao24, state.callsign};
}

}  // namespace

const char* switch_reason_name(SwitchReason reason) {
    switch (reason) {  // LCOV_EXCL_BR_LINE: exhaustive over enum class, no-match branch unreachable
        case SwitchReason::INITIAL:    return "initial";
        case SwitchReason::NEARER:     return "nearer";
        case SwitchReason::LANDED:     return "landed";
        case SwitchReason::LEFT:       return "left";
        case SwitchReason::INCOMPLETE: return "incomplete";
    }
    return "UNKNOWN";  // LCOV_EXCL_LINE (unreachable: the switch covers every enumerator)
}

bool operator==(const AircraftIdentity& a, const AircraftIdentity& b) {
    return a.icao24 == b.icao24 && a.callsign == b.callsign;
}

bool operator!=(const AircraftIdentity& a, const AircraftIdentity& b) {
    return !(a == b);
}

AircraftSelector::AircraftSelector(const AdsbReference& reference, double switch_margin_m, int gone_after_polls)
    : reference_(reference), switch_margin_m_(switch_margin_m), gone_after_polls_(gone_after_polls) {}

bool AircraftSelector::usable(const opensky_json::AircraftState& state) {
    return !state.on_ground && state.latitude.has_value() && state.longitude.has_value() &&
           state.geo_altitude_m.has_value() && state.velocity_mps.has_value() && state.true_track_deg.has_value() &&
           state.time_position.has_value();
}

bool AircraftSelector::candidate(const opensky_json::AircraftState& state) {
    return usable(state) && state.vertical_rate_mps.has_value() && *state.vertical_rate_mps < 0.0f;
}

double AircraftSelector::distance_m(double latitude, double longitude, const AdsbReference& reference) {
    const double north = (latitude - reference.latitude) * kMetersPerDegree;
    const double east = (longitude - reference.longitude) * kMetersPerDegree * std::cos(reference.latitude * kPi / 180.0);
    return std::hypot(north, east);
}

const std::optional<AircraftIdentity>& AircraftSelector::selected() const { return selected_; }
const std::optional<opensky_json::AircraftState>& AircraftSelector::selected_state() const { return selected_state_; }
double AircraftSelector::selected_distance_m() const { return selected_distance_m_; }

// The one place the selection changes: `state` null means the kept
// aircraft is dropped with nothing in its place.
std::optional<SelectionChange> AircraftSelector::take(const opensky_json::AircraftState* state, SwitchReason reason) {
    SelectionChange change;
    change.previous = selected_;
    change.reason = reason;
    misses_ = 0;
    if (state == nullptr) {
        selected_.reset();
        selected_state_.reset();
        selected_distance_m_ = 0.0;
        return change;
    }
    selected_ = identity_of(*state);
    selected_state_ = *state;
    selected_distance_m_ = distance_m(*state->latitude, *state->longitude, reference_);
    change.current = selected_;
    change.distance_m = selected_distance_m_;
    return change;
}

std::optional<SelectionChange> AircraftSelector::update(const opensky_json::StatesDocument& document) {
    // The nearest candidate, ties to the smaller address.
    const opensky_json::AircraftState* nearest = nullptr;
    double nearest_m = 0.0;
    for (const opensky_json::AircraftState& state : document.states) {
        if (!candidate(state)) {
            continue;
        }
        const double d = distance_m(*state.latitude, *state.longitude, reference_);
        const bool nearer = nearest == nullptr || d < nearest_m || (d == nearest_m && state.icao24 < nearest->icao24);
        if (nearer) {
            nearest = &state;
            nearest_m = d;
        }
    }

    if (!selected_.has_value()) {
        if (nearest == nullptr) {
            return std::nullopt;
        }
        return take(nearest, SwitchReason::INITIAL);
    }

    const auto current = std::find_if(document.states.begin(), document.states.end(),
                                      [this](const opensky_json::AircraftState& s) { return s.icao24 == selected_->icao24; });
    if (current == document.states.end()) {
        // Absent from this response: held through the tolerated misses with
        // no vector to read, gone after them.
        misses_++;
        selected_state_.reset();
        if (misses_ < gone_after_polls_) {
            return std::nullopt;
        }
        return take(nearest, SwitchReason::LEFT);
    }
    if (!usable(*current)) {
        return take(nearest, current->on_ground ? SwitchReason::LANDED : SwitchReason::INCOMPLETE);
    }

    // Kept: present, airborne and complete, descending or not. A
    // challenger must be nearer by more than the margin.
    misses_ = 0;
    selected_state_ = *current;
    selected_distance_m_ = distance_m(*current->latitude, *current->longitude, reference_);
    const bool challenged = nearest != nullptr && nearest->icao24 != current->icao24 &&
                            nearest_m + switch_margin_m_ < selected_distance_m_;
    if (challenged) {
        return take(nearest, SwitchReason::NEARER);
    }
    return std::nullopt;
}
