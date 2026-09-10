# Flight Telemetry System — Software Requirements

> **Document ID:** FTS-SRD-001
> **Version:** 0.1.0
> **Status:** Draft
> **Last Updated:** 2026-09-06
> **Related:** FTS-TM-001 (traceability), FTS-DD-001 (design), FTS-TP-001 (test plan), FTS-FM-001 (fault model)

---

## Sensor Data Acquisition

REQ-SENS-001: The system shall read barometric pressure from a BMP280 sensor via I2C at a configurable rate between 1 Hz and 50 Hz.

REQ-SENS-002: The system shall read temperature from a BMP280 sensor via I2C.

REQ-SENS-003: The system shall read 3-axis accelerometer data from an MPU6050 sensor via I2C at a configurable IMU sample rate between 1 Hz and 100 Hz.

REQ-SENS-004: The system shall read 3-axis gyroscope data from an MPU6050 sensor via I2C at the same configurable IMU sample rate as REQ-SENS-003. The MPU6050 uses a single sample-rate divider for both sensors, so accelerometer and gyroscope rates cannot be set independently.

REQ-SENS-005: The system shall read GPS position (latitude, longitude, altitude), ground speed, and course over ground (heading) from a NEO-6M module via UART.

REQ-SENS-006: The system shall support a simulated data source that produces telemetry frames with configurable noise, for use in testing and replay.

## Data Processing

REQ-PROC-001: The system shall convert barometric pressure to altitude using the hypsometric formula, referenced to a configurable sea-level pressure.

REQ-PROC-002: The system shall compute pitch and roll angles from accelerometer and gyroscope data using a complementary filter.

REQ-PROC-003: The system shall fuse barometric altitude and GPS altitude using a 1D Kalman filter with two states (altitude, vertical velocity).

REQ-PROC-004: The Kalman filter covariance parameters shall be derived from measured sensor noise characteristics.

REQ-PROC-005: The system shall produce a complete TelemetryFrame containing raw sensor readings, computed values, and fused outputs at the configured processing rate.

## Fault Detection

REQ-FAULT-001: The system shall detect a sensor communication failure within a bounded time: no successful I2C read within 500 ms, or no NMEA sentence received on UART within a configurable timeout that exceeds the GPS module's output period.

REQ-FAULT-002: The system shall detect a stuck sensor condition (N consecutive identical readings on any channel or axis, where N is configurable).

REQ-FAULT-003: The system shall detect out-of-range or invalid sensor readings based on configurable limits (e.g., pressure outside 300–1100 hPa, temperature outside -40–85°C, accelerometer at full-scale, GPS fix quality of 0).

REQ-FAULT-004: Upon detecting a sensor fault, the system shall mark the affected channel as DEGRADED, log a fault event with timestamp and fault type, and continue processing all remaining healthy sensors.

REQ-FAULT-005: Upon fault clearance (M consecutive valid readings from the affected sensor, where M is configurable), the system shall mark the channel as NOMINAL and log the recovery event.

## Timing

REQ-TIME-001: The main processing loop shall execute at a configurable fixed rate using absolute-time scheduling (clock_nanosleep with TIMER_ABSTIME).

REQ-TIME-002: The system shall measure and log per-cycle timing jitter (difference between scheduled and actual execution time).

REQ-TIME-003: The system shall sustain the configured processing rate with jitter below 1 ms under normal operating conditions.

## Transport

REQ-TRANS-001: The system shall publish processed TelemetryFrames via ZeroMQ using a pub/sub pattern for local IPC.

REQ-TRANS-002: The system shall publish processed TelemetryFrames via UDP to a configurable IP address and port number.

REQ-TRANS-003: Transport failures shall not block or delay the processing loop.

## Logging and Replay

REQ-LOG-001: The system shall log processed TelemetryFrames to a binary file on local storage.

REQ-LOG-002: The system shall support log file rotation based on configurable maximum file size.

REQ-LOG-003: The system shall support deterministic replay — reading from a recorded binary log and producing identical processing outputs as the original live session.

REQ-LOG-004: Replay mode shall use the same processing pipeline as live mode, differing only in data source.

## Configuration

REQ-CFG-001: System parameters (sampling rates, fault thresholds, transport addresses, log file paths, Kalman tuning) shall be configurable via a YAML configuration file.

REQ-CFG-002: The system shall validate configuration values at startup and report errors for invalid or out-of-range parameters.

## Testing

REQ-TEST-001: All C++ modules (drivers, processing, timing, logging, transport) shall have unit test coverage using Google Test.

REQ-TEST-002: The full data pipeline (C++ processing → transport → Python receiver) shall have integration test coverage using pytest.

REQ-TEST-003: Structural code coverage (statement and branch) shall be measured using gcov/lcov and reported in CI.

REQ-TEST-004: Static analysis (cppcheck) shall run on every build with zero warnings in the final release.
