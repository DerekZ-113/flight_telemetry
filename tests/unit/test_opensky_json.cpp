// Tests for src/drivers/opensky_json (REQ-SENS-008, REQ-CFG-004; FTS-FM-001
// FAULT-016, FAULT-017). Test card TC-027 steps 1 through 4: the credentials
// file by name and never by content, the state vector field by field with
// every null read as absent, and every malformed document refused without
// an exception. The responses are the synthetic ones of opensky_examples.h.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "drivers/opensky_json.h"
#include "opensky_examples.h"
#include "scratch_dir.h"

namespace fs = std::filesystem;
namespace ex = opensky_examples;
using namespace opensky_json;

namespace {

// A directory of this test's own under the temporary directory, removed
// after it; the credentials files written here never leave it.
class CredentialsDir {
public:
    fs::path write(const std::string& name, const std::string& text, fs::perms mode) {
        const fs::path path = dir_ / name;
        std::ofstream(path, std::ios::binary) << text;
        fs::permissions(path, mode, fs::perm_options::replace);
        return path;
    }
    const fs::path& dir() const { return dir_; }

private:
    ScratchDir scratch_{"creds"};   // scratch_dir.h: unique per object, removed with it
    fs::path dir_ = scratch_.path();
};

constexpr fs::perms kOwnerOnly = fs::perms::owner_read | fs::perms::owner_write;
constexpr fs::perms kWorldReadable = kOwnerOnly | fs::perms::group_read | fs::perms::others_read;

}  // namespace

// TC-027 step 4. The example response, every index to its field and unit,
// the callsign trimmed; an 18-element row parses as a 17-element one does;
// a null leaves its field empty and the rest held.
TEST(OpenSkyJsonTest, ParsesTheExampleStateVectors) {
    const StatesParse parse = parse_states(ex::kStatesResponse);
    ASSERT_TRUE(parse.ok) << parse.problem;
    EXPECT_EQ(parse.document.time, ex::kTime);
    ASSERT_EQ(parse.document.states.size(), 6u);
    EXPECT_EQ(parse.document.rows_skipped, 0u);

    const AircraftState& first = parse.document.states[0];
    EXPECT_EQ(first.icao24, "000001");
    EXPECT_EQ(first.callsign, "TEST001");
    EXPECT_EQ(first.time_position, std::optional<int64_t>(1759999998));
    EXPECT_EQ(first.last_contact, 1759999999);
    EXPECT_EQ(first.longitude, std::optional<double>(-122.3289));
    EXPECT_EQ(first.latitude, std::optional<double>(37.6002));
    EXPECT_EQ(first.baro_altitude_m, std::optional<float>(450.0f));
    EXPECT_FALSE(first.on_ground);
    EXPECT_EQ(first.velocity_mps, std::optional<float>(72.0f));
    EXPECT_EQ(first.true_track_deg, std::optional<float>(298.0f));
    EXPECT_EQ(first.vertical_rate_mps, std::optional<float>(-3.5f));
    EXPECT_EQ(first.geo_altitude_m, std::optional<float>(480.0f));

    const AircraftState& extended = parse.document.states[2];   // 18 elements, the category last
    EXPECT_EQ(extended.icao24, "000003");
    EXPECT_EQ(extended.geo_altitude_m, std::optional<float>(1030.0f));

    const AircraftState& grounded = parse.document.states[4];
    EXPECT_TRUE(grounded.on_ground);
    EXPECT_FALSE(grounded.baro_altitude_m.has_value());
    EXPECT_FALSE(grounded.geo_altitude_m.has_value());
    EXPECT_EQ(grounded.velocity_mps, std::optional<float>(5.0f));

    const AircraftState& incomplete = parse.document.states[5];
    EXPECT_FALSE(incomplete.geo_altitude_m.has_value());
    EXPECT_EQ(incomplete.latitude, std::optional<double>(37.5917));
}

// TC-027 step 4. Each nullable field null, and one of the wrong type, reads
// as absent on its own; the row still parses (FAULT-016 is judged per
// field, not per row).
TEST(OpenSkyJsonTest, EveryNullableFieldReadsAsAbsent) {
    const std::string nulls =
        "{\"time\":1760000000,\"states\":["
        "[\"000001\",null,\"United States\",null,1759999999,null,null,null,false,null,null,null,null,null,null,false,0]]}";
    const StatesParse parse = parse_states(nulls);
    ASSERT_TRUE(parse.ok) << parse.problem;
    ASSERT_EQ(parse.document.states.size(), 1u);
    const AircraftState& s = parse.document.states[0];
    EXPECT_EQ(s.icao24, "000001");
    EXPECT_EQ(s.callsign, "");
    EXPECT_FALSE(s.time_position.has_value());
    EXPECT_FALSE(s.longitude.has_value());
    EXPECT_FALSE(s.latitude.has_value());
    EXPECT_FALSE(s.baro_altitude_m.has_value());
    EXPECT_FALSE(s.velocity_mps.has_value());
    EXPECT_FALSE(s.true_track_deg.has_value());
    EXPECT_FALSE(s.vertical_rate_mps.has_value());
    EXPECT_FALSE(s.geo_altitude_m.has_value());

    const std::string wrong_type =
        "{\"time\":1760000000,\"states\":["
        "[\"000001\",\"TEST001\",\"United States\",1759999998,1759999999,-122.3289,\"north\",450.0,false,72.0,298.0,-3.5,null,480.0,\"1200\",false,0]]}";
    const StatesParse typed = parse_states(wrong_type);
    ASSERT_TRUE(typed.ok) << typed.problem;
    ASSERT_EQ(typed.document.states.size(), 1u);
    EXPECT_FALSE(typed.document.states[0].latitude.has_value());
    EXPECT_EQ(typed.document.states[0].longitude, std::optional<double>(-122.3289));
}

// TC-027 step 4. An empty sky is null, not an empty array, and a document
// without the key means the same.
TEST(OpenSkyJsonTest, NullStatesIsAnEmptySky) {
    const StatesParse parse = parse_states(ex::kEmptyStatesResponse);
    ASSERT_TRUE(parse.ok) << parse.problem;
    EXPECT_TRUE(parse.document.states.empty());
    EXPECT_EQ(parse.document.time, ex::kTime);
    const StatesParse absent = parse_states("{\"time\":1760000000}");
    ASSERT_TRUE(absent.ok) << absent.problem;
    EXPECT_TRUE(absent.document.states.empty());
}

// TC-027 step 4 (FAULT-016). A row that is not a state vector is skipped
// and counted; the rest of the response is used.
TEST(OpenSkyJsonTest, ShortAndNonArrayRowsAreSkippedAndCounted) {
    const std::string rows =
        "{\"time\":1760000000,\"states\":["
        "[\"000001\",\"TEST001\",\"United States\",1759999998,1759999999,-122.3289,37.6002,450.0,false,72.0,298.0,-3.5,null],"   // 13 elements
        "{\"icao24\":\"000002\"},"                                                                                               // an object
        "[7,\"TEST003\",\"United States\",1759999998,1759999999,-122.3289,37.6002,450.0,false,72.0,298.0,-3.5,null,480.0],"      // the address a number
        "[\"000004\",\"TEST004\",\"United States\",1759999998,1759999999,-122.3289,37.6002,450.0,\"no\",72.0,298.0,-3.5,null,480.0]," // on_ground not a boolean
        "[\"000005\",\"TEST005 \",\"United States\",1759999998,1759999999,-122.3289,37.6002,450.0,false,72.0,298.0,-3.5,null,480.0]"
        "]}";
    const StatesParse parse = parse_states(rows);
    ASSERT_TRUE(parse.ok) << parse.problem;
    EXPECT_EQ(parse.document.rows_skipped, 4u);
    ASSERT_EQ(parse.document.states.size(), 1u);
    EXPECT_EQ(parse.document.states[0].icao24, "000005");
}

// TC-027 step 4 (FAULT-017). What the schema says the document is not,
// each refused by name and none by an exception.
TEST(OpenSkyJsonTest, MalformedDocumentsAreRefusedByName) {
    EXPECT_EQ(parse_states("{\"time\":1760000000,\"states\":[[\"000001\"").problem, "the body is not JSON");
    EXPECT_EQ(parse_states("[1, 2]").problem, "the document is not an object");
    EXPECT_EQ(parse_states("{\"states\":[]}").problem, "time is missing or not a whole number");
    EXPECT_EQ(parse_states("{\"time\":\"now\",\"states\":[]}").problem, "time is missing or not a whole number");
    EXPECT_EQ(parse_states("{\"time\":1760000000,\"states\":\"many\"}").problem, "states is neither an array nor null");
    EXPECT_FALSE(parse_states("").ok);
}

// TC-027 steps 6 and 10. The token response's two fields, the lifetime
// defaulting to 1800 s when absent, and a refusal that never echoes the
// token.
TEST(OpenSkyJsonTest, TokenResponseYieldsTokenAndExpiry) {
    const TokenParse parsed = parse_token(ex::kTokenResponse);
    ASSERT_TRUE(parsed.ok) << parsed.problem;
    EXPECT_EQ(parsed.access_token, "test-token");
    EXPECT_EQ(parsed.expires_in_s, 1800);

    const TokenParse no_expiry = parse_token(ex::kTokenResponseWithoutExpiry);
    ASSERT_TRUE(no_expiry.ok) << no_expiry.problem;
    EXPECT_EQ(no_expiry.expires_in_s, kDefaultExpiresInS);

    for (const char* bad : {"nope", "[]", "{}", "{\"access_token\":7}", "{\"access_token\":\"\"}"}) {
        const TokenParse refused = parse_token(bad);
        EXPECT_FALSE(refused.ok) << bad;
        EXPECT_FALSE(refused.problem.empty()) << bad;
        EXPECT_EQ(refused.problem.find("test-token"), std::string::npos) << bad;
    }
}

// TC-027 steps 1 and 2. The credentials file's two keys; every problem
// names the path and the key and never a value.
TEST(OpenSkyJsonTest, CredentialsParseAndProblemsNameTheKeyNeverTheValue) {
    const CredentialsLoad load = parse_credentials(ex::kCredentials, "creds.json");
    ASSERT_TRUE(load.ok) << load.problem;
    EXPECT_EQ(load.credentials.client_id, "test-client");
    EXPECT_EQ(load.credentials.client_secret, "test-secret");

    const std::pair<const char*, const char*> cases[] = {
        {"{\"clientId\":\"test-client\"}", "creds.json: missing key clientSecret"},
        {"{\"clientSecret\":\"test-secret\"}", "creds.json: missing key clientId"},
        {"{\"clientId\":\"test-client\",\"clientSecret\":\"\"}", "creds.json: empty key clientSecret"},
        {"{\"clientId\":\"test-client\",\"clientSecret\":5}", "creds.json: missing key clientSecret"},
        {"[]", "creds.json: not a JSON object"},
        {"{\"clientId\":", "creds.json: not a JSON object"},
    };
    for (const auto& [text, problem] : cases) {
        const CredentialsLoad refused = parse_credentials(text, "creds.json");
        EXPECT_FALSE(refused.ok) << text;
        EXPECT_EQ(refused.problem, problem);
        EXPECT_EQ(refused.problem.find("test-client"), std::string::npos) << text;
        EXPECT_EQ(refused.problem.find("test-secret"), std::string::npos) << text;
    }
}

// TC-027 steps 1 and 2 (REQ-CFG-004). The file by name: read when it is a
// regular file only its owner can read; refused as missing, a directory,
// readable by others, or malformed, each by path and problem, with the
// secret's text absent from every message.
TEST(OpenSkyJsonTest, CredentialsFileIsReadByNameAndRefusedByName) {
    CredentialsDir dir;
    const fs::path good = dir.write("good.json", ex::kCredentials, kOwnerOnly);
    const CredentialsLoad load = load_credentials_file(good);
    ASSERT_TRUE(load.ok) << load.problem;
    EXPECT_EQ(load.credentials.client_id, "test-client");
    EXPECT_EQ(load.credentials.client_secret, "test-secret");

    const fs::path missing = dir.dir() / "missing.json";
    const CredentialsLoad none = load_credentials_file(missing);
    EXPECT_FALSE(none.ok);
    EXPECT_EQ(none.problem, "cannot read credentials file " + missing.string() + ": no such file");

    const CredentialsLoad directory = load_credentials_file(dir.dir());
    EXPECT_FALSE(directory.ok);
    EXPECT_EQ(directory.problem, "cannot read credentials file " + dir.dir().string() + ": not a regular file");

    const fs::path open = dir.write("open.json", ex::kCredentials, kWorldReadable);
    const CredentialsLoad world = load_credentials_file(open);
    EXPECT_FALSE(world.ok);
    EXPECT_EQ(world.problem, "credentials file " + open.string() + " is readable by other users; make it mode 600");
    EXPECT_EQ(world.problem.find("test-secret"), std::string::npos);

    const fs::path broken = dir.write("broken.json", "{\"clientId\":\"test-client\"}", kOwnerOnly);
    const CredentialsLoad malformed = load_credentials_file(broken);
    EXPECT_FALSE(malformed.ok);
    EXPECT_EQ(malformed.problem, broken.string() + ": missing key clientSecret");
}

// TC-027 step 3. The default path under a home, and none without one.
TEST(OpenSkyJsonTest, DefaultCredentialsPathNeedsAHome) {
    EXPECT_EQ(default_credentials_path("/home/x"),
              std::optional<fs::path>(fs::path("/home/x/.config/flight_telemetry/opensky_credentials.json")));
    EXPECT_FALSE(default_credentials_path(nullptr).has_value());
    EXPECT_FALSE(default_credentials_path("").has_value());
}
