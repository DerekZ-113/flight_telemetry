#include "drivers/opensky_json.h"

#include <sys/stat.h>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace opensky_json {

namespace {

using json = nlohmann::json;

// A number at `index`, or nothing when the element is null, absent or of
// another type: the service's nulls are per field, so each field is read
// on its own.
std::optional<double> number_at(const json& row, size_t index) {
    if (index >= row.size()) {
        return std::nullopt;
    }
    const json& element = row[index];
    if (!element.is_number()) {
        return std::nullopt;
    }
    return element.get<double>();
}

std::optional<float> number_at_f(const json& row, size_t index) {
    const std::optional<double> value = number_at(row, index);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return static_cast<float>(*value);
}

std::optional<int64_t> integer_at(const json& row, size_t index) {
    if (index >= row.size()) {
        return std::nullopt;
    }
    const json& element = row[index];
    if (!element.is_number_integer()) {
        return std::nullopt;
    }
    return element.get<int64_t>();
}

std::string trimmed(const std::string& text) {
    const size_t begin = text.find_first_not_of(' ');
    if (begin == std::string::npos) {
        return "";
    }
    const size_t end = text.find_last_not_of(' ');
    return text.substr(begin, end - begin + 1);
}

// One row, or nothing when the row is not what a state vector is.
std::optional<AircraftState> parse_row(const json& row) {
    const bool shaped = row.is_array() && row.size() >= kMinStateElements;
    if (!shaped) {
        return std::nullopt;
    }
    const bool identified = row[0].is_string() && row[8].is_boolean();
    if (!identified) {
        return std::nullopt;
    }
    AircraftState state;
    state.icao24 = row[0].get<std::string>();
    if (row[1].is_string()) {
        state.callsign = trimmed(row[1].get<std::string>());
    }
    state.time_position = integer_at(row, 3);
    state.last_contact = integer_at(row, 4).value_or(0);
    state.longitude = number_at(row, 5);
    state.latitude = number_at(row, 6);
    state.baro_altitude_m = number_at_f(row, 7);
    state.on_ground = row[8].get<bool>();
    state.velocity_mps = number_at_f(row, 9);
    state.true_track_deg = number_at_f(row, 10);
    state.vertical_rate_mps = number_at_f(row, 11);
    state.geo_altitude_m = number_at_f(row, 13);
    return state;
}

std::string read_whole(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

StatesParse parse_states(const std::string& body) {
    StatesParse parse;
    const json document = json::parse(body, nullptr, false);
    if (document.is_discarded()) {
        parse.problem = "the body is not JSON";
        return parse;
    }
    if (!document.is_object()) {
        parse.problem = "the document is not an object";
        return parse;
    }
    const bool has_time = document.contains("time") && document["time"].is_number_integer();
    if (!has_time) {
        parse.problem = "time is missing or not a whole number";
        return parse;
    }
    parse.document.time = document["time"].get<int64_t>();
    if (!document.contains("states") || document["states"].is_null()) {
        parse.ok = true;   // no aircraft in the box: the service's "null, not an empty array"
        return parse;
    }
    const json& states = document["states"];
    if (!states.is_array()) {
        parse.problem = "states is neither an array nor null";
        return parse;
    }
    for (const json& row : states) {  // LCOV_EXCL_EXCEPTION_BR_LINE: a range-for over nlohmann's iterators, which can throw; its decision is tested, not measured (FTS-VR-001 §1.3)
        const std::optional<AircraftState> state = parse_row(row);
        if (state.has_value()) {
            parse.document.states.push_back(*state);
        } else {
            parse.document.rows_skipped++;
        }
    }
    parse.ok = true;
    return parse;
}

TokenParse parse_token(const std::string& body) {
    TokenParse parse;
    const json document = json::parse(body, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        parse.problem = "the token response is not a JSON object";
        return parse;
    }
    const bool has_token = document.contains("access_token") && document["access_token"].is_string();
    if (!has_token) {
        parse.problem = "the token response has no access_token";
        return parse;
    }
    parse.access_token = document["access_token"].get<std::string>();
    if (parse.access_token.empty()) {
        parse.problem = "the token response's access_token is empty";
        return parse;
    }
    parse.expires_in_s = kDefaultExpiresInS;
    if (document.contains("expires_in") && document["expires_in"].is_number_integer()) {
        parse.expires_in_s = document["expires_in"].get<int64_t>();
    }
    parse.ok = true;
    return parse;
}

CredentialsLoad parse_credentials(const std::string& text, const std::string& name) {
    CredentialsLoad load;
    const json document = json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        load.problem = name + ": not a JSON object";
        return load;
    }
    for (const char* key : {"clientId", "clientSecret"}) {
        const bool present = document.contains(key) && document[key].is_string();
        if (!present) {
            load.problem = name + ": missing key " + key;
            return load;
        }
        const bool empty = document[key].get<std::string>().empty();
        if (empty) {
            load.problem = name + ": empty key " + key;
            return load;
        }
    }
    load.credentials.client_id = document["clientId"].get<std::string>();
    load.credentials.client_secret = document["clientSecret"].get<std::string>();
    load.ok = true;
    return load;
}

CredentialsLoad load_credentials_file(const std::filesystem::path& path) {
    CredentialsLoad load;
    const std::string name = path.string();
    struct stat info {};
    const int stat_result = ::stat(path.c_str(), &info);
    if (stat_result != 0) {
        load.problem = "cannot read credentials file " + name + ": no such file";
        return load;
    }
    if (!S_ISREG(info.st_mode)) {
        load.problem = "cannot read credentials file " + name + ": not a regular file";
        return load;
    }
    const bool readable_by_others = (info.st_mode & (S_IRGRP | S_IROTH)) != 0;
    if (readable_by_others) {
        load.problem = "credentials file " + name + " is readable by other users; make it mode 600";
        return load;
    }
    std::ifstream probe(path, std::ios::binary);
    if (!probe) {
        load.problem = "cannot read credentials file " + name + ": permission denied";
        return load;
    }
    probe.close();
    return parse_credentials(read_whole(path), name);
}

std::optional<std::filesystem::path> default_credentials_path(const char* home) {
    if (home == nullptr || home[0] == '\0') {
        return std::nullopt;
    }
    return std::filesystem::path(home) / ".config" / "flight_telemetry" / "opensky_credentials.json";
}

}  // namespace opensky_json
