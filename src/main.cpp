#include <iostream>
#include <memory>
#include "telemetry_frame.h"
#include "data_source.h"
#include "processing/altitude.h"
#include "processing/processor.h"

// The only place that knows a concrete source type exists. Everything
// below create_source() sees DataSource and nothing else.
#include "drivers/simulated_source.h"

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
}

// Factory: decides which concrete DataSource to build. Later this reads
// the YAML config (REQ-CFG-001) and returns a live-sensor source, the
// simulator, or a log replay source. The return type is the base class,
// so callers cannot tell which one they got. That is the point.
//
// std::make_unique constructs a SimulatedSource on the heap and wraps the
// pointer in a std::unique_ptr<SimulatedSource>, which converts implicitly
// to std::unique_ptr<DataSource> because SimulatedSource is-a DataSource.
std::unique_ptr<DataSource> create_source() {
    // 20 ms interval = 50 Hz. Seed 42 keeps the run reproducible (REQ-SENS-006).
    // Reproducibility here is separate from deterministic replay (REQ-LOG-003),
    // which comes from feeding logged frames back through the pipeline.
    return std::make_unique<SimulatedSource>(20, 42);
}

// The processing loop: source -> processor -> output.
//
// Takes the source as the base class by reference and never learns the
// concrete type. Swapping the simulator for real sensors or a log replay
// changes create_source() and nothing here (REQ-LOG-004).
//
// The processor is passed by non-const reference because process() will
// mutate filter state on every call once the Kalman and complementary
// filters exist. The same processor instance must see every frame in
// order, which is why it is created once outside the loop, not per frame.
void run(DataSource& source, TelemetryProcessor& processor, int frame_count) {
    for (int i = 0; i < frame_count; i++) {
        // Virtual dispatch: the compiler emits a lookup through the object's
        // vtable, so this line runs SimulatedSource::read_frame() today and
        // would run Bmp280Source::read_frame() or LogReplaySource::read_frame()
        // tomorrow, with no change to this code.
        TelemetryFrame raw = source.read_frame();

        // Non-virtual call: there is one TelemetryProcessor, and its
        // behavior is configured, not substituted.
        TelemetryFrame processed = processor.process(raw);

        print_frame(processed);
        std::cout << std::endl;
    }
}

int main() {
    std::cout << "flight-telemetry v0.1.0\n" << std::endl;

    // unique_ptr owns the source. When `source` goes out of scope at the end
    // of main, its destructor deletes the object through the DataSource
    // pointer, which is why ~DataSource must be virtual.
    std::unique_ptr<DataSource> source = create_source();

    // The processor lives on the stack: main owns it for the whole run and
    // nothing else needs to share it, so there is no reason for the heap.
    TelemetryProcessor processor;

    // Baro Alt (REQ-PROC-001), Fused Alt and Vert Speed (REQ-PROC-003) are
    // now computed. Pitch/roll still print 0 until the complementary
    // filter exists.
    run(*source, processor, 5);

    // Sanity checks
    std::cout << "Sanity check: pressure_to_altitude(1013.25) = "
              << pressure_to_altitude(1013.25f) << " m (should be 0.0)" << std::endl;
    std::cout << "Sanity check: pressure_to_altitude(898.76) = "
              << pressure_to_altitude(898.76f) << " m (should be ~1000)" << std::endl;

    return 0;
}
