# Flight Telemetry System — Software Requirements

> **Document ID:** FTS-SRD-001
> **Version:** 0.2.0
> **Status:** Draft
> **Last Updated:** 2026-09-09
> **Related:** FTS-TM-001 (traceability), FTS-DD-001 (design), FTS-TP-001 (test plan), FTS-FM-001 (fault model), FTS-VR-001 (verification results)

## Conventions

"Shall" marks a binding requirement. Every shall statement has a row in FTS-TM-001 that traces it to the code that implements it and the test that verifies it. Text after **Rationale:** explains a requirement and is not itself binding.

A *channel* is one sensor's data path: barometer (BMP280), IMU (MPU6050), or GPS (NEO-6M). Each channel is either NOMINAL or DEGRADED (REQ-FAULT-004). A *cycle* is one pass of the processing loop (REQ-TIME-001). A *TelemetryFrame* is the record the system produces once per cycle; its field names and units are defined in `src/telemetry_frame.h`. *Configurable* means set through the configuration file of REQ-CFG-001.

---

## Sensor Data Acquisition

REQ-SENS-001: The system shall read barometric pressure from the BMP280 sensor via I2C at a configurable sample rate between 1 Hz and 50 Hz.

REQ-SENS-002: The system shall read temperature from the BMP280 sensor via I2C with each pressure reading.

> **Rationale:** the BMP280 compensates pressure using its own temperature measurement, so the two are read together and an invalid temperature invalidates the pressure (FAULT-009).

REQ-SENS-003: The system shall read 3-axis accelerometer data from the MPU6050 sensor via I2C at a configurable IMU sample rate between 1 Hz and 100 Hz.

REQ-SENS-004: The system shall read 3-axis gyroscope data from the MPU6050 sensor via I2C at the IMU sample rate configured under REQ-SENS-003.

> **Rationale:** the MPU6050 has a single sample-rate divider for both sensors; accelerometer and gyroscope rates cannot be set independently.

REQ-SENS-005: The system shall read position (latitude, longitude, altitude), ground speed, and course over ground (heading) from the NEO-6M GPS module via UART as NMEA sentences.

REQ-SENS-006: The system shall provide a simulated data source that produces TelemetryFrames of raw sensor readings with configurable noise.

> **Rationale:** the pipeline can be developed and tested without hardware. Replay of recorded sessions is a separate function (REQ-LOG-003).

## Data Processing

REQ-PROC-001: The system shall convert barometric pressure to altitude using the ISA hypsometric formula, referenced to a configurable sea-level pressure.

REQ-PROC-002: The system shall compute pitch and roll angles from accelerometer and gyroscope data using a complementary filter.

REQ-PROC-003: The system shall fuse barometric altitude and GPS altitude using a 1D (single spatial axis) Kalman filter with two states, altitude and vertical velocity.

REQ-PROC-004: The Kalman filter's covariance parameters (measurement noise, process noise, and initial state covariance) shall be derived from sensor noise characteristics measured and recorded in docs/noise_profile.md.

REQ-PROC-005: Once per processing cycle (REQ-TIME-001), the system shall produce a TelemetryFrame containing the raw sensor readings, the computed and fused values, and the status of every channel.

## Fault Detection

REQ-FAULT-001: The system shall declare a communication fault on an I2C channel when no successful read has occurred within a configurable timeout (500 ms by default), and on the GPS channel when no NMEA sentence has been received within a configurable timeout longer than the module's output period.

REQ-FAULT-002: The system shall detect a stuck sensor condition: N consecutive identical readings on any channel, or on any single axis of a channel, where N is configurable.

REQ-FAULT-003: The system shall detect out-of-range or invalid sensor readings against configurable limits. Default limits: pressure 300 to 1100 hPa; temperature −40 to 85 °C; accelerometer within its configured full-scale range (±2 g at power-on); GPS fix quality greater than 0.

REQ-FAULT-004: Upon detecting a sensor fault, the system shall mark the affected channel DEGRADED, log a fault event carrying the timestamp, channel, and fault type, and continue processing with the remaining NOMINAL channels.

REQ-FAULT-005: Upon fault clearance (M consecutive valid readings on the affected channel, where M is configurable), the system shall mark the channel NOMINAL and log a recovery event.

## Timing

REQ-TIME-001: The main processing loop shall execute at a configurable fixed rate using absolute-time scheduling: on the target platform, clock_nanosleep with TIMER_ABSTIME.

REQ-TIME-002: The system shall measure and log, for every cycle, the timing jitter: the difference between the cycle's scheduled start time and its actual start time.

REQ-TIME-003: The system shall sustain the configured processing rate, with no missed cycles and a maximum per-cycle jitter below 1 ms, under normal operating conditions.

> **Rationale:** normal operating conditions means the target hardware with the standard configuration and no competing load, as exercised by TC-009 step 7 of FTS-TP-001.

## Transport

REQ-TRANS-001: The system shall publish every processed TelemetryFrame over ZeroMQ using the publish/subscribe pattern for local inter-process communication.

REQ-TRANS-002: The system shall publish every processed TelemetryFrame over UDP to a configurable destination address and port.

REQ-TRANS-003: A transport failure shall not block the processing loop or cause it to miss a cycle deadline (REQ-TIME-001).

## Logging and Replay

REQ-LOG-001: The system shall log every processed TelemetryFrame to a binary file on local storage.

REQ-LOG-002: The system shall rotate to a new log file when the current file would exceed a configurable maximum size.

REQ-LOG-003: When replaying a recorded binary log, the system shall produce processing outputs identical, bit for bit, to those of the original session recorded by the same build on the same platform.

> **Rationale:** cross-platform identity is not claimed, because standard-library math and random-number implementations differ between toolchains (FTS-DD-001 Section 8).

REQ-LOG-004: Replay mode shall use the same processing pipeline as live mode, differing only in data source.

## Configuration

REQ-CFG-001: The following system parameters shall be configurable through a YAML configuration file: sensor sample rates and the processing rate; fault detection timeouts, counts, and limits; filter tuning (Kalman covariances, complementary filter gain, and the sea-level reference pressure); transport addresses and ports; log file paths and maximum file size; and simulated-source noise.

REQ-CFG-002: At startup, the system shall validate every configuration value and report an error naming each invalid or out-of-range parameter.

## Testing

REQ-TEST-001: Every C++ module (drivers, processing, timing, logging, replay, transport) shall have unit tests written with Google Test.

REQ-TEST-002: The full data pipeline (C++ processing, transport, Python receiver) shall have integration tests written with pytest.

REQ-TEST-003: Structural coverage (statement and branch) shall be measured with gcov and lcov on every CI run, reported, and shall meet the thresholds defined in FTS-TP-001 Section 6.3.

REQ-TEST-004: cppcheck shall run on every build, and any release-tagged build shall have zero findings at the severity levels defined in FTS-TP-001.
