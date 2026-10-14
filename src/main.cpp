#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
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
#include "timing/clock.h"
#include "timing/fixed_rate_scheduler.h"
#include "timing/jitter_log.h"
#include "transport/publisher.h"
#include "transport/udp_publisher.h"
#include "transport/zmq_publisher.h"

// The only place that knows concrete source types exist. Everything
// below create_source() sees DataSource and nothing else.
#include "drivers/i2c_bus.h"
#include "drivers/live_sensor_source.h"
#include "drivers/simulated_source.h"
#include "replay/log_replay_source.h"

// The processor's default re-seed bound and the detector's default I2C
// timeout are one number, "the IMU communication timeout" of REQ-PROC-002
// and REQ-FAULT-001. Checked at compile time so the two defaults the unit
// tests build on cannot drift apart in a header edit; main below hands
// both the one configured value, `fault.i2c_timeout_ms`.
static_assert(FaultDetectorConfig{}.i2c_timeout_ms == TelemetryProcessor::kDefaultImuTimeoutMs,
              "REQ-PROC-002: the processor's re-seed bound must equal the detector's I2C timeout");

// Frames per run without --cycles: a quick smoke run. A run length is a
// property of the run, not of the system, so it is a flag and not a key
// of the configuration file (REQ-CFG-001 does not list it).
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
    std::cout << "Heading:      " << frame.heading_deg << " deg" << std::endl;
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
// could see refused (REQ-TRANS-003). Loss is the receiver's number.
void print_transport_summary(const PublishStats& stats, const std::string& where) {
    std::cout << "Transport: " << stats.published << " frames published " << where
              << ", send failures = " << stats.send_failures;
    if (stats.send_failures > 0) {
        std::cout << " (last errno " << stats.last_errno << ")";
    }
    std::cout << std::endl;
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

void print_event(const FaultEvent& event) {
    std::cout << "[FAULT] t=" << event.timestamp_ms << " ms"
              << " channel=" << channel_name(event.channel)
              << " type=" << fault_type_name(event.type)
              << " value=" << event.value << std::endl;
}

// Command line. The configuration file is the standing configuration
// (REQ-CFG-001); a flag is a per-run instruction and wins where the two
// overlap. --live takes no value and swaps the source for the real
// sensors; the rest take one: --config <file> names the configuration,
// --replay <file> swaps the source for a log, --zmq <endpoint> and
// --udp <ipv4>:<port> each enable a publisher for this run and override
// its address in the file (both may be given), --cycles <n> sets the run
// length (TC-005 step 2 needs 200; the default stays the 5-cycle smoke
// run). Nothing else on the command line overrides a key.
struct Options {
    bool live = false;
    std::optional<std::string> config_path;
    std::optional<std::string> replay_file;
    std::optional<std::string> zmq_endpoint;
    std::optional<std::string> udp_destination;   // as typed, for messages
    std::string udp_host;                         // the two halves, split here
    int udp_port = 0;
    int cycles = kDefaultCycles;
    std::string error;   // non-empty: a usage error; main prints it and exits
};

void print_usage() {
    std::cerr << "usage: telemetry [--config <file>] [--live | --replay <log.bin>] [--zmq <endpoint>] [--udp <ipv4>:<port>] [--cycles <n>]\n"
              << "  --config <file>      the configuration (default " << kDefaultConfigPath
              << "; a replay reads the file recorded beside its log)\n"
              << "  --live               read the BMP280 and MPU6050 on the configured I2C bus (sensors.i2c_device), paced\n"
              << "  --replay <log.bin>   feed a recorded log through the pipeline, unpaced\n"
              << "  --zmq <endpoint>     publish every processed frame, e.g. tcp://127.0.0.1:5555 (enables and overrides transport.zmq)\n"
              << "  --udp <ipv4>:<port>  publish every processed frame as one datagram, e.g. 127.0.0.1:5556 (enables and overrides transport.udp)\n"
              << "  --cycles <n>         frames to run (default " << kDefaultCycles << ")" << std::endl;
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
        } else {
            options.error = "unknown or incomplete argument: " + flag;
            return options;
        }
    }
    if (options.live && options.replay_file.has_value()) {
        options.error = "--live and --replay each choose the source; pass one of them";
    }
    return options;
}

// Factory: decides which concrete DataSource to build. Two command-line
// flags choose the source; the configuration supplies each source's
// parameters (the bus and addresses, the IMU rate, the simulator's seed,
// GPS period and noise: REQ-CFG-001). The return type is the base class,
// so callers cannot tell which one they got. That is the point
// (REQ-LOG-004).
//
// std::make_unique constructs the object on the heap and wraps the
// pointer in a std::unique_ptr<Derived>, which converts implicitly to
// std::unique_ptr<DataSource> because Derived is-a DataSource.
//
// Returns null with `error` set when the live source cannot start: the
// bus and the source report failure through ok()/error() rather than
// throwing, the project's shape for startup errors, and a missing device
// file or a chip answering with the wrong identity is a wiring or
// configuration error that the first live run wants on one line.
std::unique_ptr<DataSource> create_source(const Options& options, const TelemetryConfig& cfg, Clock& clock,
                                          std::string& error) {
    if (options.live) {
        auto bus = std::make_unique<LinuxI2cBus>(cfg.sensors.i2c_device);
        if (!bus->ok()) {
            error = bus->error();
            return nullptr;
        }
        // The raw pointer outlives the move below only for the message: the
        // source owns the bus from here on.
        const LinuxI2cBus* const bus_view = bus.get();
        auto live = std::make_unique<LiveSensorSource>(std::move(bus), cfg.sensors.barometer.address,
                                                       cfg.sensors.imu.address, clock,
                                                       cfg.sensors.imu.sample_rate_hz);
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
    return std::make_unique<SimulatedSource>(cfg.timing.period_ms, cfg.simulation.seed,
                                             cfg.simulation.gps_interval_ms, cfg.simulation.noise);
}

// The processing loop: source -> fault detector -> processor -> output.
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
// order, which is why they are created once outside the loop.
//
// The scheduler and jitter log are pointers, not references, because each
// may be absent: a reference cannot be null, a pointer can. Replay passes
// a null scheduler and runs unpaced; the frames carry their own timestamps
// and pacing would add nothing to the identity check (REQ-LOG-003).
//
// The publishers are base pointers in a vector, empty without --zmq or
// --udp. The loop publishes to each one it is given and never learns which
// transport it is: the DataSource rule applied to the output side
// (REQ-TRANS-001, REQ-TRANS-002).
ValidityTally run(DataSource& source, FaultDetector& detector, TelemetryProcessor& processor,
                  BinaryLogger& logger, const std::vector<Publisher*>& publishers,
                  FixedRateScheduler* scheduler, JitterLog* jitter, int frame_count) {
    ValidityTally tally;
    for (int i = 0; i < frame_count; i++) {
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

        // Non-virtual calls: one detector, one processor, behavior
        // configured rather than substituted.
        TelemetryFrame checked = detector.check(raw);
        TelemetryFrame processed = processor.process(checked);
        tally.account(processed);

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
        for (const FaultEvent& event : detector.take_events()) {
            logger.log_event(event);
            print_event(event);
        }
        std::cout << std::endl;
    }
    return tally;
}

// Usage: telemetry                        simulated source, 5 frames, config/telemetry_config.yaml
//        telemetry --config other.yaml    the same with another configuration file
//        telemetry --live --cycles 250    real sensors on the configured I2C bus, 5 s paced
//        telemetry --replay <log.bin>     replay a recorded log with the configuration recorded beside it
//        telemetry --zmq tcp://127.0.0.1:5555 --cycles 500
//                                         publish every processed frame; 10 s at 50 Hz
//        telemetry --udp 127.0.0.1:5556 --cycles 500
//                                         the same, one datagram per frame; both flags may be given
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
        // version-1 input is a legitimate replay, said so on one line: the
        // raw fields are the record and the pipeline recomputes the rest
        // (REQ-LOG-004), so its replay log is a version-2 log.
        const LogReader probe(input);
        if (!probe.ok()) {
            std::cerr << "error: cannot replay " << input.string() << ": " << probe.error() << std::endl;
            return 1;
        }
        std::cout << "Replay input:  " << input.string() << " (format version " << probe.header().version;
        if (probe.header().version != kLogFormatVersion) {
            std::cout << "; this build writes version " << kLogFormatVersion
                      << ", so the replay log is version " << kLogFormatVersion
                      << " with the computed fields and their validity recomputed from the raw fields";
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
    std::string source_error;
    std::unique_ptr<DataSource> source = create_source(options, cfg, clock, source_error);
    if (!source) {
        std::cerr << "error: " << source_error << std::endl;
        return 1;
    }
    // The composition root built the source and knows which one it built;
    // nothing below create_source() does. The replay source's counters are
    // read at exit so a run that outlasted its log says so (REQ-LOG-005):
    // a replay that ran past its log is a plausible-looking log too.
    const LogReplaySource* replay = replaying ? static_cast<const LogReplaySource*>(source.get()) : nullptr;

    // The processor lives on the stack: main owns it for the whole run and
    // nothing else needs to share it, so there is no reason for the heap.
    // One configured value feeds both: the IMU timeout is the detector's
    // fault bound and the processor's re-seed bound (REQ-PROC-002), and
    // they must be the same number.
    FaultDetector detector(cfg.fault);
    TelemetryProcessor processor(cfg.processing, cfg.fault.i2c_timeout_ms);

    BinaryLogger logger(cfg.logging.directory, prefix, cfg.logging.max_file_bytes);
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
        publisher.emplace(*zmq_context, zmq_endpoint);
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
        udp_publisher.emplace(udp_host, udp_port);
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

    // Pacing (REQ-TIME-001, REQ-TIME-002). Live and simulated runs are
    // held to the configured grid and their jitter is logged beside the
    // log; a replay run is unpaced (see run()). The objects are built
    // either way so their lifetime covers the loop; only the pointers
    // decide whether they act.
    FixedRateScheduler scheduler(clock, cfg.timing.period_ms * 1'000'000);
    const std::filesystem::path jitter_path = std::filesystem::path(cfg.logging.directory) / "telemetry_jitter.csv";
    std::optional<JitterLog> jitter;
    if (!replaying) {
        jitter.emplace(jitter_path);
    }

    // Baro Alt (REQ-PROC-001), Pitch/Roll (REQ-PROC-002), Fused Alt and
    // Vert Speed (REQ-PROC-003) and their validity (REQ-PROC-007) are all
    // computed by the processor. Status comes from the detector
    // (REQ-FAULT-004, REQ-FAULT-006); the simulator never faults.
    const ValidityTally tally = run(*source, detector, processor, logger, publishers,
                                    replaying ? nullptr : &scheduler,
                                    jitter.has_value() ? &jitter.value() : nullptr,
                                    options.cycles);

    std::cout << "Logged to " << logger.current_path().string()
              << "  (replay with: telemetry --replay <file>)" << std::endl;
    print_validity_summary(tally);

    if (replay != nullptr && replay->exhausted()) {
        std::cout << "Replay:    exhausted after " << replay->frames_read() << " frames; "
                  << static_cast<size_t>(options.cycles) - replay->frames_read()
                  << " cycles repeated the last frame" << std::endl;
    }

    if (jitter.has_value()) {
        const JitterStats& st = jitter->stats();
        std::cout << "Timing:    " << st.cycles << " cycles at " << cfg.timing.period_ms << " ms"
                  << ", jitter min/mean/max = " << st.min_ns / 1000.0 << " / "
                  << st.mean_ns() / 1000.0 << " / " << st.max_ns / 1000.0 << " us"
                  << ", missed cycles = " << st.missed_cycles
                  << "  (" << jitter_path.string() << ")" << std::endl;
    } else {
        std::cout << "Timing:    unpaced (replay)" << std::endl;
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
