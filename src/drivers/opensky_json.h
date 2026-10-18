#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Text to typed values for the ADS-B source (REQ-SENS-008, REQ-CFG-004):
// the OpenSky Network's states response, its token response, and the
// credentials file. opensky_json.cpp is the one translation unit that
// includes nlohmann/json, as telemetry_config.cpp is for yaml-cpp. Nothing
// here throws: the parser is told not to, and every field is type-checked
// before it is read, so a malformed document is a value naming the problem
// (FTS-FM-001 FAULT-016, FAULT-017), and no exception edge reaches the
// coverage record. No message ever carries a value from the credentials
// file or a token: a problem names a key or a path.
namespace opensky_json {

// One row of "states", by the service's indices (0 icao24, 1 callsign,
// 3 time_position, 4 last_contact, 5 longitude, 6 latitude, 7
// baro_altitude, 8 on_ground, 9 velocity, 10 true_track, 11 vertical_rate,
// 13 geo_altitude). Every optional field may be null in the service's
// data, and reads as absent; a field of the wrong type reads as absent
// too. Units are the service's, SI already: metres, metres per second,
// degrees clockwise from true north.
struct AircraftState {
    std::string icao24;
    std::string callsign;                    // trimmed; empty when null
    std::optional<int64_t> time_position;    // unix seconds of the last position report
    int64_t last_contact = 0;
    std::optional<double> longitude;
    std::optional<double> latitude;
    std::optional<float> baro_altitude_m;    // the aircraft's pressure altitude: the frame's gps_pressure_altitude_m since format version 3
    bool on_ground = false;
    std::optional<float> velocity_mps;
    std::optional<float> true_track_deg;
    std::optional<float> vertical_rate_mps;  // positive climbing: the frame's gps_vertical_rate_mps since format version 3
    std::optional<float> geo_altitude_m;     // GNSS height above the WGS-84 ellipsoid
};

// A states response: the server's time (the vectors cover the second
// before it) and the rows that parsed. A row that is not an array, has
// fewer than kMinStateElements elements, or whose address or on-ground
// flag is of the wrong type is skipped and counted (FAULT-016): the rest
// of the response is used. "states" null is a response with no aircraft.
constexpr size_t kMinStateElements = 14;   // index 13, geo_altitude, is the last field read; the category (17) exists only with extended=1

struct StatesDocument {
    int64_t time = 0;
    std::vector<AircraftState> states;
    size_t rows_skipped = 0;
};

struct StatesParse {
    bool ok = false;
    StatesDocument document;
    std::string problem;   // when !ok: what the document is not (FAULT-017)
};

StatesParse parse_states(const std::string& body);

// The token endpoint's answer: the bearer token and its lifetime in
// seconds, kDefaultExpiresInS when the field is absent (the service's
// documented default, which its own client assumes too).
constexpr int64_t kDefaultExpiresInS = 1800;

struct TokenParse {
    bool ok = false;
    std::string access_token;
    int64_t expires_in_s = 0;
    std::string problem;   // names the key, never a value
};

TokenParse parse_token(const std::string& body);

// The credentials file: the service's own format, {"clientId": ...,
// "clientSecret": ...}, outside the repository (REQ-CFG-004).
struct Credentials {
    std::string client_id;
    std::string client_secret;
};

struct CredentialsLoad {
    bool ok = false;
    Credentials credentials;
    std::string problem;   // names the path and the key, never a value
};

// `name` labels the problem ("<name>: missing key clientId").
CredentialsLoad parse_credentials(const std::string& text, const std::string& name);

// Reads and parses the file. Refused by name: a missing file, a directory,
// a file this process cannot read, a file readable by group or others
// (the key-file rule: a secret other users can read is not a secret), and
// every problem parse_credentials reports.
CredentialsLoad load_credentials_file(const std::filesystem::path& path);

// Where the file is looked for without --adsb-credentials:
// <home>/.config/flight_telemetry/opensky_credentials.json, or nothing
// without a home (null or empty), when the flag is required.
std::optional<std::filesystem::path> default_credentials_path(const char* home);

}  // namespace opensky_json
