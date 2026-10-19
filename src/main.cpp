#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <zmq.hpp>

#include "telemetry_frame.h"
#include "config/telemetry_config.h"
#include "data_source.h"
#include "logging/binary_logger.h"
#include "logging/log_reader.h"
#include "processing/altitude.h"
#include "processing/processor.h"
#include "processing/fault_detector.h"
#include "processing/redundant_processor.h"
#include "processing/voter.h"
#include "timing/clock.h"
#include "timing/fixed_rate_scheduler.h"
#include "timing/jitter_log.h"
#include "timing/timestamp_pacer.h"
#include "transport/publisher.h"
#include "transport/session_message.h"
#include "transport/udp_publisher.h"
#include "transport/zmq_publisher.h"

// The only place that knows concrete source types exist. Everything
// below create_source() sees DataSource and nothing else.
#include "drivers/adsb_source.h"
#include "drivers/curl_http_client.h"
#include "drivers/i2c_bus.h"
#include "drivers/live_sensor_source.h"
#include "drivers/motion_profile.h"
#include "drivers/opensky_json.h"
#include "drivers/selection_log.h"
#include "drivers/serial_port.h"
#include "drivers/simulated_source.h"
#include "replay/log_replay_source.h"

// The processor's default re-seed bound and the detector's default I2C
// timeout are one number, "the IMU communication timeout" of REQ-PROC-002
// and REQ-FAULT-001. Checked at compile time so the two defaults the unit
// tests build on cannot drift apart in a header edit; main below hands
// both the one configured value, `fault.i2c_timeout_ms`.
static_assert(FaultDetectorConfig{}.i2c_timeout_ms == TelemetryProcessor::kDefaultImuTimeoutMs,
              "REQ-PROC-002: the processor's re-seed bound must equal the detector's I2C timeout");

// Frames per live or simulated run without --cycles: a quick smoke run. A
// replay without --cycles runs to the end of its log (REQ-LOG-006). A run
// length is a property of the run, not of the system, so it is a flag and
// not a key of the configuration file (REQ-CFG-001 does not list it).
constexpr int kDefaultCycles = 5;

// Where the configuration of record is read from unless --config says
// otherwise: relative to the working directory, which for the README's
// commands and the Pi is the repository root (REQ-CFG-001).
constexpr const char* kDefaultConfigPath = "config/telemetry_config.yaml";

// Each computed value with its validity beside it (REQ-PROC-007): what a
// display must do with the number is in the same line as the number.
void print_frame(const TelemetryFrame& frame) {
    std::cout << "=== Telemetry Frame ===" << std::endl;
    std::cout << "Timestamp:    " << frame.timestamp_ms << " ms" << std::endl;
    std::cout << "Pressure:     " << frame.pressure_hpa << " hPa" << std::endl;
    std::cout << "Temperature:  " << frame.temperature_c << " C" << std::endl;
    std::cout << "Baro Alt:     " << frame.baro_altitude_m << " m ["
              << validity_name(frame.baro_altitude_validity) << "]" << std::endl;
    std::cout << "Accel:        [" << frame.accel_x << ", "
              << frame.accel_y << ", " << frame.accel_z << "] m/s2" << std::endl;
    std::cout << "Gyro:         [" << frame.gyro_x << ", "
              << frame.gyro_y << ", " << frame.gyro_z << "] deg/s" << std::endl;
    std::cout << "Pitch/Roll:   " << frame.pitch_deg << " / "
              << frame.roll_deg << " deg [" << validity_name(frame.pitch_validity)
              << " / " << validity_name(frame.roll_validity) << "]" << std::endl;
    std::cout << "GPS:          " << frame.latitude << ", "
              << frame.longitude << std::endl;
    std::cout << "GPS Alt:      " << frame.gps_altitude_m << " m" << std::endl;
    std::cout << "Ground Speed: " << frame.ground_speed_mps << " m/s" << std::endl;
    std::cout << "Course:       " << frame.course_deg << " deg" << std::endl;
    std::cout << "Fused Alt:    " << frame.fused_altitude_m << " m ["
              << validity_name(frame.fused_altitude_validity) << "]" << std::endl;
    std::cout << "Vert Speed:   " << frame.vertical_speed_mps << " m/s ["
              << validity_name(frame.vertical_speed_validity) << "]" << std::endl;
    std::cout << "Status:       baro=" << status_name(frame.baro_status)
              << " imu=" << status_name(frame.imu_status)
              << " gps=" << status_name(frame.gps_status) << std::endl;
    std::cout << "Read OK:      baro=" << frame.baro_read_ok
              << " imu=" << frame.imu_read_ok
              << " gps=" << frame.gps_read_ok << std::endl;
}

// One line per transport at exit: what the publisher accepted and what it
// could see refused (REQ-TRANS-003), and the session statuses it sent
// (REQ-TRANS-004). Loss is the receiver's number.
void print_transport_summary(const PublishStats& stats, const std::string& where) {
    std::cout << "Transport: " << stats.published << " frames published " << where
              << ", send failures = " << stats.send_failures;
    if (stats.send_failures > 0) {
        std::cout << " (last errno " << stats.last_errno << ")";
    }
    std::cout << ", status messages = " << stats.status_published << std::endl;
}

// One line for the receiver at exit (REQ-SENS-005). The detector only
// ever sees whether a fix arrived; these counters are how a silent
// receiver, one without a fix, and one at the wrong baud rate are told
// apart (FTS-FM-001 FAULT-003).
void print_gps_summary(const std::optional<GpsCounters>& counters, const std::optional<GpsFix>& last_fix,
                       size_t startup_discarded, const GpsConfig& gps) {
    if (!counters.has_value()) {
        std::cout << "GPS:       disabled (sensors.gps.enabled)" << std::endl;
        return;
    }
    const GpsCounters& c = *counters;
    std::cout << "GPS:       " << c.sentences << " sentences (" << c.used << " GGA/RMC) on " << gps.device
              << " (" << gps.baud_rate << " baud): " << c.fixes << " fixes (" << startup_discarded
              << " discarded in the first second), "
              << c.no_fix_epochs << " epochs without a fix, " << c.incomplete_epochs << " incomplete, "
              << c.fixes_skipped << " skipped, " << c.bad_checksum << " bad checksums, "
              << c.malformed << " malformed, " << c.overlong << " overlong, " << c.resyncs << " resyncs, "
              << c.read_errors << " read errors, " << c.bytes << " bytes";
    if (last_fix.has_value()) {
        std::cout << "; last fix: quality " << last_fix->fix_quality << ", " << last_fix->satellites
                  << " satellites, UTC " << last_fix->utc_time;
    }
    std::cout << std::endl;
}

// The selected aircraft by callsign and address, or none (REQ-SENS-009).
std::string describe(const std::optional<AircraftIdentity>& identity) {
    if (!identity.has_value()) {
        return "none";
    }
    std::string name = identity->callsign;
    if (name.empty()) {
        name = "(no callsign)";
    }
    return name + " (" + identity->icao24 + ")";
}

// One line per change of the selected aircraft (REQ-SENS-009): both
// identities, the reason and the new aircraft's distance from the
// reference point, at the timestamp of the first frame that carries it.
// Formatted apart from std::cout, so the fixed precision does not stick
// to the frames printed after it.
void print_switch(const SwitchRecord& record) {
    std::ostringstream line;
    line << "[ADS-B] t=" << record.timestamp_ms << " ms " << describe(record.change.previous) << " -> "
         << describe(record.change.current) << ": " << switch_reason_name(record.change.reason);
    if (record.change.current.has_value()) {
        line << ", " << std::fixed << std::setprecision(1) << record.change.distance_m / 1000.0
             << " km from the reference point";
    }
    std::cout << line.str() << std::endl;
}

// One line for the ADS-B source at exit (REQ-SENS-008): every poll by its
// outcome (FTS-FM-001 FAULT-012 to FAULT-014, FAULT-017), the token
// requests, the readings delivered and the switches, the last response's
// census, the delivered positions' ages, and the aircraft selected at the
// end. Each poll counts in exactly one outcome; `auth` counts every 401
// the states endpoint returned, repaired or not. Never a token, a
// credential or a body (REQ-CFG-004).
void print_adsb_summary(const AdsbCounters& counters, const std::optional<AircraftIdentity>& identity,
                        const std::optional<AdsbReading>& last_reading, const std::filesystem::path& selection_path) {
    const OpenSkyCounters& c = counters.client;
    std::cout << "ADS-B:     " << c.polls << " polls (" << c.ok << " ok, " << c.auth << " auth, " << c.token_refused
              << " token refused, " << c.token_unavailable << " token unavailable, " << c.unauthorized
              << " unauthorized, " << c.rate_limited << " rate-limited, " << c.http_errors << " http errors, "
              << c.transport_errors << " transport errors, " << c.malformed << " malformed), " << c.token_requests
              << " token requests, " << counters.readings << " readings, " << counters.changes
              << " selection changes, " << counters.aircraft << " aircraft and " << counters.candidates
              << " candidates in the last response, " << c.rows_skipped << " rows skipped, " << counters.reports
              << " reports taken, " << counters.overwritten << " overwritten";
    if (counters.readings > 0) {
        std::cout << ", position age min/mean/max = " << counters.age_min_s << " / "
                  << counters.age_sum_s / static_cast<double>(counters.readings) << " / " << counters.age_max_s
                  << " s";
    }
    if (c.last_http_status != 0 || c.last_error_code != 0) {
        std::cout << ", last HTTP status " << c.last_http_status << ", last curl code " << c.last_error_code;
    }
    if (c.last_retry_after_s.has_value()) {
        std::cout << ", last Retry-After " << *c.last_retry_after_s << " s";
    }
    std::cout << "; selected " << describe(identity);
    if (last_reading.has_value()) {
        std::cout << ", last position at " << last_reading->time_position << " (server time "
                  << last_reading->server_time << ")";
    }
    std::cout << "  (" << selection_path.string() << ")" << std::endl;
}

// What the run saw of the validity bytes (REQ-PROC-007), for one line at
// exit: on the Pi it shows the start-up window before every estimate
// exists, and the pytest reads it.
struct ValidityTally {
    int frames = 0;
    int all_normal = 0;            // frames on which every computed field is NORMAL
    int first_all_normal = -1;     // index of the first such frame; -1 if none
    int any_no_computed_data = 0;  // frames with at least one NO_COMPUTED_DATA field
    int any_failure_warning = 0;   // frames with at least one FAILURE_WARNING field

    void account(const TelemetryFrame& frame) {
        const Validity v[5] = {frame.baro_altitude_validity, frame.pitch_validity, frame.roll_validity,
                               frame.fused_altitude_validity, frame.vertical_speed_validity};
        bool normal = true;
        bool no_computed_data = false;
        bool failure_warning = false;
        for (const Validity x : v) {
            normal = normal && x == Validity::NORMAL;
            no_computed_data = no_computed_data || x == Validity::NO_COMPUTED_DATA;
            failure_warning = failure_warning || x == Validity::FAILURE_WARNING;
        }
        if (normal) {
            if (first_all_normal < 0) {
                first_all_normal = frames;
            }
            all_normal++;
        }
        any_no_computed_data += no_computed_data ? 1 : 0;
        any_failure_warning += failure_warning ? 1 : 0;
        frames++;
    }
};

void print_validity_summary(const ValidityTally& tally) {
    std::cout << "Validity:  every computed field NORMAL on " << tally.all_normal << " of " << tally.frames
              << " frames (first on frame ";
    if (tally.first_all_normal >= 0) {
        std::cout << tally.first_all_normal;
    } else {
        std::cout << "none";
    }
    std::cout << "); NO_COMPUTED_DATA on " << tally.any_no_computed_data
              << " frames, FAILURE_WARNING on " << tally.any_failure_warning << std::endl;
}

// What run() hands back for the exit lines: the validity bytes' tally and
// the voter's (REQ-RED-001), the second counted in its own tested unit.
struct RunTallies {
    ValidityTally validity;
    VoterTally voter;
};

void print_event(const FaultEvent& event) {
    std::cout << "[FAULT] t=" << event.timestamp_ms << " ms"
              << " channel=" << channel_name(event.channel)
              << " type=" << fault_type_name(event.type)
              << " value=" << event.value << std::endl;
}

// Command line. The configuration file is the standing configuration
// (REQ-CFG-001); a flag is a per-run instruction and wins where the two
// overlap. --live and --pace take no value: the first swaps the source for
// the real sensors, the second paces a replay to its log's own timestamps
// (REQ-TIME-004); the rest take one: --config <file> names the configuration,
// --replay <file> swaps the source for a log, --zmq <endpoint> and
// --udp <ipv4>:<port> each enable a publisher for this run and override
// its address in the file (both may be given), --cycles <n> sets the run
// length (TC-005 step 2 needs 200; the default stays the 5-cycle smoke
// run, and a replay without it runs to the end of its log, REQ-LOG-006),
// --motion <name> scripts the simulator's flight (REQ-SENS-007: a scenario
// is a choice of run, not a key); --adsb swaps the source for the ADS-B
// source (REQ-SENS-008), --adsb-credentials <file> names its credentials
// file (REQ-CFG-004: a path is not a tunable and must never be a key) and
// --adsb-origin <url> points the service's two endpoints at a test server.
// Nothing else on the command line overrides a key.
struct Options {
    bool live = false;
    bool adsb = false;                            // the ADS-B source (REQ-SENS-008)
    std::optional<std::string> adsb_credentials;  // the credentials file; the default is under $HOME (REQ-CFG-004)
    std::optional<std::string> adsb_origin;       // a test server in place of the service's origin
    bool pace = false;                            // --replay at the log's recorded cadence (REQ-TIME-004)
    std::optional<std::string> motion;            // the simulator's scripted flight, by name (REQ-SENS-007)
    std::optional<std::string> config_path;
    std::optional<std::string> replay_file;
    std::optional<std::string> zmq_endpoint;
    std::optional<std::string> udp_destination;   // as typed, for messages
    std::string udp_host;                         // the two halves, split here
    int udp_port = 0;
    std::optional<int> cycles;                    // absent: kDefaultCycles, or a replay's whole log
    std::string error;   // non-empty: a usage error; main prints it and exits
};

void print_usage() {
    std::cerr << "usage: telemetry [--config <file>] [--live | --replay <log.bin> [--pace] | --motion <name> | --adsb [--adsb-credentials <file>] [--adsb-origin <url>]] [--zmq <endpoint>] [--udp <ipv4>:<port>] [--cycles <n>]\n"
              << "  --config <file>      the configuration (default " << kDefaultConfigPath
              << "; a replay reads the file recorded beside its log)\n"
              << "  --live               read the BMP280 and MPU6050 on the configured I2C bus (sensors.i2c_device) and, when\n"
              << "                       sensors.gps.enabled, the GPS receiver on sensors.gps.device, paced\n"
              << "  --replay <log.bin>   feed a recorded log through the pipeline, unpaced, to its end\n"
              << "  --pace               with --replay: release each frame at its recorded time; lateness to logs/replay_jitter.csv\n"
              << "  --motion <name>      script the simulator's flight (" << MotionProfile::known_names()
              << ": 150 s of takeoff, climb, turn and descent); without it the simulator sits still\n"
              << "  --adsb               present one aircraft on approach from the OpenSky Network (the adsb block of the configuration), paced\n"
              << "  --adsb-credentials <file>\n"
              << "                       with --adsb: the OpenSky credentials file, {\"clientId\", \"clientSecret\"}, mode 600\n"
              << "                       (default $HOME/.config/flight_telemetry/opensky_credentials.json); never printed\n"
              << "  --adsb-origin <url>  with --adsb: a server in place of the service's origin, e.g. http://127.0.0.1:8080 (the end-to-end test)\n"
              << "  --zmq <endpoint>     publish every processed frame, e.g. tcp://127.0.0.1:5555 (enables and overrides transport.zmq)\n"
              << "  --udp <ipv4>:<port>  publish every processed frame as one datagram, e.g. 127.0.0.1:5556 (enables and overrides transport.udp)\n"
              << "  --cycles <n>         frames to run (default " << kDefaultCycles
              << "; a replay runs to the end of its log, and --cycles caps it)" << std::endl;
}

// Both levels const: the pointers and the characters are only read.
// main's `char* argv[]` converts to this without a cast.
Options parse_args(int argc, const char* const argv[]) {
    Options options;
    for (int i = 1; i < argc; i++) {
        const std::string flag = argv[i];
        const bool has_value = (i + 1 < argc);
        if (flag == "--config" && has_value) {
            options.config_path = argv[++i];
        } else if (flag == "--replay" && has_value) {
            options.replay_file = argv[++i];
        } else if (flag == "--zmq" && has_value) {
            options.zmq_endpoint = argv[++i];
        } else if (flag == "--udp" && has_value) {
            // host:port at the last colon; the port a whole number in range
            // here, the address the publisher's to judge (inet_pton) with
            // its own message. Both refusals end the run before it starts.
            options.udp_destination = argv[++i];
            const std::string& destination = *options.udp_destination;
            const size_t colon = destination.rfind(':');
            if (colon == std::string::npos) {
                options.error = "--udp needs <ipv4>:<port>";
                return options;
            }
            options.udp_host = destination.substr(0, colon);
            char* end = nullptr;
            const long port = std::strtol(destination.c_str() + colon + 1, &end, 10);
            if (end == destination.c_str() + colon + 1 || *end != '\0' || port < 1 || port > 65535) {
                options.error = "--udp port must be a whole number from 1 to 65535";
                return options;
            }
            options.udp_port = static_cast<int>(port);
        } else if (flag == "--cycles" && has_value) {
            // strtol with an end pointer, not atoi: "12x" is an error, not 12.
            char* end = nullptr;
            const long n = std::strtol(argv[++i], &end, 10);
            if (*end != '\0' || n < 1) {
                options.error = "--cycles needs a positive whole number";
                return options;
            }
            options.cycles = static_cast<int>(n);
        } else if (flag == "--live") {
            options.live = true;   // a flag without a value
        } else if (flag == "--pace") {
            options.pace = true;   // likewise; meaningful only with --replay, checked below
        } else if (flag == "--motion" && has_value) {
            options.motion = argv[++i];   // the simulator's script; refused with another source below
        } else if (flag == "--adsb") {
            options.adsb = true;          // the ADS-B source (REQ-SENS-008); refused with another source below
        } else if (flag == "--adsb-credentials" && has_value) {
            options.adsb_credentials = argv[++i];
        } else if (flag == "--adsb-origin" && has_value) {
            options.adsb_origin = argv[++i];
        } else {
            options.error = "unknown or incomplete argument: " + flag;
            return options;
        }
    }
    if (options.live && options.replay_file.has_value()) {
        options.error = "--live and --replay each choose the source; pass one of them";
    } else if (options.adsb && (options.live || options.replay_file.has_value() || options.motion.has_value())) {
        options.error = std::string("--adsb chooses the ADS-B source; ") +
                        (options.live ? "--live" : options.replay_file.has_value() ? "--replay" : "--motion") +
                        " chooses another; pass one of them";
    } else if (!options.adsb && (options.adsb_credentials.has_value() || options.adsb_origin.has_value())) {
        options.error = std::string(options.adsb_credentials.has_value() ? "--adsb-credentials" : "--adsb-origin") +
                        " belongs to the ADS-B source; pass --adsb with it";
    } else if (options.pace && !options.replay_file.has_value()) {
        options.error = "--pace paces a replay to its log's timestamps; pass --replay <log.bin> with it";
    } else if (options.motion.has_value() && (options.live || options.replay_file.has_value())) {
        options.error = std::string("--motion scripts the simulator; ") + (options.live ? "--live" : "--replay") +
                        " chooses another source; pass one of them";
    } else if (options.motion.has_value() && !MotionProfile::named(*options.motion).has_value()) {
        options.error = "--motion: no profile is named '" + *options.motion + "' (known: " +
                        MotionProfile::known_names() + ")";
    }
    return options;
}

// Factory: decides which concrete DataSource to build. Three command-line
// flags choose the source; the configuration supplies each source's
// parameters (the bus and addresses, the IMU rate, the simulator's seed,
// GPS period and noise, the ADS-B block: REQ-CFG-001). The return type is
// the base class, so callers cannot tell which one they got. That is the
// point (REQ-LOG-004).
//
// std::make_unique constructs the object on the heap and wraps the
// pointer in a std::unique_ptr<Derived>, which converts implicitly to
// std::unique_ptr<DataSource> because Derived is-a DataSource.
//
// Returns null with `error` set when the live or the ADS-B source cannot
// start: the bus and the sources report failure through ok()/error()
// rather than throwing, the project's shape for startup errors, and a
// missing device file, a chip answering with the wrong identity or a
// credentials file the service refuses is a wiring or configuration error
// that the first run wants on one line. `credentials_path` is the ADS-B
// source's credentials file, resolved by main (REQ-CFG-004).
std::unique_ptr<DataSource> create_source(const Options& options, const TelemetryConfig& cfg, Clock& clock,
                                          const std::filesystem::path& credentials_path, std::string& error) {
    if (options.adsb) {
        // The ADS-B source (REQ-SENS-008). The optional block is required
        // here and nowhere else (REQ-CFG-001); the credentials are read once,
        // by path, and refused by name without their contents (REQ-CFG-004);
        // libcurl is the real HTTP client; start() runs the first poll on
        // this thread, so a refused token refuses the run before the loop,
        // while an unreachable service is counted and tried again.
        if (!cfg.adsb.has_value()) {
            error = "--adsb needs the adsb block of the configuration file, which this file lacks";
            return nullptr;
        }
        const opensky_json::CredentialsLoad credentials = opensky_json::load_credentials_file(credentials_path);
        if (!credentials.ok) {
            error = credentials.problem;
            return nullptr;
        }
        auto http = std::make_unique<CurlHttpClient>();
        if (!http->ok()) {
            error = "libcurl: " + http->error();
            return nullptr;
        }
        auto adsb = std::make_unique<AdsbSource>(std::move(http), clock, *cfg.adsb, credentials.credentials,
                                                 credentials_path.string(), options.adsb_origin.value_or(""));
        if (!adsb->start()) {
            error = adsb->error();
            return nullptr;
        }
        return adsb;
    }
    if (options.live) {
        auto bus = std::make_unique<LinuxI2cBus>(cfg.sensors.i2c_device);
        if (!bus->ok()) {
            error = bus->error();
            return nullptr;
        }
        // The receiver, when the file names one (REQ-SENS-005). A device
        // that does not open is a configuration error at startup, like a
        // missing chip; a receiver that stops talking later is the
        // detector's (FAULT-003).
        std::unique_ptr<PosixSerialPort> port;
        if (cfg.sensors.gps.enabled) {
            port = std::make_unique<PosixSerialPort>(cfg.sensors.gps.device, cfg.sensors.gps.baud_rate);
            if (!port->ok()) {
                error = "GPS: " + port->error() +
                        " (sensors.gps.device; set sensors.gps.enabled to false to run without a receiver)";
                return nullptr;
            }
        }
        // The raw pointer outlives the move below only for the message: the
        // source owns the bus from here on.
        const LinuxI2cBus* const bus_view = bus.get();
        auto live = std::make_unique<LiveSensorSource>(std::move(bus), cfg.sensors.barometer.address,
                                                       cfg.sensors.imu.address, clock,
                                                       cfg.sensors.imu.sample_rate_hz, std::move(port));
        if (!live->ok()) {
            error = live->error();
            if (bus_view->last_errno() != 0) {
                error += " (last bus error: " + std::string(std::strerror(bus_view->last_errno())) + ")";
            }
            return nullptr;
        }
        return live;
    }
    if (options.replay_file.has_value()) {
        return std::make_unique<LogReplaySource>(
            std::vector<std::filesystem::path>{*options.replay_file});
    }
    // The simulator stamps its frames on the loop's own period and the
    // configured seed keeps the run reproducible (REQ-SENS-006).
    // Reproducibility here is separate from deterministic replay (REQ-LOG-003),
    // which comes from feeding logged frames back through the pipeline.
    // --motion hands it a script (REQ-SENS-007); parse_args refused a name
    // no profile has, so the lookup cannot fail here.
    std::optional<MotionProfile> profile;
    if (options.motion.has_value()) {
        profile = MotionProfile::named(*options.motion);
    }
    return std::make_unique<SimulatedSource>(cfg.timing.period_ms, cfg.simulation.seed,
                                             cfg.simulation.gps_interval_ms, cfg.simulation.noise,
                                             std::move(profile));
}

// The processing loop: source -> fault detector -> three processing lanes
// and the voter -> output.
//
// Takes the source as the base class by reference and never learns the
// concrete type. Swapping the simulator for real sensors or a log replay
// changes create_source() and nothing here (REQ-LOG-004).
//
// The detector runs before the processor, and this order is not
// negotiable. The filters gate on ChannelStatus and carry state across
// frames; a bad reading that reaches them marked NOMINAL corrupts every
// estimate that follows. Status must be right before the frame arrives.
//
// Detector and processor are passed by non-const reference because both
// mutate per-frame state. The same instances must see every frame in
// order, which is why they are created once outside the loop. The
// processor is three identical lanes and a voter (REQ-RED-001), every
// source through them alike: the loop hands it the checked frame and logs
// what the voter chose.
//
// The scheduler, the pacer and the jitter log are pointers, not references,
// because each may be absent: a reference cannot be null, a pointer can. A
// live or simulated run has the scheduler; a paced replay has the pacer
// (REQ-TIME-004), which waits after the read because its deadline is the
// frame's own timestamp; an unpaced replay has neither, since its frames
// carry their own timestamps and pacing adds nothing to the identity check
// (REQ-LOG-003). The statements an unpaced run executes, and their order,
// are the same with or without the pacer in the loop: pacing decides when a
// frame goes out, never what is in it.
//
// The publishers are base pointers in a vector, empty without --zmq or
// --udp. The loop publishes to each one it is given and never learns which
// transport it is: the DataSource rule applied to the output side
// (REQ-TRANS-001, REQ-TRANS-002).
//
// The session status (REQ-TRANS-004) goes to the same publishers: RUNNING
// with the run's mode and the source mode of its frames (REQ-LOG-007, the
// run's own except in a replay) before frame 0 and every `status_every_cycles` after
// it (one second of cycles), so a consumer that joins late learns the mode
// from the next one, and at once when the identity it names changes;
// ENDED after the last frame, repeated, because a lost datagram carrying
// the end has no later message to repair it. `identity` is the composition
// root's function: the aircraft the frames present in ADS-B mode (read
// from the source main built, so the loop still never learns its type),
// nothing in every other mode. It is called once per frame, after the
// read, and the ADS-B composition reports the switches on the way.
RunTallies run(DataSource& source, FaultDetector& detector, RedundantProcessor& processor,
                  BinaryLogger& logger, const std::vector<Publisher*>& publishers,
                  FixedRateScheduler* scheduler, TimestampPacer* pacer, JitterLog* jitter,
                  std::optional<int> frame_count, RunMode mode, RunMode source_mode, int status_every_cycles,
                  const std::function<SessionIdentity()>& identity) {
    RunTallies tally;
    SessionIdentity published_identity;
    // The source is asked before every read (REQ-LOG-006): a replay stops
    // at its log's end, an endless source always answers yes, and
    // --cycles, when given, caps either.
    for (int i = 0; (!frame_count.has_value() || i < *frame_count) && source.has_next(); i++) {
        // The wait is the first thing in the cycle, so everything below it
        // starts on the deadline grid and the jitter measured is the
        // lateness of the whole cycle, not of some step inside it.
        if (scheduler != nullptr) {
            const CycleTiming timing = scheduler->wait_for_next_cycle();
            if (jitter != nullptr) {
                jitter->record(timing);
            }
        }

        // Virtual dispatch: the compiler emits a lookup through the object's
        // vtable, so this line runs SimulatedSource::read_frame() today and
        // would run LiveSensorSource::read_frame() or LogReplaySource::read_frame()
        // tomorrow, with no change to this code.
        TelemetryFrame raw = source.read_frame();

        // The status precedes the frame it announces, on every transport:
        // before frame 0, every second of cycles, and at once when the
        // identity it names changes (REQ-TRANS-004). After the read, so it
        // names the aircraft of the frame below; before the frame goes out,
        // so a consumer re-takes its track first (REQ-DISP-007).
        const SessionIdentity current = identity();
        if (i % status_every_cycles == 0 || current != published_identity) {
            for (Publisher* publisher : publishers) {
                publisher->publish_status(SessionStatus{mode, source_mode, StreamState::RUNNING, current});
            }
            published_identity = current;
        }

        // A paced replay waits here, after the read: the deadline is the
        // frame's recorded timestamp, and the wait changes nothing in the
        // frame, which is what keeps the paced log byte-identical
        // (REQ-TIME-004). Its lateness goes to the same jitter log.
        if (pacer != nullptr) {
            const CycleTiming timing = pacer->wait_for(raw.timestamp_ms);
            if (jitter != nullptr) {
                jitter->record(timing);
            }
        }

        // Non-virtual calls: one detector, then three lanes and the voter
        // (REQ-RED-001), behavior configured rather than substituted.
        TelemetryFrame checked = detector.check(raw);
        TelemetryFrame processed = processor.process(checked);
        tally.validity.account(processed);
        tally.voter.account(processed);

        // Processed frames are what the log holds (REQ-LOG-001); replay
        // strips them back to raw before the pipeline sees them again.
        logger.log_frame(processed);

        // The wire carries a copy of the record, never the record itself:
        // the log is written first, and a publisher that drops or fails
        // (REQ-TRANS-003) changes nothing about what was logged.
        for (Publisher* publisher : publishers) {
            publisher->publish(processed);
        }

        print_frame(processed);
        // The detector's events, then the voter's (REQ-RED-003, REQ-RED-004,
        // REQ-RED-006), both after the frame they occurred at; a healthy run
        // raises no voter event, so its log holds the records it held before
        // the voter.
        for (const FaultEvent& event : detector.take_events()) {
            logger.log_event(event);
            print_event(event);
        }
        const std::vector<FaultEvent> voter_events = processor.take_events();
        tally.voter.account_events(voter_events.size());
        for (const FaultEvent& event : voter_events) {
            logger.log_event(event);
            print_event(event);
        }
        std::cout << std::endl;
    }

    // The end, after the last frame: the one message with no successor to
    // repair it on a lossy channel, so it goes out kEndedRepeats times and
    // a receiver treats the repeats as one (REQ-TRANS-004).
    for (int k = 0; k < kEndedRepeats; k++) {
        for (Publisher* publisher : publishers) {
            publisher->publish_status(SessionStatus{mode, source_mode, StreamState::ENDED, published_identity});
        }
    }
    return tally;
}

// Usage: telemetry                        simulated source, 5 frames, config/telemetry_config.yaml
//        telemetry --config other.yaml    the same with another configuration file
//        telemetry --live --cycles 250    real sensors on the configured I2C bus, 5 s paced
//        telemetry --replay <log.bin>     replay a recorded log with the configuration recorded beside it, to its end
//        telemetry --replay <log.bin> --pace
//                                         the same at the log's recorded cadence, lateness to logs/replay_jitter.csv
//        telemetry --zmq tcp://127.0.0.1:5555 --cycles 500
//                                         publish every processed frame; 10 s at 50 Hz
//        telemetry --udp 127.0.0.1:5556 --cycles 500
//                                         the same, one datagram per frame; both flags may be given
//        telemetry --adsb --cycles 15000 --zmq tcp://127.0.0.1:5555
//                                         one aircraft on approach to SFO from the OpenSky Network, five
//                                         minutes, to the bridge and the page
int main(int argc, char* argv[]) {
    std::cout << "flight-telemetry v0.1.0\n" << std::endl;

    const Options options = parse_args(argc, argv);
    if (!options.error.empty()) {
        std::cerr << "error: " << options.error << std::endl;
        print_usage();
        return 1;
    }
    const bool replaying = options.replay_file.has_value();

    // The configuration (REQ-CFG-001, REQ-CFG-002, REQ-CFG-003). A replay
    // reads the configuration recorded beside its log unless --config
    // names another file, which is a legitimate run (tuning by replay) but
    // not an identity claim, and the line below says so. A live or
    // simulated run reads --config or the default path. Every problem is
    // reported at once and nothing is built before the file passes: the
    // loop reads values, never the file.
    std::filesystem::path config_path = options.config_path.value_or(kDefaultConfigPath);
    std::optional<std::filesystem::path> recorded;
    if (replaying) {
        recorded = recorded_config_path(*options.replay_file);
        if (!recorded.has_value()) {
            std::cerr << "error: cannot derive the configuration recorded with " << *options.replay_file
                      << ": the log is not named <prefix>_NNN.bin; pass --config <file>" << std::endl;
            return 1;
        }
        if (!options.config_path.has_value()) {
            config_path = *recorded;
        }
    }
    const ConfigLoad load = load_config_file(config_path);
    if (!load.ok) {
        const bool unreadable = load.problems.size() == 1 && load.problems[0].rfind("cannot read", 0) == 0;
        if (unreadable) {
            std::cerr << "error: " << load.problems[0];
            if (config_path != kDefaultConfigPath && !replaying) {
                std::cerr << std::endl;
            } else {
                std::cerr << "; pass --config <file>" << std::endl;
            }
        } else {
            std::cerr << "error: configuration " << config_path.string() << " has " << load.problems.size()
                      << (load.problems.size() == 1 ? " problem:" : " problems:") << std::endl;
            for (const std::string& problem : load.problems) {
                std::cerr << "  " << problem << std::endl;
            }
        }
        return 1;
    }
    const TelemetryConfig& cfg = load.config;

    // What the Configuration line says about a replay: the record beside
    // the log, the same configuration by content under another name, a
    // different one, or no record at all. Identity (REQ-LOG-003) is a
    // property of the configuration's text, not of the file's path.
    std::string config_note;
    if (replaying) {
        const std::string& log = *options.replay_file;
        if (!options.config_path.has_value()) {
            config_note = " (recorded with " + log + ")";
        } else {
            const ConfigLoad record = load_config_file(*recorded);
            if (record.text.empty()) {
                config_note = " (no configuration recorded with " + log + "; replay identity is not claimed)";
            } else if (record.text == load.text) {
                config_note = " (equal to the configuration recorded with " + log + ")";
            } else {
                config_note = " (not the configuration recorded with " + log + "; replay identity is not claimed)";
            }
        }
    }
    std::cout << "Configuration: " << config_path.string() << config_note << std::endl;

    // A replay run logs under a different prefix, so session A and
    // session B sit side by side, which is also what TC-004 compares. The
    // replay source opens its input lazily, on the first read, after the
    // logger has truncated its output; the prefixes keep the two apart
    // unless the input is itself a replay log in the output directory,
    // which is refused here rather than destroyed (REQ-LOG-003,
    // REQ-CFG-003).
    const std::string prefix = replaying ? "replay" : "telemetry";

    // The run's mode, decided where the source is (REQ-TRANS-004,
    // REQ-LOG-007), and the source mode its frames are produced in: the
    // run's own, except for a replay, which copies its input's forward
    // below. Both go into every record's header and every status.
    const RunMode mode = options.adsb ? RunMode::ADSB
                         : options.live ? RunMode::LIVE
                         : replaying ? RunMode::REPLAY : RunMode::SIMULATED;
    RunMode source_mode = mode;
    if (replaying) {
        const std::filesystem::path input(*options.replay_file);
        std::filesystem::path input_directory = input.parent_path();
        if (input_directory.empty()) {
            input_directory = ".";
        }
        std::error_code ec;
        const bool same_directory = std::filesystem::weakly_canonical(input_directory, ec) ==
                                    std::filesystem::weakly_canonical(cfg.logging.directory, ec);
        if (log_prefix(input) == prefix && same_directory) {
            std::cerr << "error: refusing to replay " << input.string() << ": this run would write "
                      << (std::filesystem::path(cfg.logging.directory) / (prefix + "_NNN.bin")).string()
                      << " over it; copy the file elsewhere or change logging.directory" << std::endl;
            return 1;
        }

        // The input's header, checked before anything is built (REQ-LOG-005):
        // a log this build cannot read stops the run here, by name, instead
        // of feeding zero frames through the pipeline for --cycles cycles
        // (FTS-VR-001 Entry 11 §11.8). The replay source opens the file
        // again on its first read; this probe reads the header only. A
        // version-1 or version-2 input is a legitimate replay, said so on
        // one line: the raw fields are the record and the pipeline
        // recomputes the rest (REQ-LOG-004), so its replay log is a
        // version-3 log.
        LogReader probe(input);
        if (!probe.ok()) {
            std::cerr << "error: cannot replay " << input.string() << ": " << probe.error() << std::endl;
            return 1;
        }
        // The header says how the frames were produced (REQ-LOG-007), and
        // the replay carries that forward in its own header and status: an
        // ADS-B recording's GPS channel takes the configured ADS-B timeout
        // below (REQ-FAULT-001), so the replay reproduces the run's faults
        // (REQ-LOG-004), and the page labels its datum. A file older than
        // version 3 records neither mode; UNKNOWN goes forward, never a
        // guess from the frames.
        source_mode = static_cast<RunMode>(probe.header().source_mode);
        std::cout << "Replay input:  " << input.string() << " (format version " << probe.header().version
                  << ", run mode " << mode_name(static_cast<RunMode>(probe.header().run_mode))
                  << ", source mode " << mode_name(source_mode);
        if (probe.header().version != kLogFormatVersion) {
            std::cout << "; this build writes version " << kLogFormatVersion
                      << ", so the replay log is version " << kLogFormatVersion
                      << " with the computed fields and their validity recomputed from the raw fields";
        }
        if (source_mode == RunMode::ADSB) {
            std::cout << "; an ADS-B recording by its header, so the GPS channel's timeout is adsb.timeout_ms";
        }
        std::cout << ")" << std::endl;
    }

    // One monotonic clock paces the loop (below) and stamps live frames, so
    // the frame timestamps and the jitter CSV measure the same time
    // (design.md section 10).
    MonotonicClock clock;

    // unique_ptr owns the source. When `source` goes out of scope at the end
    // of main, its destructor deletes the object through the DataSource
    // pointer, which is why ~DataSource must be virtual.

    // The credentials file (REQ-CFG-004): named by --adsb-credentials or
    // found under the home directory, read once by the source, its path
    // printed and its contents never. A flag and not a key, because the
    // configuration file is public and is recorded beside every log.
    std::filesystem::path credentials_path;
    if (options.adsb) {
        if (options.adsb_credentials.has_value()) {
            credentials_path = *options.adsb_credentials;
        } else {
            const std::optional<std::filesystem::path> default_path =
                opensky_json::default_credentials_path(std::getenv("HOME"));
            if (!default_path.has_value()) {
                std::cerr << "error: --adsb-credentials <file> is required: HOME is not set, so the default path "
                             "cannot be formed" << std::endl;
                return 1;
            }
            credentials_path = *default_path;
        }
        std::cout << "ADS-B:     credentials from " << credentials_path.string()
                  << " (read once, never printed)";
        if (options.adsb_origin.has_value()) {
            std::cout << "; service origin " << *options.adsb_origin;
        }
        std::cout << std::endl;
    }

    std::string source_error;
    std::unique_ptr<DataSource> source = create_source(options, cfg, clock, credentials_path, source_error);
    if (!source) {
        std::cerr << "error: " << source_error << std::endl;
        return 1;
    }
    // The composition root built the source and knows which one it built;
    // nothing below create_source() does. The replay source is asked at
    // exit whether its log went on (REQ-LOG-006), so the Replay: line says
    // which ended the run, the log or --cycles.
    LogReplaySource* replay = replaying ? static_cast<LogReplaySource*>(source.get()) : nullptr;
    const LiveSensorSource* live = options.live ? static_cast<const LiveSensorSource*>(source.get()) : nullptr;
    AdsbSource* adsb = options.adsb ? static_cast<AdsbSource*>(source.get()) : nullptr;

    // The processor lives on the stack: main owns it for the whole run and
    // nothing else needs to share it, so there is no reason for the heap.
    // It is three lanes and a voter (REQ-RED-001), every lane built from
    // the same tuning; no lane-fault hook, which only a test builds
    // (REQ-RED-005). One configured value feeds both: the IMU timeout is
    // the detector's fault bound and the lanes' re-seed bound
    // (REQ-PROC-002), and they must be the same number.
    // The GPS channel's timeout (REQ-FAULT-001): the receiver's in a live
    // run, and for ADS-B frames the configured ADS-B timeout, because the
    // channel's delivery period is the poll interval there; a replay of an
    // ADS-B recording (by its header's source mode above, REQ-LOG-007)
    // takes the same value, so the replay reproduces the run's faults
    // (REQ-LOG-004). The composition root chooses which configured value a
    // channel's timeout is, as it hands the IMU timeout to two consumers.
    FaultDetectorConfig fault = cfg.fault;
    if (source_mode == RunMode::ADSB) {
        if (!cfg.adsb.has_value()) {
            std::cerr << "error: replaying an ADS-B log needs the adsb block of the configuration, which "
                      << config_path.string() << " lacks" << std::endl;
            return 1;
        }
        fault.uart_timeout_ms = cfg.adsb->timeout_ms;
    }
    FaultDetector detector(fault);
    RedundantProcessor processor(cfg.processing, cfg.fault.i2c_timeout_ms);

    // The header every record and every frame message carries
    // (REQ-LOG-007): built once, from the run's mode and the source mode,
    // and handed to the logger and both publishers, so the file and the
    // wire say the same thing.
    const LogFileHeader header = make_log_header(mode, source_mode);
    BinaryLogger logger(cfg.logging.directory, prefix, cfg.logging.max_file_bytes, header);
    if (!logger.ok()) {
        std::cerr << "cannot open log file in " << cfg.logging.directory << std::endl;
        return 1;
    }

    // The session's configuration record (REQ-CFG-003): the file's text,
    // verbatim, beside the log under the log's prefix, so a replay of this
    // session reads exactly what this run read.
    const std::filesystem::path record_path = std::filesystem::path(cfg.logging.directory) / (prefix + "_config.yaml");
    {
        std::ofstream record(record_path, std::ios::binary | std::ios::trunc);
        record << load.text;
        if (!record) {
            std::cerr << "error: cannot write the configuration record " << record_path.string() << std::endl;
            return 1;
        }
    }

    // ZeroMQ (REQ-TRANS-001). One context per process, declared before the
    // publisher so it is destroyed after it: terminating the context waits
    // for every socket on it to close, so this order is the difference
    // between a clean exit and a hang. Both exist only when the file
    // enables the transport or --zmq does; the flag's endpoint wins. A
    // bind that fails is a startup error, loud and fatal like an
    // unwritable log directory; failures during the loop are counted,
    // never fatal (REQ-TRANS-003).
    const bool zmq_enabled = options.zmq_endpoint.has_value() || cfg.transport.zmq.enabled;
    const std::string zmq_endpoint = options.zmq_endpoint.value_or(cfg.transport.zmq.endpoint);
    std::optional<zmq::context_t> zmq_context;
    std::optional<ZmqPublisher> publisher;
    if (zmq_enabled) {
        zmq_context.emplace(1);   // one I/O thread moves messages to the wire
        publisher.emplace(*zmq_context, zmq_endpoint, kDefaultSendHighWaterMark, header);
        if (!publisher->ok()) {
            std::cerr << "cannot bind ZeroMQ publisher to " << zmq_endpoint
                      << ": " << publisher->error() << std::endl;
            return 1;
        }
    }

    // UDP (REQ-TRANS-002): one datagram per frame to one unicast
    // destination, the same startup rule as ZeroMQ: a destination the
    // publisher refuses (not an IPv4 address, not unicast, no route) is
    // fatal here; a send the kernel refuses inside the loop is counted
    // (REQ-TRANS-003).
    const bool udp_enabled = options.udp_destination.has_value() || cfg.transport.udp.enabled;
    const std::string udp_host = options.udp_destination.has_value() ? options.udp_host : cfg.transport.udp.host;
    const int udp_port = options.udp_destination.has_value() ? options.udp_port : cfg.transport.udp.port;
    const std::string udp_destination = udp_host + ":" + std::to_string(udp_port);
    std::optional<UdpPublisher> udp_publisher;
    if (udp_enabled) {
        udp_publisher.emplace(udp_host, udp_port, header);
        if (!udp_publisher->ok()) {
            std::cerr << "cannot send UDP to " << udp_destination
                      << ": " << udp_publisher->error() << std::endl;
            return 1;
        }
    }

    // The loop takes every transport as the same base pointer (run()). The
    // vector owns nothing: the optionals above own the objects, and the
    // vector is declared after them so it is destroyed first.
    std::vector<Publisher*> publishers;
    if (publisher.has_value()) {
        publishers.push_back(&publisher.value());
    }
    if (udp_publisher.has_value()) {
        publishers.push_back(&udp_publisher.value());
    }

    // Pacing (REQ-TIME-001, REQ-TIME-002, REQ-TIME-004). Live and simulated
    // runs are held to the configured grid; a paced replay to its log's own
    // timestamps; an unpaced replay to nothing (see run()). Each paced run's
    // lateness goes to a CSV named by the run's prefix, beside the log, so a
    // paced replay never overwrites a live run's timing evidence. The objects
    // are built either way so their lifetime covers the loop; only the
    // pointers decide whether they act.
    const uint64_t period_ns = cfg.timing.period_ms * 1'000'000ull;
    FixedRateScheduler scheduler(clock, period_ns);
    TimestampPacer pacer(clock, period_ns);
    const std::filesystem::path jitter_path = std::filesystem::path(cfg.logging.directory) / (prefix + "_jitter.csv");
    std::optional<JitterLog> jitter;
    if (!replaying || options.pace) {
        jitter.emplace(jitter_path);
    }

    // Every change of the selected aircraft, beside the log under the run's
    // prefix (REQ-SENS-009): the jitter CSV's shape, and the record of which
    // aircraft a recorded run tracked, frame by frame, which a replay
    // cannot carry (design.md section 12). Part of the session's record,
    // like the configuration copy, so an unwritable file is refused.
    const std::filesystem::path selection_path = std::filesystem::path(cfg.logging.directory) / (prefix + "_adsb.csv");
    std::optional<SelectionLog> selection_log;
    if (adsb != nullptr) {
        selection_log.emplace(selection_path);
        if (!selection_log->ok()) {
            std::cerr << "error: cannot write the selection record " << selection_path.string() << std::endl;
            return 1;
        }
    }

    // Baro Alt (REQ-PROC-001), Pitch/Roll (REQ-PROC-002), Fused Alt and
    // Vert Speed (REQ-PROC-003) and their validity (REQ-PROC-007) are all
    // computed by the processor. Status comes from the detector
    // (REQ-FAULT-004, REQ-FAULT-006); the simulator never faults.
    // The run length: --cycles when given; otherwise the smoke run's five
    // frames, or for a replay no cap at all, the log's end ends it
    // (REQ-LOG-006).
    std::optional<int> cycles = options.cycles;
    if (!cycles.has_value() && !replaying) {
        cycles = kDefaultCycles;
    }
    // One RUNNING status per second of cycles (REQ-TRANS-004): the period
    // is 20 to 1000 ms, so this is 50 cycles down to every cycle, and the
    // floor keeps the interval at or under a second.
    const int status_every_cycles = kStatusIntervalMs / cfg.timing.period_ms;

    // The identity the status carries (REQ-TRANS-004, REQ-DISP-007), read
    // from the ADS-B source after each frame, with every switch the frame
    // brought reported on the way: one console line and one row beside the
    // log, both at the timestamp of the first frame carrying the new
    // aircraft (REQ-SENS-009). Nothing in every other mode.
    size_t switches_reported = 0;
    const std::function<SessionIdentity()> identity_after_read = [&]() -> SessionIdentity {
        if (adsb == nullptr) {
            return SessionIdentity{};
        }
        const std::vector<SwitchRecord>& switches = adsb->switches();
        for (; switches_reported < switches.size(); switches_reported++) {
            print_switch(switches[switches_reported]);
            selection_log->record(switches[switches_reported]);
        }
        const std::optional<AircraftIdentity>& selected = adsb->identity();
        return selected.has_value() ? session_identity(selected->icao24, selected->callsign) : SessionIdentity{};
    };

    const RunTallies tally = run(*source, detector, processor, logger, publishers,
                                    replaying ? nullptr : &scheduler,
                                    options.pace ? &pacer : nullptr,
                                    jitter.has_value() ? &jitter.value() : nullptr,
                                    cycles, mode, source_mode, status_every_cycles, identity_after_read);

    // The poll thread is joined before the exit lines read its counters.
    if (adsb != nullptr) {
        adsb->stop();
    }

    std::cout << "Logged to " << logger.current_path().string()
              << "  (replay with: telemetry --replay <file>)" << std::endl;
    print_validity_summary(tally.validity);
    std::cout << tally.voter.summary() << std::endl;

    // Which ended the run, the log or --cycles (REQ-LOG-006). Asking the
    // source here looks one frame ahead and processes nothing.
    if (replay != nullptr) {
        std::cout << "Replay:    " << replay->frames_read() << " frames, "
                  << (replay->has_next() ? "stopped by --cycles before the end of the log" : "end of log reached")
                  << std::endl;
    }

    // The script a simulated run followed (REQ-SENS-007): the log's
    // provenance, beside the run's other facts.
    if (options.motion.has_value()) {
        std::cout << "Motion:    profile " << *options.motion << ", "
                  << MotionProfile::named(*options.motion)->duration_ms() / 1000 << " s scripted flight"
                  << std::endl;
    }

    if (jitter.has_value() && options.pace) {
        // The pacer's lateness, in the scheduler's terms: whole periods late
        // are its overruns (REQ-TIME-004).
        const JitterStats& st = jitter->stats();
        std::cout << "Timing:    " << st.cycles << " frames paced to the log's timestamps"
                  << ", lateness min/mean/max = " << st.min_ns / 1000.0 << " / "
                  << st.mean_ns() / 1000.0 << " / " << st.max_ns / 1000.0 << " us"
                  << ", late by a period or more = " << st.overruns
                  << "  (" << jitter_path.string() << ")" << std::endl;
    } else if (jitter.has_value()) {
        const JitterStats& st = jitter->stats();
        std::cout << "Timing:    " << st.cycles << " cycles at " << cfg.timing.period_ms << " ms"
                  << ", jitter min/mean/max = " << st.min_ns / 1000.0 << " / "
                  << st.mean_ns() / 1000.0 << " / " << st.max_ns / 1000.0 << " us"
                  << ", missed cycles = " << st.missed_cycles
                  << "  (" << jitter_path.string() << ")" << std::endl;
    } else {
        std::cout << "Timing:    unpaced (replay)" << std::endl;
    }

    if (live != nullptr) {
        print_gps_summary(live->gps_counters(), live->last_gps_fix(), live->gps_startup_discarded(),
                          cfg.sensors.gps);
    }
    if (adsb != nullptr) {
        print_adsb_summary(adsb->counters(), adsb->identity(), adsb->last_reading(), selection_path);
    }

    if (publisher.has_value()) {
        print_transport_summary(publisher->stats(), "on " + zmq_endpoint + " (ZeroMQ)");
    }
    if (udp_publisher.has_value()) {
        print_transport_summary(udp_publisher->stats(), "to " + udp_destination + " (UDP)");
    }

    // Sanity checks
    std::cout << "Sanity check: pressure_to_altitude(1013.25) = "
              << pressure_to_altitude(1013.25f) << " m (should be 0.0)" << std::endl;
    std::cout << "Sanity check: pressure_to_altitude(898.76) = "
              << pressure_to_altitude(898.76f) << " m (should be ~1000)" << std::endl;

    return 0;
}
