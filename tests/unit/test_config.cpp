// Tests for src/config/telemetry_config (REQ-CFG-001, REQ-CFG-002,
// REQ-CFG-003). Test card TC-016, steps 1 through 9. The shipped file is
// the baseline: every test but the file-level ones edits its text in
// memory and loads the result, so a problem's exact wording, its key, and
// its place in the report are all pinned. Messages are compared whole: an
// operator reads them, and a key reported under the wrong path is a bug.

#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "config/telemetry_config.h"
#include "logging/log_format.h"

namespace {

namespace fs = std::filesystem;

const fs::path kShipped = TELEMETRY_SHIPPED_CONFIG;

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string shipped_text() {
    return read_file(kShipped);
}

std::string joined(const std::vector<std::string>& problems) {
    std::string all;
    for (const std::string& p : problems) {
        all += "\n  " + p;
    }
    return all;
}

// `text` with its one occurrence of `from` replaced by `to`.
std::string edit(const std::string& text, const std::string& from, const std::string& to) {
    const size_t at = text.find(from);
    EXPECT_NE(at, std::string::npos) << "not in the text: " << from;
    EXPECT_EQ(text.find(from, at + 1), std::string::npos) << "ambiguous: " << from;
    return text.substr(0, at) + to + text.substr(at + from.size());
}

// `text` without the one line that contains `marker` (comment included).
std::string without_line(const std::string& text, const std::string& marker) {
    const size_t at = text.find(marker);
    EXPECT_NE(at, std::string::npos) << "not in the text: " << marker;
    const size_t line_start = text.rfind('\n', at) + 1;
    const size_t line_end = text.find('\n', at);
    return text.substr(0, line_start) + text.substr(line_end + 1);
}

ConfigLoad load_edited(const std::string& from, const std::string& to) {
    return load_config_text(edit(shipped_text(), from, to), "test.yaml");
}

void expect_one_problem(const ConfigLoad& load, const std::string& expected) {
    EXPECT_FALSE(load.ok);
    ASSERT_EQ(load.problems.size(), 1u) << joined(load.problems);
    EXPECT_EQ(load.problems[0], expected);
}

void expect_accepted(const ConfigLoad& load) {
    EXPECT_TRUE(load.ok) << joined(load.problems);
    EXPECT_TRUE(load.problems.empty()) << joined(load.problems);
}

}  // namespace

// TC-016 step 1, the drift test. The shipped file loads clean and every
// field equals the default a bare struct carries: the file is what the
// binary runs with, the defaults are what the unit tests run with, and
// they must be the same numbers. Floats compared exactly: each default
// is written in the file with digits that round-trip. (REQ-CFG-001)
TEST(ConfigTest, ShippedFileEqualsDefaults) {
    const ConfigLoad load = load_config_file(kShipped);
    ASSERT_TRUE(load.ok) << joined(load.problems);
    EXPECT_EQ(load.text, shipped_text());
    const TelemetryConfig d{};
    const TelemetryConfig& c = load.config;

    EXPECT_EQ(c.timing.period_ms, d.timing.period_ms);
    EXPECT_EQ(c.sensors.i2c_device, d.sensors.i2c_device);
    EXPECT_EQ(c.sensors.barometer.address, d.sensors.barometer.address);
    EXPECT_EQ(c.sensors.imu.address, d.sensors.imu.address);
    EXPECT_EQ(c.sensors.imu.sample_rate_hz, d.sensors.imu.sample_rate_hz);
    EXPECT_EQ(c.sensors.gps.enabled, d.sensors.gps.enabled);
    EXPECT_EQ(c.sensors.gps.device, d.sensors.gps.device);
    EXPECT_EQ(c.sensors.gps.baud_rate, d.sensors.gps.baud_rate);
    EXPECT_EQ(c.fault.i2c_timeout_ms, d.fault.i2c_timeout_ms);
    EXPECT_EQ(c.fault.uart_timeout_ms, d.fault.uart_timeout_ms);
    EXPECT_EQ(c.fault.stuck_count, d.fault.stuck_count);
    EXPECT_EQ(c.fault.recovery_count, d.fault.recovery_count);
    EXPECT_EQ(c.fault.pressure_min_hpa, d.fault.pressure_min_hpa);
    EXPECT_EQ(c.fault.pressure_max_hpa, d.fault.pressure_max_hpa);
    EXPECT_EQ(c.fault.temperature_min_c, d.fault.temperature_min_c);
    EXPECT_EQ(c.fault.temperature_max_c, d.fault.temperature_max_c);
    EXPECT_EQ(c.processing.baro_measurement_variance_m2, d.processing.baro_measurement_variance_m2);
    EXPECT_EQ(c.processing.gps_measurement_variance_m2, d.processing.gps_measurement_variance_m2);
    EXPECT_EQ(c.processing.initial_velocity_variance, d.processing.initial_velocity_variance);
    EXPECT_EQ(c.processing.accel_noise_variance, d.processing.accel_noise_variance);
    EXPECT_EQ(c.processing.complementary_alpha, d.processing.complementary_alpha);
    EXPECT_EQ(c.processing.sea_level_pressure_hpa, d.processing.sea_level_pressure_hpa);
    EXPECT_EQ(c.logging.directory, d.logging.directory);
    EXPECT_EQ(c.logging.max_file_bytes, d.logging.max_file_bytes);
    EXPECT_EQ(c.transport.zmq.enabled, d.transport.zmq.enabled);
    EXPECT_EQ(c.transport.zmq.endpoint, d.transport.zmq.endpoint);
    EXPECT_EQ(c.transport.udp.enabled, d.transport.udp.enabled);
    EXPECT_EQ(c.transport.udp.host, d.transport.udp.host);
    EXPECT_EQ(c.transport.udp.port, d.transport.udp.port);
    EXPECT_EQ(c.simulation.seed, d.simulation.seed);
    EXPECT_EQ(c.simulation.gps_interval_ms, d.simulation.gps_interval_ms);
    EXPECT_EQ(c.simulation.noise.pressure_sigma_hpa, d.simulation.noise.pressure_sigma_hpa);
    EXPECT_EQ(c.simulation.noise.temperature_sigma_c, d.simulation.noise.temperature_sigma_c);
    EXPECT_EQ(c.simulation.noise.accel_sigma_mps2, d.simulation.noise.accel_sigma_mps2);
    EXPECT_EQ(c.simulation.noise.gyro_sigma_dps, d.simulation.noise.gyro_sigma_dps);
    EXPECT_EQ(c.simulation.noise.latitude_sigma_deg, d.simulation.noise.latitude_sigma_deg);
    EXPECT_EQ(c.simulation.noise.longitude_sigma_deg, d.simulation.noise.longitude_sigma_deg);
    EXPECT_EQ(c.simulation.noise.gps_altitude_sigma_m, d.simulation.noise.gps_altitude_sigma_m);

    // The optional block (REQ-CFG-001): the shipped file carries it, so
    // its ten keys equal AdsbConfig's defaults; a bare TelemetryConfig has
    // no block, the recorded-configuration case.
    EXPECT_FALSE(d.adsb.has_value());
    ASSERT_TRUE(c.adsb.has_value());
    const AdsbConfig a{};
    EXPECT_EQ(c.adsb->box.lat_min, a.box.lat_min);
    EXPECT_EQ(c.adsb->box.lat_max, a.box.lat_max);
    EXPECT_EQ(c.adsb->box.lon_min, a.box.lon_min);
    EXPECT_EQ(c.adsb->box.lon_max, a.box.lon_max);
    EXPECT_EQ(c.adsb->reference.latitude, a.reference.latitude);
    EXPECT_EQ(c.adsb->reference.longitude, a.reference.longitude);
    EXPECT_EQ(c.adsb->poll_interval_ms, a.poll_interval_ms);
    EXPECT_EQ(c.adsb->switch_margin_m, a.switch_margin_m);
    EXPECT_EQ(c.adsb->gone_after_polls, a.gone_after_polls);
    EXPECT_EQ(c.adsb->timeout_ms, a.timeout_ms);
}

// The same text through the in-memory entry point, which every test
// below uses, keeps the text verbatim for the record (REQ-CFG-003).
TEST(ConfigTest, ValidTextLoadsWithNoProblems) {
    const std::string text = shipped_text();
    const ConfigLoad load = load_config_text(text, "test.yaml");
    expect_accepted(load);
    EXPECT_EQ(load.text, text);
}

// TC-016 step 2. A path with no file behind it is one problem that names
// the path, so the operator knows which file the binary looked for.
// (REQ-CFG-002)
TEST(ConfigTest, MissingFileIsReported) {
    const fs::path path = fs::temp_directory_path() / "ftlg_config_does_not_exist.yaml";
    const ConfigLoad load = load_config_file(path);
    expect_one_problem(load, "cannot read configuration file " + path.string() + ": no such file");
    EXPECT_TRUE(load.text.empty());
}

TEST(ConfigTest, DirectoryPathIsReported) {
    const fs::path path = fs::temp_directory_path();
    const ConfigLoad load = load_config_file(path);
    expect_one_problem(load, "cannot read configuration file " + path.string() + ": not a regular file");
}

// A file that exists but cannot be opened reports the OS's reason.
TEST(ConfigTest, UnreadableFileIsReported) {
    if (::geteuid() == 0) {
        GTEST_SKIP() << "root can read a file with no permission bits; the branch needs an ordinary user";
    }
    const fs::path path = fs::temp_directory_path() / "ftlg_config_unreadable.yaml";
    {
        std::ofstream out(path);
        out << shipped_text();
    }
    ASSERT_EQ(::chmod(path.c_str(), 0), 0);
    const ConfigLoad load = load_config_file(path);
    ::chmod(path.c_str(), 0600);
    fs::remove(path);
    expect_one_problem(load, "cannot read configuration file " + path.string() + ": Permission denied");
}

// TC-016 step 3. A document yaml-cpp cannot parse is one problem with
// the file name, a line and a column, counted from one. The flow
// sequence opened on line 3 is never closed; where the parser gives up
// is its business, so the line is bounded below rather than pinned.
// (REQ-CFG-002)
TEST(ConfigTest, SyntaxErrorNamesLineAndColumn) {
    const ConfigLoad load = load_config_text("timing:\n  period_ms: 20\nsensors: [unclosed\n", "test.yaml");
    EXPECT_FALSE(load.ok);
    ASSERT_EQ(load.problems.size(), 1u) << joined(load.problems);
    const std::string& problem = load.problems[0];
    ASSERT_EQ(problem.rfind("test.yaml:", 0), 0u) << problem;
    const int line = std::stoi(problem.substr(std::string("test.yaml:").size()));
    EXPECT_GE(line, 3) << problem;
    EXPECT_NE(problem.find(": syntax error: "), std::string::npos) << problem;
}

TEST(ConfigTest, EmptyOrNonMappingDocumentIsRefused) {
    expect_one_problem(load_config_text("", "test.yaml"), "test.yaml: the document must be a mapping of sections");
    expect_one_problem(load_config_text("- a\n- b\n", "test.yaml"),
                       "test.yaml: the document must be a mapping of sections");
}

// TC-016 step 4. A section that is absent is one problem, not one per
// key it would have held.
TEST(ConfigTest, MissingSectionIsReportedOnce) {
    const std::string text = edit(shipped_text(), "timing:\n  period_ms: 20\n", "");
    expect_one_problem(load_config_text(text, "test.yaml"), "timing: missing section");
}

TEST(ConfigTest, SectionOfWrongTypeIsReportedOnce) {
    expect_one_problem(load_edited("timing:\n  period_ms: 20", "timing: 5"),
                       "timing: must be a mapping (got \"5\")");
    expect_one_problem(load_edited("timing:\n  period_ms: 20", "timing: [1, 2]"),
                       "timing: must be a mapping (got a sequence)");
    expect_one_problem(load_edited("port: 5556", "port: 5556\n  extra:\n    k: 1"),
                       "transport.extra: unknown key");
}

TEST(ConfigTest, MissingKeyIsNamed) {
    // One key of each kind the schema reads: a whole number, a switch,
    // text, a number, a positive number, a fraction.
    const std::pair<const char*, const char*> cases[] = {
        {"stuck_count:", "fault.stuck_count: missing"},
        {"    enabled: false", "sensors.gps.enabled: missing"},   // the first such line is the receiver's
        {"i2c_device:", "sensors.i2c_device: missing"},
        {"pressure_min_hpa:", "fault.pressure_min_hpa: missing"},
        {"accel_noise_variance:", "processing.accel_noise_variance: missing"},
        {"complementary_alpha:", "processing.complementary_alpha: missing"},
    };
    for (const auto& [marker, expected] : cases) {
        const std::string text = without_line(shipped_text(), marker);
        expect_one_problem(load_config_text(text, "test.yaml"), expected);
    }
}

// Every section the schema has is required, at the root and nested.
// Renaming one leaves two problems: the section missing, and the
// renamed key unknown, in that order.
TEST(ConfigTest, EverySectionIsRequired) {
    // The marker is the key's line start without its colon; renaming it
    // keeps the block's contents, which then hang off the unknown key.
    const std::pair<const char*, const char*> cases[] = {
        {"\ntiming", "timing"},
        {"\nsensors", "sensors"},
        {"\n  barometer", "sensors.barometer"},
        {"\n  imu", "sensors.imu"},
        {"\n  gps", "sensors.gps"},
        {"\nfault", "fault"},
        {"\nprocessing", "processing"},
        {"\nlogging", "logging"},
        {"\ntransport", "transport"},
        {"\n  zmq", "transport.zmq"},
        {"\n  udp", "transport.udp"},
        {"\nsimulation", "simulation"},
        {"\n  noise", "simulation.noise"},
        {"\n  box", "adsb.box"},               // required inside the optional block
        {"\n  reference", "adsb.reference"},
    };
    for (const auto& [marker, path] : cases) {
        const std::string text = edit(shipped_text(), std::string(marker) + ":", std::string(marker) + "x:");
        const ConfigLoad load = load_config_text(text, "test.yaml");
        EXPECT_FALSE(load.ok);
        const std::vector<std::string> expected = {std::string(path) + ": missing section",
                                                   std::string(path) + "x: unknown key"};
        EXPECT_EQ(load.problems, expected) << joined(load.problems);
    }
}

// Unknown keys are named with their path, at the root and inside a
// section, and a key that is not a plain word is reported as such.
TEST(ConfigTest, UnknownKeyIsNamed) {
    expect_one_problem(load_config_text(shipped_text() + "\nextra: 1\n", "test.yaml"), "extra: unknown key");
    expect_one_problem(load_edited("  period_ms: 20", "  period_ms: 20\n  perid_ms: 20"),
                       "timing.perid_ms: unknown key");
    expect_one_problem(load_edited("  period_ms: 20", "  period_ms: 20\n  ? [a, b]\n  : 1"),
                       "timing.(a non-scalar key): unknown key");
}

// A key given twice is reported once, and the first value is the one
// the (refused) configuration would have carried.
TEST(ConfigTest, DuplicateKeyIsNamed) {
    const ConfigLoad load = load_edited("  period_ms: 20", "  period_ms: 20\n  period_ms: 30");
    expect_one_problem(load, "timing.period_ms: duplicate key");
    EXPECT_EQ(load.config.timing.period_ms, 20u);
}

// TC-016 step 8. Every problem in one report, in the file's order
// (REQ-CFG-002 says "each"); ok is false.
TEST(ConfigTest, AllProblemsAreCollectedInSchemaOrder) {
    std::string text = shipped_text();
    text = edit(text, "period_ms: 20", "period_ms: 0");
    text = edit(text, "stuck_count: 10", "stuck_count: 1");
    text = edit(text, "port: 5556", "port: 70000");
    const ConfigLoad load = load_config_text(text, "test.yaml");
    EXPECT_FALSE(load.ok);
    const std::vector<std::string> expected = {
        "timing.period_ms: must be 20 to 1000 (got 0)",
        "fault.stuck_count: must be at least 2 (got 1)",
        "transport.udp.port: must be 1 to 65535 (got 70000)",
    };
    EXPECT_EQ(load.problems, expected) << joined(load.problems);
}

// TC-016 step 5, the whole-number rule: decimal or 0x hex, a sign
// allowed, nothing else. 010 is refused rather than read as octal, 1.5
// is not a whole number, an empty value and a bare 0x are nothing, a
// mapping is not a number, and more digits than 64 bits hold is refused
// rather than wrapped.
TEST(ConfigTest, IntegerRuleAcceptsDecimalAndHexOnly) {
    ConfigLoad load = load_edited("address: 0x76", "address: 0x77");
    expect_accepted(load);
    EXPECT_EQ(load.config.sensors.barometer.address, 0x77);
    load = load_edited("address: 0x76", "address: 119");
    expect_accepted(load);
    EXPECT_EQ(load.config.sensors.barometer.address, 0x77);
    load = load_edited("period_ms: 20", "period_ms: +30");
    expect_accepted(load);
    EXPECT_EQ(load.config.timing.period_ms, 30u);

    expect_one_problem(load_edited("period_ms: 20", "period_ms: 010"),
                       "timing.period_ms: must be a whole number (got \"010\")");
    expect_one_problem(load_edited("period_ms: 20", "period_ms: 0020"),
                       "timing.period_ms: must be a whole number (got \"0020\")");
    expect_one_problem(load_edited("port: 5556", "port: 0"), "transport.udp.port: must be 1 to 65535 (got 0)");
    expect_one_problem(load_edited("period_ms: 20", "period_ms: 1.5"),
                       "timing.period_ms: must be a whole number (got \"1.5\")");
    expect_one_problem(load_edited("period_ms: 20", "period_ms: abc"),
                       "timing.period_ms: must be a whole number (got \"abc\")");
    expect_one_problem(load_edited("period_ms: 20", "period_ms:"),
                       "timing.period_ms: must be a whole number (got nothing)");
    expect_one_problem(load_edited("period_ms: 20", "period_ms: 0x"),
                       "timing.period_ms: must be a whole number (got \"0x\")");
    expect_one_problem(load_edited("period_ms: 20", "period_ms: {a: 1}"),
                       "timing.period_ms: must be a whole number (got a mapping)");
    expect_one_problem(load_edited("period_ms: 20", "period_ms: 99999999999999999999"),
                       "timing.period_ms: must be a whole number (got \"99999999999999999999\")");
    expect_one_problem(load_edited("period_ms: 20", "period_ms: -5"),
                       "timing.period_ms: must be 20 to 1000 (got -5)");
    expect_one_problem(load_edited("period_ms: 20", "period_ms: \"\""),
                       "timing.period_ms: must be a whole number (got \"\")");
    expect_one_problem(load_edited("period_ms: 20", "period_ms: 5"),
                       "timing.period_ms: must be 20 to 1000 (got 5)");
    expect_one_problem(load_edited("address: 0x76", "address: 0xZZ"),
                       "sensors.barometer.address: must be a whole number (got \"0xZZ\")");
    load = load_edited("address: 0x76", "address: 0X77");
    expect_accepted(load);
    EXPECT_EQ(load.config.sensors.barometer.address, 0x77);
}

// The number rule: strtod must read the whole text and the value must be
// finite, in YAML's spelling (.inf, .nan) or strtod's own (inf, nan).
TEST(ConfigTest, NumberRuleRequiresFinite) {
    const ConfigLoad load = load_edited("accel_noise_variance: 1.0", "accel_noise_variance: 1e-3");
    expect_accepted(load);
    EXPECT_FLOAT_EQ(load.config.processing.accel_noise_variance, 1e-3f);
    for (const char* bad : {".inf", ".nan", "inf", "nan", "abc", "1.5x"}) {
        expect_one_problem(load_edited("accel_noise_variance: 1.0", std::string("accel_noise_variance: ") + bad),
                           std::string("processing.accel_noise_variance: must be a finite number (got \"") + bad + "\")");
    }
    expect_one_problem(load_edited("accel_noise_variance: 1.0", "accel_noise_variance: [1]"),
                       "processing.accel_noise_variance: must be a finite number (got a sequence)");
    expect_one_problem(load_edited("accel_noise_variance: 1.0", "accel_noise_variance: \"\""),
                       "processing.accel_noise_variance: must be a finite number (got \"\")");
    expect_one_problem(load_edited("complementary_alpha: 0.98", "complementary_alpha: abc"),
                       "processing.complementary_alpha: must be a finite number (got \"abc\")");
}

// The switch rule: exactly true or false. YAML 1.1's yes/no/on/off and a
// bare 1 are refused, because a reader of the file should not have to
// know which spellings a library honours.
TEST(ConfigTest, BoolRuleIsTrueOrFalseOnly) {
    const ConfigLoad load = load_edited("  zmq:\n    enabled: false", "  zmq:\n    enabled: true");
    expect_accepted(load);
    EXPECT_TRUE(load.config.transport.zmq.enabled);
    for (const char* bad : {"yes", "on", "1", "True"}) {
        expect_one_problem(load_edited("  zmq:\n    enabled: false", std::string("  zmq:\n    enabled: ") + bad),
                           std::string("transport.zmq.enabled: must be true or false (got \"") + bad + "\")");
    }
    expect_one_problem(load_edited("  zmq:\n    enabled: false", "  zmq:\n    enabled:"),
                       "transport.zmq.enabled: must be true or false (got nothing)");
}

// The text rule: a scalar that is not empty.
TEST(ConfigTest, StringRuleRejectsEmpty) {
    expect_one_problem(load_edited("i2c_device: /dev/i2c-1", "i2c_device: \"\""),
                       "sensors.i2c_device: must not be empty");
    expect_one_problem(load_edited("i2c_device: /dev/i2c-1", "i2c_device:"),
                       "sensors.i2c_device: must be text (got nothing)");
    expect_one_problem(load_edited("directory: logs", "directory: [logs]"),
                       "logging.directory: must be text (got a sequence)");
}

// TC-016 step 6, every range at both edges, one test per rule.

TEST(ConfigTest, PeriodRange) {
    expect_one_problem(load_edited("period_ms: 20", "period_ms: 19"), "timing.period_ms: must be 20 to 1000 (got 19)");
    expect_one_problem(load_edited("period_ms: 20", "period_ms: 1001"), "timing.period_ms: must be 20 to 1000 (got 1001)");
    expect_accepted(load_edited("period_ms: 20", "period_ms: 1000"));
    EXPECT_EQ(load_edited("period_ms: 20", "period_ms: 1000").config.timing.period_ms, 1000u);
}

TEST(ConfigTest, AddressRange) {
    expect_one_problem(load_edited("address: 0x76", "address: 0x07"),
                       "sensors.barometer.address: must be 0x08 to 0x77 (got 0x07)");
    expect_one_problem(load_edited("address: 0x68", "address: 0x78"),
                       "sensors.imu.address: must be 0x08 to 0x77 (got 0x78)");
    expect_accepted(load_edited("address: 0x76", "address: 0x08"));
    expect_accepted(load_edited("address: 0x68", "address: 0x77"));
}

TEST(ConfigTest, ImuRateMustBeAnExactDivisor) {
    expect_one_problem(load_edited("sample_rate_hz: 100", "sample_rate_hz: 30"),
                       "sensors.imu.sample_rate_hz: must be an exact divisor of 1000 Hz (got 30)");
    expect_one_problem(load_edited("sample_rate_hz: 100", "sample_rate_hz: 3"),
                       "sensors.imu.sample_rate_hz: must be 4 to 100 (got 3)");
    expect_one_problem(load_edited("sample_rate_hz: 100", "sample_rate_hz: 101"),
                       "sensors.imu.sample_rate_hz: must be 4 to 100 (got 101)");
    expect_accepted(load_edited("sample_rate_hz: 100", "sample_rate_hz: 50"));
    // 4 Hz is reachable only with a loop no faster than 4 Hz (the cross rule below).
    std::string text = edit(shipped_text(), "sample_rate_hz: 100", "sample_rate_hz: 4");
    text = edit(text, "period_ms: 20", "period_ms: 1000");
    expect_accepted(load_config_text(text, "test.yaml"));
}

TEST(ConfigTest, TimeoutsAtLeastOne) {
    expect_one_problem(load_edited("i2c_timeout_ms: 500", "i2c_timeout_ms: 0"),
                       "fault.i2c_timeout_ms: must be at least 1 (got 0)");
    expect_one_problem(load_edited("uart_timeout_ms: 2000", "uart_timeout_ms: 0"),
                       "fault.uart_timeout_ms: must be at least 1 (got 0)");
}

TEST(ConfigTest, StuckCountAtLeastTwo) {
    expect_one_problem(load_edited("stuck_count: 10", "stuck_count: 1"), "fault.stuck_count: must be at least 2 (got 1)");
    const ConfigLoad load = load_edited("stuck_count: 10", "stuck_count: 2");
    expect_accepted(load);
    EXPECT_EQ(load.config.fault.stuck_count, 2);
}

TEST(ConfigTest, RecoveryCountAtLeastOne) {
    expect_one_problem(load_edited("recovery_count: 5", "recovery_count: 0"),
                       "fault.recovery_count: must be at least 1 (got 0)");
    expect_accepted(load_edited("recovery_count: 5", "recovery_count: 1"));
}

// Each minimum below its maximum, reported on the maximum's key; a
// minimum that is not a number is reported once, as a type problem, and
// the ordering rule stays quiet because it has no operand.
TEST(ConfigTest, PressureAndTemperatureBoundsOrdered) {
    expect_one_problem(load_edited("pressure_max_hpa: 1100", "pressure_max_hpa: 300"),
                       "fault.pressure_max_hpa: must exceed fault.pressure_min_hpa (300 against 300)");
    expect_one_problem(load_edited("temperature_min_c: -40", "temperature_min_c: 90"),
                       "fault.temperature_max_c: must exceed fault.temperature_min_c (85 against 90)");
    expect_one_problem(load_edited("pressure_min_hpa: 300", "pressure_min_hpa: abc"),
                       "fault.pressure_min_hpa: must be a finite number (got \"abc\")");
    expect_one_problem(load_edited("pressure_max_hpa: 1100", "pressure_max_hpa: abc"),
                       "fault.pressure_max_hpa: must be a finite number (got \"abc\")");
    expect_one_problem(load_edited("temperature_min_c: -40", "temperature_min_c: abc"),
                       "fault.temperature_min_c: must be a finite number (got \"abc\")");
    expect_one_problem(load_edited("temperature_max_c: 85", "temperature_max_c: abc"),
                       "fault.temperature_max_c: must be a finite number (got \"abc\")");
    expect_accepted(load_edited("temperature_min_c: -40", "temperature_min_c: 84.5"));
}

TEST(ConfigTest, VariancesPositive) {
    for (const char* key : {"baro_measurement_variance_m2: 3.0", "gps_measurement_variance_m2: 25.0",
                            "initial_velocity_variance: 1.0", "accel_noise_variance: 1.0"}) {
        const std::string line = key;
        const std::string name = line.substr(0, line.find(':'));
        expect_one_problem(load_edited(line, name + ": 0"), "processing." + name + ": must be greater than 0 (got 0)");
    }
}

TEST(ConfigTest, AlphaInclusiveRange) {
    expect_accepted(load_edited("complementary_alpha: 0.98", "complementary_alpha: 0"));
    expect_accepted(load_edited("complementary_alpha: 0.98", "complementary_alpha: 1"));
    expect_one_problem(load_edited("complementary_alpha: 0.98", "complementary_alpha: -0.1"),
                       "processing.complementary_alpha: must be 0 to 1 (got -0.1)");
    expect_one_problem(load_edited("complementary_alpha: 0.98", "complementary_alpha: 1.1"),
                       "processing.complementary_alpha: must be 0 to 1 (got 1.1)");
}

TEST(ConfigTest, SeaLevelPositive) {
    expect_one_problem(load_edited("sea_level_pressure_hpa: 1013.25", "sea_level_pressure_hpa: 0"),
                       "processing.sea_level_pressure_hpa: must be greater than 0 (got 0)");
}

TEST(ConfigTest, MaxFileBytesAtLeastOneRecord) {
    static_assert(kFrameMessageBytes == 137, "the message below names the record size");   // 121 until format version 3
    expect_one_problem(load_edited("max_file_bytes: 1048576", "max_file_bytes: 136"),
                       "logging.max_file_bytes: must be at least 137 (got 136)");
    expect_accepted(load_edited("max_file_bytes: 1048576", "max_file_bytes: 137"));
}

// The UDP host is checked with the publisher's own test, and whether or
// not the transport is enabled: every value is validated.
TEST(ConfigTest, UdpHostAndPort) {
    expect_one_problem(load_edited("host: 127.0.0.1", "host: localhost"),
                       "transport.udp.host: must be an IPv4 dotted-quad address (got \"localhost\")");
    expect_one_problem(load_edited("host: 127.0.0.1", "host: 256.1.1.1"),
                       "transport.udp.host: must be an IPv4 dotted-quad address (got \"256.1.1.1\")");
    expect_one_problem(load_edited("host: 127.0.0.1", "host: [127, 0, 0, 1]"),
                       "transport.udp.host: must be text (got a sequence)");
    expect_one_problem(load_edited("port: 5556", "port: 0"), "transport.udp.port: must be 1 to 65535 (got 0)");
    expect_one_problem(load_edited("port: 5556", "port: 65536"), "transport.udp.port: must be 1 to 65535 (got 65536)");
    expect_accepted(load_edited("port: 5556", "port: 1"));
    expect_accepted(load_edited("port: 5556", "port: 65535"));
    expect_one_problem(load_edited("endpoint: tcp://127.0.0.1:5555", "endpoint: \"\""),
                       "transport.zmq.endpoint: must not be empty");
}

TEST(ConfigTest, SeedRange) {
    expect_one_problem(load_edited("seed: 42", "seed: 4294967296"),
                       "simulation.seed: must be 0 to 4294967295 (got 4294967296)");
    const ConfigLoad load = load_edited("seed: 42", "seed: 4294967295");
    expect_accepted(load);
    EXPECT_EQ(load.config.simulation.seed, 4294967295u);
}

TEST(ConfigTest, GpsIntervalAtLeastOne) {
    expect_one_problem(load_edited("gps_interval_ms: 1000", "gps_interval_ms: 0"),
                       "simulation.gps_interval_ms: must be at least 1 (got 0)");
}

// Every sigma positive: std::normal_distribution requires it.
TEST(ConfigTest, SigmasPositive) {
    for (const char* key : {"pressure_sigma_hpa: 0.2", "temperature_sigma_c: 0.1", "accel_sigma_mps2: 0.05",
                            "gyro_sigma_dps: 0.1", "latitude_sigma_deg: 0.00002", "longitude_sigma_deg: 0.00002",
                            "gps_altitude_sigma_m: 2.0"}) {
        const std::string line = key;
        const std::string name = line.substr(0, line.find(':'));
        expect_one_problem(load_edited(line, name + ": 0"), "simulation.noise." + name + ": must be greater than 0 (got 0)");
    }
    expect_one_problem(load_edited("gyro_sigma_dps: 0.1", "gyro_sigma_dps: -0.1"),
                       "simulation.noise.gyro_sigma_dps: must be greater than 0 (got -0.1)");
}

// TC-016 steps 4 to 6, every key. The rules above are each proven on one
// key; this proves that every key of the file is read by its rule: the
// key missing, a sequence where a scalar is required, text the rule
// refuses, and for a whole number one past each edge are each named
// under the key's dotted path, one problem at a time. gcov counts a
// template's branches once per instantiation, and read_int is one
// function per field type (uint64_t, uint8_t, int, uint32_t), read_positive
// one for float and one for double, so a fault proven on one key leaves
// the same decision untaken in the other instantiations: Entry 11's
// coverage preview found ten such branches. One key per type is the
// floor; every key is the requirement's own word, "each". (REQ-CFG-002)
TEST(ConfigTest, EveryKeyReportsEachKindOfFault) {
    enum class Rule { Whole, Number, Positive, Fraction, Switch, Text, Range, NonNegative };
    struct Key {
        const char* marker;   // text found once in the shipped file, starting in the key's line
        const char* value;    // the value as written there
        const char* path;     // the key's dotted path
        Rule rule;
        const char* below = nullptr;   // one past the lower edge, as written (Whole)
        const char* below_report = nullptr;
        const char* above = nullptr;   // one past the upper edge (bounded Whole)
        const char* above_report = nullptr;
        const char* missing_report = nullptr;   // when removing the key empties its section
    };
    const Key keys[] = {
        {"period_ms: 20", "20", "timing.period_ms", Rule::Whole, "19", "must be 20 to 1000 (got 19)", "1001",
         "must be 20 to 1000 (got 1001)", "timing: must be a mapping (got nothing)"},
        {"i2c_device: /dev/i2c-1", "/dev/i2c-1", "sensors.i2c_device", Rule::Text},
        {"address: 0x76", "0x76", "sensors.barometer.address", Rule::Whole, "0x07", "must be 0x08 to 0x77 (got 0x07)",
         "0x78", "must be 0x08 to 0x77 (got 0x78)", "sensors.barometer: must be a mapping (got nothing)"},
        {"address: 0x68", "0x68", "sensors.imu.address", Rule::Whole, "0x07", "must be 0x08 to 0x77 (got 0x07)", "0x78",
         "must be 0x08 to 0x77 (got 0x78)"},
        {"sample_rate_hz: 100", "100", "sensors.imu.sample_rate_hz", Rule::Whole, "3", "must be 4 to 100 (got 3)", "101",
         "must be 4 to 100 (got 101)"},
        {"enabled: false\n    device", "false", "sensors.gps.enabled", Rule::Switch},
        {"device: /dev/serial0", "/dev/serial0", "sensors.gps.device", Rule::Text},
        {"baud_rate: 9600", "9600", "sensors.gps.baud_rate", Rule::Whole, "4799", "must be 4800 to 115200 (got 4799)",
         "115201", "must be 4800 to 115200 (got 115201)"},
        {"i2c_timeout_ms: 500", "500", "fault.i2c_timeout_ms", Rule::Whole, "0", "must be at least 1 (got 0)"},
        {"uart_timeout_ms: 2000", "2000", "fault.uart_timeout_ms", Rule::Whole, "0", "must be at least 1 (got 0)"},
        {"stuck_count: 10", "10", "fault.stuck_count", Rule::Whole, "1", "must be at least 2 (got 1)"},
        {"recovery_count: 5", "5", "fault.recovery_count", Rule::Whole, "0", "must be at least 1 (got 0)"},
        {"pressure_min_hpa: 300", "300", "fault.pressure_min_hpa", Rule::Number},
        {"pressure_max_hpa: 1100", "1100", "fault.pressure_max_hpa", Rule::Number},
        {"temperature_min_c: -40", "-40", "fault.temperature_min_c", Rule::Number},
        {"temperature_max_c: 85", "85", "fault.temperature_max_c", Rule::Number},
        {"baro_measurement_variance_m2: 3.0", "3.0", "processing.baro_measurement_variance_m2", Rule::Positive},
        {"gps_measurement_variance_m2: 25.0", "25.0", "processing.gps_measurement_variance_m2", Rule::Positive},
        {"initial_velocity_variance: 1.0", "1.0", "processing.initial_velocity_variance", Rule::Positive},
        {"accel_noise_variance: 1.0", "1.0", "processing.accel_noise_variance", Rule::Positive},
        {"complementary_alpha: 0.98", "0.98", "processing.complementary_alpha", Rule::Fraction},
        {"sea_level_pressure_hpa: 1013.25", "1013.25", "processing.sea_level_pressure_hpa", Rule::Positive},
        {"directory: logs", "logs", "logging.directory", Rule::Text},
        {"max_file_bytes: 1048576", "1048576", "logging.max_file_bytes", Rule::Whole, "120",
         "must be at least 137 (got 120)"},
        {"enabled: false\n    endpoint", "false", "transport.zmq.enabled", Rule::Switch},
        {"endpoint: tcp://127.0.0.1:5555", "tcp://127.0.0.1:5555", "transport.zmq.endpoint", Rule::Text},
        {"enabled: false\n    host", "false", "transport.udp.enabled", Rule::Switch},
        {"host: 127.0.0.1", "127.0.0.1", "transport.udp.host", Rule::Text},
        {"port: 5556", "5556", "transport.udp.port", Rule::Whole, "0", "must be 1 to 65535 (got 0)", "65536",
         "must be 1 to 65535 (got 65536)"},
        {"seed: 42", "42", "simulation.seed", Rule::Whole, "-1", "must be 0 to 4294967295 (got -1)", "4294967296",
         "must be 0 to 4294967295 (got 4294967296)"},
        {"gps_interval_ms: 1000", "1000", "simulation.gps_interval_ms", Rule::Whole, "0", "must be at least 1 (got 0)"},
        {"pressure_sigma_hpa: 0.2", "0.2", "simulation.noise.pressure_sigma_hpa", Rule::Positive},
        {"temperature_sigma_c: 0.1", "0.1", "simulation.noise.temperature_sigma_c", Rule::Positive},
        {"accel_sigma_mps2: 0.05", "0.05", "simulation.noise.accel_sigma_mps2", Rule::Positive},
        {"gyro_sigma_dps: 0.1", "0.1", "simulation.noise.gyro_sigma_dps", Rule::Positive},
        {"latitude_sigma_deg: 0.00002", "0.00002", "simulation.noise.latitude_sigma_deg", Rule::Positive},
        {"longitude_sigma_deg: 0.00002", "0.00002", "simulation.noise.longitude_sigma_deg", Rule::Positive},
        {"gps_altitude_sigma_m: 2.0", "2.0", "simulation.noise.gps_altitude_sigma_m", Rule::Positive},
        // The optional block's ten keys, required when the block is present (REQ-CFG-001).
        {"lat_min: 37.45", "37.45", "adsb.box.lat_min", Rule::Range, "-90.1", "must be -90 to 90 (got -90.1)", "90.1",
         "must be -90 to 90 (got 90.1)"},
        {"lat_max: 37.75", "37.75", "adsb.box.lat_max", Rule::Range, "-90.1", "must be -90 to 90 (got -90.1)", "90.1",
         "must be -90 to 90 (got 90.1)"},
        {"lon_min: -122.55", "-122.55", "adsb.box.lon_min", Rule::Range, "-180.1", "must be -180 to 180 (got -180.1)",
         "180.1", "must be -180 to 180 (got 180.1)"},
        {"lon_max: -122.10", "-122.10", "adsb.box.lon_max", Rule::Range, "-180.1", "must be -180 to 180 (got -180.1)",
         "180.1", "must be -180 to 180 (got 180.1)"},
        {"latitude: 37.6213", "37.6213", "adsb.reference.latitude", Rule::Range, "-90.1", "must be -90 to 90 (got -90.1)",
         "90.1", "must be -90 to 90 (got 90.1)"},
        {"longitude: -122.3790", "-122.3790", "adsb.reference.longitude", Rule::Range, "-180.1",
         "must be -180 to 180 (got -180.1)", "180.1", "must be -180 to 180 (got 180.1)"},
        {"poll_interval_ms: 5000", "5000", "adsb.poll_interval_ms", Rule::Whole, "999", "must be 1000 to 3600000 (got 999)",
         "3600001", "must be 1000 to 3600000 (got 3600001)"},
        {"switch_margin_m: 2000", "2000", "adsb.switch_margin_m", Rule::NonNegative, "-1", "must be at least 0 (got -1)"},
        {"gone_after_polls: 2", "2", "adsb.gone_after_polls", Rule::Whole, "0", "must be at least 1 (got 0)"},
        {"timeout_ms: 20000", "20000", "adsb.timeout_ms", Rule::Whole, "0", "must be at least 1 (got 0)"},
    };
    EXPECT_EQ(std::size(keys), 48u);   // the file's key count, as the documents state it: 38, and the optional block's 10

    const std::string shipped = shipped_text();
    for (const Key& key : keys) {
        SCOPED_TRACE(key.path);
        const std::string path = key.path;
        const std::string marker = key.marker;
        // The marker with its value replaced: "seed: 42" becomes "seed: [1]".
        const auto with_value = [&](const char* value) {
            const std::string from = std::string(": ") + key.value;
            const size_t at = marker.find(from);
            EXPECT_NE(at, std::string::npos) << marker;
            return edit(shipped, marker, marker.substr(0, at) + ": " + value + marker.substr(at + from.size()));
        };
        const auto expect = [&](const std::string& text, const std::string& report) {
            expect_one_problem(load_config_text(text, "test.yaml"), report);
        };

        const char* missing = key.missing_report;
        expect(without_line(shipped, marker), missing != nullptr ? std::string(missing) : path + ": missing");

        std::string asks;   // what the rule asks for, in its own words
        switch (key.rule) {
            case Rule::Whole: asks = "must be a whole number"; break;
            case Rule::Number: case Rule::Positive: case Rule::Fraction: case Rule::Range: case Rule::NonNegative:
                asks = "must be a finite number"; break;
            case Rule::Switch: asks = "must be true or false"; break;
            case Rule::Text: asks = "must be text"; break;
        }
        expect(with_value("[1]"), path + ": " + asks + " (got a sequence)");

        switch (key.rule) {
            case Rule::Whole:
                expect(with_value("abc"), path + ": must be a whole number (got \"abc\")");
                expect(with_value(key.below), path + ": " + key.below_report);
                if (key.above != nullptr) {
                    expect(with_value(key.above), path + ": " + key.above_report);
                }
                break;
            case Rule::Number:
                expect(with_value(".nan"), path + ": must be a finite number (got \".nan\")");
                break;
            case Rule::Positive:
                expect(with_value(".nan"), path + ": must be a finite number (got \".nan\")");
                expect(with_value("0"), path + ": must be greater than 0 (got 0)");
                break;
            case Rule::Fraction:
                expect(with_value(".nan"), path + ": must be a finite number (got \".nan\")");
                expect(with_value("-0.1"), path + ": must be 0 to 1 (got -0.1)");
                expect(with_value("1.1"), path + ": must be 0 to 1 (got 1.1)");
                break;
            case Rule::Range:
                expect(with_value(".nan"), path + ": must be a finite number (got \".nan\")");
                expect(with_value(key.below), path + ": " + key.below_report);
                expect(with_value(key.above), path + ": " + key.above_report);
                break;
            case Rule::NonNegative:
                expect(with_value(".nan"), path + ": must be a finite number (got \".nan\")");
                expect(with_value(key.below), path + ": " + key.below_report);
                break;
            case Rule::Switch:
                expect(with_value("yes"), path + ": must be true or false (got \"yes\")");
                break;
            case Rule::Text:
                expect(with_value("\"\""), path + ": must not be empty");
                break;
        }
    }
}

// TC-016 step 7, the rules between sections.

TEST(ConfigTest, UartTimeoutMustExceedGpsInterval) {
    expect_one_problem(load_edited("gps_interval_ms: 1000", "gps_interval_ms: 2000"),
                       "fault.uart_timeout_ms: must exceed simulation.gps_interval_ms (2000 against 2000)");
    expect_accepted(load_edited("gps_interval_ms: 1000", "gps_interval_ms: 1999"));
}

// TC-016 step 7, continued (2026-10-15): the ADS-B channel's timeout must
// outlast the poll interval as the UART timeout outlasts the fix period.
TEST(ConfigTest, AdsbTimeoutMustExceedPollInterval) {
    expect_one_problem(load_edited("timeout_ms: 20000", "timeout_ms: 5000"),
                       "adsb.timeout_ms: must exceed adsb.poll_interval_ms (5000 against 5000)");
    expect_accepted(load_edited("timeout_ms: 20000", "timeout_ms: 5001"));
}

// TC-016 step 6, continued: the box's edges are ordered, as the pressure
// bounds are, a rule inside the section.
TEST(ConfigTest, AdsbBoxEdgesMustBeOrdered) {
    expect_one_problem(load_edited("lat_max: 37.75", "lat_max: 37.45"),
                       "adsb.box.lat_max: must exceed lat_min (37.45 against 37.45)");
    expect_one_problem(load_edited("lon_max: -122.10", "lon_max: -122.55"),
                       "adsb.box.lon_max: must exceed lon_min (-122.55 against -122.55)");
    expect_accepted(load_edited("lat_max: 37.75", "lat_max: 37.4501"));
}

// TC-016 step 21. The adsb block is the one a file may omit (REQ-CFG-001):
// a configuration recorded before the block existed replays as recorded,
// every other key read as before. Present, it must be a mapping and every
// key inside it is required like any other.
TEST(ConfigTest, AdsbBlockIsOptional) {
    const std::string shipped = shipped_text();
    const size_t at = shipped.find("\nadsb:");
    ASSERT_NE(at, std::string::npos);
    const std::string without_block = shipped.substr(0, at + 1);
    const ConfigLoad without = load_config_text(without_block, "test.yaml");
    expect_accepted(without);
    EXPECT_FALSE(without.config.adsb.has_value());
    EXPECT_EQ(without.config.timing.period_ms, 20u);
    EXPECT_EQ(without.config.simulation.noise.gps_altitude_sigma_m, 2.0f);

    expect_one_problem(load_config_text(without_block + "adsb: 5\n", "test.yaml"),
                       "adsb: must be a mapping (got \"5\")");
    expect_one_problem(load_config_text(without_line(shipped, "gone_after_polls: 2"), "test.yaml"),
                       "adsb.gone_after_polls: missing");
}

// TC-018 step 19: the receiver's baud rate is one of the six termios names.
TEST(ConfigTest, GpsBaudRateMustBeStandard) {
    expect_one_problem(load_edited("baud_rate: 9600", "baud_rate: 9601"),
                       "sensors.gps.baud_rate: must be one of 4800, 9600, 19200, 38400, 57600, 115200 (got 9601)");
    for (const char* rate : {"4800", "9600", "19200", "38400", "57600", "115200"}) {
        expect_accepted(load_edited("baud_rate: 9600", std::string("baud_rate: ") + rate));
    }
}

TEST(ConfigTest, ImuRateMustCoverTheLoopRate) {
    expect_one_problem(load_edited("sample_rate_hz: 100", "sample_rate_hz: 20"),
                       "sensors.imu.sample_rate_hz: must be at least the processing rate, 1000 / timing.period_ms (20 against 50)");
    std::string text = edit(shipped_text(), "sample_rate_hz: 100", "sample_rate_hz: 20");
    text = edit(text, "period_ms: 20", "period_ms: 50");
    expect_accepted(load_config_text(text, "test.yaml"));
}

// TC-016 step 9. The naming contract behind the configuration record
// (REQ-CFG-003): a prefix is what stands before _NNN.bin, three digits
// or more, and the record sits beside the log under that prefix.
TEST(ConfigTest, LogPrefixAndRecordedConfigPath) {
    EXPECT_EQ(log_prefix("logs/telemetry_007.bin"), std::optional<std::string>("telemetry"));
    EXPECT_EQ(log_prefix("logs/replay_000.bin"), std::optional<std::string>("replay"));
    EXPECT_EQ(log_prefix("/tmp/run/live_1234.bin"), std::optional<std::string>("live"));
    EXPECT_EQ(log_prefix("logs/x.bin"), std::nullopt);
    EXPECT_EQ(log_prefix("logs/telemetry.bin"), std::nullopt);
    EXPECT_EQ(log_prefix("logs/telemetry_07.bin"), std::nullopt);
    EXPECT_EQ(log_prefix("logs/telemetry_abc.bin"), std::nullopt);
    EXPECT_EQ(log_prefix("logs/telemetry_000.binx"), std::nullopt);
    EXPECT_EQ(log_prefix("logs/telemetry_000"), std::nullopt);
    EXPECT_EQ(log_prefix("logs/_000.bin"), std::nullopt);
    EXPECT_EQ(recorded_config_path("logs/telemetry_007.bin"), std::optional<fs::path>("logs/telemetry_config.yaml"));
    EXPECT_EQ(recorded_config_path("telemetry_000.bin"), std::optional<fs::path>("telemetry_config.yaml"));
    EXPECT_EQ(recorded_config_path("logs/x.bin"), std::nullopt);
}
