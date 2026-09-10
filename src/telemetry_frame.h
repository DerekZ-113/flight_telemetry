#pragma once

#include <cstdint>

// Health of one sensor channel. Only two states exist (REQ-FAULT-004/005);
// per-field validity flags come later with the fault detection module.
enum class ChannelStatus : uint8_t {
    NOMINAL = 0,
    DEGRADED = 1,
};

// Core data structure — one snapshot of all sensor readings, computed values,
// and channel health. Produced at the configured processing rate.
//
// Every field traces to a requirement:
//   Sensor fields    → REQ-SENS-001 through REQ-SENS-005
//   Computed fields  → REQ-PROC-001 through REQ-PROC-003
//   Channel status   → REQ-FAULT-004, REQ-FAULT-005
//   Read-ok flags    → REQ-FAULT-001 (communication timeout detection)
//   Fused outputs    → REQ-PROC-003 (Kalman states)
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

    // Channel health (REQ-FAULT-004). Owned by the fault detector; sources
    // set NOMINAL and never change it.
    ChannelStatus baro_status;
    ChannelStatus imu_status;
    ChannelStatus gps_status;

    // Whether the driver obtained a fresh reading this cycle (REQ-FAULT-001).
    // false means the fields for that channel are stale from the previous
    // cycle. This is not health: one failed I2C transaction is normal, and
    // the fault detector decides when staleness has lasted long enough to
    // be a fault. Sources report; the detector judges.
    bool baro_read_ok;
    bool imu_read_ok;
    bool gps_read_ok;
};

// Helper for printing channel status
const char* status_name(ChannelStatus status);
