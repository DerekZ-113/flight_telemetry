#include <iostream>
#include "telemetry_frame.h"
#include "processing/altitude.h"
#include "drivers/simulated_data.h"

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

int main() {
    std::cout << "flight-telemetry v0.1.0\n" << std::endl;

    // Create a simulated data source with a fixed seed (REQ-SENS-006).
    // Same seed = same noise sequence, so tests are reproducible.
    // This is separate from deterministic replay (REQ-LOG-003), which
    // comes from feeding logged frames back through the pipeline.
    SimulatedDataGenerator sim(42);

    // Generate 5 frames at 20ms intervals (50 Hz)
    // Each frame has different noise but the same base values
    for (int i = 0; i < 5; i++) {
        uint64_t timestamp = i * 20;  // 50 Hz = 20ms between frames
        TelemetryFrame frame = sim.generate(timestamp);
        print_frame(frame);
        std::cout << std::endl;
    }

    // Sanity checks
    std::cout << "Sanity check: pressure_to_altitude(1013.25) = "
              << pressure_to_altitude(1013.25f) << " m (should be 0.0)" << std::endl;
    std::cout << "Sanity check: pressure_to_altitude(898.76) = "
              << pressure_to_altitude(898.76f) << " m (should be ~1000)" << std::endl;

    return 0;
}
