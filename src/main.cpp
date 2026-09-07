#include <iostream>
#include <cstdint>

// This struct will grow as we add sensors and processing.
// Right now it's the skeleton — every field maps to a sensor
// or a computed value in the final system.
struct TelemetryFrame {
    uint64_t timestamp_ms;      // milliseconds since system start

    // BMP280 (barometer)
    float pressure_hpa;         // raw pressure in hectopascals
    float temperature_c;        // temperature in celsius
    float baro_altitude_m;      // computed from pressure

    // MPU6050 (IMU)
    float accel_x, accel_y, accel_z;   // m/s^2
    float gyro_x, gyro_y, gyro_z;      // degrees/s
    float pitch_deg, roll_deg;          // computed from accel+gyro

    // NEO-6M (GPS)
    float latitude, longitude;
    float gps_altitude_m;
    float ground_speed_mps;     // meters per second

    // Fused outputs
    float fused_altitude_m;     // Kalman filter output (baro + GPS)
};

int main() {
    std::cout << "flight-telemetry v0.1.0" << std::endl;
    std::cout << "TelemetryFrame size: " << sizeof(TelemetryFrame) << " bytes" << std::endl;
    return 0;
}
