#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "drivers/adsb_config.h"
#include "drivers/simulated_data.h"
#include "processing/fault_detector.h"
#include "processing/processor.h"

// The configuration of record (REQ-CFG-001, REQ-CFG-002, REQ-CFG-003).
//
// One YAML file, config/telemetry_config.yaml or the path given by
// --config, holds every tunable the SRD calls configurable. main loads it
// once at startup through load_config_file(), which turns the untyped
// document into the typed structs below and checks every value's type and
// range as it goes: parse, don't validate. After that only values flow;
// no module reads the file, and nothing re-checks a value its consumer
// receives (a uint8_t address that passed 0x08 to 0x77 carries its own
// proof). Every problem the file has is reported together, named by its
// dotted key (REQ-CFG-002 says "each"), and the binary refuses to start.
//
// The module-owned structs (FaultDetectorConfig, ProcessorConfig,
// SimulationNoise) are reused as they are; the plain structs here cover
// what no module owns. Every default member initializer equals the
// shipped file's value: the defaults are the unit tests' baseline, never
// the binary's fallback, and ConfigTest.ShippedFileEqualsDefaults keeps
// the two equal. No module includes this header; main unpacks the plain
// structs into constructor arguments, so the drivers, the logger, the
// scheduler and the transports never learn that a file exists.
//
// This header exposes nothing of yaml-cpp; telemetry_config.cpp is the
// one translation unit that includes it.

struct TimingConfig {
    // REQ-TIME-001. 20 to 1000 ms: the barometer is read once per cycle,
    // so this is also its sample rate, 50 to 1 Hz (REQ-SENS-001).
    uint64_t period_ms = 20;
};

struct BarometerConfig {
    // REQ-SENS-001. 7-bit I2C address, 0x08 to 0x77. The boards' default
    // with SDO to ground; provisional until the chip is read on the Pi
    // (design.md section 12).
    uint8_t address = 0x76;
};

struct ImuConfig {
    // REQ-SENS-003. AD0 low on the GY-521.
    uint8_t address = 0x68;
    // REQ-SENS-003, REQ-SENS-004: 4 to 100 Hz, an exact divisor of 1000
    // (the chip's divider), and at least the processing rate so that
    // every poll sees a new sample (design.md section 3).
    int sample_rate_hz = 100;
};

struct GpsConfig {
    // REQ-SENS-005. Off in the shipped file: the bench has no receiver
    // wired by default, and a run with one passes a copy of the file that
    // names it. When on, --live opens the device at startup and refuses to
    // start if it cannot, as it does for a missing chip.
    bool enabled = false;
    // The serial device: /dev/serial0 on the Pi's header UART, /dev/ttyACM0
    // or /dev/ttyUSB0 when the board's micro-USB port is used.
    std::string device = "/dev/serial0";
    // One of the six rates termios names (PosixSerialPort::supports_baud_rate);
    // a u-blox receiver ships at 9600. A USB device ignores it, harmlessly.
    int baud_rate = 9600;
};

struct SensorConfig {
    std::string i2c_device = "/dev/i2c-1";   // REQ-SENS-001 to REQ-SENS-004, live mode
    BarometerConfig barometer;
    ImuConfig imu;
    GpsConfig gps;                           // REQ-SENS-005
};

struct LoggingConfig {
    // REQ-LOG-001. The jitter CSV (REQ-TIME-002) and the configuration
    // record (REQ-CFG-003) are written to the same directory.
    std::string directory = "logs";
    // REQ-LOG-002. At least one record: header, tag and frame, 137 bytes since format version 3.
    uint64_t max_file_bytes = 1u << 20;
};

struct ZmqConfig {
    bool enabled = false;
    std::string endpoint = "tcp://127.0.0.1:5555";   // REQ-TRANS-001; the bind validates it
};

struct UdpConfig {
    bool enabled = false;
    std::string host = "127.0.0.1";   // REQ-TRANS-002; an IPv4 dotted quad, one unicast host
    int port = 5556;                  // 1 to 65535
};

// --zmq and --udp enable a transport for one run and override its
// address: the only place a flag and a key overlap.
struct TransportConfig {
    ZmqConfig zmq;
    UdpConfig udp;
};

struct SimulationConfig {
    uint32_t seed = 42;   // REQ-SENS-006: same seed, same run
    uint64_t gps_interval_ms = SimulatedDataGenerator::kDefaultGpsIntervalMs;   // a NEO-6M fixes once per second
    SimulationNoise noise;   // the seven sigmas, each positive
};

struct TelemetryConfig {
    TimingConfig timing;
    SensorConfig sensors;
    FaultDetectorConfig fault;
    ProcessorConfig processing;
    LoggingConfig logging;
    TransportConfig transport;
    SimulationConfig simulation;
    // The one block a file may omit (REQ-CFG-001): empty when the file
    // has no `adsb` block, which a run without --adsb accepts, so that a
    // configuration recorded before the block existed replays as
    // recorded; present, its eleven keys are required like any other
    // (adsb_config.h). A bare TelemetryConfig has none.
    std::optional<AdsbConfig> adsb;
};

// What loading produced. `ok` means the document parsed and every value
// passed; `problems` holds one line per fault in schema order, each
// starting with the dotted key ("fault.stuck_count: must be at least 2
// (got 1)"); `text` is the file verbatim, which main copies beside the
// log as the session's configuration record (REQ-CFG-003).
struct ConfigLoad {
    bool ok = false;
    TelemetryConfig config;
    std::vector<std::string> problems;
    std::string text;
};

// `name` only labels messages ("test.yaml:3:5: syntax error: ...").
ConfigLoad load_config_text(const std::string& text, const std::string& name);

// Reads the file (a regular file this process can open) and loads its
// text. A file that cannot be read is one problem naming the path.
ConfigLoad load_config_file(const std::filesystem::path& path);

// The naming contract of the log files (REQ-CFG-003): "telemetry" for
// logs/telemetry_007.bin, "replay" for replay_000.bin, nothing for a name
// that is not <prefix>_NNN.bin with at least three digits.
std::optional<std::string> log_prefix(const std::filesystem::path& log);

// Where a session's configuration record sits beside its log:
// <directory of log>/<prefix>_config.yaml, or nothing for a name
// log_prefix() does not accept.
std::optional<std::filesystem::path> recorded_config_path(const std::filesystem::path& log);
