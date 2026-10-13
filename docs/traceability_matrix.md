# Flight Telemetry System — Traceability Matrix

> **Document ID:** FTS-TM-001
> **Version:** 0.1.0
> **Status:** Draft
> **Last Updated:** 2026-10-12
> **Related:** FTS-SRD-001 v0.5.1 (requirements), FTS-TP-001 (test plan), FTS-DD-001 (design), FTS-FM-001 (fault model), FTS-NP-001 (noise profile)

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
| REQ-SENS-001 | `src/drivers/bmp280.cpp` (`Bmp280::init`, `Bmp280::read`, `Bmp280::compensate_temperature`, `Bmp280::compensate_pressure`), `src/drivers/live_sensor_source.cpp` (`LiveSensorSource::read_frame`), `src/drivers/i2c_bus.cpp` (`LinuxI2cBus`, the I2C seam) | `test_bmp280.cpp: Bmp280Test.DatasheetExampleCompensatesExactly`, `ReadsTheExampleThroughTheRegisters`, `InitAcceptsBothChipIdsAndRefusesOthers`, `InitWritesSleepConfigNormalInOrder`, `FailedTransactionsRefuseInit`, `FailedBurstAndSkippedMeasurementAreEmpty`, `ByteSwappedCalibrationGivesAnotherAnswer`, `ZeroCalibrationDoesNotDivideByZero`, `ReadBeforeInitIsEmpty`; `test_live_sensor_source.cpp: LiveSensorSourceTest.FrameCarriesBothChipsRawOnly`, `FailedBaroReadHoldsFieldsAndClearsReadOk` | TC-012, TC-014 | Partial | Compensation verified against the datasheet's worked example to the count; register protocol over the fake bus. The read rate is the loop's 50 Hz and "configurable" waits on REQ-CFG-001. On-target evidence (TC-012 step 8, TC-014 steps 11 through 15) pending in FTS-VR-001 Entry 8 §8.6; the driver's name and the 0x76 address are provisional until the chip is read (FTS-DD-001 Section 12). |
| REQ-SENS-002 | `src/drivers/bmp280.cpp` (`Bmp280::read`: one burst holds both fields, and `t_fine` carries the temperature into the pressure compensation) | `test_bmp280.cpp: Bmp280Test.DatasheetExampleCompensatesExactly`, `ReadsTheExampleThroughTheRegisters`, `FailedBurstAndSkippedMeasurementAreEmpty`; `test_live_sensor_source.cpp: LiveSensorSourceTest.FrameCarriesBothChipsRawOnly` | TC-012 | Partial | Nothing in it is configurable: Verified once the on-target run shows temperature read with each pressure (Entry 8 §8.6). |
| REQ-SENS-003 | `src/drivers/mpu6050.cpp` (`Mpu6050::init`, `Mpu6050::read`, `Mpu6050::sample_rate_divider`), `src/drivers/i2c_bus.cpp` (`LinuxI2cBus`, the I2C seam) | `test_mpu6050.cpp: Mpu6050Test.InitVerifiesIdentityAndWritesConfiguration`, `WrongWhoAmIRefusesInit`, `FailedIdentityReadRefusesInit`, `FailedConfigurationWriteRefusesInit`, `ScalesRawCountsToFrameUnits`, `ByteOrderIsHighByteFirst`, `SampleRateDividerArithmetic`, `UnreachableRateRefusesInit`, `FailedBurstReturnsEmpty`, `ReadBeforeInitReturnsEmpty`; `test_live_sensor_source.cpp: LiveSensorSourceTest.FrameCarriesBothChipsRawOnly`, `FailedImuReadHoldsFieldsAndClearsReadOk` | TC-013, TC-014 | Partial | Driver verified against the register map's sensitivity and divider arithmetic over the fake bus; on-target evidence in FTS-VR-001 Entry 8. The sample rate is a constructor parameter (default 100 Hz; the 8-bit divider floors at 4 Hz, the requirement's lower bound); "configurable" through YAML waits on REQ-CFG-001. |
| REQ-SENS-004 | `src/drivers/mpu6050.cpp` (`Mpu6050::init` writes SMPLRT_DIV once for both sensors; `Mpu6050::read` scales the gyroscope pairs) | `test_mpu6050.cpp: Mpu6050Test.InitVerifiesIdentityAndWritesConfiguration`, `ScalesRawCountsToFrameUnits`, `ByteOrderIsHighByteFirst`, `SampleRateDividerArithmetic` | TC-013 | Partial | One divider register sets both rates, which is the requirement's point. Configurability as for REQ-SENS-003. |
| REQ-SENS-005 | — | — | — | Not started | NEO-6M driver, NMEA parsing |
| REQ-SENS-006 | `src/drivers/simulated_data.cpp` (`SimulatedDataGenerator`), `src/drivers/simulated_source.cpp` (`SimulatedSource`) | `test_simulated_data.cpp: SimulatedDataTest.ProducesRawReadingsOnly`, `SameSeedSameSequence`, `DifferentSeedDifferentSequence`, `NoiseIsCenteredOnBaseValues`, `PressureNoiseHasStatedSpread`, `GpsFixOncePerSecond`, `GpsIntervalIsConfigurable`; `SimulatedSourceTest.TimestampsAdvanceByInterval`, `SourceMatchesGeneratorWithSameSeed` | TC-011 (steps 7 and 8) | Partial | Raw-only contract, seed reproducibility, the stated noise level, and the 1 Hz GPS schedule are tested directly. Noise sigmas are hardcoded in the generator constructor and the GPS period is a constructor parameter; "configurable" through YAML waits on REQ-CFG-001. |

## 2. Data Processing

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-PROC-001 | `src/processing/altitude.cpp` (`pressure_to_altitude`) | `test_altitude.cpp: AltitudeTest.*` (8 tests) | TC-001 | Verified | |
| REQ-PROC-002 | `src/processing/complementary_filter.cpp` (`ComplementaryFilter`, `reset`), `src/processing/processor.cpp` (`TelemetryProcessor::process`, the re-seed gate on `imu_timeout_ms_`) | `test_complementary.cpp: ComplementaryTest.*` (14 tests, including `StaleImuFrameHoldsAttitude` and `GyroIntegratesOverStaleGap` for REQ-PROC-006, and `LongImuGapReseedsFromAccel`, `GapEqualToTimeoutStillIntegrates`, `ReseedAdoptsTiltedAccel` for the re-seed rule) | TC-006 | Verified | Axis and sign convention is an assumption until verified on hardware. DEGRADED and stale frames hold the last angles; a gap longer than the IMU timeout re-initializes them from the accelerometer on the next accepted reading (FTS-DD-001 Section 6); FAULT-006 requires an invalid marker. |
| REQ-PROC-003 | `src/processing/kalman_filter.cpp` (`KalmanFilter1D`), `src/processing/processor.cpp` (`TelemetryProcessor::process`) | `test_kalman.cpp: KalmanTest.*` (12 tests, including the four REQ-PROC-006 gating tests) | TC-007 | Verified | Covariance constants are placeholders (see REQ-PROC-004). |
| REQ-PROC-004 | `analysis/noise_profile.py` (`analyze`, `channel_report`, `allan_deviation`, `derive_constants`), `docs/noise_profile.md` (FTS-NP-001: the method, the estimators, the derivation of each constant) | `tests/integration/test_noise_profile.py` (26 tests: synthetic oracles for sigma and mean, bias, the Allan law of white noise, drift separated from noise, autocorrelation, stuck runs and quantization, the formulas, the R rule's correlated-noise branch and its warnings; the binary's seed-42 log recovers the simulator's sigmas within 15% and a barometric variance within 25% of 2.774 m²) | TC-015 | Partial | The measurement and its oracles exist; the stationary capture of 2026-10-13 supplies the values (TC-015 steps 9 to 12). After it, the barometric variance and the initial altitude variance are measured, the process noise and the initial velocity variance are documented tuning choices, and the GPS variance stays the datasheet figure until REQ-SENS-005 delivers a receiver to measure. Constants in `processor.cpp` are placeholders until then. |
| REQ-PROC-005 | `src/processing/processor.cpp` (`TelemetryProcessor::process`), `src/telemetry_frame.cpp`, `src/main.cpp` (`run`) | `test_kalman.cpp: KalmanTest.ConvergenceReducesNoise`, `test_complementary.cpp: ComplementaryTest.DegradedImuHoldsAttitude` (indirect) | — | Partial | No fixed-rate loop yet (REQ-TIME-001). No test asserts that every computed field is filled. |
| REQ-PROC-006 | `src/processing/processor.cpp` (`TelemetryProcessor::process`: the fresh-and-NOMINAL gate per channel, `imu_last_fresh_ms_`, filter construction on the first fresh barometer reading) | `test_kalman.cpp: KalmanTest.StaleBaroInsideTimeoutIsPredictOnly`, `StaleGpsIsNotApplied`, `OneGpsUpdatePerFix`, `FilterWaitsForFirstFreshBaro`; `test_complementary.cpp: ComplementaryTest.StaleImuFrameHoldsAttitude`, `GyroIntegratesOverStaleGap` | TC-011 | Verified | Predict every cycle, update only on a fresh NOMINAL reading. Found 2026-10-11: the gate was on status only, invisible while the simulator fixed GPS on every frame (FTS-VR-001 Entry 7). |

## 3. Fault Detection

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-FAULT-001 | `src/processing/fault_detector.cpp` (`FaultDetector::comm_timed_out`, `FaultDetectorConfig::i2c_timeout_ms` default 500) | `test_fault_detection.cpp: FaultDetectionTest.BaroTimeoutDetectedWithinBound`, `ImuTimeoutDetectedWithinBound`, `GpsTimeoutUsesUartBound`, `TransientReadErrorDoesNotFault`, `TimeoutViaInjector` | TC-002 | Verified | Time is `frame.timestamp_ms`; no wall clock. Driver-level I2C/UART error reporting lands with the drivers. |
| REQ-FAULT-002 | `src/processing/fault_detector.cpp` (`StuckTracker::push`) | `test_fault_detection.cpp: FaultDetectionTest.TenthIdenticalPressureIsStuck`, `StuckAccelXWithOtherAxesVarying`, `RepeatsOfTwoAreNotStuck`, `DifferingValueResetsRun`, `StuckGyroZ`, `StaleValuesDuringDropoutAreNotStuck` | TC-003 | Verified | GPS channel has no stuck detection: no FAULT entry covers it and a stationary receiver legitimately repeats. |
| REQ-FAULT-003 | `src/processing/fault_detector.cpp` (`observe_baro`, `observe_imu`) | `test_fault_detection.cpp: FaultDetectionTest.PressureBelowRange`, `PressureAboveRange`, `TemperatureOutOfRangeDegradesBaro`, `AccelSaturationDegradesImu`, `BoundaryValuesAreValid` | — | Partial | GPS fix quality (FAULT-003b) deferred until the frame carries the field. |
| REQ-FAULT-004 | `src/processing/fault_detector.cpp` (`FaultDetector::transition`, `FaultEvent`), `src/main.cpp` (`run`) | `test_fault_detection.cpp: FaultDetectionTest.ProcessingContinuesOnDegradedChannel`, `OtherChannelsStayNominalDuringTimeout`, `BusFailureDegradesBothI2cChannels` | — | Verified | Events are `FaultEvent` structs persisted by `BinaryLogger::log_event` as tagged records in the telemetry log. FAULT-004 single bus-level event deferred. |
| REQ-FAULT-005 | `src/processing/fault_detector.cpp` (`FaultDetector::transition`) | `test_fault_detection.cpp: FaultDetectionTest.RecoveryAfterMValidReadings`, `BadReadingResetsRecoveryCount`, `StuckSensorRecovers` | — | Verified | |

## 4. Timing

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-TIME-001 | `src/timing/clock.cpp` (`MonotonicClock::sleep_until_ns`), `src/timing/fixed_rate_scheduler.cpp` (`FixedRateScheduler::wait_for_next_cycle`), `src/main.cpp` (`run`) | `test_timing.cpp: TimingTest.DeadlinesAdvanceByExactPeriod`, `WorkWithinPeriodDoesNotShiftPhase`, `LateByLessThanPeriodIsJitterNotAMiss`, `OverrunRephasesAndCountsMissed`, `StartIsImplicitOnFirstWait`, `MonotonicClockNeverWakesEarly`, `MonotonicClockSleepUntilPastReturnsPromptly` | TC-009 | Verified | Linux: `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)`, exercised on CI. macOS: `nanosleep` fallback toward the absolute target (FTS-DD-001 §10). Rate is a constant in `main.cpp` until REQ-CFG-001. |
| REQ-TIME-002 | `src/timing/fixed_rate_scheduler.h` (`CycleTiming`), `src/timing/jitter_log.cpp` (`JitterLog`, `JitterStats`), `src/main.cpp` | `test_timing.cpp: TimingTest.JitterIsActualMinusScheduled`, `PunctualClockHasZeroJitterAndNoMisses`, `JitterStatsAggregate`, `JitterLogWritesCsv`, `JitterLogUnwritablePathReportsNotOk` | TC-009 | Verified | Per-cycle CSV in `logs/<prefix>_jitter.csv`, summary printed at end of run. |
| REQ-TIME-003 | `src/timing/` (measurement in place) | TC-009 step 7 (pending hardware) | TC-009 | Partial | The 1 ms bound is judged on the Pi's `clock_nanosleep` path. macOS preview numbers in FTS-VR-001 do not satisfy it. |

## 5. Transport

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-TRANS-001 | `src/transport/zmq_publisher.cpp` (`ZmqPublisher::publish`), `src/transport/publisher.h` (`Publisher`, the seam), `src/logging/log_format.cpp` (`frame_message`), `src/main.cpp` (`run`, `--zmq`); receiver: `pipeline/frame_format.py`, `pipeline/receiver.py` (`FrameReceiver`, `ZmqSource`) | `test_zmq_publisher.cpp: ZmqPublisherTest.BindsAndReportsOk`, `MessageIsHeaderTagAndFrameBytes`, `EveryPublishedFrameArrivesInOrder`; `test_logging.cpp: LoggingTest.FrameMessageIsHeaderTagAndFrame`; `tests/integration/test_transport.py` (TC-005 over `tcp://`; both transports at once) | TC-005 | Verified | Unit level: round trip over `inproc://`, every message byte-identical to the log record behind its own header. Process level: TC-005 automated by the pytest over `tcp://` with the join index recorded (FTS-VR-001 Entry 10), after the hand run of 2026-10-11 (Entry 6). |
| REQ-TRANS-002 | `src/transport/udp_publisher.cpp` (`UdpPublisher`), `src/transport/publisher.h`, `src/logging/log_format.cpp` (`frame_message`), `src/main.cpp` (`run`, `--udp`); receiver: `pipeline/frame_format.py`, `pipeline/receiver.py` (`UdpSource`) | `test_udp_publisher.cpp: UdpPublisherTest.ConnectsAndReportsOk`, `DatagramIsHeaderTagAndFrameBytes`, `EveryPublishedFrameArrivesInOrder`; `tests/integration/test_transport.py` (TC-005 over UDP; both transports at once) | TC-005, TC-010 | Partial | One datagram per frame, the same one-record log message as ZeroMQ, to one unicast destination given by `--udp <ipv4>:<port>`; verified at unit and process level (FTS-VR-001 Entry 10). "Configurable" through YAML waits on REQ-CFG-001, the row's only gap. |
| REQ-TRANS-003 | `src/transport/zmq_publisher.cpp` (`ZmqPublisher` constructor, `publish`, `kDefaultSendHighWaterMark`, `kLingerMs`), `src/transport/udp_publisher.cpp` (`UdpPublisher` constructor: refusals at startup; `publish`: `MSG_DONTWAIT`, every refusal counted), `src/main.cpp` (`run`) | `test_zmq_publisher.cpp: ZmqPublisherTest.BindFailureIsReportedNotThrown`, `InvalidEndpointIsReportedNotThrown`, `NoSubscriberNeverBlocks`, `StalledSubscriberDropsInsteadOfBlocking`, `SendErrorIsCountedNotThrown`; `test_udp_publisher.cpp: UdpPublisherTest.BadAddressOrPortIsRefusedNotThrown`, `NonUnicastAddressIsRefused`, `SocketFailureIsReportedNotThrown`, `NoReceiverNeverBlocks`, `PortUnreachableIsCountedNotThrown`, `StalledReceiverDropsInsteadOfBlocking`, `DirectedBroadcastIsRefusedOrRouted`; `tests/integration/test_transport.py` (TC-010 steps 6 and 7 over both transports; the startup refusals) | TC-010 | Verified | Non-blocking proven at unit level for both publishers and at loop level by the pytest: a receiver stalled for 3 s and no receiver at all, each over both transports, with the loop's missed cycles and the receiver's counts recorded (FTS-VR-001 Entry 10). |

## 6. Logging and Replay

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-LOG-001 | `src/logging/log_format.h/.cpp` (`LogFileHeader`, `RecordType`), `src/logging/binary_logger.cpp` (`BinaryLogger`), `src/logging/log_reader.cpp` (`LogReader`), `src/main.cpp` (`run`) | `test_logging.cpp: LoggingTest.*` (22 tests); `tests/integration/test_log_reader.py` (18 tests: the read side in Python, header refusal naming the field, torn tail, unknown tag, rotation as one run, the numpy layout's offsets; TC-008 steps 8 through 14) | TC-008 | Verified | Raw-struct records with a versioned, size-checked header. Same-platform format by design (FTS-DD-001 §8, §9). `pipeline/log_reader.py` is the Python twin of `LogReader` (same torn-tail rule). |
| REQ-LOG-002 | `src/logging/binary_logger.cpp` (`BinaryLogger::write_record`, `open_next_file`) | `test_logging.cpp: LoggingTest.RotationSplitsAtRecordBoundary`, `TinyLimitStillWritesOneRecordPerFile`, `CurrentPathTracksRotation`, `RotationIntoRemovedDirectoryReportsNotOk` | TC-008 | Verified | Limit is a constant in `main.cpp` until REQ-CFG-001. |
| REQ-LOG-003 | `src/replay/log_replay_source.cpp` (`LogReplaySource`), `src/logging/log_reader.cpp`, `pipeline/log_reader.py` (the Python parse of TC-004 step 3) | `test_replay.cpp: ReplayTest.ReplayReproducesLiveSession` (500 frames, injected faults, `memcmp` on every frame and event); `tests/integration/test_replay_identity.py` (6 tests: 500 simulated frames and their replay parsed in Python, every raw and computed field equal, the two files byte-identical, every frame's bytes equal to its repacking, the timestamps on the simulator's grid) | TC-004 | Verified | Unit, file-level, and Python-parsed identity proven on one toolchain per run. Cross-platform identity is not claimed (FTS-DD-001 §8). |
| REQ-LOG-004 | `src/data_source.h` (`DataSource`), `src/drivers/simulated_source.cpp`, `src/drivers/live_sensor_source.cpp`, `src/replay/log_replay_source.cpp`, `src/main.cpp` (`create_source`, `run`) | `test_replay.cpp: ReplayTest.ReplayStripsComputedFieldsAndStatus`, `ReplaySpansRotatedFiles`, `ReplayReproducesLiveSession`; `test_live_sensor_source.cpp: LiveSensorSourceTest.FrameCarriesBothChipsRawOnly` (the raw-only contract on the live side) | TC-004, TC-014 (step 14 on hardware) | Verified | Three concrete sources share one pipeline; `main` selects by `--replay` or `--live`. Replay strips computed fields and status so the pipeline must recompute them. |

## 7. Configuration

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-CFG-001 | — | — | — | Not started | |
| REQ-CFG-002 | — | — | — | Not started | |

## 8. Testing

| Requirement | Implementation | Test | Test Card | Status | Notes |
|---|---|---|---|---|---|
| REQ-TEST-001 | `CMakeLists.txt` (`telemetry_core`, `unit_tests`, `gtest_discover_tests`), `src/drivers/fault_injecting_source.cpp` (fault injection support, FTS-TP-001 §2.3) | `tests/unit/test_altitude.cpp`, `test_kalman.cpp`, `test_complementary.cpp`, `test_fault_detection.cpp`, `test_logging.cpp`, `test_replay.cpp`, `test_timing.cpp`, `test_simulated_data.cpp`, `test_zmq_publisher.cpp`, `test_udp_publisher.cpp`, `test_i2c_bus.cpp`, `test_bmp280.cpp`, `test_mpu6050.cpp`, `test_live_sensor_source.cpp` | — | Verified | Every module the requirement names has unit tests: drivers (simulator, fault injector, the I2C seam, BMP280, MPU6050, the live source), processing, timing, logging, replay, and both transports. |
| REQ-TEST-002 | `pytest.ini`, `tests/integration/conftest.py` (the binary, one simulated session and its replay per run, in a temporary directory; a free port and a published run for the transport tests), `pipeline/log_reader.py`, `pipeline/receiver.py` | `tests/integration/test_replay_identity.py` (6 tests), `test_log_reader.py` (18), `test_noise_profile.py` (26), `test_transport.py` (16) | TC-004, TC-005, TC-008, TC-010, TC-015 | Verified | 66 tests in about 50 s. TC-005 and TC-010 steps 6 and 7 automated over both transports; the suite runs in CI on every push (`.github/workflows/ci.yml`, step "Integration tests"), measurement of record FTS-VR-001 Entry 10 §10.6. |
| REQ-TEST-003 | `CMakeLists.txt` (`ENABLE_COVERAGE`), `.github/workflows/ci.yml` (coverage steps), `scripts/coverage_check.sh` | CI job `build-test-coverage`, step "Coverage thresholds" | — | Verified | Branch coverage measured with gcov/lcov and gated per FTS-TP-001 §6.3 on every push, the pytest suite's runs of the instrumented binary counted since its step landed before the capture. Measurement of record: the CI run named in FTS-VR-001 Entry 10 §10.6 (before it, run 34445017604, Entry 5 §5.8). |
| REQ-TEST-004 | `.github/workflows/ci.yml` (static analysis steps) | CI job `build-test-coverage`, steps "Static analysis" | — | Partial | Runs on every push and blocks on release tags (`v*`) with `--error-exitcode=1`. Zero findings on CI run 34445017604; no release-tagged build has been made yet, so the gate has not been exercised. |

---

## 9. Coverage Summary

| Status | Count | Requirements |
|---|---|---|
| Verified | 19 | PROC-001, PROC-002, PROC-003, PROC-006, FAULT-001, FAULT-002, FAULT-004, FAULT-005, LOG-001, LOG-002, LOG-003, LOG-004, TIME-001, TIME-002, TRANS-001, TRANS-003, TEST-001, TEST-002, TEST-003 |
| Partial | 11 | SENS-001, SENS-002, SENS-003, SENS-004, SENS-006, PROC-004, PROC-005, FAULT-003, TIME-003, TRANS-002, TEST-004 |
| Implemented | 0 | |
| Not started | 3 | SENS-005, CFG-001, CFG-002 |
| **Total** | **33** | |

**Reverse trace.** Every source file under `src/` is named in at least one row above, so no code exists without a requirement:

| File | Traced by |
|---|---|
| `src/main.cpp` | REQ-LOG-001, REQ-LOG-004, REQ-PROC-005, REQ-FAULT-004, REQ-TIME-001, REQ-TIME-002, REQ-TRANS-001, REQ-TRANS-002, REQ-TRANS-003 |
| `src/telemetry_frame.h` / `.cpp` | REQ-PROC-005 |
| `src/data_source.h` | REQ-LOG-004 |
| `src/drivers/simulated_data.h` / `.cpp` | REQ-SENS-006 |
| `src/drivers/simulated_source.h` / `.cpp` | REQ-SENS-006, REQ-LOG-004 |
| `src/drivers/fault_injecting_source.h` / `.cpp` | REQ-TEST-001 (fault injection support) |
| `src/drivers/i2c_bus.h` / `.cpp` | REQ-SENS-001, REQ-SENS-002, REQ-SENS-003, REQ-SENS-004 (the I2C seam) |
| `src/drivers/mpu6050.h` / `.cpp` | REQ-SENS-003, REQ-SENS-004 |
| `src/drivers/bmp280.h` / `.cpp` | REQ-SENS-001, REQ-SENS-002 |
| `src/drivers/live_sensor_source.h` / `.cpp` | REQ-SENS-001, REQ-SENS-002, REQ-SENS-003, REQ-SENS-004, REQ-LOG-004 |
| `src/logging/log_format.h` / `.cpp` | REQ-LOG-001, REQ-TRANS-001, REQ-TRANS-002 (the frame message both transports send) |
| `src/logging/binary_logger.h` / `.cpp` | REQ-LOG-001, REQ-LOG-002 |
| `src/logging/log_reader.h` / `.cpp` | REQ-LOG-001, REQ-LOG-003 |
| `src/replay/log_replay_source.h` / `.cpp` | REQ-LOG-003, REQ-LOG-004 |
| `src/timing/clock.h` / `.cpp` | REQ-TIME-001 |
| `src/timing/fixed_rate_scheduler.h` / `.cpp` | REQ-TIME-001, REQ-TIME-002 |
| `src/timing/jitter_log.h` / `.cpp` | REQ-TIME-002, REQ-TIME-003 |
| `src/transport/publisher.h` | REQ-TRANS-001, REQ-TRANS-002, REQ-TRANS-003 (the seam the loop sees) |
| `src/transport/zmq_publisher.h` / `.cpp` | REQ-TRANS-001, REQ-TRANS-003 |
| `src/transport/udp_publisher.h` / `.cpp` | REQ-TRANS-002, REQ-TRANS-003 |
| `src/processing/altitude.h` / `.cpp` | REQ-PROC-001 |
| `src/processing/kalman_filter.h` / `.cpp` | REQ-PROC-003 |
| `src/processing/complementary_filter.h` / `.cpp` | REQ-PROC-002 |
| `src/processing/fault_detector.h` / `.cpp` | REQ-FAULT-001, REQ-FAULT-002, REQ-FAULT-003, REQ-FAULT-004, REQ-FAULT-005 |
| `src/processing/processor.h` / `.cpp` | REQ-PROC-002, REQ-PROC-003, REQ-PROC-005, REQ-PROC-006 |

Adding a file under `src/` without adding it to this table is a traceability failure.

The Python modules trace the same way. Test files are named in the Test column of the requirement they verify, as the C++ test files are.

| File | Traced by |
|---|---|
| `pipeline/frame_format.py` | REQ-LOG-001, REQ-TRANS-001, REQ-TRANS-002 (the byte layout shared by the log and the wire) |
| `pipeline/receiver.py` | REQ-TRANS-001, REQ-TRANS-002, REQ-TRANS-003 |
| `pipeline/log_reader.py` | REQ-LOG-001, REQ-LOG-003, REQ-TEST-002 |
| `analysis/noise_profile.py` | REQ-PROC-004 |

Adding a module under `pipeline/` or `analysis/` without adding it here is the same failure.
