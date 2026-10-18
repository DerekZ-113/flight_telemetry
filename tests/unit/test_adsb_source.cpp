// Tests for src/drivers/adsb_source (REQ-SENS-008, REQ-SENS-010,
// REQ-SENS-011, REQ-FAULT-007; FTS-FM-001 FAULT-012 to FAULT-015,
// FAULT-018). Test card TC-029 steps 1 through 10: the raw-only frame with
// the absent channels not available, the timestamp from the clock,
// sample-and-hold under the freshness rule, a failed poll holding and
// counting, the switch record at the frame's timestamp, a refused token
// refusing the start by status and path, the next delay, and the real
// thread. Every exchange is scripted on FakeHttpClient, time is
// FakeClock's, and the source is driven through poll_once() on the test's
// thread, except in the two thread tests, which run on the real clock with
// generous bounds. CI never calls the service.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "drivers/adsb_source.h"
#include "fake_clock.h"
#include "fake_http_client.h"
#include "opensky_examples.h"

namespace ex = opensky_examples;

namespace {

constexpr const char* kPath = "/nowhere/opensky_credentials.json";
constexpr uint64_t kPeriodNs = 20'000'000;
const size_t npos = std::string::npos;

opensky_json::Credentials credentials() {
    return {"test-client", "test-secret"};
}

// The fake handed to the source; the raw pointer stays with the test to
// script and inspect it after the move (the live source's Rig).
struct Rig {
    std::unique_ptr<FakeHttpClient> owned = std::make_unique<FakeHttpClient>();
    FakeHttpClient* http = owned.get();
    FakeClock clock;
    AdsbConfig config;

    AdsbSource make() {
        return AdsbSource(std::move(owned), clock, config, credentials(), kPath);
    }
    void script(const std::string& body) { http->queue(FakeHttpClient::response(200, body)); }
    void script_token() { script(ex::kTokenResponse); }
};

// The example with the row of `icao24` removed, as JSON text.
std::string without(const std::string& body, const std::string& icao24) {
    const size_t start = body.find("[\"" + icao24 + "\"");
    EXPECT_NE(start, npos) << icao24;
    const size_t end = body.find(']', start);   // a row holds no nested array
    if (body[end + 1] == ',') {
        return body.substr(0, start) + body.substr(end + 2);
    }
    return body.substr(0, start - 1) + body.substr(end + 1);   // the last row: the comma before it goes
}

// The example with the position time of `icao24` replaced.
std::string with_time_position(const std::string& body, const std::string& icao24, int64_t time_position) {
    const std::string country = "\"United States\",";
    const size_t row = body.find("[\"" + icao24 + "\"");
    EXPECT_NE(row, npos) << icao24;
    const size_t field = body.find(country, row) + country.size();
    const size_t comma = body.find(',', field);
    return body.substr(0, field) + std::to_string(time_position) + body.substr(comma);
}

// TEST001's vector, as the six raw fields.
void expect_test001(const TelemetryFrame& f) {
    EXPECT_NEAR(f.latitude, 37.6002, 1e-9);
    EXPECT_NEAR(f.longitude, -122.3289, 1e-9);
    EXPECT_FLOAT_EQ(f.gps_altitude_m, 480.0f);
    EXPECT_FLOAT_EQ(f.ground_speed_mps, 72.0f);
    EXPECT_FLOAT_EQ(f.course_deg, 298.0f);
}

long long elapsed_ms(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since).count();
}

}  // namespace

// TC-029 steps 1, 2 and 4 (REQ-SENS-011, REQ-FAULT-007, FAULT-018). Before
// any poll every raw field is zero and nothing is fresh; after one, the six
// GPS fields are the selected aircraft's and fresh, the barometer and the
// IMU zero, never fresh and NOT_AVAILABLE, the GPS channel NOMINAL for the
// detector to judge, every computed field untouched and every validity
// unjudged (the raw-only contract of design.md section 3); and across 100
// frames and two polls the two absent channels never change.
TEST(AdsbSourceTest, FrameIsRawOnlyWithTheAbsentChannelsNotAvailable) {
    Rig rig;
    AdsbSource source = rig.make();

    const TelemetryFrame before = source.read_frame();
    EXPECT_EQ(before.timestamp_ms, 0u);
    EXPECT_EQ(before.sequence, 0u);
    EXPECT_EQ(before.flags, 0u);
    EXPECT_FALSE(before.gps_read_ok);
    EXPECT_EQ(before.latitude, 0.0);
    EXPECT_EQ(before.longitude, 0.0);
    EXPECT_EQ(before.gps_altitude_m, 0.0f);
    EXPECT_EQ(before.ground_speed_mps, 0.0f);
    EXPECT_EQ(before.course_deg, 0.0f);
    EXPECT_EQ(before.baro_status, ChannelStatus::NOT_AVAILABLE);
    EXPECT_EQ(before.imu_status, ChannelStatus::NOT_AVAILABLE);
    EXPECT_EQ(before.gps_status, ChannelStatus::NOMINAL);
    EXPECT_FALSE(source.identity().has_value());
    EXPECT_FALSE(source.last_reading().has_value());

    rig.script_token();
    rig.script(ex::kStatesResponse);
    source.poll_once();
    rig.clock.advance(kPeriodNs);
    const TelemetryFrame f = source.read_frame();
    EXPECT_EQ(f.timestamp_ms, 20u);
    EXPECT_TRUE(f.gps_read_ok);
    expect_test001(f);
    EXPECT_EQ(f.pressure_hpa, 0.0f);
    EXPECT_EQ(f.temperature_c, 0.0f);
    EXPECT_EQ(f.accel_x, 0.0f);
    EXPECT_EQ(f.accel_y, 0.0f);
    EXPECT_EQ(f.accel_z, 0.0f);
    EXPECT_EQ(f.gyro_x, 0.0f);
    EXPECT_EQ(f.gyro_y, 0.0f);
    EXPECT_EQ(f.gyro_z, 0.0f);
    EXPECT_FALSE(f.baro_read_ok);
    EXPECT_FALSE(f.imu_read_ok);
    EXPECT_EQ(f.baro_status, ChannelStatus::NOT_AVAILABLE);
    EXPECT_EQ(f.imu_status, ChannelStatus::NOT_AVAILABLE);
    EXPECT_EQ(f.gps_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(f.baro_altitude_m, 0.0f);
    EXPECT_EQ(f.pitch_deg, 0.0f);
    EXPECT_EQ(f.roll_deg, 0.0f);
    EXPECT_EQ(f.fused_altitude_m, 0.0f);
    EXPECT_EQ(f.vertical_speed_mps, 0.0f);
    EXPECT_EQ(f.baro_altitude_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.pitch_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.roll_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.fused_altitude_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.vertical_speed_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.sequence, 1u);          // the source's count (REQ-LOG-008)
    // The first frame carrying TEST001 says so (REQ-PROC-008), and states
    // no fix quality: a state vector reports none (REQ-SENS-005).
    EXPECT_EQ(f.flags, static_cast<uint8_t>(FrameFlag::NEW_VEHICLE));
    EXPECT_EQ(f.gps_fix_quality, 0u);
    EXPECT_EQ(f.gps_satellites, 0u);
    EXPECT_EQ(f.voter_status, 0u);
    EXPECT_EQ(f.reserved_d, 0u);
    ASSERT_TRUE(source.identity().has_value());
    EXPECT_EQ(source.identity()->icao24, "000001");
    EXPECT_EQ(source.identity()->callsign, "TEST001");

    for (int i = 0; i < 100; ++i) {
        if (i == 50) {
            rig.script(ex::kStatesResponse);
            source.poll_once();
        }
        rig.clock.advance(kPeriodNs);
        const TelemetryFrame g = source.read_frame();
        EXPECT_EQ(g.pressure_hpa, 0.0f) << i;
        EXPECT_EQ(g.temperature_c, 0.0f) << i;
        EXPECT_EQ(g.accel_x, 0.0f) << i;
        EXPECT_EQ(g.accel_y, 0.0f) << i;
        EXPECT_EQ(g.accel_z, 0.0f) << i;
        EXPECT_EQ(g.gyro_x, 0.0f) << i;
        EXPECT_EQ(g.gyro_y, 0.0f) << i;
        EXPECT_EQ(g.gyro_z, 0.0f) << i;
        EXPECT_FALSE(g.baro_read_ok) << i;
        EXPECT_FALSE(g.imu_read_ok) << i;
        EXPECT_EQ(g.baro_status, ChannelStatus::NOT_AVAILABLE) << i;
        EXPECT_EQ(g.imu_status, ChannelStatus::NOT_AVAILABLE) << i;
        EXPECT_EQ(g.gps_status, ChannelStatus::NOMINAL) << i;
    }
}

// TC-031 step 8 (REQ-PROC-008, REQ-SENS-009). The first frame that carries a
// newly selected aircraft says so, NEW_VEHICLE, and no other does: not the
// frames that hold it, not the one a re-poll of the same vector delivers,
// not the one a later position time of the same aircraft makes fresh, not
// the frame after a miss; the first frame carrying the next aircraft says
// so again.
TEST(AdsbSourceTest, NewVehicleFlagMarksTheFirstFrameOfEachAircraft) {
    Rig rig;
    AdsbSource source = rig.make();
    EXPECT_FALSE(has_flag(source.read_frame(), FrameFlag::NEW_VEHICLE));   // nothing selected yet

    rig.script_token();
    rig.script(ex::kStatesResponse);
    source.poll_once();
    rig.clock.advance(kPeriodNs);
    const TelemetryFrame first = source.read_frame();
    EXPECT_TRUE(first.gps_read_ok);
    EXPECT_TRUE(has_flag(first, FrameFlag::NEW_VEHICLE));
    rig.clock.advance(kPeriodNs);
    EXPECT_FALSE(has_flag(source.read_frame(), FrameFlag::NEW_VEHICLE));   // held

    rig.script(ex::kStatesResponse);
    source.poll_once();                                          // the same vector again
    rig.clock.advance(kPeriodNs);
    const TelemetryFrame same = source.read_frame();
    EXPECT_FALSE(same.gps_read_ok);
    EXPECT_FALSE(has_flag(same, FrameFlag::NEW_VEHICLE));

    rig.script(with_time_position(ex::kStatesResponse, "000001", 1759999999));
    source.poll_once();                                          // the same aircraft, a later position
    rig.clock.advance(kPeriodNs);
    const TelemetryFrame later = source.read_frame();
    EXPECT_TRUE(later.gps_read_ok);
    EXPECT_FALSE(has_flag(later, FrameFlag::NEW_VEHICLE));

    const std::string gone = without(ex::kStatesResponse, "000001");
    rig.script(gone);
    source.poll_once();                                          // one miss: kept
    rig.clock.advance(kPeriodNs);
    EXPECT_FALSE(has_flag(source.read_frame(), FrameFlag::NEW_VEHICLE));
    rig.script(gone);
    source.poll_once();                                          // two: left, TEST002 takes over
    rig.clock.advance(kPeriodNs);
    const TelemetryFrame next = source.read_frame();
    EXPECT_TRUE(next.gps_read_ok);
    EXPECT_TRUE(has_flag(next, FrameFlag::NEW_VEHICLE));
    ASSERT_TRUE(source.identity().has_value());
    EXPECT_EQ(source.identity()->icao24, "000002");
    rig.clock.advance(kPeriodNs);
    EXPECT_FALSE(has_flag(source.read_frame(), FrameFlag::NEW_VEHICLE));
}

// REQ-LOG-008, TC-031 step 5. The source counts its own frames from zero,
// one per read_frame(), whether a poll delivered anything or not: the
// count is the frames', not the polls' or the readings'.
TEST(AdsbSourceTest, NumbersItsFramesFromZero) {
    Rig rig;
    AdsbSource source = rig.make();
    EXPECT_EQ(source.read_frame().sequence, 0u);    // before any poll
    rig.clock.advance(kPeriodNs);
    EXPECT_EQ(source.read_frame().sequence, 1u);
    rig.script_token();
    rig.script(ex::kStatesResponse);
    source.poll_once();
    for (uint32_t i = 2; i < 300; ++i) {
        rig.clock.advance(kPeriodNs);
        const TelemetryFrame f = source.read_frame();
        EXPECT_EQ(f.sequence, i);
        EXPECT_EQ(f.gps_read_ok, i == 2) << i;   // the vector handed over once, then held
    }
}

// TC-029 step 3 (REQ-SENS-008). Timestamps are milliseconds since the
// first frame, rounded to the nearest millisecond, the live source's rule:
// 39.6 ms reads as 40 and 79.6 as 80.
TEST(AdsbSourceTest, TimestampsAreMillisecondsSinceTheFirstFrame) {
    Rig rig;
    AdsbSource source = rig.make();
    EXPECT_EQ(source.read_frame().timestamp_ms, 0u);
    rig.clock.advance(39'600'000);
    EXPECT_EQ(source.read_frame().timestamp_ms, 40u);
    rig.clock.advance(40'000'000);
    EXPECT_EQ(source.read_frame().timestamp_ms, 80u);
}

// TC-029 step 5 (REQ-SENS-010, REQ-PROC-006). One vector is one
// measurement: fresh on the first frame after the poll, held with the flag
// false and the fields unchanged on the 249 after it.
TEST(AdsbSourceTest, ReadingIsFreshOnceThenHeld) {
    Rig rig;
    AdsbSource source = rig.make();
    rig.script_token();
    rig.script(ex::kStatesResponse);
    source.poll_once();
    const TelemetryFrame first = source.read_frame();
    EXPECT_TRUE(first.gps_read_ok);
    expect_test001(first);
    for (int i = 1; i < 250; ++i) {
        rig.clock.advance(kPeriodNs);
        const TelemetryFrame f = source.read_frame();
        EXPECT_FALSE(f.gps_read_ok) << i;
        EXPECT_EQ(f.latitude, first.latitude) << i;
        EXPECT_EQ(f.longitude, first.longitude) << i;
        EXPECT_EQ(f.gps_altitude_m, first.gps_altitude_m) << i;
        EXPECT_EQ(f.ground_speed_mps, first.ground_speed_mps) << i;
        EXPECT_EQ(f.course_deg, first.course_deg) << i;
        EXPECT_EQ(f.timestamp_ms, 20u * static_cast<unsigned>(i)) << i;
    }
    EXPECT_EQ(source.counters().readings, 1u);
    EXPECT_EQ(source.counters().reports, 1u);
    ASSERT_TRUE(source.last_reading().has_value());
    EXPECT_EQ(source.last_reading()->time_position, 1759999998);
    EXPECT_EQ(source.last_reading()->server_time, ex::kTime);
}

// TC-029 step 6, the first two cases (REQ-SENS-010). The same vector polled
// again is not a new measurement; a later position time is, once.
TEST(AdsbSourceTest, SamePositionTimeIsNotFreshALaterOneIsFreshOnce) {
    Rig rig;
    AdsbSource source = rig.make();
    rig.script_token();
    rig.script(ex::kStatesResponse);
    source.poll_once();
    EXPECT_TRUE(source.read_frame().gps_read_ok);

    rig.script(ex::kStatesResponse);
    source.poll_once();
    rig.clock.advance(kPeriodNs);
    EXPECT_FALSE(source.read_frame().gps_read_ok);
    EXPECT_EQ(source.counters().readings, 1u);

    rig.script(with_time_position(ex::kStatesResponse, "000001", 1759999999));
    source.poll_once();
    rig.clock.advance(kPeriodNs);
    const TelemetryFrame later = source.read_frame();
    EXPECT_TRUE(later.gps_read_ok);
    expect_test001(later);
    rig.clock.advance(kPeriodNs);
    EXPECT_FALSE(source.read_frame().gps_read_ok);
    EXPECT_EQ(source.counters().readings, 2u);
    EXPECT_EQ(source.last_reading()->time_position, 1759999999);
    EXPECT_EQ(source.counters().client.polls, 3u);
    EXPECT_EQ(source.counters().client.ok, 3u);
    EXPECT_EQ(source.counters().reports, 3u);
}

// TC-029 step 6, the third case, and the census (REQ-SENS-010,
// REQ-SENS-009). A newly selected aircraft is fresh once even when its
// position time is older than the last delivered one: TEST002's vector
// (…997) after TEST001's (…998), when TEST001 is gone from two responses.
TEST(AdsbSourceTest, ANewAircraftIsFreshEvenWithAnOlderPositionTime) {
    Rig rig;
    AdsbSource source = rig.make();
    rig.script_token();
    rig.script(ex::kStatesResponse);
    source.poll_once();
    EXPECT_TRUE(source.read_frame().gps_read_ok);
    EXPECT_EQ(source.counters().aircraft, 6u);
    EXPECT_EQ(source.counters().candidates, 3u);   // TEST001 to TEST003: TEST004 climbs, TEST005 is on the ground, TEST006 has no geometric altitude

    const std::string gone = without(ex::kStatesResponse, "000001");
    rig.script(gone);
    source.poll_once();                        // one miss: kept, nothing to read
    rig.clock.advance(kPeriodNs);
    EXPECT_FALSE(source.read_frame().gps_read_ok);
    ASSERT_TRUE(source.identity().has_value());
    EXPECT_EQ(source.identity()->icao24, "000001");

    rig.script(gone);
    source.poll_once();                        // two: left, TEST002 takes over
    rig.clock.advance(kPeriodNs);
    const TelemetryFrame f = source.read_frame();
    EXPECT_TRUE(f.gps_read_ok);
    EXPECT_NEAR(f.latitude, 37.5833, 1e-9);
    EXPECT_NEAR(f.longitude, -122.2888, 1e-9);
    EXPECT_FLOAT_EQ(f.gps_altitude_m, 730.0f);
    EXPECT_FLOAT_EQ(f.ground_speed_mps, 80.0f);
    EXPECT_FLOAT_EQ(f.course_deg, 298.0f);
    EXPECT_EQ(source.identity()->icao24, "000002");
    EXPECT_EQ(source.identity()->callsign, "TEST002");
    rig.clock.advance(kPeriodNs);
    EXPECT_FALSE(source.read_frame().gps_read_ok);
    EXPECT_EQ(source.counters().readings, 2u);
    EXPECT_EQ(source.counters().aircraft, 5u);
    EXPECT_EQ(source.counters().candidates, 2u);
    // The ages: 2 s for TEST001's position, 3 s for TEST002's.
    EXPECT_EQ(source.counters().age_min_s, 2);
    EXPECT_EQ(source.counters().age_max_s, 3);
    EXPECT_DOUBLE_EQ(source.counters().age_sum_s, 5.0);
}

// TC-029 step 7 (FAULT-012). A failed poll holds the fields with the flag
// false, counts, and is not a miss: the selection stands and the census is
// the last response's. Two polls with no frame between: the first report is
// replaced and counted, and the one vector is still delivered once.
TEST(AdsbSourceTest, FailedPollHoldsAndCounts) {
    Rig rig;
    AdsbSource source = rig.make();
    rig.script_token();
    rig.script(ex::kStatesResponse);
    source.poll_once();
    EXPECT_TRUE(source.read_frame().gps_read_ok);

    rig.http->queue(FakeHttpClient::transport_error(7, "Failed to connect to opensky-network.org port 443"));
    EXPECT_EQ(source.poll_once(), std::chrono::milliseconds(5000));
    rig.clock.advance(kPeriodNs);
    const TelemetryFrame held = source.read_frame();
    EXPECT_FALSE(held.gps_read_ok);
    expect_test001(held);
    EXPECT_EQ(source.counters().client.polls, 2u);
    EXPECT_EQ(source.counters().client.transport_errors, 1u);
    EXPECT_EQ(source.counters().client.last_error_code, 7);
    ASSERT_TRUE(source.identity().has_value());
    EXPECT_EQ(source.identity()->icao24, "000001");
    EXPECT_EQ(source.counters().aircraft, 6u);
    EXPECT_TRUE(source.switches().size() == 1u);

    rig.script(ex::kStatesResponse);
    source.poll_once();
    rig.script(ex::kStatesResponse);
    source.poll_once();
    rig.clock.advance(kPeriodNs);
    EXPECT_FALSE(source.read_frame().gps_read_ok);
    EXPECT_EQ(source.counters().overwritten, 1u);
    EXPECT_EQ(source.counters().reports, 3u);
    EXPECT_EQ(source.counters().client.polls, 4u);
    EXPECT_EQ(source.counters().readings, 1u);
}

// TC-029 step 8 (REQ-SENS-009; FAULT-015). identity() is empty before any
// poll and follows the selection; every change is a record carrying the
// timestamp of the first frame with the new aircraft and the poll that
// caused it.
TEST(AdsbSourceTest, SwitchIsRecordedWithTheFrameTimestamp) {
    Rig rig;
    AdsbSource source = rig.make();
    EXPECT_FALSE(source.identity().has_value());
    EXPECT_TRUE(source.switches().empty());
    EXPECT_EQ(source.read_frame().timestamp_ms, 0u);   // the epoch

    rig.script_token();
    rig.script(ex::kStatesResponse);
    source.poll_once();
    rig.clock.advance(100'000'000);
    EXPECT_EQ(source.read_frame().timestamp_ms, 100u);
    ASSERT_EQ(source.switches().size(), 1u);
    const SwitchRecord& initial = source.switches()[0];
    EXPECT_EQ(initial.timestamp_ms, 100u);
    EXPECT_EQ(initial.poll, 1u);
    EXPECT_EQ(initial.change.reason, SwitchReason::INITIAL);
    EXPECT_FALSE(initial.change.previous.has_value());
    ASSERT_TRUE(initial.change.current.has_value());
    EXPECT_EQ(initial.change.current->icao24, "000001");
    EXPECT_NEAR(initial.change.distance_m, 5000.0, 150.0);

    const std::string gone = without(ex::kStatesResponse, "000001");
    rig.script(gone);
    source.poll_once();
    rig.script(gone);
    source.poll_once();
    rig.clock.advance(4'900'000'000);
    EXPECT_EQ(source.read_frame().timestamp_ms, 5000u);
    ASSERT_EQ(source.switches().size(), 2u);
    const SwitchRecord& left = source.switches()[1];
    EXPECT_EQ(left.timestamp_ms, 5000u);
    EXPECT_EQ(left.poll, 3u);
    EXPECT_EQ(left.change.reason, SwitchReason::LEFT);
    ASSERT_TRUE(left.change.previous.has_value());
    EXPECT_EQ(left.change.previous->icao24, "000001");
    ASSERT_TRUE(left.change.current.has_value());
    EXPECT_EQ(left.change.current->icao24, "000002");
    EXPECT_NEAR(left.change.distance_m, 9000.0, 200.0);
    EXPECT_EQ(source.counters().changes, 2u);
    EXPECT_EQ(source.identity()->icao24, "000002");
}

// TC-029 step 7, the last case (REQ-SENS-009). The switches of untaken
// reports are kept, in order, each with the poll that caused it: three
// polls with no frame between, the first selecting TEST001 and the third,
// with TEST001 gone twice, switching to TEST002; one frame then carries
// both records and the vector of the aircraft now selected, TEST001's
// vector overtaken before any frame took it.
TEST(AdsbSourceTest, UntakenReportsKeepTheirSwitchesInOrder) {
    Rig rig;
    AdsbSource source = rig.make();
    EXPECT_EQ(source.read_frame().timestamp_ms, 0u);
    rig.script_token();
    rig.script(ex::kStatesResponse);
    source.poll_once();
    const std::string gone = without(ex::kStatesResponse, "000001");
    rig.script(gone);
    source.poll_once();
    rig.script(gone);
    source.poll_once();
    rig.clock.advance(kPeriodNs);
    const TelemetryFrame f = source.read_frame();
    EXPECT_TRUE(f.gps_read_ok);
    EXPECT_NEAR(f.latitude, 37.5833, 1e-9);
    ASSERT_EQ(source.switches().size(), 2u);
    EXPECT_EQ(source.switches()[0].change.reason, SwitchReason::INITIAL);
    EXPECT_EQ(source.switches()[0].poll, 1u);
    EXPECT_EQ(source.switches()[0].timestamp_ms, 20u);
    EXPECT_EQ(source.switches()[1].change.reason, SwitchReason::LEFT);
    EXPECT_EQ(source.switches()[1].poll, 3u);
    EXPECT_EQ(source.switches()[1].timestamp_ms, 20u);
    EXPECT_EQ(source.counters().overwritten, 2u);
    EXPECT_EQ(source.counters().reports, 1u);
    EXPECT_EQ(source.counters().readings, 1u);
    EXPECT_EQ(source.counters().changes, 2u);
    ASSERT_TRUE(source.identity().has_value());
    EXPECT_EQ(source.identity()->icao24, "000002");
}

// TC-029 step 9 (REQ-CFG-004; FAULT-013, FAULT-012). A token endpoint that
// refuses the credentials refuses the start, the message naming the status
// and the path and never the body or a credential, and no states request
// is made; a token endpoint in error is weather: counted, the run starts
// with nothing to read, and the thread tries again at the interval.
TEST(AdsbSourceTest, StartRefusesARefusedTokenByStatus) {
    {
        Rig rig;
        AdsbSource source = rig.make();
        rig.http->queue(FakeHttpClient::response(
            401, "{\"error\":\"invalid_client\",\"error_description\":\"Invalid client secret\"}"));
        EXPECT_FALSE(source.start());
        EXPECT_FALSE(source.polling());
        const std::string& error = source.error();
        EXPECT_NE(error.find("401"), npos) << error;
        EXPECT_NE(error.find(kPath), npos) << error;
        EXPECT_EQ(error.find("invalid_client"), npos) << error;
        EXPECT_EQ(error.find("Invalid client secret"), npos) << error;
        EXPECT_EQ(error.find("test-secret"), npos) << error;
        EXPECT_EQ(error.find("test-client"), npos) << error;
        EXPECT_FALSE(source.read_frame().gps_read_ok);
        EXPECT_EQ(source.counters().client.token_refused, 1u);
        EXPECT_EQ(rig.http->requests().size(), 1u);
    }
    {
        Rig rig;
        AdsbSource source = rig.make();
        rig.http->queue(FakeHttpClient::response(503, "Service Unavailable"));
        EXPECT_TRUE(source.start());
        EXPECT_TRUE(source.polling());
        EXPECT_TRUE(source.error().empty());
        EXPECT_FALSE(source.read_frame().gps_read_ok);
        EXPECT_EQ(source.counters().client.token_unavailable, 1u);
        EXPECT_FALSE(source.identity().has_value());
        source.stop();
        EXPECT_FALSE(source.polling());
        source.stop();
    }
}

// TC-029 step 10, the delay (FAULT-014, FAULT-012). The next poll waits the
// interval, or the seconds a 429 names; the vector from before the failures
// is still delivered once, the three untaken reports merged into one.
TEST(AdsbSourceTest, NextDelayIsTheIntervalOrTheRetryAfter) {
    Rig rig;
    AdsbSource source = rig.make();
    rig.script_token();
    rig.script(ex::kStatesResponse);
    EXPECT_EQ(source.poll_once(), std::chrono::milliseconds(5000));
    rig.http->queue(FakeHttpClient::response(429, "", {{"X-Rate-Limit-Retry-After-Seconds", "7"}}));
    EXPECT_EQ(source.poll_once(), std::chrono::milliseconds(7000));
    rig.http->queue(FakeHttpClient::response(429, ""));
    EXPECT_EQ(source.poll_once(), std::chrono::milliseconds(5000));
    rig.http->queue(FakeHttpClient::response(500, ""));
    EXPECT_EQ(source.poll_once(), std::chrono::milliseconds(5000));

    rig.clock.advance(kPeriodNs);
    const TelemetryFrame f = source.read_frame();
    EXPECT_TRUE(f.gps_read_ok);
    expect_test001(f);
    EXPECT_EQ(source.counters().client.rate_limited, 2u);
    EXPECT_EQ(source.counters().client.http_errors, 1u);
    ASSERT_TRUE(source.counters().client.last_retry_after_s.has_value());
    EXPECT_EQ(*source.counters().client.last_retry_after_s, 7);
    EXPECT_EQ(source.counters().overwritten, 3u);
    EXPECT_EQ(source.counters().reports, 1u);
    EXPECT_EQ(source.counters().readings, 1u);
}

// TC-029 step 10, the real thread. The first poll runs before the thread,
// so the first frame is fresh; the thread then polls at the interval; after
// stop() no request is made; and the same vector every poll is one
// measurement.
TEST(AdsbSourceTest, TheThreadPollsAtTheIntervalAndStopsOnJoin) {
    Rig rig;
    rig.config.poll_interval_ms = 10;
    rig.script_token();
    for (int i = 0; i < 200; ++i) {
        rig.script(ex::kStatesResponse);
    }
    AdsbSource source = rig.make();
    ASSERT_TRUE(source.start()) << source.error();
    EXPECT_TRUE(source.polling());
    EXPECT_TRUE(source.read_frame().gps_read_ok);

    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    while (source.counters().client.polls < 3 && elapsed_ms(t0) < 2000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        source.read_frame();
    }
    EXPECT_GE(source.counters().client.polls, 3u);
    source.stop();
    EXPECT_FALSE(source.polling());
    const size_t requests = rig.http->requests().size();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(rig.http->requests().size(), requests);
    source.read_frame();
    EXPECT_EQ(source.counters().readings, 1u);
    EXPECT_EQ(source.counters().client.polls + 1, requests);   // one token request, then one per poll
}

// TC-029 step 10, the long wait. A source parked on an hour's Retry-After
// is destroyed at once: the worker's predicate is the stop, and the
// destructor joins before anything the poll uses is gone.
TEST(AdsbSourceTest, ParkedOnALongRetryAfterIsDestroyedAtOnce) {
    Rig rig;
    rig.script_token();
    rig.http->queue(FakeHttpClient::response(429, "", {{"X-Rate-Limit-Retry-After-Seconds", "3600"}}));
    auto source = std::make_unique<AdsbSource>(std::move(rig.owned), rig.clock, rig.config, credentials(), kPath);
    ASSERT_TRUE(source->start()) << source->error();   // rate-limited is not a refusal
    EXPECT_TRUE(source->polling());
    EXPECT_EQ(rig.http->requests().size(), 2u);   // the token, the one poll; the fake dies with the source below
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    source.reset();
    EXPECT_LT(elapsed_ms(t0), 1000);
}
