#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "drivers/adsb_config.h"
#include "drivers/opensky_json.h"

// Which aircraft the ADS-B source presents, with hysteresis (REQ-SENS-009;
// FTS-FM-001 FAULT-015). The system tracks one vehicle; a response holds
// dozens. A *candidate* is airborne, complete (a position, a geometric
// altitude, a ground speed, a track and the time of the position, so one
// reading fills every GPS field) and descending: nearest-and-descending to
// the airport's reference point is the aircraft on final, departures
// climb and the ground excludes the taxiing. The kept aircraft stays while
// it is airborne and complete, descending or not, and through fewer than
// `gone_after_polls` consecutive responses without it (a receiver's
// coverage flickers); a candidate replaces it only when nearer the
// reference point by more than the margin, so two parallel arrivals do not
// alternate on the display. Equal distances choose the smaller address, so
// the choice is a function of the response. A failed poll never reaches
// this class: a response that says nothing about the sky is not a miss.
// Every change is returned with both identities and its reason, for the
// console, the file beside the log and the session status.
enum class SwitchReason : uint8_t {
    INITIAL,     // none → an aircraft
    NEARER,      // a candidate nearer by more than the margin
    LANDED,      // the kept aircraft reported on the ground
    LEFT,        // the kept aircraft absent from `gone_after_polls` consecutive responses
    INCOMPLETE,  // the kept aircraft's state lost a field a reading needs
};

const char* switch_reason_name(SwitchReason reason);

struct AircraftIdentity {
    std::string icao24;
    std::string callsign;   // trimmed; may be empty
};

bool operator==(const AircraftIdentity& a, const AircraftIdentity& b);
bool operator!=(const AircraftIdentity& a, const AircraftIdentity& b);

struct SelectionChange {
    std::optional<AircraftIdentity> previous;   // empty for INITIAL
    std::optional<AircraftIdentity> current;    // empty when nothing replaced the kept aircraft
    SwitchReason reason = SwitchReason::INITIAL;
    double distance_m = 0.0;                    // the new aircraft's distance to the reference; 0 with none
};

class AircraftSelector {
public:
    AircraftSelector(const AdsbReference& reference, double switch_margin_m, int gone_after_polls);

    // One response. The change it caused, if any; afterwards selected()
    // names the kept aircraft and selected_state() its vector in this
    // response, when it was in it and usable.
    std::optional<SelectionChange> update(const opensky_json::StatesDocument& document);

    const std::optional<AircraftIdentity>& selected() const;
    const std::optional<opensky_json::AircraftState>& selected_state() const;
    double selected_distance_m() const;

    // Airborne with every field a reading needs.
    static bool usable(const opensky_json::AircraftState& state);
    // Usable and descending: eligible to be selected.
    static bool candidate(const opensky_json::AircraftState& state);
    // The equirectangular approximation the bridge and the profile use
    // (6,371,000 m × π / 180 per degree, east scaled by the reference
    // latitude's cosine): under half a percent across the box.
    static double distance_m(double latitude, double longitude, const AdsbReference& reference);

private:
    std::optional<SelectionChange> take(const opensky_json::AircraftState* state, SwitchReason reason);

    AdsbReference reference_;
    double switch_margin_m_;
    int gone_after_polls_;
    std::optional<AircraftIdentity> selected_;
    std::optional<opensky_json::AircraftState> selected_state_;
    double selected_distance_m_ = 0.0;
    int misses_ = 0;
};
