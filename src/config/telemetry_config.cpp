#include "config/telemetry_config.h"

#include <arpa/inet.h>   // inet_pton: the same test UdpPublisher applies to a destination

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#include <yaml-cpp/yaml.h>

#include "drivers/mpu6050.h"
#include "drivers/serial_port.h"
#include "logging/log_format.h"

// Coverage note. yaml-cpp throws; this file catches at one place, the
// parse, and every other call into the library is a type query or a
// scalar read that cannot throw. GCC still records an exception-unwind
// edge on any line that calls a function that might, an edge no test
// can take, and lcov's branch filter drops a line's edges only when the
// line holds nothing it reads as a conditional: ?, !, <, >, ==, !=, &&,
// ||, if, for, while, switch, case, after stripping strings, comments,
// ->, <<, :: and single-word template arguments (static_cast<long long>
// survives). Style here: a call sits on one statement line of its own,
// with none of those characters, into a local; the branch on the next
// line tests the local. The one loop over a YAML node is the exception
// and carries the marker (verification_results.md Entry 5 §5.6, Entry
// 11 §11.3).

namespace {

// ---- the four scalar rules -------------------------------------------------
// yaml-cpp is the document parser only. Its own scalar conversions read
// 010 as octal and accept yes/no/on/off as booleans, two traps in a file
// the SRD says must be validated naming each fault. These rules are the
// project's, printed in the file's header comment and tested one by one.

// A whole number: an optional sign, then decimal digits or 0x and hex
// digits. Nothing else, so "1.5" and "" are refused, and so is a decimal
// with a leading zero ("010"): a YAML 1.1 reader would take it as octal,
// and a number that two readers read differently has no place here.
bool parse_integer(const std::string& text, long long& out) {
    size_t start = 0;
    if (!text.empty() && (text[0] == '+' || text[0] == '-')) {
        start = 1;
    }
    const bool hex = text.size() > start + 1 && text[start] == '0' &&
                     (text[start + 1] == 'x' || text[start + 1] == 'X');
    const size_t digits = hex ? start + 2 : start;
    if (digits >= text.size()) {
        return false;   // "", "+", "0x"
    }
    if (!hex && text.size() - digits > 1 && text[digits] == '0') {
        return false;   // "010", "007": octal to some readers, refused here
    }
    for (size_t i = digits; i < text.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const bool digit = hex ? std::isxdigit(c) != 0 : std::isdigit(c) != 0;
        if (!digit) {
            return false;
        }
    }
    errno = 0;
    const long long value = std::strtoll(text.c_str(), nullptr, hex ? 16 : 10);
    if (errno == ERANGE) {
        return false;   // more digits than a 64-bit number holds
    }
    out = value;
    return true;
}

// A finite number: strtod must consume the whole string, and the
// infinities and NaN it accepts ("inf", "nan") are refused, as are
// YAML's own spellings (".inf" and ".nan" do not parse at all).
bool parse_number(const std::string& text, double& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (*end != '\0') {
        return false;
    }
    if (!std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

// Exactly "true" or "false".
bool parse_bool(const std::string& text, bool& out) {
    if (text == "true") {
        out = true;
        return true;
    }
    if (text == "false") {
        out = false;
        return true;
    }
    return false;
}

// What a node holds, for the "(got ...)" part of a message.
std::string describe(const YAML::Node& node) {
    const YAML::NodeType::value type = node.Type();
    if (type == YAML::NodeType::Scalar) {
        return "got \"" + node.Scalar() + "\"";
    }
    if (type == YAML::NodeType::Sequence) {
        return "got a sequence";
    }
    if (type == YAML::NodeType::Map) {
        return "got a mapping";
    }
    return "got nothing";
}

std::string show(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

enum class Radix { Decimal, Hex };

std::string show(long long value, Radix radix) {
    if (radix == Radix::Hex) {
        std::ostringstream out;
        out << "0x" << std::hex << std::setw(2) << std::setfill('0') << value;
        return out.str();
    }
    return std::to_string(value);
}

constexpr long long kNoMaximum = std::numeric_limits<long long>::max();

// The smallest log file that holds one record (header, tag, frame), the
// floor of logging.max_file_bytes; cast here so the call that reads the
// key carries no template bracket (the coverage note above).
constexpr long long kOneRecordBytes = static_cast<long long>(kFrameMessageBytes);

// One mapping of the document: its keys in document order, which of them
// were read, and the problem list every read appends to. Built only from
// a node that is a map, so operator[] can never throw.
class Section {
public:
    Section(const YAML::Node& node, std::string path, std::vector<std::string>& problems)
        : node_(node), path_(std::move(path)), problems_(problems) {
        // yaml-cpp keeps a repeated key as two entries and returns the
        // first on lookup, so iteration sees the repeat and reports it
        // once; the first value is the one read below.
        for (const auto& entry : node_) {   // LCOV_EXCL_EXCEPTION_BR_LINE: a range-for over yaml-cpp's iterators, which can throw; its decision is tested, not measured (FTS-VR-001 §1.3)
            const YAML::Node key = entry.first;
            const bool scalar_key = key.IsScalar();
            std::string name = "(a non-scalar key)";
            if (scalar_key) {
                name = key.Scalar();
            }
            const auto found = std::find(keys_.begin(), keys_.end(), name);
            const bool repeated = found != keys_.end();
            if (repeated) {
                problem(name, "duplicate key");
            } else {
                keys_.push_back(name);
            }
        }
    }

    // A whole number within [min, max]; `out` is written only on success.
    template <typename T>
    bool read_int(const std::string& key, long long min, long long max, T& out,
                  Radix radix = Radix::Decimal) {
        const std::optional<YAML::Node> node = value(key);
        if (!node.has_value()) {
            return false;
        }
        const bool scalar = node->IsScalar();
        long long parsed = 0;
        bool valid = false;
        if (scalar) {
            valid = parse_integer(node->Scalar(), parsed);
        }
        if (!valid) {
            problem(key, "must be a whole number (" + describe(*node) + ")");
            return false;
        }
        if (parsed < min || parsed > max) {
            problem(key, range_text(min, max, radix) + " (got " + show(parsed, radix) + ")");
            return false;
        }
        out = static_cast<T>(parsed);
        return true;
    }

    // A finite number, any value.
    template <typename T>
    bool read_number(const std::string& key, T& out) {
        double parsed = 0.0;
        const bool valid = read_finite(key, parsed);
        if (!valid) {
            return false;
        }
        out = static_cast<T>(parsed);
        return true;
    }

    // A finite number greater than zero.
    template <typename T>
    bool read_positive(const std::string& key, T& out) {
        double parsed = 0.0;
        const bool valid = read_finite(key, parsed);
        if (!valid) {
            return false;
        }
        if (parsed <= 0.0) {
            problem(key, "must be greater than 0 (got " + show(parsed) + ")");
            return false;
        }
        out = static_cast<T>(parsed);
        return true;
    }

    // A finite number from 0 to 1 inclusive.
    template <typename T>
    bool read_fraction(const std::string& key, T& out) {
        double parsed = 0.0;
        const bool valid = read_finite(key, parsed);
        if (!valid) {
            return false;
        }
        if (parsed < 0.0 || parsed > 1.0) {
            problem(key, "must be 0 to 1 (got " + show(parsed) + ")");
            return false;
        }
        out = static_cast<T>(parsed);
        return true;
    }

    // A finite number from `min` to `max` inclusive: a latitude, a
    // longitude (REQ-SENS-008's box and reference point).
    bool read_number_in(const std::string& key, double min, double max, double& out) {
        double parsed = 0.0;
        const bool valid = read_finite(key, parsed);
        if (!valid) {
            return false;
        }
        if (parsed < min || parsed > max) {
            problem(key, "must be " + show(min) + " to " + show(max) + " (got " + show(parsed) + ")");
            return false;
        }
        out = parsed;
        return true;
    }

    // A finite number at or above `min`: a distance that may be zero.
    bool read_number_at_least(const std::string& key, double min, double& out) {
        double parsed = 0.0;
        const bool valid = read_finite(key, parsed);
        if (!valid) {
            return false;
        }
        if (parsed < min) {
            problem(key, "must be at least " + show(min) + " (got " + show(parsed) + ")");
            return false;
        }
        out = parsed;
        return true;
    }

    bool read_bool(const std::string& key, bool& out) {
        const std::optional<YAML::Node> node = value(key);
        if (!node.has_value()) {
            return false;
        }
        const bool scalar = node->IsScalar();
        bool parsed = false;
        bool valid = false;
        if (scalar) {
            valid = parse_bool(node->Scalar(), parsed);
        }
        if (!valid) {
            problem(key, "must be true or false (" + describe(*node) + ")");
            return false;
        }
        out = parsed;
        return true;
    }

    // Non-empty text.
    bool read_string(const std::string& key, std::string& out) {
        const std::optional<YAML::Node> node = value(key);
        if (!node.has_value()) {
            return false;
        }
        const bool scalar = node->IsScalar();
        if (!scalar) {
            problem(key, "must be text (" + describe(*node) + ")");
            return false;
        }
        const std::string text = node->Scalar();
        if (text.empty()) {
            problem(key, "must not be empty");
            return false;
        }
        out = text;
        return true;
    }

    // A nested mapping. A missing or wrongly typed section is one problem
    // and its keys are not reported one by one.
    std::optional<Section> section(const std::string& key) {
        read_.push_back(key);
        const YAML::Node found = node_[key];
        const bool defined = found.IsDefined();
        if (!defined) {
            problem(key, "missing section");
            return std::nullopt;
        }
        const bool map = found.IsMap();
        if (!map) {
            problem(key, "must be a mapping (" + describe(found) + ")");
            return std::nullopt;
        }
        return Section(found, child_path(key), problems_);
    }

    // A block the schema allows a file to omit (the `adsb` block,
    // REQ-CFG-001): nothing, and no problem, when the key is absent; a
    // problem when it is present and not a mapping; the section
    // otherwise, every key inside it then required like any other.
    std::optional<Section> optional_section(const std::string& key) {
        read_.push_back(key);
        const YAML::Node found = node_[key];
        const bool defined = found.IsDefined();
        if (!defined) {
            return std::nullopt;
        }
        const bool map = found.IsMap();
        if (!map) {
            problem(key, "must be a mapping (" + describe(found) + ")");
            return std::nullopt;
        }
        return Section(found, child_path(key), problems_);
    }

    // Every key the schema did not ask for, in document order.
    void finish() {
        for (const std::string& name : keys_) {
            const auto found = std::find(read_.begin(), read_.end(), name);
            const bool read = found != read_.end();
            if (!read) {
                problem(name, "unknown key");
            }
        }
    }

    // Appends "<path>.<key>: <message>" ("<key>: <message>" at the root).
    void problem(const std::string& key, const std::string& message) {
        problems_.push_back(child_path(key) + ": " + message);
    }

private:
    std::optional<YAML::Node> value(const std::string& key) {
        read_.push_back(key);
        const YAML::Node found = node_[key];
        const bool defined = found.IsDefined();
        if (!defined) {
            problem(key, "missing");
            return std::nullopt;
        }
        return found;
    }

    bool read_finite(const std::string& key, double& out) {
        const std::optional<YAML::Node> node = value(key);
        if (!node.has_value()) {
            return false;
        }
        const bool scalar = node->IsScalar();
        double parsed = 0.0;
        bool valid = false;
        if (scalar) {
            valid = parse_number(node->Scalar(), parsed);
        }
        if (!valid) {
            problem(key, "must be a finite number (" + describe(*node) + ")");
            return false;
        }
        out = parsed;
        return true;
    }

    std::string child_path(const std::string& key) const {
        if (path_.empty()) {
            return key;
        }
        return path_ + "." + key;
    }

    static std::string range_text(long long min, long long max, Radix radix) {
        if (max == kNoMaximum) {
            return "must be at least " + show(min, radix);
        }
        return "must be " + show(min, radix) + " to " + show(max, radix);
    }

    const YAML::Node node_;
    std::string path_;
    std::vector<std::string>& problems_;
    std::vector<std::string> keys_;   // document order
    std::vector<std::string> read_;
};

}  // namespace

ConfigLoad load_config_text(const std::string& text, const std::string& name) {
    ConfigLoad load;
    load.text = text;

    // The one place yaml-cpp may throw: a document it cannot parse. The
    // mark is zero-based; people count from one.
    YAML::Node root;
    try {
        root = YAML::Load(text);
    } catch (const YAML::Exception& e) {
        const std::string line = std::to_string(e.mark.line + 1);
        const std::string column = std::to_string(e.mark.column + 1);
        load.problems.push_back(name + ":" + line + ":" + column + ": syntax error: " + e.msg);
        return load;
    }
    const bool document_is_map = root.IsMap();
    if (!document_is_map) {
        load.problems.push_back(name + ": the document must be a mapping of sections");
        return load;
    }

    // The schema, in the file's order. Each section reads its keys, then
    // reports the ones it did not ask for. Bools for the keys the
    // cross-field rules below depend on; a rule between two values runs
    // only when both were accepted.
    TelemetryConfig& c = load.config;
    Section top(root, "", load.problems);

    bool period_ok = false;
    std::optional<Section> timing = top.section("timing");
    if (timing.has_value()) {
        Section& s = *timing;
        period_ok = s.read_int("period_ms", 20, 1000, c.timing.period_ms);
        s.finish();
    }

    bool imu_rate_ok = false;
    std::optional<Section> sensors = top.section("sensors");
    if (sensors.has_value()) {
        Section& s = *sensors;
        s.read_string("i2c_device", c.sensors.i2c_device);
        std::optional<Section> barometer = s.section("barometer");
        if (barometer.has_value()) {
            Section& b = *barometer;
            b.read_int("address", 0x08, 0x77, c.sensors.barometer.address, Radix::Hex);
            b.finish();
        }
        std::optional<Section> imu = s.section("imu");
        if (imu.has_value()) {
            Section& i = *imu;
            i.read_int("address", 0x08, 0x77, c.sensors.imu.address, Radix::Hex);
            // REQ-SENS-003's range first, then the chip's own arithmetic:
            // the divider produces 1 kHz / (1 + n), so the rate must divide
            // 1000 exactly (the driver refuses anything else at init).
            const bool in_range = i.read_int("sample_rate_hz", 4, 100, c.sensors.imu.sample_rate_hz);
            if (in_range) {
                const bool exact = Mpu6050::sample_rate_divider(c.sensors.imu.sample_rate_hz).has_value();
                if (exact) {
                    imu_rate_ok = true;
                } else {
                    const std::string got = std::to_string(c.sensors.imu.sample_rate_hz);
                    i.problem("sample_rate_hz", "must be an exact divisor of 1000 Hz (got " + got + ")");
                }
            }
            i.finish();
        }
        std::optional<Section> gps = s.section("gps");
        if (gps.has_value()) {
            Section& g = *gps;
            g.read_bool("enabled", c.sensors.gps.enabled);
            g.read_string("device", c.sensors.gps.device);
            // REQ-SENS-005: the range first, then the six rates termios
            // names, which the port would refuse at open anyway; a wrong
            // rate is a configuration error before it is a port one.
            const bool in_range = g.read_int("baud_rate", 4800, 115200, c.sensors.gps.baud_rate);
            if (in_range) {
                const bool standard = PosixSerialPort::supports_baud_rate(c.sensors.gps.baud_rate);
                if (!standard) {
                    const std::string got = std::to_string(c.sensors.gps.baud_rate);
                    const std::string rates = PosixSerialPort::kSupportedRates;
                    g.problem("baud_rate", "must be one of " + rates + " (got " + got + ")");
                }
            }
            g.finish();
        }
        s.finish();
    }

    bool uart_ok = false;
    std::optional<Section> fault = top.section("fault");
    if (fault.has_value()) {
        Section& s = *fault;
        s.read_int("i2c_timeout_ms", 1, kNoMaximum, c.fault.i2c_timeout_ms);
        uart_ok = s.read_int("uart_timeout_ms", 1, kNoMaximum, c.fault.uart_timeout_ms);
        // A count of 1 would mark the very first reading stuck (StuckTracker
        // starts its run at 1), so 2 is the floor; a recovery count of 1
        // recovers on the first good reading, which is allowed.
        s.read_int("stuck_count", 2, kNoMaximum, c.fault.stuck_count);
        s.read_int("recovery_count", 1, kNoMaximum, c.fault.recovery_count);
        const bool pressure_min_ok = s.read_number("pressure_min_hpa", c.fault.pressure_min_hpa);
        const bool pressure_max_ok = s.read_number("pressure_max_hpa", c.fault.pressure_max_hpa);
        if (pressure_min_ok && pressure_max_ok && c.fault.pressure_max_hpa <= c.fault.pressure_min_hpa) {
            const std::string against = show(c.fault.pressure_max_hpa) + " against " + show(c.fault.pressure_min_hpa);
            s.problem("pressure_max_hpa", "must exceed fault.pressure_min_hpa (" + against + ")");
        }
        const bool temperature_min_ok = s.read_number("temperature_min_c", c.fault.temperature_min_c);
        const bool temperature_max_ok = s.read_number("temperature_max_c", c.fault.temperature_max_c);
        if (temperature_min_ok && temperature_max_ok && c.fault.temperature_max_c <= c.fault.temperature_min_c) {
            const std::string against = show(c.fault.temperature_max_c) + " against " + show(c.fault.temperature_min_c);
            s.problem("temperature_max_c", "must exceed fault.temperature_min_c (" + against + ")");
        }
        // No accelerometer limit: FAULT-010's rails are the driver's full
        // scale, derived in mpu6050.h, not a key (FTS-DD-001 section 3).
        s.finish();
    }

    std::optional<Section> processing = top.section("processing");
    if (processing.has_value()) {
        Section& s = *processing;
        s.read_positive("baro_measurement_variance_m2", c.processing.baro_measurement_variance_m2);
        s.read_positive("gps_measurement_variance_m2", c.processing.gps_measurement_variance_m2);
        s.read_positive("initial_velocity_variance", c.processing.initial_velocity_variance);
        s.read_positive("accel_noise_variance", c.processing.accel_noise_variance);
        s.read_fraction("complementary_alpha", c.processing.complementary_alpha);
        s.read_positive("sea_level_pressure_hpa", c.processing.sea_level_pressure_hpa);
        s.finish();
    }

    std::optional<Section> logging = top.section("logging");
    if (logging.has_value()) {
        Section& s = *logging;
        s.read_string("directory", c.logging.directory);
        s.read_int("max_file_bytes", kOneRecordBytes, kNoMaximum, c.logging.max_file_bytes);
        s.finish();
    }

    std::optional<Section> transport = top.section("transport");
    if (transport.has_value()) {
        Section& s = *transport;
        std::optional<Section> zmq = s.section("zmq");
        if (zmq.has_value()) {
            Section& z = *zmq;
            z.read_bool("enabled", c.transport.zmq.enabled);
            z.read_string("endpoint", c.transport.zmq.endpoint);
            z.finish();
        }
        std::optional<Section> udp = s.section("udp");
        if (udp.has_value()) {
            Section& u = *udp;
            u.read_bool("enabled", c.transport.udp.enabled);
            // The same test UdpPublisher applies at startup, run here
            // whether or not the transport is enabled: every value is
            // validated (REQ-CFG-002), and a wrong host is a configuration
            // error before it is a transport one.
            const bool host_ok = u.read_string("host", c.transport.udp.host);
            if (host_ok) {
                in_addr address{};
                const int converted = ::inet_pton(AF_INET, c.transport.udp.host.c_str(), &address);
                if (converted != 1) {
                    u.problem("host", "must be an IPv4 dotted-quad address (got \"" + c.transport.udp.host + "\")");
                }
            }
            u.read_int("port", 1, 65535, c.transport.udp.port);
            u.finish();
        }
        s.finish();
    }

    bool gps_interval_ok = false;
    std::optional<Section> simulation = top.section("simulation");
    if (simulation.has_value()) {
        Section& s = *simulation;
        s.read_int("seed", 0, 4294967295LL, c.simulation.seed);
        gps_interval_ok = s.read_int("gps_interval_ms", 1, kNoMaximum, c.simulation.gps_interval_ms);
        std::optional<Section> noise = s.section("noise");
        if (noise.has_value()) {
            Section& n = *noise;
            n.read_positive("pressure_sigma_hpa", c.simulation.noise.pressure_sigma_hpa);
            n.read_positive("temperature_sigma_c", c.simulation.noise.temperature_sigma_c);
            n.read_positive("accel_sigma_mps2", c.simulation.noise.accel_sigma_mps2);
            n.read_positive("gyro_sigma_dps", c.simulation.noise.gyro_sigma_dps);
            n.read_positive("latitude_sigma_deg", c.simulation.noise.latitude_sigma_deg);
            n.read_positive("longitude_sigma_deg", c.simulation.noise.longitude_sigma_deg);
            n.read_positive("gps_altitude_sigma_m", c.simulation.noise.gps_altitude_sigma_m);
            n.finish();
        }
        s.finish();
    }

    // The one block a file may omit (REQ-CFG-001): an ADS-B run requires
    // it (main refuses --adsb without it, by name), no other run reads it,
    // and a configuration recorded before the block existed replays as
    // recorded. Present, every key inside it is required. The box's edges
    // are ordered inside the section, as the pressure bounds are.
    bool adsb_interval_ok = false;
    bool adsb_timeout_ok = false;
    std::optional<Section> adsb = top.optional_section("adsb");
    if (adsb.has_value()) {
        Section& s = *adsb;
        AdsbConfig a;
        std::optional<Section> box = s.section("box");
        if (box.has_value()) {
            Section& b = *box;
            const bool lat_min_ok = b.read_number_in("lat_min", -90.0, 90.0, a.box.lat_min);
            const bool lat_max_ok = b.read_number_in("lat_max", -90.0, 90.0, a.box.lat_max);
            const bool lon_min_ok = b.read_number_in("lon_min", -180.0, 180.0, a.box.lon_min);
            const bool lon_max_ok = b.read_number_in("lon_max", -180.0, 180.0, a.box.lon_max);
            if (lat_min_ok && lat_max_ok && a.box.lat_max <= a.box.lat_min) {
                const std::string against = show(a.box.lat_max) + " against " + show(a.box.lat_min);
                b.problem("lat_max", "must exceed lat_min (" + against + ")");
            }
            if (lon_min_ok && lon_max_ok && a.box.lon_max <= a.box.lon_min) {
                const std::string against = show(a.box.lon_max) + " against " + show(a.box.lon_min);
                b.problem("lon_max", "must exceed lon_min (" + against + ")");
            }
            b.finish();
        }
        std::optional<Section> reference = s.section("reference");
        if (reference.has_value()) {
            Section& r = *reference;
            r.read_number_in("latitude", -90.0, 90.0, a.reference.latitude);
            r.read_number_in("longitude", -180.0, 180.0, a.reference.longitude);
            r.finish();
        }
        adsb_interval_ok = s.read_int("poll_interval_ms", 1000, 3600000, a.poll_interval_ms);
        s.read_number_at_least("switch_margin_m", 0.0, a.switch_margin_m);
        s.read_int("gone_after_polls", 1, kNoMaximum, a.gone_after_polls);
        adsb_timeout_ok = s.read_int("timeout_ms", 1, kNoMaximum, a.timeout_ms);
        s.finish();
        c.adsb = a;
    }

    top.finish();

    // Rules between sections, each on the value the other constrains.
    // The UART timeout must outlast the GPS output period or the
    // simulated receiver is declared timed out between every two fixes
    // (fault_detector.h); the IMU must sample at least as fast as it is
    // polled or the polls repeat values and trip the stuck fault
    // (design.md section 3).
    if (uart_ok && gps_interval_ok && c.fault.uart_timeout_ms <= c.simulation.gps_interval_ms) {
        const std::string against = std::to_string(c.fault.uart_timeout_ms) + " against " + std::to_string(c.simulation.gps_interval_ms);
        top.problem("fault.uart_timeout_ms", "must exceed simulation.gps_interval_ms (" + against + ")");
    }
    if (period_ok && imu_rate_ok) {
        const long long loop_rate_hz = 1000 / static_cast<long long>(c.timing.period_ms);
        if (c.sensors.imu.sample_rate_hz < loop_rate_hz) {
            const std::string against = std::to_string(c.sensors.imu.sample_rate_hz) + " against " + std::to_string(loop_rate_hz);
            const std::string rule = "must be at least the processing rate, 1000 / timing.period_ms";
            top.problem("sensors.imu.sample_rate_hz", rule + " (" + against + ")");
        }
    }
    // The ADS-B channel's delivery period is the poll interval, so its
    // timeout must outlast it as the UART timeout outlasts the fix period.
    if (adsb_interval_ok && adsb_timeout_ok && c.adsb->timeout_ms <= c.adsb->poll_interval_ms) {
        const std::string against = std::to_string(c.adsb->timeout_ms) + " against " + std::to_string(c.adsb->poll_interval_ms);
        top.problem("adsb.timeout_ms", "must exceed adsb.poll_interval_ms (" + against + ")");
    }

    load.ok = load.problems.empty();
    return load;
}

ConfigLoad load_config_file(const std::filesystem::path& path) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (!exists) {
        ConfigLoad load;
        load.problems.push_back("cannot read configuration file " + path.string() + ": no such file");
        return load;
    }
    const bool regular = std::filesystem::is_regular_file(path, ec);
    if (!regular) {
        ConfigLoad load;
        load.problems.push_back("cannot read configuration file " + path.string() + ": not a regular file");
        return load;
    }
    std::ifstream in(path, std::ios::binary);
    const bool open = in.is_open();
    if (!open) {
        ConfigLoad load;
        load.problems.push_back("cannot read configuration file " + path.string() + ": " + std::strerror(errno));
        return load;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return load_config_text(buffer.str(), path.string());
}

std::optional<std::string> log_prefix(const std::filesystem::path& log) {
    const std::string name = log.filename().string();
    const size_t underscore = name.rfind('_');
    if (underscore == std::string::npos || underscore == 0) {
        return std::nullopt;   // "x.bin", "telemetry.bin", "_000.bin"
    }
    const std::string tail = name.substr(underscore + 1);   // "007.bin"
    const size_t suffix = tail.rfind(".bin");
    if (suffix == std::string::npos || suffix + 4 != tail.size() || suffix < 3) {
        return std::nullopt;   // no .bin, something after it, or fewer than three digits
    }
    for (size_t i = 0; i < suffix; i++) {
        const unsigned char c = static_cast<unsigned char>(tail[i]);
        const int digit = std::isdigit(c);
        if (digit == 0) {
            return std::nullopt;
        }
    }
    return name.substr(0, underscore);
}

std::optional<std::filesystem::path> recorded_config_path(const std::filesystem::path& log) {
    const std::optional<std::string> prefix = log_prefix(log);
    if (!prefix.has_value()) {
        return std::nullopt;
    }
    return log.parent_path() / (*prefix + "_config.yaml");
}
