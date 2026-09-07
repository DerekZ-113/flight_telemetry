# Flight Telemetry System — Fault Model

> **Document ID:** FTS-FM-001
> **Version:** 0.1.0
> **Status:** Draft
> **Last Updated:** 2026-09-06

This document defines every known failure mode, how the system detects it, and how the system responds. Each entry traces to a requirement and a test.

---

## Sensor Communication Failures

### FAULT-001: BMP280 I2C timeout

- **What:** BMP280 stops responding to I2C reads (no ACK on the bus)
- **Cause:** Loose wire, sensor hardware failure, I2C bus contention
- **Likelihood:** Low
- **Detection:** I2C read returns an error (NACK or bus timeout). Fault is declared when no successful read has occurred within 500 ms, allowing transient errors to be retried.
- **Response:** Mark barometer channel DEGRADED. Log fault event with timestamp. Continue processing with IMU + GPS only. Fused altitude relies solely on GPS until barometer recovers.
- **Requirement:** REQ-FAULT-001, REQ-FAULT-004
- **Test:** `tests/unit/test_bmp280.cpp` → TBD

### FAULT-002: MPU6050 I2C timeout

- **What:** MPU6050 stops responding to I2C reads
- **Cause:** Loose wire, sensor hardware failure, I2C bus contention
- **Likelihood:** Low
- **Detection:** I2C read returns an error (NACK or bus timeout). Fault is declared when no successful read has occurred within 500 ms.
- **Response:** Mark IMU channel DEGRADED. Log fault event. Continue processing with barometer + GPS only. Pitch and roll marked invalid until IMU recovers.
- **Requirement:** REQ-FAULT-001, REQ-FAULT-004
- **Test:** `tests/unit/test_mpu6050.cpp` → TBD

### FAULT-003: NEO-6M UART timeout or GPS fix loss

- **What:** Two distinct conditions with the same response. (a) The GPS module stops sending NMEA sentences on UART. (b) The module keeps sending sentences but reports no satellite fix (fix quality 0).
- **Cause:** (a) Loose wire, wrong baud rate, module power issue. (b) Indoor operation, obstructed sky view.
- **Likelihood:** (a) Low. (b) Medium, especially indoors or near windows with partial sky.
- **Detection:** (a) No NMEA sentence received within the configured UART timeout. The timeout must exceed the module's 1 Hz default output period, so it is configurable rather than fixed at 500 ms. (b) Fix quality indicator in the GGA sentence reads 0.
- **Response:** Mark GPS channel DEGRADED. Log fault event with the condition (timeout or no-fix). Continue processing with barometer + IMU only. Fused altitude relies solely on barometer. Position, heading, and ground speed marked invalid.
- **Requirement:** (a) REQ-FAULT-001. (b) REQ-FAULT-003. Both: REQ-FAULT-004
- **Test:** `tests/unit/test_gps.cpp` → TBD

### FAULT-004: I2C bus failure (both BMP280 and MPU6050 affected)

- **What:** Entire I2C bus becomes unresponsive — both BMP280 and MPU6050 fail simultaneously
- **Cause:** SDA or SCL wire disconnected, bus lockup, electrical issue
- **Likelihood:** Very low
- **Detection:** Both I2C devices time out within the same 500 ms window
- **Response:** Mark barometer and IMU channels DEGRADED. Log a single bus-level fault event rather than two separate sensor faults. Continue processing with GPS only. System is heavily degraded but still running — altitude from GPS only, no attitude data.
- **Requirement:** REQ-FAULT-001, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → TBD

---

## Stuck Sensor Failures

### FAULT-005: BMP280 stuck pressure reading

- **What:** Barometer returns identical pressure values on consecutive reads
- **Cause:** Sensor firmware hang, register read returning stale cached data
- **Likelihood:** Low
- **Detection:** N consecutive identical pressure readings (N configurable, default 10)
- **Response:** Mark barometer channel DEGRADED. Log fault event with the stuck value. Continue with IMU + GPS.
- **Requirement:** REQ-FAULT-002, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → TBD

### FAULT-006: MPU6050 stuck accelerometer

- **What:** Accelerometer returns identical raw values on consecutive reads on one or more axes
- **Cause:** Sensor firmware hang, I2C read returning stale data
- **Likelihood:** Low
- **Detection:** N consecutive identical raw readings on any axis (N configurable, default 10). Comparison uses the raw 16-bit register values, since a live sensor at rest still shows LSB-level noise.
- **Response:** Mark IMU channel DEGRADED. Log fault event. Pitch and roll marked invalid until the accelerometer recovers. Attitude is not held at the last good value, so the display shows a clear failure rather than a frozen reading that looks live.
- **Requirement:** REQ-FAULT-002, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → TBD

### FAULT-007: MPU6050 stuck gyroscope

- **What:** Gyroscope returns identical raw angular rate values on consecutive reads on one or more axes
- **Cause:** Sensor firmware hang, register stale
- **Likelihood:** Low
- **Detection:** N consecutive identical raw readings on any axis (N configurable, default 10)
- **Response:** Mark IMU channel DEGRADED with gyroscope data excluded. Complementary filter falls back to accelerometer-only attitude, which is noisier but still valid because gravity alone determines static pitch and roll. Log fault event.
- **Requirement:** REQ-FAULT-002, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → TBD

---

## Out-of-Range Failures

### FAULT-008: BMP280 pressure out of physical range

- **What:** Pressure reading falls outside physically possible range
- **Cause:** Sensor malfunction, calibration corruption, electrical noise
- **Likelihood:** Very low
- **Detection:** Pressure reading below 300 hPa or above 1100 hPa (configurable limits). These are the BMP280 datasheet operating limits. 300 hPa corresponds to roughly 9,000 m, far above anything this system will see. 1100 hPa is above the highest sea-level pressure ever recorded (about 1084 hPa).
- **Response:** Discard reading. Mark barometer channel DEGRADED. Log fault event with the out-of-range value. Continue with IMU + GPS.
- **Requirement:** REQ-FAULT-003, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → TBD

### FAULT-009: BMP280 temperature out of sensor range

- **What:** Temperature reading falls outside the sensor's operating range
- **Cause:** Sensor malfunction, calibration corruption
- **Likelihood:** Very low
- **Detection:** Temperature below -40°C or above 85°C (BMP280 specified operating range)
- **Response:** Discard reading. Mark barometer channel DEGRADED. Log fault event with the out-of-range value. The BMP280 computes compensated pressure from its own temperature measurement, so an invalid temperature also invalidates pressure and barometric altitude. Continue with IMU + GPS.
- **Requirement:** REQ-FAULT-003, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → TBD

### FAULT-010: MPU6050 accelerometer saturation

- **What:** Acceleration reading hits the configured full-scale range of the sensor
- **Cause:** Physical shock (dropping the board), sensor configured at too narrow a range for the motion
- **Likelihood:** Low (more likely during handling)
- **Detection:** Any axis reads at or near the configured full-scale range (±2g at the MPU6050 power-on default, configurable up to ±16g)
- **Response:** Discard reading. Mark IMU channel DEGRADED. Pitch and roll marked invalid until readings return within range. Log fault event with the clipped value.
- **Requirement:** REQ-FAULT-003, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → TBD

> **TODO (deferred, decide in build session):** The gyroscope has an equivalent saturation mode (±250°/s at power-on default, up to ±2000°/s). No fault entry exists yet.

---

## Recovery

### FAULT-011: Sensor recovery from DEGRADED state

- **What:** A previously failed sensor resumes producing valid readings
- **Cause:** Intermittent connection restored, sensor firmware recovers, GPS reacquires lock
- **Likelihood:** Medium (especially GPS after temporary obstruction)
- **Detection:** M consecutive valid readings after a DEGRADED state (M configurable, default 5). Requiring more than a single good reading prevents a flapping sensor from toggling between states every cycle.
- **Response:** Mark channel as NOMINAL. Log recovery event with timestamp and duration of degraded state. Resume using sensor data in processing pipeline and Kalman filter.
- **Requirement:** REQ-FAULT-005, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → TBD

---

## System-Level Degradation Summary

| Sensors Available | System Capability |
|---|---|
| All NOMINAL | Full operation — fused altitude, attitude, position, heading |
| BMP280 DEGRADED | Altitude from GPS only (noisier). Attitude and position normal. |
| MPU6050 DEGRADED | No pitch/roll. Altitude and position normal. |
| GPS DEGRADED | Altitude from barometer only (drifts with changes in sea-level pressure). No position or heading. |
| BMP280 + MPU6050 DEGRADED | GPS only — position, heading, and GPS altitude. No attitude. Heavily degraded. |
| BMP280 + GPS DEGRADED | IMU only — attitude available. No altitude, position, or heading. Critically degraded. |
| MPU6050 + GPS DEGRADED | Barometer only — barometric altitude. No attitude, position, or heading. Critically degraded. |
| All DEGRADED | System logs fault, continues running (processing loop doesn't crash), but every output is flagged invalid. |

The system never stops. A crashed telemetry process is worse than a degraded one producing partial data. This is the fundamental principle: degrade gracefully, never crash.
