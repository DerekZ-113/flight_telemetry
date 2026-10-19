// Tests for src/drivers/aircraft_selector (REQ-SENS-009; FTS-FM-001
// FAULT-015). Test card TC-028: the nearest descending airborne aircraft
// with a complete state at or below the ceiling is chosen, kept while
// airborne, complete and under the ceiling and through the tolerated
// misses, replaced only by a candidate nearer by more than the margin or
// when it is gone, every change recorded with both identities and its
// reason. The documents are opensky_examples.h's six aircraft, edited by
// hand per step; steps 11 to 15 place aircraft by distance and bearing
// from the reference point, so no test writes a coordinate.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "drivers/aircraft_selector.h"
#include "opensky_examples.h"

namespace ex = opensky_examples;
using opensky_json::AircraftState;
using opensky_json::StatesDocument;

namespace {

constexpr double kPi = 3.14159265358979323846;

StatesDocument example() {
    const opensky_json::StatesParse parse = opensky_json::parse_states(ex::kStatesResponse);
    EXPECT_TRUE(parse.ok) << parse.problem;
    return parse.document;
}

AircraftState& find(StatesDocument& doc, const std::string& icao24) {
    for (AircraftState& s : doc.states) {
        if (s.icao24 == icao24) {
            return s;
        }
    }
    throw std::runtime_error("no aircraft " + icao24);
}

StatesDocument without(StatesDocument doc, const std::string& icao24) {
    for (auto it = doc.states.begin(); it != doc.states.end(); ++it) {
        if (it->icao24 == icao24) {
            doc.states.erase(it);
            break;
        }
    }
    return doc;
}

AircraftSelector make() {
    return AircraftSelector(AdsbReference{}, 2000.0, 2, AdsbConfig{}.ceiling_m);
}

// Any other ceiling: the rule as it was, for step 11's contrast, is a
// ceiling above the overflight.
AircraftSelector make_with_ceiling(double ceiling_m) {
    return AircraftSelector(AdsbReference{}, 2000.0, 2, ceiling_m);
}

// Places an aircraft `distance_m` from the reference point on
// `bearing_deg` (clockwise from north) at `geo_altitude_m`, by the inverse
// of AircraftSelector::distance_m's equirectangular formula, so a test
// states geometry, never a place; the placement is checked against the
// selector's own distance.
void place(AircraftState& state, double distance_m, double bearing_deg, float geo_altitude_m) {
    const AdsbReference ref{};
    const double meters_per_degree = 6'371'000.0 * kPi / 180.0;
    const double bearing = bearing_deg * kPi / 180.0;
    state.latitude = ref.latitude + distance_m * std::cos(bearing) / meters_per_degree;
    state.longitude = ref.longitude +
                      distance_m * std::sin(bearing) / (meters_per_degree * std::cos(ref.latitude * kPi / 180.0));
    state.geo_altitude_m = geo_altitude_m;
    EXPECT_NEAR(AircraftSelector::distance_m(*state.latitude, *state.longitude, ref), distance_m, 1.0);
}

// The haversine, the oracle for the equirectangular distance.
double haversine_m(double lat1, double lon1, double lat2, double lon2) {
    const double r = 6'371'000.0;
    const double p1 = lat1 * kPi / 180.0, p2 = lat2 * kPi / 180.0;
    const double dp = p2 - p1, dl = (lon2 - lon1) * kPi / 180.0;
    const double a = std::sin(dp / 2) * std::sin(dp / 2) + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return 2 * r * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

}  // namespace

// TC-028 step 1. Grounded, climbing and incomplete aircraft are never
// candidates: with only those in the box nothing is selected.
TEST(AircraftSelectorTest, NothingWithoutACandidate) {
    StatesDocument doc = example();
    doc = without(without(without(doc, "000001"), "000002"), "000003");   // TEST004 climbs, TEST005 is grounded, TEST006 is incomplete
    AircraftSelector selector = make();
    EXPECT_FALSE(selector.update(doc).has_value());
    EXPECT_FALSE(selector.selected().has_value());
    EXPECT_FALSE(selector.candidate(find(doc, "000004")));
    EXPECT_FALSE(selector.candidate(find(doc, "000005")));
    EXPECT_FALSE(selector.candidate(find(doc, "000006")));
    EXPECT_FALSE(AircraftSelector::usable(find(doc, "000005")));
    EXPECT_FALSE(AircraftSelector::usable(find(doc, "000006")));
    EXPECT_TRUE(AircraftSelector::usable(find(doc, "000004")));   // airborne and complete, only climbing
}

// TC-028 step 2. The nearest descending of six, past a climbing aircraft
// nearer than it and a grounded one at the airport itself.
TEST(AircraftSelectorTest, PicksTheNearestDescendingAircraft) {
    AircraftSelector selector = make();
    const std::optional<SelectionChange> change = selector.update(example());
    ASSERT_TRUE(change.has_value());
    EXPECT_FALSE(change->previous.has_value());
    ASSERT_TRUE(change->current.has_value());
    EXPECT_EQ(change->current->icao24, "000001");
    EXPECT_EQ(change->current->callsign, "TEST001");
    EXPECT_EQ(change->reason, SwitchReason::INITIAL);
    EXPECT_NEAR(change->distance_m, 5000.0, 150.0);
    ASSERT_TRUE(selector.selected_state().has_value());
    EXPECT_EQ(selector.selected_state()->icao24, "000001");
    EXPECT_NEAR(selector.selected_distance_m(), 5000.0, 150.0);
}

// TC-028 step 3. A challenger inside the margin is ignored; one past it
// takes over, with both identities and distances recorded.
TEST(AircraftSelectorTest, KeepsTheCurrentAircraftWithinTheMargin) {
    AircraftSelector selector = make();
    StatesDocument doc = example();
    ASSERT_TRUE(selector.update(doc).has_value());
    const double current_m = selector.selected_distance_m();

    // TEST002 moved to 1,500 m nearer than TEST001 along the approach.
    AircraftState& challenger = find(doc, "000002");
    const double ratio = (current_m - 1500.0) / current_m;
    challenger.latitude = 37.6213 + (*challenger.latitude - 37.6213) * ratio * (current_m / 9000.0) * 1.0;
    challenger.latitude = 37.6213 + (37.6002 - 37.6213) * ratio;
    challenger.longitude = -122.3790 + (-122.3289 + 122.3790) * ratio;
    EXPECT_FALSE(selector.update(doc).has_value());
    EXPECT_EQ(selector.selected()->icao24, "000001");

    // Now 2,500 m nearer.
    const double ratio2 = (current_m - 2500.0) / current_m;
    challenger.latitude = 37.6213 + (37.6002 - 37.6213) * ratio2;
    challenger.longitude = -122.3790 + (-122.3289 + 122.3790) * ratio2;
    const std::optional<SelectionChange> change = selector.update(doc);
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(change->reason, SwitchReason::NEARER);
    EXPECT_EQ(change->previous->icao24, "000001");
    EXPECT_EQ(change->current->icao24, "000002");
    EXPECT_NEAR(change->distance_m, current_m - 2500.0, 10.0);
    EXPECT_EQ(selector.selected()->icao24, "000002");
}

// TC-028 step 4. The kept aircraft reported on the ground: the nearest
// descending candidate takes over, reason landed.
TEST(AircraftSelectorTest, LandedAircraftIsReplaced) {
    AircraftSelector selector = make();
    StatesDocument doc = example();
    ASSERT_TRUE(selector.update(doc).has_value());
    find(doc, "000001").on_ground = true;
    const std::optional<SelectionChange> change = selector.update(doc);
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(change->reason, SwitchReason::LANDED);
    EXPECT_EQ(change->previous->icao24, "000001");
    EXPECT_EQ(change->current->icao24, "000002");
}

// TC-028 step 5. Absent once with a tolerance of two: kept, with no vector
// to read; absent twice: left. A failed poll never reaches the selector,
// so the miss count stands across one.
TEST(AircraftSelectorTest, AbsenceIsToleratedOnceThenCountsAsLeft) {
    AircraftSelector selector = make();
    const StatesDocument doc = example();
    ASSERT_TRUE(selector.update(doc).has_value());
    const StatesDocument gone = without(doc, "000001");

    EXPECT_FALSE(selector.update(gone).has_value());
    EXPECT_EQ(selector.selected()->icao24, "000001");
    EXPECT_FALSE(selector.selected_state().has_value());

    const std::optional<SelectionChange> change = selector.update(gone);
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(change->reason, SwitchReason::LEFT);
    EXPECT_EQ(change->previous->icao24, "000001");
    EXPECT_EQ(change->current->icao24, "000002");

    // The first aircraft back in the response is 4 km nearer than the kept one, past the 2 km margin:
    // the margin rule takes it back.
    const std::optional<SelectionChange> back = selector.update(doc);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->reason, SwitchReason::NEARER);
    EXPECT_EQ(back->current->icao24, "000001");
    // Presence resets the miss count: absent, present, absent is one miss each time, never two.
    EXPECT_FALSE(selector.update(gone).has_value());   // one miss
    EXPECT_FALSE(selector.update(doc).has_value());    // present again: reset
    EXPECT_FALSE(selector.update(gone).has_value());   // one miss, not two
    EXPECT_EQ(selector.selected()->icao24, "000001");
}

// TC-028 step 6. The kept aircraft's altitude gone null: incomplete, the
// nearest candidate takes over.
TEST(AircraftSelectorTest, IncompleteStateIsReplaced) {
    AircraftSelector selector = make();
    StatesDocument doc = example();
    ASSERT_TRUE(selector.update(doc).has_value());
    find(doc, "000001").geo_altitude_m.reset();
    const std::optional<SelectionChange> change = selector.update(doc);
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(change->reason, SwitchReason::INCOMPLETE);
    EXPECT_EQ(change->current->icao24, "000002");
}

// TC-028 step 7. The last candidate gone with nothing to take its place:
// the selection becomes none, with the previous identity and the reason;
// a candidate returning is initial again.
TEST(AircraftSelectorTest, DroppedWithNoReplacementThenInitialAgain) {
    AircraftSelector selector = make();
    StatesDocument only_one = example();
    only_one = without(without(only_one, "000002"), "000003");
    ASSERT_TRUE(selector.update(only_one).has_value());
    find(only_one, "000001").on_ground = true;
    const std::optional<SelectionChange> dropped = selector.update(only_one);
    ASSERT_TRUE(dropped.has_value());
    EXPECT_EQ(dropped->reason, SwitchReason::LANDED);
    EXPECT_EQ(dropped->previous->icao24, "000001");
    EXPECT_FALSE(dropped->current.has_value());
    EXPECT_EQ(dropped->distance_m, 0.0);
    EXPECT_FALSE(selector.selected().has_value());
    EXPECT_FALSE(selector.selected_state().has_value());

    const std::optional<SelectionChange> again = selector.update(example());
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->reason, SwitchReason::INITIAL);
    EXPECT_EQ(again->current->icao24, "000001");
}

// TC-028 step 8. Candidacy requires descent; keeping does not: an aircraft
// that levels off is kept against a descending challenger inside the
// margin.
TEST(AircraftSelectorTest, AKeptAircraftThatLevelsOffIsKept) {
    AircraftSelector selector = make();
    StatesDocument doc = example();
    ASSERT_TRUE(selector.update(doc).has_value());
    find(doc, "000001").vertical_rate_mps = 0.0f;
    EXPECT_FALSE(selector.update(doc).has_value());
    EXPECT_EQ(selector.selected()->icao24, "000001");
    ASSERT_TRUE(selector.selected_state().has_value());
    EXPECT_EQ(selector.selected_state()->vertical_rate_mps, std::optional<float>(0.0f));
}

// TC-028 step 9. Two candidates at one distance: the smaller address, so
// the choice is a function of the response.
TEST(AircraftSelectorTest, EqualDistancesChooseTheSmallerAddress) {
    StatesDocument doc = example();
    AircraftState& twin = find(doc, "000002");
    twin.latitude = find(doc, "000001").latitude;
    twin.longitude = find(doc, "000001").longitude;
    AircraftSelector selector = make();
    const std::optional<SelectionChange> change = selector.update(doc);
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(change->current->icao24, "000001");

    AircraftState& twin2 = find(doc, "000001");
    twin2.icao24 = "000009";
    AircraftSelector other = make();
    EXPECT_EQ(other.update(doc)->current->icao24, "000002");
}

// TC-028 step 10. The equirectangular distance against the haversine at
// the shipped box's four corners from the reference point: within 1%.
TEST(AircraftSelectorTest, DistanceIsWithinOnePercentOfTheHaversine) {
    const AdsbReference ref{};
    const AdsbBox box{};
    for (const double lat : {box.lat_min, box.lat_max}) {
        for (const double lon : {box.lon_min, box.lon_max}) {
            const double expected = haversine_m(ref.latitude, ref.longitude, lat, lon);
            const double actual = AircraftSelector::distance_m(lat, lon, ref);
            EXPECT_NEAR(actual, expected, expected * 0.01) << lat << ", " << lon;
            EXPECT_GT(actual, 10'000.0);
        }
    }
    EXPECT_EQ(AircraftSelector::distance_m(ref.latitude, ref.longitude, ref), 0.0);
}

// TC-028 step 11. FTS-VR-001 Entry 18 §18.5's geometry: an arrival
// descending through 3,498 m 2.4 km from the reference point is nearer
// than the aircraft on short final by more than the margin, and the rule
// as it was took it for 36 s; under the ceiling it is usable but not a
// candidate, and the aircraft on final is kept.
TEST(AircraftSelectorTest, AnOverflightAboveTheCeilingIsNotACandidate) {
    StatesDocument doc = example();
    place(find(doc, "000001"), 4500.0, 118.0, 152.0f);   // A, on short final
    AircraftState& overflight = find(doc, "000003");
    place(overflight, 2400.0, 140.0, 3498.0f);            // B; the bearing is immaterial
    overflight.vertical_rate_mps = -10.0f;
    const StatesDocument before = without(doc, "000003");

    AircraftSelector selector = make();
    const std::optional<SelectionChange> first = selector.update(before);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->current->icao24, "000001");
    EXPECT_NEAR(first->distance_m, 4500.0, 1.0);
    EXPECT_FALSE(selector.update(doc).has_value());   // the overflight's arrival changes nothing
    EXPECT_EQ(selector.selected()->icao24, "000001");
    EXPECT_TRUE(AircraftSelector::usable(overflight));
    EXPECT_FALSE(selector.candidate(overflight));

    // The rule as it was, a ceiling above the overflight: it takes over, the live run's switch.
    AircraftSelector as_it_was = make_with_ceiling(4000.0);
    ASSERT_TRUE(as_it_was.update(before).has_value());
    const std::optional<SelectionChange> displaced = as_it_was.update(doc);
    ASSERT_TRUE(displaced.has_value());
    EXPECT_EQ(displaced->reason, SwitchReason::NEARER);
    EXPECT_EQ(displaced->previous->icao24, "000001");
    EXPECT_EQ(displaced->current->icao24, "000003");
    EXPECT_NEAR(displaced->distance_m, 2400.0, 1.0);
}

// TC-028 step 12. "At most": an aircraft exactly at the ceiling is a
// candidate and is kept; one metre above it is neither.
TEST(AircraftSelectorTest, TheCeilingIsInclusiveOnBothSides) {
    const float ceiling = static_cast<float>(AdsbConfig{}.ceiling_m);
    StatesDocument only_one = example();
    only_one = without(without(only_one, "000002"), "000003");   // TEST001 the one candidate
    AircraftState& aircraft = find(only_one, "000001");

    aircraft.geo_altitude_m = ceiling;
    AircraftSelector at = make();
    EXPECT_TRUE(at.candidate(aircraft));
    const std::optional<SelectionChange> taken = at.update(only_one);
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(taken->reason, SwitchReason::INITIAL);
    EXPECT_FALSE(at.update(only_one).has_value());   // kept at the ceiling

    aircraft.geo_altitude_m = ceiling + 1.0f;
    AircraftSelector above = make();
    EXPECT_FALSE(above.candidate(aircraft));
    EXPECT_FALSE(above.update(only_one).has_value());
    EXPECT_FALSE(above.selected().has_value());

    // Selected at the ceiling, then one metre above: dropped with nothing in its place.
    const std::optional<SelectionChange> dropped = at.update(only_one);
    ASSERT_TRUE(dropped.has_value());
    EXPECT_EQ(dropped->reason, SwitchReason::ABOVE);
    EXPECT_EQ(dropped->previous->icao24, "000001");
    EXPECT_FALSE(dropped->current.has_value());
    EXPECT_EQ(dropped->distance_m, 0.0);
    EXPECT_FALSE(at.selected().has_value());
}

// TC-028 step 13. The hysteresis is unchanged under the ceiling: an
// aircraft above it never challenges, however near; the kept aircraft
// climbing under it is kept, as a levelling-off one is; a candidate under
// it still needs the margin.
TEST(AircraftSelectorTest, HysteresisHoldsUnderTheCeiling) {
    AircraftSelector selector = make();
    StatesDocument doc = example();
    ASSERT_TRUE(selector.update(doc).has_value());
    const double kept_m = selector.selected_distance_m();   // TEST001, about 5 km

    // An aircraft above the ceiling 100 m from the reference point, descending: no challenge.
    place(find(doc, "000003"), 100.0, 118.0, static_cast<float>(AdsbConfig{}.ceiling_m) + 1.0f);
    EXPECT_FALSE(selector.update(doc).has_value());
    EXPECT_EQ(selector.selected()->icao24, "000001");

    // The kept aircraft climbing under the ceiling: kept, a go-around's first seconds.
    find(doc, "000001").vertical_rate_mps = 3.0f;
    EXPECT_FALSE(selector.update(doc).has_value());
    EXPECT_EQ(selector.selected()->icao24, "000001");
    EXPECT_FALSE(selector.candidate(find(doc, "000001")));   // no longer a candidate, still kept

    // A candidate under the ceiling 2,500 m nearer: the margin rule, as before.
    place(find(doc, "000002"), kept_m - 2500.0, 118.0, 300.0f);
    const std::optional<SelectionChange> change = selector.update(doc);
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(change->reason, SwitchReason::NEARER);
    EXPECT_EQ(change->current->icao24, "000002");
}

// TC-028 step 14. A go-around through the ceiling: the kept aircraft is
// dropped with reason `above` and the nearest candidate takes its place;
// back under the ceiling and descending, the margin rule takes it back.
// With no other candidate the drop is to none and the return is initial.
TEST(AircraftSelectorTest, AGoAroundThroughTheCeilingIsDroppedAndTakenBack) {
    const float ceiling = static_cast<float>(AdsbConfig{}.ceiling_m);
    AircraftSelector selector = make();
    StatesDocument doc = example();
    ASSERT_TRUE(selector.update(doc).has_value());
    AircraftState& a = find(doc, "000001");

    a.geo_altitude_m = ceiling + 1.0f;
    a.vertical_rate_mps = 8.0f;
    const std::optional<SelectionChange> dropped = selector.update(doc);
    ASSERT_TRUE(dropped.has_value());
    EXPECT_EQ(dropped->reason, SwitchReason::ABOVE);
    EXPECT_EQ(dropped->previous->icao24, "000001");
    EXPECT_EQ(dropped->current->icao24, "000002");
    EXPECT_NEAR(dropped->distance_m, 9000.0, 150.0);
    EXPECT_EQ(selector.selected()->icao24, "000002");

    // Back under the ceiling, descending, 4,500 m nearer than TEST002: nearer.
    a.vertical_rate_mps = -3.5f;
    place(a, selector.selected_distance_m() - 4500.0, 118.0, 400.0f);
    const std::optional<SelectionChange> back = selector.update(doc);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->reason, SwitchReason::NEARER);
    EXPECT_EQ(back->current->icao24, "000001");

    // Alone: dropped to none, then initial again.
    StatesDocument alone = without(without(doc, "000002"), "000003");
    AircraftSelector lone = make();
    ASSERT_TRUE(lone.update(alone).has_value());
    find(alone, "000001").geo_altitude_m = ceiling + 1.0f;
    const std::optional<SelectionChange> gone = lone.update(alone);
    ASSERT_TRUE(gone.has_value());
    EXPECT_EQ(gone->reason, SwitchReason::ABOVE);
    EXPECT_FALSE(gone->current.has_value());
    find(alone, "000001").geo_altitude_m = ceiling;
    const std::optional<SelectionChange> again = lone.update(alone);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->reason, SwitchReason::INITIAL);
    EXPECT_EQ(again->current->icao24, "000001");
}

// TC-028 step 15. The shipped ceiling is derived, not chosen: the 3° glide
// path's height at the far corner of the shipped box, the largest ground
// distance a candidate in the box can have, so nothing on the approach
// inside the box is excluded; and below the overflight of Entry 18 §18.5
// (3,498 m), which it exists to exclude. A change to the box fails this
// until the ceiling is re-derived.
TEST(AircraftSelectorTest, TheShippedCeilingClearsAThreeDegreePathAtTheBoxesFarCorner) {
    const AdsbReference ref{};
    const AdsbBox box{};
    double far_m = 0.0;
    for (const double lat : {box.lat_min, box.lat_max}) {
        for (const double lon : {box.lon_min, box.lon_max}) {
            far_m = std::max(far_m, AircraftSelector::distance_m(lat, lon, ref));
        }
    }
    EXPECT_GT(far_m, 31'000.0);
    EXPECT_LT(far_m, 31'200.0);
    const double path_m = far_m * std::tan(3.0 * kPi / 180.0);   // about 1,629 m above the field
    EXPECT_GE(AdsbConfig{}.ceiling_m, path_m);
    EXPECT_LT(AdsbConfig{}.ceiling_m, 3498.0);
}

TEST(AircraftSelectorTest, EveryReasonHasAName) {
    EXPECT_STREQ(switch_reason_name(SwitchReason::INITIAL), "initial");
    EXPECT_STREQ(switch_reason_name(SwitchReason::NEARER), "nearer");
    EXPECT_STREQ(switch_reason_name(SwitchReason::LANDED), "landed");
    EXPECT_STREQ(switch_reason_name(SwitchReason::LEFT), "left");
    EXPECT_STREQ(switch_reason_name(SwitchReason::INCOMPLETE), "incomplete");
    EXPECT_STREQ(switch_reason_name(SwitchReason::ABOVE), "above");
    const AircraftIdentity a{"000001", "TEST001"}, b{"000001", "TEST001"}, c{"000001", ""};
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}
