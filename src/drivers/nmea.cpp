#include "drivers/nmea.h"

#include <cmath>
#include <cstdlib>

namespace nmea {
namespace {

// Field indices, the address counting as 0 (NMEA 0183; the u-blox 6
// receiver description, sections GGA and RMC).
constexpr size_t kGgaTime = 1;
constexpr size_t kGgaLatitude = 2;    // then N or S
constexpr size_t kGgaLongitude = 4;   // then E or W
constexpr size_t kGgaQuality = 6;
constexpr size_t kGgaSatellites = 7;
constexpr size_t kGgaAltitude = 9;    // then "M"
constexpr size_t kGgaFields = 10;     // through the altitude; the geoid and DGPS fields may follow

constexpr size_t kRmcTime = 1;
constexpr size_t kRmcStatus = 2;
constexpr size_t kRmcLatitude = 3;    // then N or S
constexpr size_t kRmcLongitude = 5;   // then E or W
constexpr size_t kRmcSpeedKnots = 7;
constexpr size_t kRmcCourse = 8;
constexpr size_t kRmcFields = 9;      // through the course; the date, variation and mode may follow

bool hex_digit(char c, int& out) {
    if (c >= '0' && c <= '9') {
        out = c - '0';
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        out = c - 'A' + 10;
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        out = c - 'a' + 10;
        return true;
    }
    return false;
}

// The whole of `text` as a finite number; false for empty or partial text.
bool parse_number(const std::string& text, double& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    // To the field's last byte, not to the first NUL: a NUL is invisible
    // to the XOR checksum and a BREAK on a UART can read as one.
    const bool consumed = end == text.data() + text.size();
    if (!consumed) {
        return false;
    }
    const bool finite = std::isfinite(value);
    if (!finite) {
        return false;
    }
    out = value;
    return true;
}

// The whole of `text` as a whole number from min to max; an empty field
// is the absent value, 0, which a no-fix receiver sends for neither.
bool parse_count(const std::string& text, long min, long max, int& out) {
    if (text.empty()) {
        out = 0;
        return true;
    }
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    const bool consumed = end == text.data() + text.size();
    if (!consumed) {
        return false;
    }
    if (value < min || value > max) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

// Latitude and longitude come as a pair: both present or neither. One
// without the other is malformed, and so is either that does not convert.
enum class Position : uint8_t { ABSENT, PRESENT, MALFORMED };

Position read_position(const std::vector<std::string>& f, size_t lat_index, size_t lon_index,
                       double& latitude_out, double& longitude_out) {
    const bool lat_present = !f[lat_index].empty();
    const bool lon_present = !f[lon_index].empty();
    if (!lat_present && !lon_present) {
        return Position::ABSENT;
    }
    if (lat_present != lon_present) {
        return Position::MALFORMED;
    }
    const std::optional<double> latitude = to_degrees(f[lat_index], f[lat_index + 1]);
    const std::optional<double> longitude = to_degrees(f[lon_index], f[lon_index + 1]);
    const bool both = latitude.has_value() && longitude.has_value();
    if (!both) {
        return Position::MALFORMED;
    }
    latitude_out = *latitude;
    longitude_out = *longitude;
    return Position::PRESENT;
}

}  // namespace

bool checksum_ok(std::string_view sentence) {
    const bool starts = !sentence.empty() && sentence.front() == '$';
    if (!starts) {
        return false;
    }
    const size_t star = sentence.find('*', 1);
    const bool star_then_two_digits = star != std::string_view::npos && star + 3 == sentence.size();
    if (!star_then_two_digits) {
        return false;
    }
    int high = 0;
    int low = 0;
    const bool hex = hex_digit(sentence[star + 1], high) && hex_digit(sentence[star + 2], low);
    if (!hex) {
        return false;
    }
    // XOR of the body: every byte between "$" and "*". It catches any
    // single-bit error and most bursts, not every two-byte swap; it is
    // what the standard gives, and a sentence that fails it is dropped.
    uint8_t sum = 0;
    for (size_t i = 1; i < star; ++i) {
        const uint8_t byte = static_cast<uint8_t>(sentence[i]);
        sum = static_cast<uint8_t>(sum ^ byte);
    }
    return sum == static_cast<uint8_t>(high * 16 + low);
}

std::vector<std::string> fields(std::string_view sentence) {
    std::vector<std::string> out;
    const bool starts = !sentence.empty() && sentence.front() == '$';
    if (!starts) {
        return out;
    }
    const size_t star = sentence.find('*', 1);
    if (star == std::string_view::npos) {
        return out;
    }
    const std::string_view body = sentence.substr(1, star - 1);
    size_t start = 0;
    bool more = true;
    while (more) {
        const size_t comma = body.find(',', start);
        more = comma != std::string_view::npos;
        std::string_view field;
        if (more) {
            field = body.substr(start, comma - start);
        } else {
            field = body.substr(start);
        }
        out.emplace_back(field);
        start = comma + 1;
    }
    return out;
}

Formatter formatter(std::string_view address) {
    if (address.size() != 5) {
        return Formatter::OTHER;
    }
    const std::string_view kind = address.substr(2);
    if (kind == "GGA") {
        return Formatter::GGA;
    }
    if (kind == "RMC") {
        return Formatter::RMC;
    }
    return Formatter::OTHER;
}

std::optional<double> to_degrees(std::string_view ddmm, std::string_view hemisphere) {
    const bool north_south = hemisphere == "N" || hemisphere == "S";
    const bool east_west = hemisphere == "E" || hemisphere == "W";
    if (!north_south && !east_west) {
        return std::nullopt;
    }
    const std::string text(ddmm);
    double value = 0.0;
    const bool parsed = parse_number(text, value);
    if (!parsed || value < 0.0) {
        return std::nullopt;
    }
    // The receiver writes degrees and minutes as one number, "4717.11399"
    // being 47 degrees 17.11399 minutes; the hundreds are the degrees.
    const double whole_degrees = std::floor(value / 100.0);
    const double minutes = value - whole_degrees * 100.0;
    if (minutes >= 60.0) {
        return std::nullopt;
    }
    const double degrees = whole_degrees + minutes / 60.0;
    const double limit = north_south ? 90.0 : 180.0;
    if (degrees > limit) {
        return std::nullopt;
    }
    const bool negative = hemisphere == "S" || hemisphere == "W";
    return negative ? -degrees : degrees;
}

float knots_to_mps(double knots) {
    return static_cast<float>(knots * kMetersPerSecondPerKnot);
}

std::optional<Gga> parse_gga(const std::vector<std::string>& f) {
    if (f.size() < kGgaFields) {
        return std::nullopt;
    }
    Gga gga{};
    gga.utc_time = f[kGgaTime];
    const bool quality_ok = parse_count(f[kGgaQuality], 0, 9, gga.fix_quality);
    if (!quality_ok) {
        return std::nullopt;
    }
    const bool satellites_ok = parse_count(f[kGgaSatellites], 0, 99, gga.satellites);
    if (!satellites_ok) {
        return std::nullopt;
    }
    const Position position = read_position(f, kGgaLatitude, kGgaLongitude, gga.latitude_deg, gga.longitude_deg);
    if (position == Position::MALFORMED) {
        return std::nullopt;
    }
    const bool altitude_present = !f[kGgaAltitude].empty();
    if (altitude_present) {
        double altitude = 0.0;
        const bool ok = parse_number(f[kGgaAltitude], altitude);
        if (!ok) {
            return std::nullopt;
        }
        gga.altitude_m = static_cast<float>(altitude);
    }
    gga.has_position = position == Position::PRESENT && altitude_present;
    // A receiver never reports a fix it cannot place; a sentence that does
    // is corrupt, not a fix with a missing field.
    const bool fix_without_position = gga.fix_quality >= 1 && !gga.has_position;
    if (fix_without_position) {
        return std::nullopt;
    }
    return gga;
}

std::optional<Rmc> parse_rmc(const std::vector<std::string>& f) {
    if (f.size() < kRmcFields) {
        return std::nullopt;
    }
    Rmc rmc{};
    rmc.utc_time = f[kRmcTime];
    // A string_view: its comparisons are noexcept, so these lines carry no
    // exception edge on GCC (the coverage style rule).
    const std::string_view status = f[kRmcStatus];
    const bool known_status = status == "A" || status == "V" || status.empty();
    if (!known_status) {
        return std::nullopt;
    }
    rmc.valid = status == "A";
    const Position position = read_position(f, kRmcLatitude, kRmcLongitude, rmc.latitude_deg, rmc.longitude_deg);
    if (position == Position::MALFORMED) {
        return std::nullopt;
    }
    rmc.has_position = position == Position::PRESENT;
    const bool speed_present = !f[kRmcSpeedKnots].empty();
    if (speed_present) {
        double knots = 0.0;
        const bool ok = parse_number(f[kRmcSpeedKnots], knots);
        if (!ok || knots < 0.0) {
            return std::nullopt;
        }
        rmc.speed_mps = knots_to_mps(knots);
    }
    const bool course_present = !f[kRmcCourse].empty();
    if (course_present) {
        double course = 0.0;
        const bool ok = parse_number(f[kRmcCourse], course);
        if (!ok || course < 0.0 || course > 360.0) {
            return std::nullopt;
        }
        rmc.course_deg = static_cast<float>(course);
    }
    const bool fix_without_position = rmc.valid && !rmc.has_position;
    if (fix_without_position) {
        return std::nullopt;
    }
    return rmc;
}

}  // namespace nmea
