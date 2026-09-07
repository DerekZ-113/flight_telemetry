#include <iostream>
#include <cstdint>
#include "processing/altitude.h"

// Health of one sensor channel. Only two states exist (REQ-FAULT-004/005);
// per-field validity flags come later with the fault detection module.
enum class ChannelStatus : uint8_t {
    NOMINAL = 0,
    DEGRADED = 1,
};

// This struct will grow as we add sensors and processing.
// Right now it's the skeleton — every field maps to a sensor
// or a computed value in the final system.
struct TelemetryFrame {
    uint64_t timestamp_ms;      // milliseconds since system start

    // BMP280 (barometer)
    float pressure_hpa;         // raw pressure in hectopascals
    float temperature_c;        // temperature in celsius
    float baro_altitude_m;      // computed from pressure (REQ-PROC-001)

    // MPU6050 (IMU)
    float accel_x, accel_y, accel_z;   // m/s^2
    float gyro_x, gyro_y, gyro_z;      // degrees/s
    float pitch_deg, roll_deg;          // complementary filter (REQ-PROC-002)

    // NEO-6M (GPS)
    // double, not float: float resolves to ~0.5 m at mid latitudes, which is
    // too close to the receiver's 2.5 m accuracy to throw away.
    double latitude, longitude;
    float gps_altitude_m;
    float ground_speed_mps;     // meters per second
    float heading_deg;          // course over ground, 0-360 (REQ-SENS-005)

    // Fused outputs (REQ-PROC-003: Kalman states are altitude and vertical velocity)
    float fused_altitude_m;
    float vertical_speed_mps;

    // Channel health (REQ-FAULT-004)
    ChannelStatus baro_status;
    ChannelStatus imu_status;
    ChannelStatus gps_status;
};

const char* status_name(ChannelStatus status) {
    return status == ChannelStatus::NOMINAL ? "NOMINAL" : "DEGRADED";
}

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

    // Create a frame with simulated data — as if we're sitting
    // at roughly Foster City elevation (~2m above sea level).
    // 1013.00 hPa is 0.25 hPa below the ISA standard, which the
    // formula maps to about 2 m. Each 1 hPa is roughly 8 m near sea level.
    TelemetryFrame frame;
    frame.timestamp_ms = 1000;
    frame.pressure_hpa = 1013.00f;
    frame.temperature_c = 21.0f;

    // Convert pressure to altitude — REQ-PROC-001
    frame.baro_altitude_m = pressure_to_altitude(frame.pressure_hpa);

    // Simulated IMU — board sitting flat, gravity on z-axis
    frame.accel_x = 0.05f;            // slight noise
    frame.accel_y = -0.02f;           // slight noise
    frame.accel_z = 9.78f;            // gravity (~9.81 m/s2)
    frame.gyro_x = 0.1f;
    frame.gyro_y = -0.05f;
    frame.gyro_z = 0.0f;
    frame.pitch_deg = 0.3f;           // nearly level
    frame.roll_deg = -0.1f;           // nearly level

    // Simulated GPS — Foster City area
    frame.latitude = 37.5585;
    frame.longitude = -122.2711;
    frame.gps_altitude_m = 3.0f;
    frame.ground_speed_mps = 0.0f;    // stationary
    frame.heading_deg = 0.0f;         // course over ground is meaningless when stationary

    // For now, fused altitude is just baro altitude and vertical speed is zero.
    // Kalman filter replaces both (REQ-PROC-003).
    frame.fused_altitude_m = frame.baro_altitude_m;
    frame.vertical_speed_mps = 0.0f;

    // No fault detection yet, so every channel is healthy by definition
    frame.baro_status = ChannelStatus::NOMINAL;
    frame.imu_status = ChannelStatus::NOMINAL;
    frame.gps_status = ChannelStatus::NOMINAL;

    print_frame(frame);

    // Sanity checks against the ISA standard atmosphere table.
    // These become the first two Google Test cases for this module.
    std::cout << "\nSanity check: pressure_to_altitude(1013.25) = "
              << pressure_to_altitude(1013.25f) << " m (should be 0.0)" << std::endl;
    std::cout << "Sanity check: pressure_to_altitude(898.76) = "
              << pressure_to_altitude(898.76f) << " m (should be ~1000)" << std::endl;

    return 0;
}
