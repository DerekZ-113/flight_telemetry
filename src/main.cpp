#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include "telemetry_frame.h"
#include "data_source.h"
#include "logging/binary_logger.h"
#include "processing/altitude.h"
#include "processing/processor.h"
#include "processing/fault_detector.h"
#include "timing/clock.h"
#include "timing/fixed_rate_scheduler.h"
#include "timing/jitter_log.h"

// The only place that knows concrete source types exist. Everything
// below create_source() sees DataSource and nothing else.
#include "drivers/simulated_source.h"
#include "replay/log_replay_source.h"

// Log rotation size. Placeholder until config (REQ-CFG-001).
constexpr uint64_t kMaxLogFileBytes = 1u << 20;   // 1 MiB, about 10,000 frames

// Processing period, 50 Hz (REQ-TIME-001). Placeholder until config
// (REQ-CFG-001). Digit separators: 20 ms in nanoseconds.
constexpr uint64_t kPeriodNs = 20'000'000;

void print_frame(const TelemetryFrame& frame) {
    std::cout << "=== Telemetry Frame ===" << std::endl;
    std::cout << "Timestamp:    " << frame.timestamp_ms << " ms" << std::endl;
    std::cout << "Pressure:     " << frame.pressure_hpa << " hPa" << std::endl;
    std::cout << "Temperature:  " << frame.temperature_c << " C" << std::endl;
    std::cout << "Baro Alt:     " << frame.baro_altitude_m << " m" << std::endl;
    std::cout << "Accel:        [" << frame.accel_x << ", "
              << frame.accel_y << ", " << frame.accel_z << "] m/s2" << std::endl;
    std::cout << "Gyro:         [" << frame.gyro_x << ", "
              << frame.gyro_y << ", " << frame.gyro_z << "] deg/s" << std::endl;
    std::cout << "Pitch/Roll:   " << frame.pitch_deg << " / "
              << frame.roll_deg << " deg" << std::endl;
    std::cout << "GPS:          " << frame.latitude << ", "
              << frame.longitude << std::endl;
    std::cout << "GPS Alt:      " << frame.gps_altitude_m << " m" << std::endl;
    std::cout << "Ground Speed: " << frame.ground_speed_mps << " m/s" << std::endl;
    std::cout << "Heading:      " << frame.heading_deg << " deg" << std::endl;
    std::cout << "Fused Alt:    " << frame.fused_altitude_m << " m" << std::endl;
    std::cout << "Vert Speed:   " << frame.vertical_speed_mps << " m/s" << std::endl;
    std::cout << "Status:       baro=" << status_name(frame.baro_status)
              << " imu=" << status_name(frame.imu_status)
              << " gps=" << status_name(frame.gps_status) << std::endl;
    std::cout << "Read OK:      baro=" << frame.baro_read_ok
              << " imu=" << frame.imu_read_ok
              << " gps=" << frame.gps_read_ok << std::endl;
}

void print_event(const FaultEvent& event) {
    std::cout << "[FAULT] t=" << event.timestamp_ms << " ms"
              << " channel=" << channel_name(event.channel)
              << " type=" << fault_type_name(event.type)
              << " value=" << event.value << std::endl;
}

// Factory: decides which concrete DataSource to build. Today the choice
// is one command-line flag; later it reads the YAML config (REQ-CFG-001)
// and adds the live-sensor source. The return type is the base class, so
// callers cannot tell which one they got. That is the point (REQ-LOG-004).
//
// std::make_unique constructs the object on the heap and wraps the
// pointer in a std::unique_ptr<Derived>, which converts implicitly to
// std::unique_ptr<DataSource> because Derived is-a DataSource.
std::unique_ptr<DataSource> create_source(int argc, char* argv[]) {
    if (argc >= 3 && std::strcmp(argv[1], "--replay") == 0) {
        return std::make_unique<LogReplaySource>(
            std::vector<std::filesystem::path>{argv[2]});
    }
    // 20 ms interval = 50 Hz. Seed 42 keeps the run reproducible (REQ-SENS-006).
    // Reproducibility here is separate from deterministic replay (REQ-LOG-003),
    // which comes from feeding logged frames back through the pipeline.
    return std::make_unique<SimulatedSource>(20, 42);
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
// The scheduler and jitter log are pointers, not references, because they
// may be absent: a reference cannot be null, a pointer can. Replay passes
// nullptr and runs unpaced; the frames carry their own timestamps and
// pacing would add nothing to the identity check (REQ-LOG-003).
void run(DataSource& source, FaultDetector& detector, TelemetryProcessor& processor,
         BinaryLogger& logger, FixedRateScheduler* scheduler, JitterLog* jitter,
         int frame_count) {
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
        // would run Bmp280Source::read_frame() or LogReplaySource::read_frame()
        // tomorrow, with no change to this code.
        TelemetryFrame raw = source.read_frame();

        // Non-virtual calls: one detector, one processor, behavior
        // configured rather than substituted.
        TelemetryFrame checked = detector.check(raw);
        TelemetryFrame processed = processor.process(checked);

        // Processed frames are what the log holds (REQ-LOG-001); replay
        // strips them back to raw before the pipeline sees them again.
        logger.log_frame(processed);

        print_frame(processed);
        for (const FaultEvent& event : detector.take_events()) {
            logger.log_event(event);
            print_event(event);
        }
        std::cout << std::endl;
    }
}

// Usage: telemetry                      simulated source, 5 frames
//        telemetry --replay <log.bin>   replay a recorded log instead
int main(int argc, char* argv[]) {
    std::cout << "flight-telemetry v0.1.0\n" << std::endl;

    // unique_ptr owns the source. When `source` goes out of scope at the end
    // of main, its destructor deletes the object through the DataSource
    // pointer, which is why ~DataSource must be virtual.
    std::unique_ptr<DataSource> source = create_source(argc, argv);

    // The processor lives on the stack: main owns it for the whole run and
    // nothing else needs to share it, so there is no reason for the heap.
    FaultDetector detector;   // default thresholds until config (REQ-CFG-001)
    TelemetryProcessor processor;

    // A replay run logs under a different prefix. The replay source opens
    // its input before the logger opens its output, and both live in
    // ./logs; with the same prefix the logger would truncate the very
    // file being replayed. Different names keep session A and session B
    // side by side, which is also what TC-004 compares.
    const bool replaying = (argc >= 3 && std::strcmp(argv[1], "--replay") == 0);
    BinaryLogger logger("logs", replaying ? "replay" : "telemetry", kMaxLogFileBytes);
    if (!logger.ok()) {
        std::cerr << "cannot open log file in ./logs" << std::endl;
        return 1;
    }

    // Pacing (REQ-TIME-001, REQ-TIME-002). Live and simulated runs are
    // held to the 50 Hz grid and their jitter is logged; a replay run is
    // unpaced (see run()). The objects are built either way so their
    // lifetime covers the loop; only the pointers decide whether they act.
    MonotonicClock clock;
    FixedRateScheduler scheduler(clock, kPeriodNs);
    std::optional<JitterLog> jitter;
    if (!replaying) {
        jitter.emplace("logs/telemetry_jitter.csv");
    }

    // Baro Alt (REQ-PROC-001), Pitch/Roll (REQ-PROC-002), Fused Alt and
    // Vert Speed (REQ-PROC-003) are all computed by the processor. Status
    // comes from the detector (REQ-FAULT-004); the simulator never faults.
    run(*source, detector, processor, logger,
        replaying ? nullptr : &scheduler,
        jitter.has_value() ? &jitter.value() : nullptr,
        5);

    std::cout << "Logged to " << logger.current_path().string()
              << "  (replay with: telemetry --replay <file>)" << std::endl;

    if (jitter.has_value()) {
        const JitterStats& st = jitter->stats();
        std::cout << "Timing:    " << st.cycles << " cycles at " << kPeriodNs / 1'000'000 << " ms"
                  << ", jitter min/mean/max = " << st.min_ns / 1000.0 << " / "
                  << st.mean_ns() / 1000.0 << " / " << st.max_ns / 1000.0 << " us"
                  << ", missed cycles = " << st.missed_cycles
                  << "  (logs/telemetry_jitter.csv)" << std::endl;
    } else {
        std::cout << "Timing:    unpaced (replay)" << std::endl;
    }

    // Sanity checks
    std::cout << "Sanity check: pressure_to_altitude(1013.25) = "
              << pressure_to_altitude(1013.25f) << " m (should be 0.0)" << std::endl;
    std::cout << "Sanity check: pressure_to_altitude(898.76) = "
              << pressure_to_altitude(898.76f) << " m (should be ~1000)" << std::endl;

    return 0;
}
