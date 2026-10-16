// Tests for src/drivers/aircraft_selector (REQ-SENS-009; FTS-FM-001
// FAULT-015). Test card TC-028: the nearest descending airborne aircraft
// with a complete state is chosen, kept while airborne and complete and
// through the tolerated misses, replaced only by a candidate nearer by more
// than the margin or when it is gone, every change recorded with both
// identities and its reason. The documents are opensky_examples.h's six
// aircraft, edited by hand per step.

#include <gtest/gtest.h>

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
    return AircraftSelector(AdsbReference{}, 2000.0, 2);
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
    EXPECT_FALSE(AircraftSelector::candidate(find(doc, "000004")));
    EXPECT_FALSE(AircraftSelector::candidate(find(doc, "000005")));
    EXPECT_FALSE(AircraftSelector::candidate(find(doc, "000006")));
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

TEST(AircraftSelectorTest, EveryReasonHasAName) {
    EXPECT_STREQ(switch_reason_name(SwitchReason::INITIAL), "initial");
    EXPECT_STREQ(switch_reason_name(SwitchReason::NEARER), "nearer");
    EXPECT_STREQ(switch_reason_name(SwitchReason::LANDED), "landed");
    EXPECT_STREQ(switch_reason_name(SwitchReason::LEFT), "left");
    EXPECT_STREQ(switch_reason_name(SwitchReason::INCOMPLETE), "incomplete");
    const AircraftIdentity a{"000001", "TEST001"}, b{"000001", "TEST001"}, c{"000001", ""};
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}
