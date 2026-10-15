#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// NMEA 0183, the text a GPS receiver speaks (REQ-SENS-005): one sentence
// per line, "$" + talker (two letters) + formatter (three letters) +
// comma-separated fields + "*" + two hex digits of checksum. These are the
// pure functions: a sentence in, fields and numbers out, no state and no
// port, so every rule is tested on strings. Framing bytes into sentences
// and pairing sentences into fixes belong to NmeaGps (nmea_gps.h).
//
// Two sentences carry what the frame needs. GGA: position, altitude above
// mean sea level, fix quality and satellites used. RMC: the receiver's own
// validity flag, speed over ground in knots, course over ground. Both carry
// the UTC time of their epoch, which is how a GGA and an RMC are known to
// describe the same fix.
//
// Parsing rules, the configuration loader's for numbers: a field is read
// in full or not at all. An empty field is absent, never an error: a
// receiver without a fix sends "$GPGGA,,,,,,0,00,99.99,,,,,,*48", its time,
// position and altitude empty. Non-empty text that does not parse, or a
// value no receiver can mean (a latitude past 90 degrees, sixty minutes,
// a fix with no position), makes the sentence malformed, and a malformed
// sentence is dropped whole: half a position is worse than none.
namespace nmea {

// 1 knot is 1852 m per hour, exactly.
constexpr double kMetersPerSecondPerKnot = 1852.0 / 3600.0;

enum class Formatter : uint8_t { OTHER, GGA, RMC };

struct Gga {
    std::string utc_time;        // "hhmmss.ss" as sent; empty before the receiver has time
    bool has_position = false;   // latitude, longitude and altitude were all present
    double latitude_deg = 0.0;   // north positive
    double longitude_deg = 0.0;  // east positive
    float altitude_m = 0.0f;     // above mean sea level
    int fix_quality = 0;         // 0 no fix, 1 GPS, 2 differential, 6 estimated
    int satellites = 0;          // used in the solution
};

struct Rmc {
    std::string utc_time;
    bool valid = false;          // status A; V is the receiver's warning: no fix
    bool has_position = false;   // latitude and longitude were present
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    float speed_mps = 0.0f;      // speed over ground; an empty field reads 0
    float course_deg = 0.0f;     // course over ground, degrees true; an empty field reads 0
};

// True when `sentence` is "$" + body + "*" + two hex digits (either case),
// nothing after, and the digits equal the XOR of every byte of the body.
// A sentence without a checksum is not evidence and reads false.
bool checksum_ok(std::string_view sentence);

// The fields of the body between "$" and "*", split on ",", the address
// ("GPGGA") first and empty fields kept. Empty if there is no "$" at the
// start or no "*" after it.
std::vector<std::string> fields(std::string_view sentence);

// The last three letters of a five-letter address, whatever the talker:
// GPGGA and GNGGA are both GGA. Anything else is OTHER.
Formatter formatter(std::string_view address);

// "ddmm.mmmm" (latitude) or "dddmm.mmmm" (longitude) with its hemisphere
// letter to signed decimal degrees: degrees plus minutes over sixty, south
// and west negative. nullopt when the text does not parse in full, is
// negative, has sixty minutes or more, exceeds 90 (N, S) or 180 (E, W), or
// the hemisphere is not one of the four letters.
std::optional<double> to_degrees(std::string_view ddmm, std::string_view hemisphere);

float knots_to_mps(double knots);

// nullopt means malformed: too few fields, a field that does not parse, a
// value out of range, or a fix (quality 1 or more, status A) without a
// position. The input is what fields() returns.
std::optional<Gga> parse_gga(const std::vector<std::string>& f);
std::optional<Rmc> parse_rmc(const std::vector<std::string>& f);

}  // namespace nmea
