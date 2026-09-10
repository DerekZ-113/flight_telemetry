# Flight Telemetry System — Traceability Matrix

> **Document ID:** FTS-TM-001
> **Version:** 0.1.0
> **Status:** Draft
> **Last Updated:** 2026-09-09
> **Related:** FTS-SRD-001 (requirements), FTS-TP-001 (test plan), FTS-DD-001 (design), FTS-FM-001 (fault model)

This matrix records, for every requirement in FTS-SRD-001, the code that implements it and the test that verifies it. It is the bidirectional trace DO-178C Table A-7 asks for: reading a row left to right shows that a requirement has code and a test; reading the Implementation column against the source tree shows that no code exists without a requirement. Both directions are checked in Section 9.

Requirement text is not repeated here. It lives in FTS-SRD-001 and is referenced by ID so that one edit updates one place. The Notes column carries what this document adds: what is missing when a row is not fully verified.

A row whose Implementation column is filled and whose Test column is empty fails the build (FTS-TP-001 Section 4, criterion 5). The intent is that code and its test land in the same commit, and this table is updated in that commit.

## Status Definitions

| Status | Meaning |
|---|---|
| **Verified** | Implementation and requirements-based test are both present and the test passes. |
| **Partial** | Some implementation or some test coverage exists, but the requirement is not fully demonstrated. Notes says what is missing. |
| **Implemented** | Code exists, no test yet. Not a permitted state on the main branch; reserved for work in progress. |
| **Not started** | No code, no test. |

Test functions are named as `file: TestSuite.TestName`. "(indirect)" means the requirement is exercised by a test written for another requirement; it counts as coverage but not as a requirements-based test of its own.

---

## 1. Sensor Data Acquisition

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-SENS-001 | — | — | — | Not started | BMP280 driver, Sprint 1 week 2 |
| REQ-SENS-002 | — | — | — | Not started | BMP280 driver |
| REQ-SENS-003 | — | — | — | Not started | MPU6050 driver |
| REQ-SENS-004 | — | — | — | Not started | MPU6050 driver, shared sample-rate divider |
| REQ-SENS-005 | — | — | — | Not started | NEO-6M driver, NMEA parsing |
| REQ-SENS-006 | `src/drivers/simulated_data.cpp` (`SimulatedDataGenerator`), `src/drivers/simulated_source.cpp` (`SimulatedSource`) | `test_kalman.cpp: KalmanTest.ConvergenceReducesNoise`, `KalmanTest.DeterministicOutput` (indirect) | — | Partial | Noise sigmas are hardcoded in the generator constructor, not configurable. No direct driver test asserts the distributions or the seed behavior. |

## 2. Data Processing

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-PROC-001 | `src/processing/altitude.cpp` (`pressure_to_altitude`) | `test_altitude.cpp: AltitudeTest.*` (8 tests) | TC-001 | Verified | |
| REQ-PROC-002 | `src/processing/complementary_filter.cpp` (`ComplementaryFilter`), `src/processing/processor.cpp` (`TelemetryProcessor::process`) | `test_complementary.cpp: ComplementaryTest.*` (7 tests) | TC-006 | Verified | Axis and sign convention is an assumption until verified on hardware. DEGRADED holds the last angles; FAULT-006 requires an invalid marker (FTS-DD-001 Section 6). |
| REQ-PROC-003 | `src/processing/kalman_filter.cpp` (`KalmanFilter1D`), `src/processing/processor.cpp` (`TelemetryProcessor::process`) | `test_kalman.cpp: KalmanTest.*` (6 tests) | TC-007 | Verified | Covariance constants are placeholders (see REQ-PROC-004). |
| REQ-PROC-004 | — | — | — | Not started | Constants in `processor.cpp` are placeholders pending `docs/noise_profile.md`. |
| REQ-PROC-005 | `src/processing/processor.cpp` (`TelemetryProcessor::process`), `src/telemetry_frame.cpp`, `src/main.cpp` (`run`) | `test_kalman.cpp: KalmanTest.ConvergenceReducesNoise`, `test_complementary.cpp: ComplementaryTest.DegradedImuHoldsAttitude` (indirect) | — | Partial | No fixed-rate loop yet (REQ-TIME-001). No test asserts that every computed field is filled. |

## 3. Fault Detection

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-FAULT-001 | `src/processing/fault_detector.cpp` (`FaultDetector::comm_timed_out`) | `test_fault_detection.cpp: FaultDetectionTest.BaroTimeoutDetectedWithinBound`, `ImuTimeoutDetectedWithinBound`, `GpsTimeoutUsesUartBound`, `TransientReadErrorDoesNotFault`, `TimeoutViaInjector` | TC-002 | Verified | Time is `frame.timestamp_ms`; no wall clock. Driver-level I2C/UART error reporting lands with the drivers. |
| REQ-FAULT-002 | `src/processing/fault_detector.cpp` (`StuckTracker::push`) | `test_fault_detection.cpp: FaultDetectionTest.TenthIdenticalPressureIsStuck`, `StuckAccelXWithOtherAxesVarying`, `RepeatsOfTwoAreNotStuck`, `DifferingValueResetsRun`, `StuckGyroZ`, `StaleValuesDuringDropoutAreNotStuck` | TC-003 | Verified | GPS channel has no stuck detection: no FAULT entry covers it and a stationary receiver legitimately repeats. |
| REQ-FAULT-003 | `src/processing/fault_detector.cpp` (`observe_baro`, `observe_imu`) | `test_fault_detection.cpp: FaultDetectionTest.PressureBelowRange`, `PressureAboveRange`, `TemperatureOutOfRangeDegradesBaro`, `AccelSaturationDegradesImu`, `BoundaryValuesAreValid` | — | Partial | GPS fix quality (FAULT-003b) deferred until the frame carries the field. |
| REQ-FAULT-004 | `src/processing/fault_detector.cpp` (`FaultDetector::transition`, `FaultEvent`), `src/main.cpp` (`run`) | `test_fault_detection.cpp: FaultDetectionTest.ProcessingContinuesOnDegradedChannel`, `OtherChannelsStayNominalDuringTimeout`, `BusFailureDegradesBothI2cChannels` | — | Verified | Events are `FaultEvent` structs persisted by `BinaryLogger::log_event` as tagged records in the telemetry log. FAULT-004 single bus-level event deferred. |
| REQ-FAULT-005 | `src/processing/fault_detector.cpp` (`FaultDetector::transition`) | `test_fault_detection.cpp: FaultDetectionTest.RecoveryAfterMValidReadings`, `BadReadingResetsRecoveryCount`, `StuckSensorRecovers` | — | Verified | |

## 4. Timing

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-TIME-001 | — | — | — | Not started | |
| REQ-TIME-002 | — | — | — | Not started | |
| REQ-TIME-003 | — | — | — | Not started | Verified on Pi hardware only. |

## 5. Transport

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-TRANS-001 | — | — | TC-005 | Not started | |
| REQ-TRANS-002 | — | — | — | Not started | |
| REQ-TRANS-003 | — | — | — | Not started | |

## 6. Logging and Replay

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-LOG-001 | `src/logging/log_format.h/.cpp` (`LogFileHeader`, `RecordType`), `src/logging/binary_logger.cpp` (`BinaryLogger`), `src/logging/log_reader.cpp` (`LogReader`), `src/main.cpp` (`run`) | `test_logging.cpp: LoggingTest.*` (22 tests) | TC-008 | Verified | Raw-struct records with a versioned, size-checked header. Same-platform format by design (FTS-DD-001 §8, §9). |
| REQ-LOG-002 | `src/logging/binary_logger.cpp` (`BinaryLogger::write_record`, `open_next_file`) | `test_logging.cpp: LoggingTest.RotationSplitsAtRecordBoundary`, `TinyLimitStillWritesOneRecordPerFile`, `CurrentPathTracksRotation`, `RotationIntoRemovedDirectoryReportsNotOk` | TC-008 | Verified | Limit is a constant in `main.cpp` until REQ-CFG-001. |
| REQ-LOG-003 | `src/replay/log_replay_source.cpp` (`LogReplaySource`), `src/logging/log_reader.cpp` | `test_replay.cpp: ReplayTest.ReplayReproducesLiveSession` (500 frames, injected faults, `memcmp` on every frame and event); file-level: `logs/telemetry_000.bin` == `logs/replay_000.bin` | TC-004 | Partial | Unit and file-level identity proven on one toolchain. TC-004's Python parse step (integration level) waits on the receiver. |
| REQ-LOG-004 | `src/data_source.h` (`DataSource`), `src/drivers/simulated_source.cpp`, `src/replay/log_replay_source.cpp`, `src/main.cpp` (`create_source`, `run`) | `test_replay.cpp: ReplayTest.ReplayStripsComputedFieldsAndStatus`, `ReplaySpansRotatedFiles`, `ReplayReproducesLiveSession` | TC-004 | Verified | Two concrete sources share one pipeline; `main` selects by `--replay`. Replay strips computed fields and status so the pipeline must recompute them. |

## 7. Configuration

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-CFG-001 | — | — | — | Not started | |
| REQ-CFG-002 | — | — | — | Not started | |

## 8. Testing

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-TEST-001 | `CMakeLists.txt` (`telemetry_core`, `unit_tests`, `gtest_discover_tests`), `src/drivers/fault_injecting_source.cpp` (fault injection support, FTS-TP-001 §2.3) | `tests/unit/test_altitude.cpp`, `test_kalman.cpp`, `test_complementary.cpp`, `test_fault_detection.cpp`, `test_logging.cpp`, `test_replay.cpp` | — | Partial | Processing, logging, replay, and the fault-injecting source. Simulator driver, timing, and transport have no unit tests. |
| REQ-TEST-002 | — | — | — | Not started | |
| REQ-TEST-003 | `CMakeLists.txt` (`ENABLE_COVERAGE`), `.github/workflows/ci.yml` (coverage steps), `scripts/coverage_check.sh` | CI job `build-test-coverage`, step "Coverage thresholds" | — | Partial | Branch coverage measured with gcov/lcov and gated per FTS-TP-001 §6.3 on every push. Results recorded in FTS-VR-001; first CI-run numbers pending. |
| REQ-TEST-004 | `.github/workflows/ci.yml` (static analysis steps) | CI job `build-test-coverage`, steps "Static analysis" | — | Partial | Runs on every push and blocks on release tags (`v*`) with `--error-exitcode=1`. Zero findings at this commit; "final release" not yet reached. |

---

## 9. Coverage Summary

| Status | Count | Requirements |
|---|---|---|
| Verified | 10 | PROC-001, PROC-002, PROC-003, FAULT-001, FAULT-002, FAULT-004, FAULT-005, LOG-001, LOG-002, LOG-004 |
| Partial | 7 | SENS-006, PROC-005, FAULT-003, LOG-003, TEST-001, TEST-003, TEST-004 |
| Implemented | 0 | |
| Not started | 15 | all others |
| **Total** | **32** | |

**Reverse trace.** Every source file under `src/` is named in at least one row above, so no code exists without a requirement:

| File | Traced by |
|---|---|
| `src/main.cpp` | REQ-LOG-001, REQ-LOG-004, REQ-PROC-005, REQ-FAULT-004 |
| `src/telemetry_frame.h` / `.cpp` | REQ-PROC-005 |
| `src/data_source.h` | REQ-LOG-004 |
| `src/drivers/simulated_data.h` / `.cpp` | REQ-SENS-006 |
| `src/drivers/simulated_source.h` / `.cpp` | REQ-SENS-006, REQ-LOG-004 |
| `src/drivers/fault_injecting_source.h` / `.cpp` | REQ-TEST-001 (fault injection support) |
| `src/logging/log_format.h` / `.cpp` | REQ-LOG-001 |
| `src/logging/binary_logger.h` / `.cpp` | REQ-LOG-001, REQ-LOG-002 |
| `src/logging/log_reader.h` / `.cpp` | REQ-LOG-001, REQ-LOG-003 |
| `src/replay/log_replay_source.h` / `.cpp` | REQ-LOG-003, REQ-LOG-004 |
| `src/processing/altitude.h` / `.cpp` | REQ-PROC-001 |
| `src/processing/kalman_filter.h` / `.cpp` | REQ-PROC-003 |
| `src/processing/complementary_filter.h` / `.cpp` | REQ-PROC-002 |
| `src/processing/fault_detector.h` / `.cpp` | REQ-FAULT-001, REQ-FAULT-002, REQ-FAULT-003, REQ-FAULT-004, REQ-FAULT-005 |
| `src/processing/processor.h` / `.cpp` | REQ-PROC-002, REQ-PROC-003, REQ-PROC-005 |

Adding a file under `src/` without adding it to this table is a traceability failure.
