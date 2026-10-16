#pragma once

#include <cstdint>

// Synthetic OpenSky responses in the service's schema (TC-027 to TC-029).
// Nothing here came from the service: its data license forbids making its
// data available to others, so every row is invented, with addresses from
// the unallocated block and callsigns no airline uses. The six aircraft sit
// on and around the 28R final at KSFO (37.6213, -122.3790), along the
// approach bearing 118 degrees from the airport, so the selection tests
// have a nearest descending aircraft (TEST001 at about 5 km), two more
// behind it (TEST002 at 9 km, TEST003 at 14 km, the latter with the
// 18th element the service adds with extended=1), a climbing departure
// nearer than any of them (TEST004, 3 km west), one on the ground at the
// airport (TEST005) and one without a geometric altitude (TEST006, 7 km):
// a row is 17 elements without extended=1; the fields are, by index,
// icao24, callsign, origin_country, time_position, last_contact,
// longitude, latitude, baro_altitude, on_ground, velocity, true_track,
// vertical_rate, sensors, geo_altitude, squawk, spi, position_source.
namespace opensky_examples {

constexpr int64_t kTime = 1760000000;

inline const char* const kStatesResponse =
    "{\"time\":1760000000,\"states\":["
    "[\"000001\",\"TEST001 \",\"United States\",1759999998,1759999999,-122.3289,37.6002,450.0,false,72.0,298.0,-3.5,null,480.0,\"1200\",false,0],"
    "[\"000002\",\"TEST002 \",\"United States\",1759999997,1759999999,-122.2888,37.5833,700.0,false,80.0,298.0,-3.8,null,730.0,\"1201\",false,0],"
    "[\"000003\",\"TEST003 \",\"United States\",1759999999,1759999999,-122.2387,37.5622,1000.0,false,90.0,298.0,-4.0,null,1030.0,\"1202\",false,0,3],"
    "[\"000004\",\"TEST004 \",\"United States\",1759999998,1759999999,-122.4131,37.6213,300.0,false,85.0,280.0,9.0,null,330.0,\"1203\",false,0],"
    "[\"000005\",\"TEST005 \",\"United States\",1759999999,1759999999,-122.3790,37.6213,null,true,5.0,280.0,0.0,null,null,\"1204\",false,0],"
    "[\"000006\",\"TEST006 \",\"United States\",1759999996,1759999999,-122.3088,37.5917,550.0,false,76.0,298.0,-3.6,null,null,\"1205\",false,0]"
    "]}";

// The box with nothing in it: the service answers null, not an empty array.
inline const char* const kEmptyStatesResponse = "{\"time\":1760000000,\"states\":null}";

inline const char* const kTokenResponse =
    "{\"access_token\":\"test-token\",\"expires_in\":1800,\"token_type\":\"Bearer\"}";
inline const char* const kSecondTokenResponse =
    "{\"access_token\":\"test-token-2\",\"expires_in\":1800,\"token_type\":\"Bearer\"}";
inline const char* const kTokenResponseWithoutExpiry = "{\"access_token\":\"test-token\"}";

// The credentials file's shape (REQ-CFG-004), with values that exist
// nowhere else, so a test can search every output for them.
inline const char* const kCredentials = "{\"clientId\":\"test-client\",\"clientSecret\":\"test-secret\"}";

}  // namespace opensky_examples
