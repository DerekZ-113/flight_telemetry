# Flight Telemetry System — Software Test Plan

> **Document ID:** FTS-TP-001
> **Version:** 0.1.0
> **Status:** Draft
> **Last Updated:** 2026-10-13
> **Related:** FTS-SRD-001 (requirements), FTS-FM-001 (fault model), FTS-TM-001 (traceability), FTS-DD-001 (design)

This document defines how the Flight Telemetry System is verified. It covers test objectives, test levels, tools, pass/fail criteria, individual test procedures (test cards), and the structural coverage strategy. What is tested, requirement by requirement, is recorded in the traceability matrix, not here.

---

## 1. Test Objectives

Verification has two objectives, mirroring DO-178C Section 6.

**Requirements-based verification.** Every test traces to a requirement in FTS-SRD-001. The purpose of a test is to demonstrate that the implementation satisfies its requirement under normal and abnormal conditions. A test that does not trace to a requirement is either evidence of a missing requirement or does not belong in the suite.

**Structural coverage.** Requirements-based tests are executed under coverage instrumentation to show that they exercise the code. Uncovered code indicates one of three things: a missing requirement, a missing test, or dead code. Each uncovered region shall be resolved into one of those categories before release.

The two objectives work in opposite directions. Requirements-based testing starts from what the system must do and confirms the code does it. Structural coverage starts from what the code does and confirms a requirement asked for it. Together they close the loop that bidirectional traceability depends on.

**Out of scope.** Hardware qualification of the BMP280, MPU6050, and NEO-6M is not performed. Sensor behavior is taken from manufacturer datasheets and from the noise characterization in docs/noise_profile.md. Environmental testing (temperature, vibration, EMI) is not performed.

---

## 2. Test Levels

### 2.1 Unit Tests

- **Scope:** Individual C++ functions and classes in isolation. Drivers, processing, timing, logging, and transport modules.
- **Framework:** Google Test.
- **Isolation:** Hardware dependencies are replaced with test doubles. The `DataSource` abstraction is the primary seam: a test-only `FakeSource` returns hand-built frames, so the pipeline can be exercised without sensors or the simulator. Time-dependent logic does not sleep in tests: fixed-rate scheduling takes an injectable `Clock` and tests drive it with a fake that advances time by fiat; fault timeouts read time from frame timestamps. One real-clock smoke test (about 20 ms) exercises the OS boundary with loose bounds.
- **Location:** `tests/unit/`, one test file per module (`test_altitude.cpp`, `test_fault_detection.cpp`, and so on).
- **Executed:** Every commit, in CI and locally.

### 2.2 Integration Tests

- **Scope:** The full pipeline across the C++/Python boundary: C++ processing publishes over ZeroMQ and UDP and the Python receiver consumes and validates; the binary's own log replayed through it and compared in Python (TC-004); and the Python log reader and analysis scripts, tested at this level on the binary's logs and on synthetic ones (TC-008 steps 8 through 14, TC-015).
- **Framework:** pytest, with numpy and matplotlib for the analysis.
- **Isolation:** The C++ binary runs with the simulated data source, so integration tests are hardware-independent and reproducible. One simulated session per run (500 frames, seed 42, and its replay) in a temporary directory, shared by every test through `conftest.py`; a missing binary fails the run rather than skipping it. Every run of the binary passes `--config` with the shipped `config/telemetry_config.yaml` (the binary reads it relative to the working directory by default, and these runs happen in temporary directories); a test that needs another configuration hands the binary a copy with one line edited, and a later `--config` on the command line wins.
- **Location:** `tests/integration/`, run as `.venv/bin/python -m pytest`.
- **Executed:** Every commit, locally and in CI: the workflow's "Integration tests" step runs `python -m pytest` after the unit tests and before the coverage capture, so the integration runs count toward structural coverage (Section 6.1).

### 2.3 Fault Injection Tests

- **Scope:** System behavior under the failure modes in FTS-FM-001. Each FAULT entry maps to at least one injected scenario.
- **Framework:** Parameterized Google Test harness for single faults; pytest scenarios for multi-fault and recovery sequences.
- **Method:** Faults are injected at the `DataSource` boundary. A `FaultInjectingSource` wraps another source and applies a scripted or seeded fault schedule: dropped reads, repeated values, out-of-range values, and recovery after a configurable interval.
- **Randomization:** Randomized schedules use a logged seed so any failing run can be reproduced exactly. A randomized test that cannot be replayed is not evidence.
- **Location:** `tests/fault_injection/`.
- **Executed:** Every commit for the deterministic scenarios. Randomized sweeps run nightly.

### 2.4 Hardware Tests

- **Scope:** Behavior that only the target can show: real sensors on the bus, the Linux sleep primitive, a pulled wire.
- **Method:** Executed by hand on the Raspberry Pi with the live source, following the card's steps. The run's conditions (board, OS, kernel, compiler, date) and the printed output are recorded in FTS-VR-001.
- **Isolation:** None. This is the level at which the test doubles are removed.
- **Executed:** On hardware days, never in CI. A card step at this level is marked *Hardware*.

---

## 3. Test Tools

| Tool | Role | Why this tool |
|---|---|---|
| Google Test | C++ unit and fault injection tests | The de facto C++ test framework. Parameterized tests (`TEST_P`) map directly onto the fault model's "for each sensor, for each fault type" structure. Integrates with CMake through CTest. |
| pytest | Integration tests | The Python receiver and analysis scripts are already Python. pytest fixtures manage the lifecycle of the C++ process cleanly, and parametrize handles frame-count and rate sweeps. |
| gcov / lcov | Structural coverage | gcov is built into GCC and produces per-line and per-branch execution counts with no source changes. lcov aggregates gcov output into HTML reports and enforces thresholds in CI. Required by REQ-TEST-003. |
| cppcheck | Static analysis | Detects undefined behavior, uninitialized members, and resource leaks without executing the code. Fast enough to run on every build. Required by REQ-TEST-004. |
| CTest | Test runner | Ships with CMake. Runs the Google Test binaries and reports results in a form GitHub Actions can consume. |

Compiler warnings (`-Wall -Wextra -Wpedantic`) are treated as a static analysis layer of their own. A warning is a failed build.

---

## 4. Pass/Fail Criteria

A build passes when all of the following hold. Any single failure fails the build.

1. All Google Test cases shall pass. There is no allowance for known failures or skipped tests in the main branch.
2. All pytest integration tests shall pass.
3. Branch coverage, measured by gcov/lcov over all C++ source under `src/`, shall meet the thresholds in Section 6.3.
4. cppcheck shall report zero warnings at the `--enable=warning,style,performance,portability` level on any release-tagged build. Non-release builds report but do not block.
5. Every requirement in the traceability matrix shall trace to at least one test. A requirement with an empty test column fails the build once that requirement's implementation column is filled.
6. The compiler shall emit zero warnings.

A release is a passing build on a tagged commit, with the coverage and cppcheck reports committed under `coverage/` and `static_analysis/` and summarized in docs/verification_results.md.

---

## 5. Test Cards

Test cards are the individual test procedures. Each card is executed by one or more automated test functions named in the traceability matrix. The card is the human-readable procedure; the test function is the executable evidence.

### TC-001: Altitude Conversion Accuracy

- **Requirement:** REQ-PROC-001
- **Level:** Unit
- **Objective:** Verify that `pressure_to_altitude()` matches the ISA standard atmosphere table across the BMP280 operating range.
- **Preconditions:** None. Pure function, no state.
- **Steps:**
  1. Call `pressure_to_altitude(1013.25f)` with the default reference. Record the result.
  2. Call `pressure_to_altitude(898.76f)`. Record the result.
  3. Call `pressure_to_altitude(795.01f)`. Record the result.
  4. Call `pressure_to_altitude(300.0f)`. Record the result.
  5. Call `pressure_to_altitude(1013.25f, 1023.25f)` to verify the reference is honored. Measured pressure below the reference places the sensor above the reference level, so the result is positive.
  6. Call `pressure_to_altitude(1012.25f, 1013.25f)` to verify sensitivity near sea level.
- **Expected result:**

  | Step | Input (hPa) | Reference (hPa) | Expected (m) | Tolerance (m) |
  |---|---|---|---|---|
  | 1 | 1013.25 | 1013.25 | 0.0 | 0.01 |
  | 2 | 898.76 | 1013.25 | 1000.0 | 1.0 |
  | 3 | 795.01 | 1013.25 | 2000.0 | 2.0 |
  | 4 | 300.00 | 1013.25 | 9164.0 | 20.0 |
  | 5 | 1013.25 | 1023.25 | 82.8 | 1.0 |
  | 6 | 1012.25 | 1013.25 | 8.3 | 0.2 |

- **Pass/fail:** All six results within tolerance. Tolerance grows with altitude because the formula's 0.1903 exponent is a four-digit rounding of the exact value, and the error scales with the pressure ratio.

### TC-002: Sensor Communication Failure Detected Within Bound

- **Requirement:** REQ-FAULT-001 (also exercises REQ-FAULT-004, REQ-FAULT-005, and REQ-PROC-002 in step 8)
- **Level:** Fault injection (steps 1 through 7), Hardware (step 8)
- **Objective:** Verify that a sensor that stops responding is marked DEGRADED within the required detection bound, and not before.
- **Preconditions:** The fault detector reads time from `frame.timestamp_ms`; there is no wall clock anywhere in the pipeline, so tests advance time by feeding frames with later timestamps. I2C bound configured at 500 ms. UART bound configured at 2000 ms. Frames are hand-built at 50 Hz with `read_ok` flags cleared per channel; one case uses a `FaultInjectingSource` wrapping the simulator to prove the seam.
- **Steps:**
  1. Feed 10 healthy frames. Assert all channels NOMINAL.
  2. Configure the injector to return a read error for the barometer on every subsequent frame.
  3. Feed one frame per 20 ms of timestamp. After each frame, read the barometer channel status.
  4. Record the frame timestamp at which the status first reads DEGRADED.
  5. Repeat steps 2 through 4 for the IMU channel.
  6. Repeat steps 2 through 4 for the GPS channel, using the 2000 ms UART bound.
  7. For each channel, assert that exactly one fault event was logged, carrying the channel, the fault type, and a timestamp.
  8. *(Hardware)* On the Raspberry Pi, during a paced live run of 60 s (`telemetry --live --cycles 3000`, output to a file), pull the barometer board's SDA jumper at about 10 s and reconnect it at about 25 s; pull the MPU6050's SDA jumper at about 35 s, hold the board about 20° nose-up, reconnect at about 50 s, then set it flat. Keep the printed output and the log, and replay the log.
- **Expected result:** Barometer and IMU transition to DEGRADED at a clock time greater than 480 ms and no greater than 500 ms after the first failed read. GPS transitions at a time greater than 1980 ms and no greater than 2000 ms. Healthy channels remain NOMINAL throughout. Processing continues for every frame; no frame is dropped. Step 8: a `COMM_TIMEOUT` event for BARO 500 to 520 ms after the pull, its value the elapsed time since the last good read, and a `RECOVERY` event on the fifth good read after reconnection, its value the time spent DEGRADED; the same pair for IMU; `baro_read_ok` false from the pull onward while the status stays NOMINAL for the first half second; pitch and roll frozen during the IMU outage and at the tilted value on the first NOMINAL frame after it (REQ-PROC-002); the other I2C channel NOMINAL throughout; the timing summary reports 0 missed cycles; the saved log replays byte-identical.
- **Pass/fail:** All three channels detected within their bound and not more than one frame period early. Any detection before 480 ms (or 1980 ms for GPS) fails, because it would false-alarm on a single transient error. Any missing or duplicated fault event fails. Step 8: a channel that never degrades, degrades early, recovers before five good reads, or takes the other I2C channel down with it fails; a missed cycle during the pull is a finding, because a failed transaction took longer than one period.

### TC-003: Stuck Sensor Detection

- **Requirement:** REQ-FAULT-002 (also exercises REQ-FAULT-004)
- **Level:** Fault injection
- **Objective:** Verify that N consecutive identical raw readings on any axis mark the channel DEGRADED, and that N-1 do not.
- **Preconditions:** Fault detector configured with N = 10. Test frames are built by hand so raw register values are controlled exactly.
- **Steps:**
  1. Feed 9 frames with an identical pressure value. Assert barometer NOMINAL.
  2. Feed a 10th identical frame. Assert barometer DEGRADED and one fault event logged carrying the stuck value.
  3. Reset. Feed 9 identical accelerometer X values while Y and Z vary each frame. Assert IMU NOMINAL.
  4. Feed a 10th. Assert IMU DEGRADED.
  5. Reset. Feed 10 frames where each axis repeats its previous value exactly once, then changes (no run longer than 2). Assert IMU NOMINAL.
  6. Reset. Feed 9 identical gyroscope Z values, then one different value, then 9 identical. Assert IMU NOMINAL throughout (the run counter must reset on a differing value).
- **Expected result:** DEGRADED exactly on the Nth identical reading, never on the (N-1)th. A single differing value resets the count. A stuck single axis is sufficient; other axes varying does not mask it.
- **Pass/fail:** All six assertions hold. Detection on frame 9 (too early) or frame 11 (too late) fails.

### TC-004: Deterministic Replay Produces Identical Output

- **Requirement:** REQ-LOG-003 (also exercises REQ-LOG-004)
- **Level:** Integration
- **Objective:** Verify that replaying a recorded log through the pipeline reproduces the original session's computed outputs bit for bit.
- **Preconditions:** `BinaryLogger` and `LogReplaySource` implemented. Pipeline uses frame timestamps, not the wall clock, for every time-dependent computation. Same build of the binary and the same configuration for both runs: the replay reads the configuration recorded beside the log (REQ-CFG-003). Steps 1 through 6 are executed at unit level by `ReplayTest.ReplayReproducesLiveSession` (500 frames with injected faults, every processed frame and fault event compared with `memcmp`) and at the integration level by `tests/integration/test_replay_identity.py`, which runs the binary twice in a temporary directory and parses both logs with `pipeline/log_reader.py`; the output directory is `logging.directory`, `logs` in the shipped file, so the files are `logs/telemetry_000.bin` and `logs/replay_000.bin`.
- **Steps:**
  1. Run the pipeline in simulated mode with seed 42 for 500 frames at 50 Hz. Log every processed frame to `logs/telemetry_000.bin`.
  2. Run the pipeline in replay mode with that log as the data source. Log every processed frame to `logs/replay_000.bin`.
  3. Parse both logs in Python. For each frame index, compare every field.
  4. Assert the frame counts are equal.
  5. Assert every raw field is identical (replay must not alter its input).
  6. Assert every computed field (baro altitude, pitch, roll, fused altitude, vertical speed, channel status) is identical, compared as raw bytes rather than with a floating-point tolerance.
- **Expected result:** 500 frames in each log. Zero field differences; the two files byte-identical; every frame's bytes equal to its repacking (padding zero); timestamps 0 to 9980 ms in both.
- **Pass/fail:** Any differing byte in any computed field fails. A tolerance-based comparison is not acceptable here: the requirement says identical, and a drift that is small today becomes a debugging problem when the Kalman filter changes. This test runs on a single toolchain. Cross-platform bit identity between the Mac development build and the Raspberry Pi target is not claimed, because `std::normal_distribution` is not specified by the standard and the two C++ standard libraries differ.

### TC-005: Pipeline Data Integrity over ZeroMQ and UDP

- **Requirement:** REQ-TRANS-001, REQ-TRANS-002 (also exercises REQ-TEST-002)
- **Level:** Integration (`tests/integration/test_transport.py`, parametrized over the two transports); steps 4 and 7 are also executed at unit level, see Preconditions
- **Objective:** Verify that every frame published by the C++ process arrives at the Python receiver intact, in order, and without loss under normal conditions, over each transport.
- **Preconditions:** C++ binary built with both publishers (`telemetry --zmq <endpoint> --cycles 200`, `telemetry --udp <ipv4>:<port> --cycles 200`). Simulated source with seed 42 so the expected frame sequence is known in advance. Message layout (FTS-DD-001 Section 9): every ZeroMQ message and every UDP datagram is a one-record log file, the 16-byte `LogFileHeader`, the `FRAME` tag byte, then the 104 frame bytes, 121 bytes in all, so the receiver parses the wire with the same code it uses for a log on disk. The receiver is `pipeline/receiver.py`'s `FrameReceiver`, driven in-process by the test with the matching source: for ZeroMQ a SUB socket with an empty subscription, connected before the publisher binds and retrying its connect every 10 ms as shipped and every 1 ms in the test (`ZmqSource(reconnect_ivl_ms=1)`); for UDP a socket bound to the destination before the publisher starts, which needs no subscription and no join. The byte identity of steps 4 and 7 is executed at unit level by `ZmqPublisherTest.MessageIsHeaderTagAndFrameBytes` and `EveryPublishedFrameArrivesInOrder` over `inproc://`, by `UdpPublisherTest.DatagramIsHeaderTagAndFrameBytes` and `EveryPublishedFrameArrivesInOrder` over loopback, and by `LoggingTest.FrameMessageIsHeaderTagAndFrame` on the one function both publishers call; the process-level steps were executed by hand over ZeroMQ on 2026-10-11 (FTS-VR-001 Entry 6) and are automated for both transports by the pytest (Entry 10).
- **Steps:**
  1. Start the receiver first. ZeroMQ: connect the subscriber; it retries until the publisher binds, and pub/sub drops whatever is sent before the subscription reaches the publisher, so the subscriber must be the one waiting. A settle delay before starting the publisher does nothing, since there is nothing to subscribe to until the bind; what catches frame 0 is the reconnect interval against the loop's 20 ms first deadline. UDP: bind the receiving socket; a datagram socket has no join.
  2. Start the C++ process configured for 200 frames at 50 Hz, with its stdout to a file (a pipe nobody drains fills and blocks the loop), then let it exit.
  3. Collect all received messages until the process has exited and the receiver has been quiet for one timeout (1 s).
  4. Treat each message as a one-record log: validate the 16-byte header (magic, format version, frame and event sizes equal to the receiver's own layout, endianness marker), then the tag, then unpack the frame with the layout defined in `telemetry_frame.h` (`frame_format.parse_message`).
  5. UDP: assert 200 frames were received. ZeroMQ: assert the first frame received is within the first five (100 ms) and that every frame from it to the last published arrived, so the count is 200 minus the join index; record the join index, 0 expected. A subscriber that joins late misses what was sent before it joined, which REQ-TRANS-001's rationale says is not loss.
  6. Assert timestamps are strictly increasing in steps of 20 ms from the first received frame to 3980 ms, with the receiver's gap and out-of-order counts zero.
  7. Compare each received frame against the binary log written by the C++ process during the same run. Assert every received frame is byte-identical to the logged frame with the same timestamp. (Python cannot regenerate the expected frames from the seed, because its random number generator differs from the C++ one, so the C++ log is the reference.) Assert the process's transport summary reports 200 published and no send failure.
- **Expected result:** every frame from the join to the end received, ordered, byte-identical to the log; every header valid; 200 frames over UDP, and over ZeroMQ whenever the subscription precedes frame 0, which the test's 1 ms reconnect makes the usual case.
- **Pass/fail:** Any missing frame after the join, any reordered or altered frame, a ZeroMQ join later than frame 5, or any header the receiver would have to guess at fails. This test exercises the normal path only. Transport failure behavior (REQ-TRANS-003) is TC-010.

---

### TC-006: Attitude Estimation Accuracy

- **Requirement:** REQ-PROC-002
- **Level:** Unit
- **Objective:** Verify that the complementary filter recovers static tilt from the accelerometer, follows the gyroscope for short-term motion, bounds gyroscope bias drift, and re-initializes from the accelerometer after an IMU gap longer than the communication timeout.
- **Preconditions:** Steps 1 through 4: none; the filter is constructed with a chosen alpha, and no hardware or simulator is required. Steps 5 through 7 drive a `TelemetryProcessor` with hand-built frames, because the gap rule is applied there; the processor's IMU timeout is its default, 500 ms.
- **Steps:**
  1. Construct a filter with default alpha. Call `update()` once with accelerometer (0, 0, 9.81) m/s² and zero gyroscope rates. Record pitch and roll.
  2. Construct a fresh filter. Call `update()` once with accelerometer (9.81, 0, 9.81) and zero rates. Record pitch and roll.
  3. Construct a filter with alpha = 1.0 (gyroscope only). Seed flat as in step 1, then call `update()` 100 times with accelerometer (0, 0, 9.81), pitch rate 1.0 deg/s, dt = 0.02 s. Record pitch.
  4. Construct a filter with alpha = 0.98. Seed flat, then call `update()` 500 times with the same inputs as step 3. Record pitch.
  5. Through a `TelemetryProcessor`: fresh flat frames at 0 and 20 ms, then NOMINAL frames with `imu_read_ok` false every 20 ms through 520 ms, then a fresh frame at 540 ms carrying a pitch rate of 5 deg/s and a flat accelerometer. Record pitch and roll.
  6. As step 5, but the fresh frame arrives at 520 ms, a gap of exactly 500 ms since the last accepted sample. Record pitch.
  7. As step 5, but the fresh frame carries accelerometer (9.81, 0, 9.81). Record pitch.
- **Expected result:**

  | Step | Expected pitch (deg) | Expected roll (deg) | Tolerance (deg) | Basis |
  |---|---|---|---|---|
  | 1 | 0.0 | 0.0 | 0.01 | gravity on z only |
  | 2 | 45.0 | 0.0 | 0.1 | atan2(9.81, 9.81) |
  | 3 | 2.0 | 0.0 | 0.01 | 100 × 0.02 s × 1 deg/s, pure integration |
  | 4 | 0.98 | 0.0 | 0.05 | steady state alpha·b·dt/(1−alpha) |
  | 5 | 0.0 | 0.0 | 0.001 | gap of 520 ms exceeds the 500 ms timeout: re-seeded from the flat accelerometer, the 5 deg/s sample not integrated |
  | 6 | 2.45 | 0.0 | 0.001 | gap equal to the timeout is integrated: 0.98 × 5 deg/s × 0.5 s |
  | 7 | 45.0 | 0.0 | 0.1 | re-seeded from the tilted accelerometer |

- **Pass/fail:** All seven within tolerance. Step 3 must show the drift (pitch near 2°, not 0°) and step 4 must show it bounded (pitch below 1.5° after ten time constants). A filter that passes step 4 by ignoring the gyroscope fails step 3. A processor that integrates across the long gap (step 5 near 2.55°, step 7 near 3.4°) or re-seeds at a gap equal to the timeout (step 6 near 0°) fails.

### TC-007: Altitude Fusion Behavior

- **Requirement:** REQ-PROC-003
- **Level:** Unit
- **Objective:** Verify that the Kalman filter propagates state under the constant-velocity model, weights measurements by their stated noise, reduces barometric noise end to end, and is deterministic.
- **Preconditions:** None for steps 1 through 4 (filter constructed directly). Steps 5 and 6 use `SimulatedSource(20, 42)` and a `TelemetryProcessor`.
- **Steps:**
  1. Construct a filter at 100 m, 0 m/s. Call `predict(0.1)`. Record altitude and altitude variance.
  2. Construct a filter at 100 m, 5 m/s. Call `predict(1.0)`. Record altitude.
  3. Construct a filter, call `predict(0.1)`, record variance, call `update(100, 3.0)`, record variance again.
  4. Construct two identical filters at 100 m and predict both. Update one with (110 m, R = 1.0) and the other with (110 m, R = 100.0). Record both altitudes.
  5. Process 500 simulated frames. Compute the population standard deviation of `baro_altitude_m` and of `fused_altitude_m`.
  6. Process 100 simulated frames twice from fresh source and processor instances with the same seed. Compare `fused_altitude_m` frame by frame.
- **Expected result:**

  | Step | Expected | Tolerance |
  |---|---|---|
  | 1 | altitude 100 m; variance greater than initial | 1e-4 m |
  | 2 | altitude 105 m | 1e-4 m |
  | 3 | variance after update less than variance after predict | exact ordering |
  | 4 | R = 1 filter closer to 110 m than R = 100 filter; neither below 100 nor above 110 | exact ordering |
  | 5 | fused sigma less than 0.5 × baro sigma | exact ordering |
  | 6 | every fused altitude identical between runs | exact equality |

- **Pass/fail:** All six hold. Step 6 uses exact equality, not a tolerance, because a bit-level difference is the defect deterministic replay (REQ-LOG-003) exists to catch.

### TC-008: Log Format and Rotation Integrity

- **Requirement:** REQ-LOG-001, REQ-LOG-002
- **Level:** Unit (steps 1 through 7), Integration (steps 8 through 14)
- **Objective:** Verify that the binary log is self-describing, round-trips frames and fault events bit for bit, rotates at record boundaries, and fails safely on a damaged or foreign file; and that the Python reader (`pipeline/log_reader.py`, the twin of `LogReader`) applies the same rules.
- **Preconditions:** A temporary directory per test, removed afterwards. Frames from `SimulatedSource(20, 42)` or built by hand. Steps 8 through 14 are `tests/integration/test_log_reader.py`, on files written by `tests/integration/synthetic_log.py` with the same layout Structs the reader uses, and on the session fixture's log from the binary.
- **Steps:**
  1. Log one frame. Reopen with `LogReader`. Assert the header validates and its version, frame size, event size, and endianness marker match this build.
  2. Log 10 simulated frames. Read them back. Compare each with `memcmp` against the frame that was written.
  3. Log frame, event, frame. Assert three records with tags FRAME, FAULT_EVENT, FRAME and that the event compares equal.
  4. Set the file limit to header plus three frame records. Log 10 frames. Assert four files, each with a valid header, none larger than the limit, and 10 frames total in order.
  5. Corrupt a header field (magic, version, frame size, event size, endianness) one at a time. Assert the reader refuses each with a non-empty error.
  6. Truncate a file in the middle of its second frame. Assert the reader returns the first frame, then false, with a "torn record" error and no crash.
  7. Point the logger at a path that cannot be a directory. Assert `ok()` is false and that logging calls return without throwing.
  8. *(Integration)* Read the session fixture's log (500 simulated frames) with the Python reader: assert 500 frames, no events, timestamps 0 to 9980 ms, nothing torn, the header's sizes equal to the module's.
  9. *(Integration)* Write frame, event, frame from Python with the same Structs and read it back: assert the frames and the event in order and each frame's bytes equal to its repacking.
  10. *(Integration)* Corrupt each header field (magic, version, frame size, event size, endianness) in a Python-written file: assert the reader refuses each, naming the field.
  11. *(Integration)* Cut a file 41 bytes into its second frame: assert the first frame is returned and 41 torn bytes counted; append an unknown tag: assert refusal naming the offset.
  12. *(Integration)* Write 10 frames across four rotated files: assert they read as one run in order; then a file that restarts at 0, a file with a gap in its index, a file starting 1920 ms after the previous ends, and a file whose timestamps repeat: assert each is refused with the file and timestamps named.
  13. *(Integration)* Assert the numpy layout derived from the struct format has 104 and 16 bytes and every field offset equal to the struct's (latitude at 56, the last flag at 97), and that a frames array's bytes equal the file's.
  14. *(Integration)* The reader's command line on a directory holding a live-shaped file (three frames, GPS never fresh, one GPS timeout event): assert exit 0 with the counts, the fresh totals, and the event line; a file that is not a log exits 2.
- **Expected result:** All assertions hold. Rotation never splits a record; a limit smaller than one record still yields one record per file. Steps 8 through 14: every refusal names what was wrong, a torn tail loses nothing before it, and rotation reads as one run only when the indices are consecutive and the timestamps continue.
- **Pass/fail:** Any bit difference in step 2 or 3 fails. Any accepted foreign header in step 5 or 10 fails. Any crash or invented frame in step 6 or 7 fails. Any accepted restart, gap, or repeated timestamp in step 12 fails; any offset disagreement in step 13 fails.

### TC-009: Fixed-Rate Scheduling and Jitter

- **Requirement:** REQ-TIME-001, REQ-TIME-002 (step 7: REQ-TIME-003)
- **Level:** Unit (steps 1 through 6), Hardware (step 7)
- **Objective:** Verify that the loop is scheduled on absolute deadlines that do not drift, that overruns re-phase without bursting, that per-cycle jitter is measured and logged, and, on the target hardware, that jitter stays below 1 ms.
- **Preconditions:** `FixedRateScheduler` driven by a `FakeClock` for steps 1 through 5; `MonotonicClock` for step 6; the Raspberry Pi for step 7. Period 20 ms unless stated.
- **Steps:**
  1. Configure the fake clock to wake 300 µs late on every sleep and simulate 5 ms of work per cycle. Run 1000 cycles. Record every `scheduled_ns`.
  2. Run 10 cycles with the same latency. Record every `jitter_ns`.
  3. After one cycle, advance the clock 25 ms (5 ms past the next deadline). Run one cycle. Record `scheduled_ns`, `jitter_ns`, `missed_cycles`.
  4. After one cycle, advance the clock 65 ms. Run two cycles. Record the same three fields for each.
  5. Record three hand-built `CycleTiming`s through a `JitterLog` to a temporary file. Read the file back.
  6. With the real clock and a 1 ms period, run 20 cycles. Record every `jitter_ns` and the total elapsed time.
  7. *(Pi only, pending hardware)* Run the telemetry binary for 60 s at 50 Hz. Read `logs/telemetry_jitter.csv`.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | `scheduled_ns` of cycle n equals `start + (n+1) × 20 ms` exactly for all 1000 cycles; the span is 20.000 s, not the 25.3 s a relative sleep would produce |
  | 2 | every `jitter_ns` equals 300 000 |
  | 3 | `scheduled` unchanged (2P), `jitter` 5 ms, `missed` 0 |
  | 4 | first cycle: `missed` 2, `scheduled` 4P, `jitter` 5 ms; second cycle: `scheduled` 5P, `jitter` 0 |
  | 5 | four lines: the documented header and one line per cycle with the fields in order |
  | 6 | every `jitter_ns ≥ 0`; elapsed at least 20 ms and at most 20 ms + 2 s |
  | 7 | maximum `jitter_ns` below 1 000 000; `missed_cycles` 0 |

- **Pass/fail:** Steps 1 through 6 all hold. Step 7 is the REQ-TIME-003 criterion and is judged on the Linux `clock_nanosleep` path only; numbers from the macOS fallback are recorded in FTS-VR-001 as a preview and do not satisfy the requirement.

### TC-010: Publisher Never Blocks the Processing Loop

- **Requirement:** REQ-TRANS-003 (also exercises REQ-TRANS-001, REQ-TRANS-002)
- **Level:** Unit (steps 1 through 5 for ZeroMQ, 8 through 12 for UDP), Integration (steps 6 and 7, `tests/integration/test_transport.py`, parametrized over the two transports)
- **Objective:** Verify that nothing on the far side of either transport, absent, slow, misconfigured, or broken, delays a publish call or reaches the loop as an exception, and that the loss the publisher cannot observe is visible at the receiver.
- **Preconditions:** `ZmqPublisher` over `inproc://` in a test-owned context; `UdpPublisher` over loopback with a test-owned receiving socket bound to port 0, the port read back; every test receiver sets a 1 s receive timeout so a missing message fails instead of hanging. A PUB socket never blocks on send (zmq_socket(3)): when a subscriber's queue reaches the high-water mark, messages to that subscriber are dropped silently and the send still reports success. A datagram send with `MSG_DONTWAIT` never waits either: the kernel queues the datagram whole or refuses it with an error code, and a receiver whose buffer is full drops on its own side. Both publishers therefore count accepted sends and refused sends only; loss is counted at the receiver from gaps in frame timestamps. Over `tcp://` the kernel's TCP buffers on both ends sit between the two ZeroMQ high-water marks and hold minutes of 121-byte frames, so a subscriber that stalls for seconds loses nothing at process level; the drop at the mark is proven at unit level over `inproc://` (step 4), and the UDP variant of step 6 shrinks the receiver's kernel buffer so the drop is visible there.
- **Steps:**
  1. Bind a second publisher to an endpoint already bound by a first one. Record `ok()`, `error()`, and the result of one `publish()`.
  2. Construct a publisher on the string `not-an-endpoint`. Record the same three things.
  3. With no subscriber connected, publish 1000 frames. Record the wall time and the statistics.
  4. Publisher high-water mark 10, subscriber receive high-water mark 10, subscription confirmed live. Publish 1000 frames without the subscriber reading. Record the wall time and the statistics, then drain the subscriber and count what arrived.
  5. Shut the context down, then publish one frame. Record the return value, the failure count, and the error code.
  6. *(pytest, each transport)* Run `telemetry` publishing 500 frames with a receiver that reads 50 frames, stops reading for 3 s, then drains; the UDP receiver's `SO_RCVBUF` is 4096 bytes. Read the process's timing summary, the receiver's gap and out-of-order counts, and compare the frames after the stall with the log.
  7. *(pytest, each transport)* Run `telemetry` publishing 50 frames with no receiver. Read the timing summary and the transport summary. (The 500-frame run by hand over ZeroMQ is on record in FTS-VR-001 Entry 6.)
  8. Construct `UdpPublisher` on `localhost`, on `256.1.1.1`, on port 0, on port 70000, on `0.0.0.0`, on `255.255.255.255`, and once with the process's descriptor limit at zero. Record `ok()`, `error()`, and the result of one `publish()` each time.
  9. With no receiver bound, publish 1000 frames. Record the wall time and the statistics.
  10. With no receiver bound, publish one frame, then publish again every millisecond for up to 100 ms. Record the first failure's error code and the result of the publish after it.
  11. Receiver bound with `SO_RCVBUF` 4096 and never reading. Publish 1000 frames. Record the wall time and the statistics, then drain the receiver and count what arrived.
  12. Construct on `127.255.255.255`, the loopback's directed broadcast address, with `SO_BROADCAST` not set, and publish once. Record `ok()`, the result, and the error code.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | `ok()` false, `error()` non-empty, `publish()` false, one send failure counted; the first publisher still `ok()`; no exception |
  | 2 | same as step 1 |
  | 3 | 1000 publishes complete in under 2 s; `published` 1000, `send_failures` 0 |
  | 4 | 1000 publishes complete in under 2 s; `published` 1000 plus the warm-ups, `send_failures` 0; drained count at least 1 and fewer than 1000 |
  | 5 | `publish()` false, `send_failures` 1, `last_errno` `ETERM`, no exception |
  | 6 | `published` 500, `send_failures` 0; missed cycles fewer than 75, which is under half the 150 a 3 s blocked send would cost, with the unstalled session run's count recorded beside it; every received frame in order and byte-identical to the log, at most one jump in the timestamps (the drop) and none after it, out-of-order count 0; gaps recorded per transport: UDP at least 50 on Linux, where the 4 KB buffer holds a handful of the 150 stalled frames, recorded only on macOS, where the kernel sometimes holds hundreds; ZeroMQ 0 expected, recorded |
  | 7 | exit 0 and 50 frames in the log; `published` plus `send_failures` 50; ZeroMQ `send_failures` 0; UDP `send_failures` 0 or the kernel's port-unreachable count, recorded |
  | 8 | `ok()` false with a message naming the address, the port, "unicast", or the descriptor; `publish()` false and counted; no exception |
  | 9 | 1000 publishes complete in under 2 s; `published` plus `send_failures` 1000; any `last_errno` is `ECONNREFUSED` |
  | 10 | at least one `publish()` false with `last_errno` `ECONNREFUSED` within the 100 ms; the publish after it true; no exception |
  | 11 | 1000 publishes complete in under 2 s; `published` plus `send_failures` 1000; drained count at least 1 and fewer than 1000 (the step 4 range: how many a kernel keeps past a 4 KB buffer is its business; macOS held 22, 104 and 301 in different runs) |
  | 12 | no exception, and one of: `ok()` false with `EACCES` named (Linux marks the address broadcast in its local table and refuses the connect), `publish()` false with `last_errno` `EACCES` (a kernel that refuses at the first send), or one publish that succeeds (macOS routes it to 127.0.0.1 like unicast, measured 2026-10-12); the limited broadcast 255.255.255.255 is refused by name in step 8 on both kernels |

- **Pass/fail:** Any publish that waits past the bound, any exception that reaches the caller, or any missed cycle attributable to the publisher fails. Step 4's drained count is asserted as a range, not a number: ZeroMQ documents that a queue may hold as much as 60 to 70% fewer messages than the configured mark. Steps 9 to 12 assert what both kernels guarantee and record what differs between them (FTS-VR-001 Entry 10): how many port-unreachables the kernel reports, and whether a directed broadcast is refused at connect, refused at the first send, or routed as unicast.

### TC-011: Measurement Freshness Gating

- **Requirement:** REQ-PROC-006 (also exercises REQ-PROC-002, REQ-PROC-003, REQ-SENS-006)
- **Level:** Unit
- **Objective:** Verify that each filter predicts every cycle and applies a channel's reading only on a cycle in which the channel is NOMINAL and the reading is fresh, that no reading is applied twice, that the gyroscope is integrated over the interval since its last accepted sample, and that the simulator delivers GPS fixes at the configured rate.
- **Preconditions:** `TelemetryProcessor` with its default tuning. Frames are built by hand so each step controls which channel is fresh, or come from `SimulatedSource` where its 1 Hz GPS schedule is the subject. "Twin" means a second processor fed the same frames with one field changed; equality is exact (`EXPECT_EQ` on the float outputs), because the claim is that the computation is identical, not close.
- **Steps:**
  1. Feed 20 fresh frames, then 10 frames whose barometer read failed (`baro_read_ok` false, pressure held, channel NOMINAL, GPS stale). Compare fused altitude and vertical speed with a twin whose 10 frames mark the barometer DEGRADED.
  2. Warm up on 20 frames with GPS on every frame, then feed 20 frames with `gps_read_ok` false and a GPS altitude of 9999 m. Compare with a twin whose stale frames carry the held fix.
  3. Feed 100 frames from the default simulator (fixes at 0 and 1000 ms). Compare with a twin told the GPS is DEGRADED on every non-fix frame, and with a twin told the GPS is DEGRADED after its first fix.
  4. Feed a first frame whose barometer read failed, then a fresh one. Record the fused outputs of both.
  5. Process one frame, then a NOMINAL frame with `imu_read_ok` false carrying 45° of accelerometer tilt. Record pitch and roll.
  6. Fresh flat frames at 0 and 20 ms, stale frames at 40, 60, and 80 ms, then a fresh frame at 100 ms with a 1 deg/s pitch rate and a flat accelerometer. Record pitch.
  7. Generate 100 frames at 20 ms from the default generator. Record `gps_read_ok` and the GPS fields of each.
  8. Generate frames with the fix period equal to the frame interval; with a 1000 ms period at a 30 ms frame interval; with a period of zero.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | identical on all 10 frames |
  | 2 | identical on all 20 frames |
  | 3 | identical to the first twin on all 100 frames; identical to the second until 980 ms and different at 1000 ms |
  | 4 | frame 0: fused altitude and vertical speed 0; frame 1: fused altitude equal to its barometric altitude, vertical speed 0 |
  | 5 | pitch and roll unchanged |
  | 6 | pitch 0.0784° (0.98 × 1 deg/s × 0.080 s) within 0.001°; roll 0 |
  | 7 | `gps_read_ok` true at 0 and 1000 ms only; GPS fields repeat the previous frame's between fixes and change at 1000 ms; barometer and IMU `read_ok` true throughout |
  | 8 | every frame fresh; fixes at 0, 1020, and 2010 ms; every frame fresh |

- **Pass/fail:** Any stale reading that moves a filter state, any fix applied more than once, any gyroscope gap integrated over the wrong interval, or any frame between fixes that claims a fresh GPS reading fails. Step 6 is the one tolerance in the card: a closed-form value compared at float precision.

### TC-012: Barometer Compensation and Register Protocol

- **Requirement:** REQ-SENS-001, REQ-SENS-002
- **Level:** Unit (steps 1 through 7), Hardware (step 8)
- **Objective:** Verify that the barometer driver reproduces the manufacturer's compensation arithmetic exactly, reads the chip through the documented register protocol (identity, little-endian calibration, big-endian burst data, the sleep-config-normal configuration sequence), accepts both Bosch identities and refuses others, and reports a failed or incomplete reading as empty rather than as a number.
- **Preconditions:** `Bmp280` over a `FakeI2cBus` at 0x76. **Primary oracle:** the worked example on page 23 of BMP280 datasheet revision 1.14 (BST-BMP280-DS001-11, May 2015): trimming set dig_T1 27504, dig_T2 26435, dig_T3 −1000, dig_P1 36477, dig_P2 −10685, dig_P3 3024, dig_P4 2855, dig_P5 140, dig_P6 −7, dig_P7 15500, dig_P8 −14600, dig_P9 6000; raw readings UT 519888 and UP 415148; answers t_fine 128422 and temperature 2508 (25.08 °C) from the int32 code, pressure 100653.27 Pa from the double code, and 25767233 in 1/256 Pa from the int64 code the driver runs (the page prints 25767236 and notes that the integer result may deviate from the double by rounding). The example's measurement table swaps the UT and UP register addresses (confirmed by Bosch, uncorrected); the fake serves UP at 0xF7 to 0xF9 and UT at 0xFA to 0xFC, the chip's real order, so the example tests register order as well as arithmetic. The same compensation code is in the BME280 datasheet (section 4.2.3), so the oracle holds for either chip.
- **Steps:**
  1. Call `compensate_temperature()` and `compensate_pressure()` with the example's trimming set and raw readings.
  2. Load the fake with chip ID 0x58, the 24 calibration bytes at 0x88 little-endian, and the six data bytes at 0xF7; call `init()`, then `read()`.
  3. Repeat step 2 with chip ID 0x60, then with 0x55 (a BMP180).
  4. After a good `init()`, record the transaction log.
  5. Fail, one at a time: the identity read, the calibration read, the first write to 0xF4, the write to 0xF5, the second write to 0xF4 (value 0x2F). Call `init()` each time.
  6. After a good `init()`: fail the burst at 0xF7 and call `read()`; lift it; load a pressure field of 0x80000 and call `read()`; load a temperature field of 0x80000 and call `read()`; restore the example and call `read()`.
  7. Serve the calibration words high byte first and read the example.
  8. *(Hardware)* With the calibration bytes (0x88 to 0x9F) and one data sample (0xF7 to 0xFC) dumped from the real chip on the Raspberry Pi (`i2cdump -y 1 0x76 b`), bake them into a test and compensate them.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | t_fine 128422 and temperature 2508 exactly; pressure 25767233 exactly, and within 0.1 Pa of 100653.27 when divided by 256 |
  | 2 | `init()` true and `chip_id()` 0x58; 1006.5325 hPa and 25.08 °C within 0.001 |
  | 3 | 0x60 accepted with `chip_id()` 0x60; 0x55 refused with 0x55, 0x58, and 0x60 in the message, one transaction only |
  | 4 | read 0xD0; read 24 bytes at 0x88; write 0xF4 = 0x00; write 0xF5 = 0x00; write 0xF4 = 0x2F, in that order |
  | 5 | each refuses `init()` with the cause named, and `read()` stays empty |
  | 6 | empty, empty, empty, then a reading |
  | 7 | a temperature and a pressure both more than 0.01 away from the example's: the suite can tell the byte order apart |
  | 8 | pressure between 950 and 1050 hPa and temperature between 10 and 35 °C: physically plausible for a bench near sea level |

- **Pass/fail:** Any deviation from the example's integer results, any accepted identity outside {0x58, 0x60}, any configuration write out of order, any number produced from a failed or reset-value burst, or any exception fails. Step 8 fails if the real chip's numbers are implausible, which would mean the register order or the calibration layout is wrong in a way the example did not catch. The test file also covers the division guard in the pressure code (an all-zero calibration yields 0, as the datasheet's code does) and `read()` before `init()`, which is empty and touches the bus not at all.

### TC-013: MPU6050 Scaling and Configuration

- **Requirement:** REQ-SENS-003, REQ-SENS-004
- **Level:** Unit
- **Objective:** Verify that the MPU6050 driver checks the chip's identity, configures the one sample-rate divider, the low-pass filter, and both full-scale ranges with the decided values, assembles big-endian two's-complement register pairs in the right order, scales raw counts to the frame's units, and reports a failed transaction as an empty reading rather than an exception.
- **Preconditions:** `Mpu6050` over a `FakeI2cBus` (TC-014 steps 1 through 3) whose WHO_AM_I register (0x75) holds 0x68. Expected values are arithmetic from the register map (RM-MPU-6000A-00 revision 4.2): 16384 LSB/g at ±2 g, 131 LSB per deg/s at ±250 deg/s, sample rate = 1 kHz / (1 + SMPLRT_DIV) with the low-pass filter enabled.
- **Steps:**
  1. Call `init()` with the default 100 Hz. Record the transaction log and the register file.
  2. Load WHO_AM_I with 0x70 (an MPU-6500's answer) and call `init()`. Record the result, the message, and the transaction count.
  3. After a good `init()`, load the fourteen output registers with accelerometer counts (0x4000, 0xC000, 0x2000), any temperature pair, and gyroscope counts (0x0083, 0xFF7D, 0x0000); call `read()`.
  4. Load the accelerometer x pair as (0x00, 0x40) and the gyroscope x pair as (0x7D, 0xFF); call `read()`.
  5. Call `sample_rate_divider()` with 100, 50, 4, 1000, 1, 3, 30, 0, −50, and 2000.
  6. Fail the burst at 0x3B and call `read()`; lift the failure and call `read()` again.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | one read of 0x75, then writes in order 0x6B = 0x01, 0x19 = 9, 0x1A = 0x04, 0x1B = 0x00, 0x1C = 0x00; `init()` true |
  | 2 | `init()` false; the message names 0x70 and 0x68; one transaction only, nothing written to a chip of unknown identity |
  | 3 | accelerometer (9.81, −9.81, 4.905) m/s² within 1e-4; gyroscope (1.0, −1.0, 0.0) deg/s within 1e-4 |
  | 4 | accelerometer x 64/16384 × 9.81 = 0.0383 m/s²; gyroscope x 32255/131 = 246.2 deg/s. Swapped bytes are a different number, never the right one with the wrong sign |
  | 5 | 9, 19, 249, 0; the other six refused |
  | 6 | empty, then a reading |

- **Pass/fail:** Any write out of order or with another value, any accepted identity other than 0x68, any scale or sign error, any rate accepted that the divider cannot produce exactly, or any exception fails. The test file also covers: a failed identity read and a failed configuration write refuse `init()` and name the cause; an unreachable rate is refused before the bus is touched; `read()` before `init()` is empty and touches the bus not at all.

### TC-014: I2C Bus Seam and Live Sensor Source

- **Requirement:** REQ-SENS-001, REQ-SENS-002, REQ-SENS-003, REQ-SENS-004 (also exercises REQ-FAULT-001's `read_ok` contract and REQ-LOG-004)
- **Level:** Unit (steps 1 through 10), Hardware (steps 11 through 15)
- **Objective:** Verify the seam the drivers stand on (the fake's semantics and the Linux implementation's failure paths), and that the live source presents real sensors through the `DataSource` contract: raw fields and `read_ok` only, held values with `read_ok` false on a failed read, zeros before the first good read, timestamps from the monotonic clock, and a startup failure that names the chip.
- **Preconditions:** `FakeI2cBus` with both chips loaded (the barometer with the TC-012 example, the MPU6050 with one g on z), a `FakeClock` for the timestamps, and `LinuxI2cBus` on files that exist on every machine. The Hardware steps need the Raspberry Pi with both sensors wired, and the Pi and the Mac on one network for step 15.
- **Steps:**
  1. Load four bytes at 0x88 on the fake and burst-read them.
  2. Write one byte to 0x6B on one device; read the register files of both devices.
  3. Fail reads at one register and writes at another; attempt each, and a neighbour of each, on both devices; lift the failures and retry.
  4. Construct `LinuxI2cBus("/dev/i2c-99")` and `LinuxI2cBus("/dev/null")`; attempt a transaction on each.
  5. Construct the live source over the fake and read one frame.
  6. Fail the barometer burst and read a frame; lift it and read again.
  7. Fail the IMU burst and read a frame; lift it and read again.
  8. Load the barometer's data registers with the reset value and fail the IMU burst from the start; read the first frame.
  9. Read frames with the fake clock advanced by 20 ms, 19.6 ms, 40 ms, and 0.4 ms between them.
  10. Construct the source with the barometer answering 0x55, then with the MPU6050 answering 0x70, then with both.
  11. *(Hardware)* `i2cdetect -y 1`; `i2cget -y 1 0x76 0xD0`; `i2cget -y 1 0x68 0x75`.
  12. *(Hardware)* `telemetry --live --cycles 50` with the board flat.
  13. *(Hardware)* `telemetry --live --cycles 1500`, watching pitch and roll: raise the end the GY-521's X arrow points to, then roll the board right.
  14. *(Hardware)* Replay the live log (`telemetry --replay logs/telemetry_000.bin`) and `cmp` the two logs.
  15. *(Hardware)* Receiver on the Mac, then `telemetry --live --zmq tcp://0.0.0.0:5555 --cycles 500` on the Pi.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | the four bytes, in order; one logged read of length 4 |
  | 2 | the byte in that device's file and in the log; the other device's register unchanged |
  | 3 | the failing transactions false and logged, the neighbours true; after lifting, all true |
  | 4 | `/dev/i2c-99`: not ok, the path in the message, transactions false with a nonzero errno. `/dev/null`: the transaction false with a nonzero errno (ENOTTY on Linux, where the file opens and `I2C_SLAVE` is refused; ENOTSUP from the stub elsewhere) |
  | 5 | 1006.5325 hPa, 25.08 °C, accelerometer (0, 0, 9.81), gyroscope zero; barometer and IMU `read_ok` true, GPS false; GPS fields zero; every status NOMINAL; every computed field zero; timestamp 0 |
  | 6 | pressure and temperature equal to the previous frame's, `baro_read_ok` false, IMU fresh and unchanged; then fresh again |
  | 7 | the six IMU fields equal to the previous frame's, `imu_read_ok` false, barometer fresh; then fresh again |
  | 8 | both `read_ok` false; pressure, temperature, accelerometer, and gyroscope all exactly zero |
  | 9 | 0, 20, 40, 80, 80 ms |
  | 10 | `ok()` false; the message names the barometer and 0x55, then the IMU and 0x70, then both chips; frames are still produced, the failed channel stale |
  | 11 | 68 and 76 (or 77) in the grid; 0x58 or 0x60; 0x68 |
  | 12 | pressure 950 to 1050 hPa, temperature 10 to 35 °C, accelerometer about (0, 0, 9.8), gyroscope within a few deg/s at rest; barometer and IMU NOMINAL with `read_ok` 1; GPS DEGRADED from 2020 ms with one COMM_TIMEOUT event; 0 missed cycles |
  | 13 | nose up: pitch positive; roll right: roll positive |
  | 14 | `cmp` silent |
  | 15 | `received 500 frames`; gaps at or near 0 |

- **Pass/fail:** Steps 1 through 10 exact as listed. Step 12 fails on any out-of-range value or any event other than the GPS timeout; step 13's signs are recorded, and a wrong sign is a one-line change in the driver's axis mapping noted in FTS-DD-001 Section 6, not a failure of the card; step 14 fails on any byte difference; step 15 fails on frames lost at the publisher (gaps the receiver reports may be the network's).

### TC-015: Noise Characterization and Derived Constants

- **Requirement:** REQ-PROC-004 (also exercises REQ-SENS-006 and REQ-PROC-003)
- **Level:** Integration (steps 1 through 8), Hardware (step 9), manual (steps 10 through 12)
- **Objective:** Verify that the noise analysis recovers known sensor noise from a log, separates white noise from drift, and derives the Kalman constants the way FTS-NP-001 states; then execute it on a stationary capture of the real sensors and carry the measured constants into the pipeline.
- **Preconditions:** `analysis/noise_profile.py` and `pipeline/log_reader.py`. Synthetic logs written by `tests/integration/synthetic_log.py` from numpy's seeded generator, 45,000 frames at 20 ms (the capture's length), and the session fixture's 500-frame seed-42 log from the binary. Steps 1 through 8 are `tests/integration/test_noise_profile.py`. Step 9 needs the Raspberry Pi with both sensors, flat and untouched for 15 minutes, with the conditions noted (FTS-NP-001 Section 4). Tolerances are multiples of the estimator's standard error, stated before the data: 3/√(2N) for a sigma, 3σ/√N for a mean, 10% for the Allan ratio, 15% for the binary's 500-frame sigmas (4.7 standard errors), 25% for the derived variance (3.2).
- **Steps:**
  1. Planted white noise on every raw channel, a 2.5 deg/s bias on gyro_y and a 0.05 m/s² offset on accel_x: assert each sigma within 3/√(2N) of its planted value, each mean within 3σ/√N, the bias recovered within 3σ/√N and its predicted tilt 0.98 × b.
  2. White noise through the Allan estimator at m = 1, 4, 16, 64, 256: assert ADEV(m) × √m / σ within 10% of 1; assert ADEV(τ₀)² equals half the mean squared first difference to 1e-12.
  3. White noise of sigma 1 plus a linear drift rising √12 over the run: assert the plain sigma equals √(σ² + (dT)²/12) within 2%, ADEV(τ₀) equals σ within 1%, their ratio √2 within 3%, and ADEV at the longest averaging time equals d × τ / √2 within 10%.
  4. Lag-1 autocorrelation: white noise within 3/√N of 0; an AR(1) series with φ = 0.3 within 0.03 of 0.3.
  5. A planted run of ten identical samples reads as 10 and a white series as 1; float32 samples on the accelerometer's LSB grid return that step within 1%; frames carrying held copies with `read_ok` false enter neither n nor the runs.
  6. The ISA slope matches a central difference of `altitude.cpp`'s formula within 1e-4 m/hPa; the accelerometer tilt of (9.81, 0, 9.81) is pitch 45°; the derived constants follow their formulas (R = ADEV(τ₀)² on a white log, P0_alt = R, the retained ones unchanged, tilt = 0.98 b, offsets in mg); the R rule takes the detrended variance on AR(1) noise of ρ₁ = 0.3 plus 1.8 m of drift, recovering the true per-sample variance within 3% where ADEV(τ₀)² understates it by 30%, and warns on anti-correlated samples and on pressure repeats far beyond float32's chance count; the jitter summary of three hand-written rows is exact; the Markdown names every channel; the four figures render; a log whose timestamps repeat is refused.
  7. The binary's seed-42 log: assert each raw sigma within 15% of the simulator's literal (0.2 hPa, 0.1 °C, 0.05 m/s², 0.1 deg/s), the means centered (the bounds of `SimulatedDataTest.NoiseIsCenteredOnBaseValues`), exactly 10 fresh GPS fixes, the barometric sigma in metres the same two ways within 1% and within 15% of 1.665 m, ADEV(τ₀)²/σ² within [0.85, 1.15] and |ρ₁| under 0.15 on every channel, no identical run over 3, no events, the fused sigma under 0.5 × the barometric sigma after the settle window, the gyro bias under 0.02 deg/s, and pitch and roll within 0.05° of the accelerometer-only tilt.
  8. The derived barometric variance within 25% of 2.774 m² = (0.2 hPa × 8.327 m/hPa)²; the command line on the same log with `--jitter`, `--figures`, `--json`: exit 0, the JSON complete, four PNGs.
  9. *(Hardware)* `telemetry --live --cycles 45000` with the board flat and untouched, the room temperature and a METAR sea-level pressure noted before and after; the five rotated files, the jitter CSV, the stdout's last lines, the commit, OS, kernel, GCC, chip IDs, and SHA-256 sums kept under `captures/`.
  10. The reader's summary and the analysis on the capture, figures to `docs/img/`, the JSON beside the capture; the tables into FTS-NP-001 with the provenance filled.
  11. `config/telemetry_config.yaml` and the defaults of `ProcessorConfig` and `SimulationNoise` together (`ConfigTest.ShippedFileEqualsDefaults` holds them equal): the barometric variance from the JSON, the initial altitude variance following it; the simulator's four sigmas; the C++ suite and pytest green.
  12. File 000 replayed on the Mac through the constants before and after the change, and through the three initial-velocity-variance candidates: the fused altitude sigma, the vertical speed sigma, and the startup transient's peak and settle time tabulated; the candidate chosen by the rule in FTS-NP-001 Section 3.
- **Expected result:** Steps 1 through 8 as listed. Step 9: 45,000 frames in five files reading as one run, one event (the GPS timeout near 2020 ms), zero torn bytes, zero missed cycles; pressure sigma a few pascals, barometric altitude sigma 0.2 to 0.35 m, gyro sigma about 0.02 deg/s with a bias of a few deg/s, accelerometer sigma about 0.02 m/s², no identical run near 10 on a stuck-tracked channel, the barometer's detrended ρ₁ within sampling error of zero and the IMU's positive (the DLPF, about 0.1 to 0.3, so its σ/ADEV(τ₀) sits above 1). Step 12: the measured constants replace the placeholders, with the climb-response and scatter consequences recorded.
- **Pass/fail:** Any tolerance in steps 1 through 8 fails. Step 9 fails on any event other than the GPS timeout, a frame count other than 45,000, pressure repeats beyond five times float32's chance count (the chip read faster than it converts), or a significantly negative detrended ρ₁ on the barometer (no mechanism produces it). A significantly positive detrended ρ₁ is not a failure: the R rule then takes the variance of the linearly detrended series (FTS-NP-001 Section 3.2). Steps 10 to 12 are recorded in FTS-VR-001 Entry 9; a constant that leaves the pipeline with a different value than the document states fails.

### TC-016: Configuration Loading, Validation, and the Session Record

- **Requirement:** REQ-CFG-001, REQ-CFG-002, REQ-CFG-003 (also exercises REQ-TRANS-002, REQ-SENS-006, REQ-LOG-003)
- **Level:** Unit (steps 1 through 9, `tests/unit/test_config.cpp`; step 10, the injection tests in `test_kalman.cpp`, `test_complementary.cpp`, `test_simulated_data.cpp`, `test_live_sensor_source.cpp`), Integration (steps 11 through 20, `tests/integration/test_config.py`)
- **Objective:** Verify that every tunable REQ-CFG-001 names is read from one YAML file and reaches the module that uses it; that every invalid value is refused at startup and named, all in one report; and that the configuration a session ran with is recorded beside its log and is what a replay uses unless another is named.
- **Preconditions:** `config/telemetry_config.yaml` (the shipped file) and `src/config/telemetry_config.cpp`. Unit steps load YAML text in memory (`load_config_text`) except where a path is the point. Integration steps run the binary from a temporary directory with `--config` naming the shipped file, as every run in the suite does (Section 2.2); a "copy" is the shipped text with one `key: value` line edited.
- **Steps:**
  1. Load the shipped file: no problems; every field equals a default-constructed `TelemetryConfig` (the drift test).
  2. A nonexistent path and a directory path: one problem each, naming the path.
  3. A syntax error (an unterminated flow sequence on line 3): one problem naming line and column; an empty document and a sequence document: "must be a mapping".
  4. A missing section, a section of the wrong type, a missing key, an unknown key at the top level and nested, a duplicate key: each named once with its dotted path. Then each of the 36 keys removed in turn: `<path>: missing`, except the two whose removal empties their section (`timing.period_ms`, `sensors.barometer.address`), reported as the section "must be a mapping (got nothing)".
  5. The scalar rules: `0x76` reads 118; `010`, `1.5`, `abc`, an empty scalar and `0x` are refused as integers; `.inf`, `.nan` and `abc` are refused as numbers; `yes`, `on` and `1` are refused as booleans; an empty string is refused where a string is required. Then each of the 36 keys given a sequence, and the text its rule refuses (`abc` for a whole number, `.nan` for a number, `yes` for a switch, an empty string for text): each named under its path by its own rule.
  6. Every range at both edges: period 20 to 1000; addresses 0x08 to 0x77; IMU rate 4 to 100 and an exact divisor of 1000; timeouts at least 1; stuck count at least 2; recovery count at least 1; pressure and temperature minimum below maximum; accelerometer limit, the four variances and the sea-level pressure positive; alpha 0 to 1 inclusive; maximum file bytes at least 121; UDP host a dotted quad, port 1 to 65535, ZeroMQ endpoint non-empty; seed within 32 bits; GPS interval at least 1; sigmas positive (`std::normal_distribution` requires it). Then every whole-number key one past each of its edges, every positive number at 0, alpha at -0.1 and 1.1: one table over every key, because gcov counts a template's branches per instantiation and one key per rule leaves the other field types' decisions untaken.
  7. The cross-field rules: the UART timeout must exceed the GPS interval; the IMU rate must be at least the processing rate.
  8. Three problems in one file (period 0, stuck count 1, port 70000): all three reported, in schema order, `ok` false.
  9. `log_prefix` and `recorded_config_path`: `logs/telemetry_007.bin` gives `telemetry` and `logs/telemetry_config.yaml`; `replay_000.bin` gives `replay`; names without `_NNN.bin` give nothing.
  10. Injection: a GPS variance of 2500 against 25 changes the fused altitude at the first fix; alpha 1.0 through `ProcessorConfig` reproduces TC-006 step 3; a sea-level reference of 1023.25 hPa moves the barometric altitude by about 83 m; a tenfold pressure sigma scales the pressure noise tenfold frame by frame while every other channel stays bit-identical, and sigmas of 1e-6 sit on the base values within float rounding (zero is outside `std::normal_distribution`'s precondition); an IMU rate of 50 Hz writes `SMPLRT_DIV` 19; a rate of 30 Hz refuses startup by name.
  11. *(pytest)* No configuration file and no flag, in a bare directory: exit 1, the default path and `--config` named, no `logs/` created.
  12. *(pytest)* `--config` naming a missing file: exit 1, the path named.
  13. *(pytest)* A copy with three invalid values: exit 1, all three dotted keys in stderr in schema order, no log written.
  14. *(pytest)* The session's `logs/telemetry_config.yaml` equals the shipped file byte for byte; the session's replay, handed the same file by `--config`, printed "equal to the configuration recorded with"; a replay with no `--config` printed "(recorded with", reproduced the session log byte for byte, and its `logs/replay_config.yaml` equals the same bytes.
  15. *(pytest)* A copy with `gps_measurement_variance_m2: 2500`, 50 cycles: the raw fields equal the session's first 50 frames and a computed field differs; the record beside the log equals the copy.
  16. *(pytest)* The session log copied alone into a bare directory and replayed: exit 1 naming the expected `telemetry_config.yaml`.
  17. *(pytest)* `--replay <session log> --config <the changed copy>`: exit 0, "not the configuration recorded" printed, `logs/replay_000.bin` differs from the session log.
  18. *(pytest)* `--replay logs/replay_000.bin` in the session directory: exit 1, "would write" in stderr, the file's bytes unchanged.
  19. *(pytest, each transport)* The shipped file (both transports disabled) with `--zmq` or `--udp`: frames received, the `Transport:` summary 50 published, 0 failures.
  20. *(pytest, each transport)* A copy with the transport enabled and the receiver's address, no flag: frames received and byte-identical to the log.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | `ok`, zero problems, every field equal |
  | 2 to 8 | `ok` false, the problems exactly as listed, nothing else reported |
  | 9 | the prefixes and paths as listed |
  | 10 | each value demonstrably used by its module |
  | 11 to 13 | exit 1, the messages as listed, no log past the header |
  | 14 | both records equal the shipped file |
  | 15 | raw fields equal, a computed field different, the record equals the copy |
  | 16 | exit 1, the path named |
  | 17 | exit 0, the notice, a different log |
  | 18 | exit 1, the input intact |
  | 19 and 20 | frames received as listed |

- **Pass/fail:** Any field of the shipped file differing from the defaults fails (step 1). Any invalid value accepted, any valid edge value refused, any problem reported under the wrong key, or fewer problems than planted fails. A record that differs from the file by one byte fails. A replay that runs without a record and without `--config`, or that runs another configuration silently, fails.

## 6. Coverage Strategy

### 6.1 Statement versus Branch Coverage

**Statement coverage** reports whether each executable line ran at least once. It is necessary but weak: an `if` with no `else` reaches 100% statement coverage when the condition is true once, even though the false path was never exercised.

**Branch coverage** reports whether each decision outcome ran. Every `if`, loop condition, and ternary counts as two branches, and both must execute. For a fault detector, the false path is the interesting one: "sensor is healthy, do nothing" is the branch most likely to hide a bug that only appears when the condition flips.

This project measures both and gates on branch coverage. Statement coverage is reported for completeness but does not block a build.

The figure is taken after both the Google Test suite and the pytest suite have run the instrumented build: the integration tests drive `build/telemetry`, whose code is `telemetry_core`'s, so their counts merge into the same files the unit tests wrote. Both levels are requirements-based tests, which is what structural coverage analysis is performed over (DO-178C 6.4.4.2), and a count can only rise. verification_results.md names the branches only the integration level reaches.

### 6.2 Why Not 100%

Some branches cannot be reached from automated tests in CI:

- Hardware error paths in the I2C and UART drivers (a failed `ioctl`, a bus that returns garbage) need physical hardware to trigger. They are exercised on the Raspberry Pi manually and documented in verification_results.md, but CI runs on a machine with no sensors.
- Defensive branches that guard against conditions the type system already excludes.
- `main()` argument handling and startup error exits.

Chasing these to 100% produces tests that exist to satisfy the metric rather than to verify a requirement. Each uncovered branch shall instead be listed in verification_results.md with a justification. That list is itself a review artifact.

### 6.3 Thresholds

| Scope | Branch coverage target |
|---|---|
| `src/processing/` (filters and fault detection) | 90% |
| `src/config/` (the configuration loader and its rules) | 90% |
| `src/timing/`, `src/logging/`, `src/transport/`, `src/replay/` | 80% |
| `src/drivers/` | 70% |
| Overall | 80% |

Processing carries the highest target because filters and fault detection are pure logic with no hardware dependency, so every branch is reachable from a unit test; the configuration loader shares it, every branch of it reachable from a string. Drivers carry the lowest because their error branches are hardware-bound. The overall figure is a floor, not a goal.

### 6.4 MC/DC

Modified Condition/Decision Coverage is the structural coverage criterion DO-178C requires for Level A software. It extends branch coverage to compound conditions: for a decision such as `if (a && b)`, MC/DC requires test cases showing that each of `a` and `b` independently changes the outcome while the other is held fixed. Branch coverage is satisfied by two test cases (true, false). MC/DC needs at least three for two conditions, and N+1 for N conditions.

MC/DC is out of scope for this project. It requires tool support (gcov does not report it) and the DAL A rigor is not proportionate to a development system. Where compound conditions appear in fault detection logic, they are kept short and each operand is tested in isolation, which approaches MC/DC intent without claiming the criterion.

---

## 7. Open Items

- Nine requirements are Partial in FTS-TM-001, the four I2C sensor requirements among them (on-target evidence pending). The Notes column there states what each is missing.
- TC-015's Hardware and manual steps (9 through 12) wait for the stationary capture of 2026-10-13. REQ-PROC-004 stays Partial until then, and after it with the GPS variance as its one named gap (REQ-SENS-005).
- The Hardware steps of TC-002 (step 8), TC-012 (step 8), and TC-014 (steps 11 through 15) wait for the Raspberry Pi; TC-012's step 8 needs the real chip's register dump. Results go to FTS-VR-001 Entry 8 §8.6. The addresses the live source starts from (`sensors.barometer.address` 0x76 and `sensors.imu.address` 0x68 in `config/telemetry_config.yaml`, the driver named for the BMP280) are provisional until step 11 reads the real values (FTS-DD-001 Section 12); a change lands in the file and the struct defaults together.
- Both transports are implemented; TC-005 and TC-010 steps 6 and 7 are `tests/integration/test_transport.py`, over both, and the UDP destination comes from the configuration file (TC-016 step 20). TC-004's Python comparison step is `tests/integration/test_replay_identity.py`.
- REQ-TIME-003 is judged on the Raspberry Pi (TC-009 step 7). Jitter numbers measured on macOS use the `nanosleep` fallback and are recorded in FTS-VR-001 as a preview only.
- `DataSource` has no end-of-stream signal; `LogReplaySource` repeats its last frame after the log ends and exposes `exhausted()`. See FTS-DD-001 open decisions.
- GPS fix-quality detection (FAULT-003b) is deferred until the NEO-6M driver adds a fix-quality field. GPS stuck detection is not covered by any FAULT entry and is not implemented.
