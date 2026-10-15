# Flight Telemetry System — Software Test Plan

> **Document ID:** FTS-TP-001
> **Version:** 0.1.0
> **Status:** Draft
> **Last Updated:** 2026-10-15
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

- **Scope:** Individual C++ functions and classes in isolation. Drivers (the NMEA parser on the manual's sentences, the serial port on a pseudo-terminal, the GPS driver over a fake port included), processing, timing, logging, and transport modules.
- **Framework:** Google Test.
- **Isolation:** Hardware dependencies are replaced with test doubles. The `DataSource` abstraction is the primary seam: a test-only `FakeSource` returns hand-built frames, so the pipeline can be exercised without sensors or the simulator. Time-dependent logic does not sleep in tests: fixed-rate scheduling takes an injectable `Clock` and tests drive it with a fake that advances time by fiat; fault timeouts read time from frame timestamps. One real-clock smoke test (about 20 ms) exercises the OS boundary with loose bounds.
- **Location:** `tests/unit/`, one test file per module (`test_altitude.cpp`, `test_fault_detection.cpp`, and so on).
- **Executed:** Every commit, in CI and locally.

### 2.2 Integration Tests

- **Scope:** The full pipeline across the C++/Python boundary: C++ processing publishes over ZeroMQ and UDP and the Python receiver consumes and validates; the binary's own log replayed through it and compared in Python (TC-004); and the Python log reader and analysis scripts, tested at this level on the binary's logs and on synthetic ones (TC-008 steps 8 through 14, TC-015); the validity bytes, NO_DATA and the format version as the binary, the receiver and the reader present them (TC-017 steps 11 through 15, `test_validity.py`); a replay that ends at its log and one paced to its timestamps, through the binary (TC-019, TC-020); the session status over both transports (TC-021); and the bridge from the receiver to a WebSocket, driven in-process with a scripted source and a client, and once end to end from a paced replay (TC-022 steps 1 through 7).
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

- **Scope:** Behavior that only the target can show: real sensors on the bus, a receiver with and without a fix, the Linux sleep primitive, a pulled wire.
- **Method:** Executed by hand on the Raspberry Pi with the live source, following the card's steps. The run's conditions (board, OS, kernel, compiler, date) and the printed output are recorded in FTS-VR-001.
- **Isolation:** None. This is the level at which the test doubles are removed.
- **Executed:** On hardware days, never in CI. A card step at this level is marked *Hardware*.

### 2.5 Page Tests

- **Scope:** The browser page in `dashboard/`: the feed hook (the socket, arrival time, the rate, staleness, reconnection) and the view (the mode label, the state badge, each validity rendered by name).
- **Framework:** Vitest with jsdom and React Testing Library.
- **Isolation:** The hook takes the `WebSocket` constructor and the clock as options, so a test injects a fake socket that delivers scripted messages and fake timers that advance by fiat: no bridge, no network, no real time.
- **Location:** `dashboard/src/**/*.test.ts` and `*.test.tsx`, run as `cd dashboard && npm test -- --run`.
- **Executed:** Every commit, locally and in CI (the workflow's `page` job).

---

## 3. Test Tools

| Tool | Role | Why this tool |
|---|---|---|
| Google Test | C++ unit and fault injection tests | The de facto C++ test framework. Parameterized tests (`TEST_P`) map directly onto the fault model's "for each sensor, for each fault type" structure. Integrates with CMake through CTest. |
| pytest | Integration tests | The Python receiver and analysis scripts are already Python. pytest fixtures manage the lifecycle of the C++ process cleanly, and parametrize handles frame-count and rate sweeps. |
| gcov / lcov | Structural coverage | gcov is built into GCC and produces per-line and per-branch execution counts with no source changes. lcov aggregates gcov output into HTML reports and enforces thresholds in CI. Required by REQ-TEST-003. |
| cppcheck | Static analysis | Detects undefined behavior, uninitialized members, and resource leaks without executing the code. Fast enough to run on every build. Required by REQ-TEST-004. |
| CTest | Test runner | Ships with CMake. Runs the Google Test binaries and reports results in a form GitHub Actions can consume. |
| Vitest | Page tests | The test runner of the page's own build tool (Vite), so one configuration serves the build and the tests; jsdom gives it a DOM without a browser, fake timers make the staleness tests instant. |
| websockets | The bridge's server and its test client | A pure-Python WebSocket implementation on asyncio, no web framework around it; the test client is the same library. |

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

- **Requirement:** REQ-FAULT-001 (also exercises REQ-FAULT-004, REQ-FAULT-005, and REQ-PROC-002 and REQ-PROC-007 in step 8)
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
- **Expected result:** Barometer and IMU transition to DEGRADED at a clock time greater than 480 ms and no greater than 500 ms after the first failed read. GPS transitions at a time greater than 1980 ms and no greater than 2000 ms. Healthy channels remain NOMINAL throughout. Processing continues for every frame; no frame is dropped. Step 8: a `COMM_TIMEOUT` event for BARO 500 to 520 ms after the pull, its value the elapsed time since the last good read, and a `RECOVERY` event on the fifth good read after reconnection, its value the time spent DEGRADED; the same pair for IMU; `baro_read_ok` false from the pull onward while the status stays NOMINAL for the first half second; pitch and roll held and flagged FAILURE_WARNING during the IMU outage (REQ-PROC-007), and NORMAL at the tilted value on the first NOMINAL frame after it (REQ-PROC-002); the other I2C channel NOMINAL throughout; the timing summary reports 0 missed cycles; the saved log replays byte-identical.
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
  1. Run the pipeline in simulated mode with seed 42 for 500 frames at 50 Hz. Log every processed frame to `logs/telemetry_000.bin`. Both runs write and read format version 2: identity is claimed within one format version (REQ-LOG-005).
  2. Run the pipeline in replay mode with that log as the data source. Log every processed frame to `logs/replay_000.bin`.
  3. Parse both logs in Python. For each frame index, compare every field.
  4. Assert the frame counts are equal.
  5. Assert every raw field is identical (replay must not alter its input).
  6. Assert every computed field (baro altitude, pitch, roll, fused altitude, vertical speed, channel status, and the five validity bytes) is identical, compared as raw bytes rather than with a floating-point tolerance; the reserved bytes are zero in both.
- **Expected result:** 500 frames in each log. Zero field differences; the two files byte-identical; every frame's bytes equal to its repacking (the reserved bytes zero); timestamps 0 to 9980 ms in both.
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
  5. Corrupt a header field (magic, version, frame size, event size, endianness) one at a time. Assert the reader refuses each with a non-empty error; the version case uses a version outside the readable range (`kLogFormatVersion + 1`), and a version-1 header with this build's sizes is accepted, its frames coming back with every validity NOT_JUDGED and the reserved bytes zero (REQ-LOG-005, TC-017 step 2).
  6. Truncate a file in the middle of its second frame. Assert the reader returns the first frame, then false, with a "torn record" error and no crash.
  7. Point the logger at a path that cannot be a directory. Assert `ok()` is false and that logging calls return without throwing.
  8. *(Integration)* Read the session fixture's log (500 simulated frames) with the Python reader: assert 500 frames, no events, timestamps 0 to 9980 ms, nothing torn, the header's sizes equal to the module's.
  9. *(Integration)* Write frame, event, frame from Python with the same Structs and read it back: assert the frames and the event in order and each frame's bytes equal to its repacking.
  10. *(Integration)* Corrupt each header field (magic, version, frame size, event size, endianness) in a Python-written file: assert the reader refuses each, naming the field; the version case is `LOG_FORMAT_VERSION + 1`, and a version-1 header is read (TC-017 step 15).
  11. *(Integration)* Cut a file 41 bytes into its second frame: assert the first frame is returned and 41 torn bytes counted; append an unknown tag: assert refusal naming the offset.
  12. *(Integration)* Write 10 frames across four rotated files: assert they read as one run in order; then a file that restarts at 0, a file with a gap in its index, a file starting 1920 ms after the previous ends, and a file whose timestamps repeat: assert each is refused with the file and timestamps named.
  13. *(Integration)* Assert the numpy layout derived from the struct format has 104 and 16 bytes and every field offset equal to the struct's (`reserved_a` at 52, latitude at 56, the last flag at 97, the validity bytes at 98 to 102, `reserved_b` at 103; the event's `reserved` at 10), and that a frames array's bytes equal the file's.
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
  | 4 | frame 0: fused altitude and vertical speed 0 and NO_COMPUTED_DATA; frame 1: fused altitude equal to its barometric altitude, vertical speed 0, both NORMAL (REQ-PROC-007) |
  | 5 | pitch and roll unchanged and NORMAL: a stale cycle inside the timeout is the detector's call, not a flag (REQ-PROC-007) |
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
- **Objective:** Verify that the MPU6050 driver checks the chip's identity, configures the one sample-rate divider, the low-pass filter, and both full-scale ranges with the decided values, assembles big-endian two's-complement register pairs in the right order, scales raw counts to the frame's units, converts the two extreme codes to the rails the fault detector compares against (FAULT-010), and reports a failed transaction as an empty reading rather than an exception.
- **Preconditions:** `Mpu6050` over a `FakeI2cBus` (TC-014 steps 1 through 3) whose WHO_AM_I register (0x75) holds 0x68. Expected values are arithmetic from the register map (RM-MPU-6000A-00 revision 4.2): 16384 LSB/g at ±2 g, 131 LSB per deg/s at ±250 deg/s, sample rate = 1 kHz / (1 + SMPLRT_DIV) with the low-pass filter enabled.
- **Steps:**
  1. Call `init()` with the default 100 Hz. Record the transaction log and the register file.
  2. Load WHO_AM_I with 0x70 (an MPU-6500's answer) and call `init()`. Record the result, the message, and the transaction count.
  3. After a good `init()`, load the fourteen output registers with accelerometer counts (0x4000, 0xC000, 0x2000), any temperature pair, and gyroscope counts (0x0083, 0xFF7D, 0x0000); call `read()`.
  4. Load the accelerometer x pair as (0x00, 0x40) and the gyroscope x pair as (0x7D, 0xFF); call `read()`.
  5. Call `sample_rate_divider()` with 100, 50, 4, 1000, 1, 3, 30, 0, −50, and 2000.
  6. Fail the burst at 0x3B and call `read()`; lift the failure and call `read()` again.
  7. Load the accelerometer x pair with 0x7FFF and the y pair with 0x8000, the two extreme codes; call `read()` and compare each axis with the driver's rail constants, `kAccelPositiveRailMps2` and `kAccelNegativeRailMps2`.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | one read of 0x75, then writes in order 0x6B = 0x01, 0x19 = 9, 0x1A = 0x04, 0x1B = 0x00, 0x1C = 0x00; `init()` true |
  | 2 | `init()` false; the message names 0x70 and 0x68; one transaction only, nothing written to a chip of unknown identity |
  | 3 | accelerometer (9.81, −9.81, 4.905) m/s² within 1e-4; gyroscope (1.0, −1.0, 0.0) deg/s within 1e-4 |
  | 4 | accelerometer x 64/16384 × 9.81 = 0.0383 m/s²; gyroscope x 32255/131 = 246.2 deg/s. Swapped bytes are a different number, never the right one with the wrong sign |
  | 5 | 9, 19, 249, 0; the other six refused |
  | 6 | empty, then a reading |
  | 7 | x equals the positive rail and y the negative rail exactly, not within a tolerance: 32767/16384 × 9.81 = 19.6194 m/s², one count (9.81/16384, 0.0006 m/s²) short of 2 × 9.81, and −2 × 9.81 exactly. The constants are the same three numbers in the same order as `read()`'s conversion, which is what ties FAULT-010's threshold to what the sensor produces |

- **Pass/fail:** Any write out of order or with another value, any accepted identity other than 0x68, any scale or sign error, any rate accepted that the divider cannot produce exactly, a rail differing from `read()`'s conversion of its code by a bit, or any exception fails. The test file also covers: a failed identity read and a failed configuration write refuse `init()` and name the cause; an unreachable rate is refused before the bus is touched; `read()` before `init()` is empty and touches the bus not at all.

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
  | 12 | pressure 950 to 1050 hPa, temperature 10 to 35 °C, accelerometer about (0, 0, 9.8), gyroscope within a few deg/s at rest; barometer and IMU NOMINAL with `read_ok` 1; with `sensors.gps.enabled` false, the shipped value: GPS NO_DATA until the UART timeout and DEGRADED from 2020 ms with one COMM_TIMEOUT event (REQ-FAULT-006), and with a receiver enabled TC-018 steps 21 and 22 apply; 0 missed cycles |
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
  4. A missing section, a section of the wrong type, a missing key, an unknown key at the top level and nested, a duplicate key: each named once with its dotted path. Then each of the 38 keys removed in turn: `<path>: missing`, except the two whose removal empties their section (`timing.period_ms`, `sensors.barometer.address`), reported as the section "must be a mapping (got nothing)".
  5. The scalar rules: `0x76` reads 118; `010`, `1.5`, `abc`, an empty scalar and `0x` are refused as integers; `.inf`, `.nan` and `abc` are refused as numbers; `yes`, `on` and `1` are refused as booleans; an empty string is refused where a string is required. Then each of the 38 keys given a sequence, and the text its rule refuses (`abc` for a whole number, `.nan` for a number, `yes` for a switch, an empty string for text): each named under its path by its own rule.
  6. Every range at both edges: period 20 to 1000; addresses 0x08 to 0x77; IMU rate 4 to 100 and an exact divisor of 1000; timeouts at least 1; stuck count at least 2; recovery count at least 1; pressure and temperature minimum below maximum; the four variances and the sea-level pressure positive; alpha 0 to 1 inclusive; maximum file bytes at least 121; UDP host a dotted quad, port 1 to 65535, ZeroMQ endpoint non-empty; seed within 32 bits; GPS interval at least 1; the receiver's baud rate 4800 to 115200 and one of the six termios rates (TC-018 step 19); sigmas positive (`std::normal_distribution` requires it). Then every whole-number key one past each of its edges, every positive number at 0, alpha at -0.1 and 1.1: one table over every key, because gcov counts a template's branches per instantiation and one key per rule leaves the other field types' decisions untaken.
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

### TC-017: Validity of Computed Fields, NO_DATA, and Format Version 2

- **Requirement:** REQ-PROC-007, REQ-LOG-005, REQ-FAULT-006 (also exercises REQ-PROC-005, REQ-LOG-003, REQ-LOG-004, REQ-FAULT-004)
- **Level:** Unit (steps 1 through 10: `tests/unit/test_telemetry_frame.cpp`, `test_validity.cpp`, and the additions to `test_logging.cpp`, `test_replay.cpp`, `test_fault_detection.cpp`, `test_complementary.cpp`, `test_kalman.cpp`), Integration (steps 11 through 15, `tests/integration/test_validity.py`), recorded (step 16, FTS-VR-001 Entry 12 §12.4)
- **Objective:** Verify that every computed field carries, in the same frame, whether it is a current estimate, has never existed, or is not trustworthy, in the order of precedence REQ-PROC-007 states; that a channel is NO_DATA until its first reading; that format version 2 has no unspecified byte, that readers accept versions 1 and 2 and refuse others by name before anything is built, and that identity holds within a version.
- **Preconditions:** `TelemetryProcessor` with its default tuning and `FaultDetector` with its defaults; frames built by hand or from `SimulatedSource(20, 42)`; a temporary directory per test for the logs. The reference for step 16 is the 500-frame seed-42 log of the build before this topic (FTS-VR-001 Entry 11 §11.4, SHA-256 `3124895f…`), compared with the candidate's by `cmp -l` and by a script that reads both record by record.
- **Steps:**
  1. `sizeof(TelemetryFrame)` and `alignof`; `offsetof` of `reserved_a`, `latitude`, `gps_read_ok`, the five validity bytes and `reserved_b`; `sizeof(FaultEvent)` and `offsetof(FaultEvent, reserved)`; `validity_name` for the four values and an out-of-range byte; `status_name` for NO_DATA and an out-of-range byte.
  2. Log one frame and read the header; write a version-1 header with this build's sizes over a frame whose validity and reserved bytes are not zero, and read it; write a version-3 header and a wrong frame size and read each; build a frame message.
  3. Through a processor: a frame with pressure 0 and `baro_read_ok` false, then one whose barometer is DEGRADED before it ever read, then the first fresh frame. Record barometric altitude, fused altitude, vertical speed and their validities.
  4. A frame with `imu_read_ok` false, then one with an unusable accelerometer, then one with the IMU DEGRADED, then the first usable sample. Record pitch, roll and their validities.
  5. After a seeded frame, a frame with the IMU DEGRADED and 45° of accelerometer tilt, then a NOMINAL frame with `imu_read_ok` false. Record the angles and their validities.
  6. Two accepted frames, stale frames through 520 ms, then a fresh frame at 540 ms with a flat accelerometer; the same with an unusable accelerometer on the recovery frame. Record pitch and its validity.
  7. Twenty fresh frames, then frames with the barometer DEGRADED and the GPS fresh; then with both DEGRADED; then a fresh run whose GPS never fixed, with the barometer DEGRADED. Record the five fields and their validities; the raw pressure on the DEGRADED frames is out of range.
  8. 200 simulated frames through detector and processor, plus every frame of steps 3 to 7: read every validity byte.
  9. Replay: log a frame with every validity NORMAL and the reserved bytes set, read it through `LogReplaySource`; run `ReplayTest.ReplayReproducesLiveSession`; write a version-1 file by hand and replay it through detector and processor.
  10. Detector: frames whose channel never reads; a channel's first reading; a fault on a channel's first-ever reading; a channel dead from power-on.
  11. *(pytest)* The session fixture's log and replay: the header version, every validity byte and status of every frame, the `Validity:` summary line of both runs, the replay's exit lines.
  12. *(pytest)* A synthetic version-1 log of 50 raw frames replayed with `--replay`: exit code, the notice on stdout, the replay log's header, its raw fields against the input's, every validity byte.
  13. *(pytest)* A synthetic version-3 log replayed: exit code, stderr, the log directory; a 5-frame log replayed with `--cycles 12`: the end-of-log line (REQ-LOG-006, TC-019).
  14. *(pytest, each transport)* A published run: each receiver line's validity names, and the frames against the log.
  15. *(pytest)* The Python reader: the dtype's offsets at 52, 98 to 103 and the event's 10; `VALIDITY_NAMES` and `STATUS_NAMES`; `invalid_as_nan` on frames with one FAILURE_WARNING field; a version-1 log read with the bytes the file holds; `summary()` on a mixed log.
  16. *(Recorded)* The reference log against the candidate's after commit 1 and after commit 2, by `cmp -l` and by the comparison script, both SHA-256s recorded.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | 104 and 8; 52, 56, 97, 98 to 102, 103; 16 and 10; the four names and `UNKNOWN`; `NO_DATA` and `UNKNOWN` |
  | 2 | version 2 in the header; the version-1 file accepted, its frame back with every validity NOT_JUDGED and both reserved fields zero; "format version 3; this build reads 1 to 2"; "frame size 112 in the header, 104 in this build"; the message's header carries version 2 |
  | 3 | before the first reading: 0, 0, 0 and NO_COMPUTED_DATA on all three (no 44330 m); DEGRADED before any reading: unchanged, NO_COMPUTED_DATA; the first fresh frame: NORMAL on all three and fused altitude equal to barometric altitude |
  | 4 | 0 and NO_COMPUTED_DATA through the stale, unusable and DEGRADED frames; NORMAL with the accelerometer's tilt on the first usable sample |
  | 5 | DEGRADED: both angles held bit for bit, FAILURE_WARNING; stale NOMINAL: held, NORMAL |
  | 6 | 540 ms with a flat accelerometer: 0°, NORMAL (re-seeded); with an unusable accelerometer: the previous angles held, FAILURE_WARNING |
  | 7 | barometer DEGRADED with a contributing GPS: fused altitude and vertical speed NORMAL, barometric altitude FAILURE_WARNING holding the last accepted conversion while the raw pressure carries the out-of-range value; both DEGRADED, and the barometer DEGRADED with a GPS that never fixed: fused altitude and vertical speed FAILURE_WARNING with the coasting prediction as their value |
  | 8 | no NOT_JUDGED byte on any frame; every computed field written |
  | 9 | validity NOT_JUDGED and reserved zero after the strip; every frame and event bit-identical between live and replay, FAILURE_WARNING on the DEGRADED frames inside the injected windows and NORMAL outside; the version-1 file's frames come back judged with no NOT_JUDGED byte |
  | 10 | NO_DATA on every frame before the first reading; NOMINAL on the first reading with no event; DEGRADED with its event on a first-reading fault; NO_DATA on every frame before the timeout and DEGRADED after it |
  | 11 | version 2; every validity NORMAL and every status NOMINAL on all 500 frames of both logs; `Validity:  every computed field NORMAL on 500 of 500 frames (first on frame 0)` in both; no exhaustion line |
  | 12 | exit 0; "format version 1" on stdout; the replay log version 2; raw fields equal; no NOT_JUDGED byte |
  | 13 | exit 1, stderr names "format version 3" and "reads 1 to 2", no `logs/` written; "Replay:    5 frames, end of log reached" and a 5-frame replay log (REQ-LOG-006; until it, "replay exhausted after 5 frames; 7 cycles repeated the last frame" and a 12-frame log) |
  | 14 | the names beside the values on every line; the frames byte-identical to the log |
  | 15 | the offsets; the two dictionaries; NaN in the flagged field only; the file's bytes; the counts |
  | 16 | after commit 1 one differing byte, offset 5; after commit 2 offset 5 and, in every frame, exactly offsets 98 to 102 with the pattern `03 03 03 03 03`; reserved bytes zero; every event identical |

- **Pass/fail:** Any NOT_JUDGED byte on an emitted frame, any flag raised over a value nobody measured, any held angle under a DEGRADED IMU reported NORMAL, any coasting altitude reported NORMAL, any accepted version outside 1 to 2, any refused version inside it, any replay that runs on an unreadable log, or any byte of the reference log differing beyond those listed fails.

### TC-018: NMEA Parsing, the Serial Port Seam, and the GPS Through the Live Source

- **Requirement:** REQ-SENS-005 (also exercises REQ-FAULT-001's `read_ok` contract on the GPS channel, REQ-PROC-006, REQ-CFG-001, REQ-LOG-004)
- **Level:** Unit (steps 1 through 19: `tests/unit/test_nmea.cpp`, `test_serial_port.cpp`, `test_nmea_gps.cpp`, and the additions to `test_live_sensor_source.cpp` and `test_config.cpp`), Hardware (steps 20 through 24, the Raspberry Pi with the GT-U7)
- **Objective:** Verify that NMEA 0183 sentences are accepted only with a valid checksum and parsed field by field into position, altitude, fix quality, satellites, speed and course; that the serial port delivers the device's bytes unaltered without blocking; that the driver frames sentences out of a byte stream cut anywhere, pairs a GGA with the RMC of the same epoch, delivers one fix per valid epoch and withholds the rest; that the live source marks `gps_read_ok` true only on the cycle a fix completed and holds the fields between fixes; and that the receiver's three keys are validated like every other.
- **Preconditions:** The primary oracle for parsing is the set of example sentences printed in the u-blox 6 Receiver Description (GPS.G6-SW-10018) with their printed checksums, recomputed independently before the tests were written: `$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*5B`, `$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A*57`, `$GPGLL,4717.11364,N,00833.91565,E,092321.00,A,A*60`, `$GPGSA,A,3,23,29,07,08,09,18,26,28,,,,,1.94,1.18,1.54*0D`, `$GPGSV,3,1,10,23,38,230,44,29,71,156,47,07,29,116,41,08,09,081,36*7F`, `$GPVTG,77.52,T,,M,0.004,N,0.008,K,A*06`, `$GPTXT,01,01,02,u-blox ag - www.u-blox.com*50`, and the receiver's no-fix forms `$GPGGA,,,,,,0,00,99.99,,,,,,*48` and `$GPRMC,,V,,,,,,,,,,N*53`; variants (another talker, an altered digit, a shifted time) carry checksums recomputed by the same XOR. The second fixture, pending the Pi session, is the GT-U7's own 30-second output captured there at 9600 baud (`tests/unit/data/gt_u7_capture.nmea`), every position shifted by one constant pair so that the first fix lands on the manual's example and the checksums recomputed; the sentence set, timing, talker IDs and the no-fix to fix transition are the board's. `FakeSerialPort` queues byte chunks and serves one per read; `PosixSerialPort` is exercised on a pseudo-terminal (`posix_openpt`), which exists on the Mac and on the CI runner. The Hardware steps need the Pi with the receiver on its micro-USB port or the header UART, a configuration copy with `sensors.gps.enabled: true` and the device path, and a window.
- **Steps:**
  1. Verify the checksum of the nine oracle sentences; of one with a hex digit altered; of one without `*`; of one with lower-case hex; of ones whose digits fall outside the hex ranges.
  2. Parse the oracle GGA and the no-fix GGA. Then the oracle GGA with its altitude field emptied, at quality 0 and at quality 1.
  3. Parse the oracle RMC, the no-fix RMC, and an RMC with status `A` and empty speed and course fields. Then the oracle RMC with its status field emptied, and `$GPRMC,,,,,,,,,,,,N*05`.
  4. Convert `4717.11399,N`, `00833.91590,E`, a southern latitude, a western longitude, `4760.00000,N` and `9117.11399,N`.
  5. Parse a GGA with `12x` in a numeric field, one with too few fields, a sentence whose address has four letters, a `$GNGGA`, and a `$GPVTG`. Then a GGA and an RMC whose longitude reads `18100.00000,E` beside the oracle latitude, and an RMC whose course field reads `east`.
  6. Open a pseudo-terminal pair, construct `PosixSerialPort` on the slave at 9600 baud, write a sentence to the master, read through the port; read again with nothing written. Write `\r\n`, 0x11, 0x13, 0x03, 0x00 and a letter and read. Construct a port at each of the six rates.
  7. Write a sentence to the master before constructing the port; construct; read.
  8. Construct on a nonexistent path; on `/dev/null`; at 1234 baud; read on a port that is not ok. Construct on a pseudo-terminal's slave, read once, close the master, read twice.
  9. The copy and move traits of `PosixSerialPort`.
  10. Queue one epoch (the oracle GGA and an RMC with the same time field) on the fake and poll.
  11. Queue the same bytes cut after every byte, then in 7-byte chunks, then with a cut at the `*`; poll until the fix.
  12. Queue junk bytes, half a sentence, a sentence with a wrong checksum, then two complete epochs with a pause between; poll twice. Then a GGA and an RMC that pass the checksum and fail the parser; poll.
  13. Queue the no-fix pair; poll. Then the oracle GGA (quality 1) with an RMC of its epoch whose status is `V`; poll; then a valid epoch; poll.
  14. Queue 200 bytes with no newline, then an epoch; poll.
  15. Queue three GGA sentences with three different times and no RMC; poll. Then an RMC followed by the next epoch's GGA and RMC; then an RMC twice and its GGA; then a GGA followed by the next epoch's RMC and GGA.
  16. Queue two complete epochs in one chunk; poll once. Then queue an epoch, an error and another epoch; poll twice; then make every read fail and poll twice.
  17. Queue 5 KB of sentences; poll once, then again. Stream the capture in 19-byte chunks, in 7-byte chunks and as one chunk, polling after each chunk; compare the three runs.
  18. Construct the live source over the fake bus and a fake port; read 50 frames with nothing queued (the first second), queue one epoch, read 50 more.
  19. Construct the live source without a port and read a frame; then with a port that returns an error and read a frame. Queue an epoch before the first frame and on each of the next 49, read 50 frames, then queue one more at 1000 ms and read. Load the shipped configuration with each of the three `sensors.gps` keys removed, set to each kind of invalid value, and `baud_rate` set to 4799, 9601 and 115201.
  20. *(Hardware)* `stty -F <device> 9600 raw -echo` and `timeout 10 cat <device>` on the receiver's device.
  21. *(Hardware)* `telemetry --live --config ~/gps.yaml --cycles 250` indoors, no fix; then the same with `device` naming a path that does not exist.
  22. *(Hardware)* `telemetry --live --config ~/gps.yaml --cycles 500` by a window with a fix; replay the log and `cmp`; `python3 -m pipeline.log_reader logs/`.
  23. *(Hardware)* During a 1500-cycle run, unplug a USB receiver for ten seconds and replug it; on the header UART, pull the board's TXD lead for ten seconds and reconnect it instead.
  24. *(Hardware)* With the receiver fixing, pull the barometer's SDA jumper for ten seconds and push it back.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | accepted; rejected; rejected; accepted |
  | 2 | 47.2852332 N, 8.5652650 E (within 1e-7°), 499.6 m, quality 1, 8 satellites, time `092725.00`; quality 0, no position, an empty time; at quality 0 parsed with no position and altitude 0; at quality 1 malformed, a fix without a position |
  | 3 | 0.002058 m/s (within 1e-6), course 77.52, valid, time `083559.00`; invalid, no position; valid with speed 0 and course 0; parsed, not valid, with the oracle's position, speed and course; parsed, not valid, no position (an empty status is a null field, no verdict, never malformed: FTS-VR-001 Entry 15) |
  | 4 | 47.2852332; 8.5652650; negative; negative; malformed; malformed |
  | 5 | malformed; malformed; not a sentence the driver uses; parsed as a GGA; reported unused; malformed, both; malformed |
  | 6 | the sentence's bytes, every one, `\r\n` included; then 0 bytes (an empty read is 0 with `VMIN` and `VTIME` zero, not an error); the seven control bytes unaltered; every rate ok |
  | 7 | 0 bytes: the stale sentence was flushed |
  | 8 | not ok, the path in the message, errno ENOENT; not ok, the path in the message, a nonzero errno that is not ENOENT (ENOTTY on Linux, ENODEV on macOS: the terminal check, not the open, refused it); not ok with errno EINVAL, the message naming 1234 and the six rates; no value, errno EBADF; 0 bytes before the master closes and no value with a nonzero errno twice after it (the hang-up seen through `poll`) |
  | 9 | not copyable, not movable, not assignable |
  | 10 | one fix: 47.2852332 N, 8.5652650 E, 499.6 m, 0.002058 m/s, 77.52°, quality 1, 8 satellites; `sentences 2`, `fixes 1`, every error counter 0 |
  | 11 | the same fix and the same counters all three ways |
  | 12 | two fixes; `bad_checksum 1`, `resyncs` at least 2, `malformed 0`; then no fix, `malformed 2`, `no_fix_epochs 0` |
  | 13 | no fix; `no_fix_epochs 1`, `sentences 2`; then no fix, `no_fix_epochs 1`, `malformed 0`, `incomplete_epochs 0`; then the oracle fix, `fixes 1`, `incomplete_epochs 0` |
  | 14 | `overlong 1`; the fix delivered |
  | 15 | no fix; `incomplete_epochs 2`, `sentences 3`; then the newer epoch's fix with `incomplete_epochs 1` each time |
  | 16 | the second epoch's fix; `fixes 1`, `fixes_skipped 1`; then the first epoch's fix with `read_errors 1` (the error ended that poll), the second epoch's fix on the next poll, then no fix twice and `read_errors 3` |
  | 17 | sixteen reads of 256 bytes on the first poll and the remainder on the second; the three capture runs (pending the file) agree on every counter and every fix, `bad_checksum 0`, and the first fix is the manual's position |
  | 18 | `gps_read_ok` true on exactly one frame, the first after the start-up second whose poll completed the epoch; the five fields zero before it and held, unchanged, on every frame after it; every status NOMINAL, every validity NOT_JUDGED, `gps_counters()` present, `last_gps_fix()` empty before the fix and carrying quality 1, 8 satellites and the epoch's time after it |
  | 19 | no port: `gps_read_ok` false, fields zero, `gps_counters()` empty, the eight existing live-source cases unchanged; the erroring port: `gps_read_ok` false, `read_errors 1`; the backlog: fifty fixes discarded and counted, none fresh, no last fix, and the fix at 1000 ms fresh with the count unchanged; each removed key named `sensors.gps.<key>: missing`, each invalid value refused under its key, 4799 and 115201 "must be 4800 to 115200", 9601 "must be one of 4800, 9600, 19200, 38400, 57600, 115200 (got 9601)", each of the six accepted |
  | 20 | lines beginning `$GP` or `$GN` about once a second; GGA with `,0,00,` and RMC with `,V,` before a fix |
  | 21 | `gps=NO_DATA` on the first frames, one `COMM_TIMEOUT` event at 2020 ms, `gps=DEGRADED` after, `GPS: 0, 0` throughout, `missed cycles = 0`, the `GPS:` exit line with about 30 sentences, 0 fixes, about 5 epochs without a fix, 0 bad checksums, `resyncs` 1 or 2 (the tail of the sentence the flush at open cut) and the discarded-in-the-first-second count naming the backlog; the second run exits 1 naming the path and `sensors.gps.device` |
  | 22 | `gps=1` on about one frame in fifty, a real position and an altitude within tens of metres of the ground's, `gps=NOMINAL` from the first fix if it came inside 2 s and otherwise one `COMM_TIMEOUT` then one `RECOVERY` after the fifth fix, every computed field NORMAL from frame 0, `missed cycles = 0`, the `GPS:` line with about 10 fixes, 0 bad checksums and the last fix's quality, satellites and UTC time; `cmp` silent; `fresh: ... gps 10`. Then, with the port left closed for two minutes before the run: the discarded count is the backlog's depth and the first delivered fix's UTC time is live, not two minutes old |
  | 23 | over USB: `COMM_TIMEOUT` about 2 s after the unplug, then DEGRADED for the rest of the run with read errors climbing by fifty a second and `bytes` frozen, because the kernel hangs up the open descriptor, which `poll` reports (reopening a device is the faults topic's, FTS-DD-001 Section 3); on the header UART, where the device node outlives the receiver: `COMM_TIMEOUT` about 2 s after the lead is pulled and `RECOVERY` after the fifth fix once reconnected |
  | 24 | BARO `COMM_TIMEOUT`, the fused altitude walking to the GPS altitude over the next fixes with `fused_altitude_validity` NORMAL throughout; after the push-back, BARO `RECOVERY` and the fused altitude returning toward the barometer's |

- **Pass/fail:** Steps 1 through 19 exact as listed. Any sentence accepted with a wrong or missing checksum, any fix delivered from a no-fix epoch, any `gps_read_ok` true on a frame whose poll completed no epoch, any difference between the three capture runs, or any `-Werror` or cppcheck finding on the new files fails. Steps 20 to 24 fail on a bad-checksum count above zero at 9600 baud, on any event other than those listed, or on a byte difference in the replay.

### TC-019: Replay Ends at the End of Its Log

- **Requirement:** REQ-LOG-006 (also exercises REQ-LOG-004 and REQ-PROC-006)
- **Level:** Unit (steps 1 through 5: `tests/unit/test_replay.cpp`), Integration (steps 6 and 7: `tests/integration/test_replay_pacing.py`)
- **Objective:** Verify that a source can say whether a next frame exists, that the replay source answers from a one-frame look-ahead without consuming the frame, that the loop stops at the end of the log whatever `--cycles` says, that `--cycles` still caps a replay, and that the run reports both outcomes by name.
- **Preconditions:** Logs written by `BinaryLogger` in a temporary directory (unit), or by `synthetic_log` with the shipped configuration copied beside them (integration), as TC-017 steps 12 and 13 do.
- **Steps:**
  1. Log three frames; call `has_next()` before each `read_frame()` and once after the third; call `has_next()` twice in a row, then read; read once more past the end.
  2. Construct the source over a list whose first path does not exist and whose second holds one frame; call `has_next()`, then read.
  3. Construct the source over an empty list; call `has_next()`.
  4. Wrap a replay source in `FaultInjectingSource` and call `has_next()` through it before and after its frames; wrap a `SimulatedSource` and call it a hundred times.
  5. Drive the thesis replay of TC-004 (`ReplayReproducesLiveSession`) with `while (source.has_next())` instead of a fixed count.
  6. *(pytest)* Replay a 5-frame log with `--cycles 12`; with `--cycles 3`; with no `--cycles`.
  7. *(pytest)* Compare the replay log of step 6's first run with that of a `--cycles 5` run, and list the timestamps of each.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | true, true, true, then false; the double call returns the same frame once and `frames_read()` counts it once; the read past the end returns the last frame and `exhausted()` reports true |
  | 2 | true; the frame from the second file; the first path skipped |
  | 3 | false, `exhausted()` true |
  | 4 | true before and false after, as the inner source answers; the simulator true a hundred times |
  | 5 | exactly 500 frames, every one bit-identical to the live run's, `exhausted()` true after the loop |
  | 6 | 5 frames and `Replay:    5 frames, end of log reached`; 3 frames and `Replay:    3 frames, stopped by --cycles before the end of the log`; 5 frames and the end-of-log line |
  | 7 | byte-identical; no timestamp repeats |

- **Pass/fail:** Exact as listed. Any frame processed, logged or published after the last logged frame, or any replay log longer than its input, fails.

### TC-020: Paced Replay Against the Recorded Timestamps

- **Requirement:** REQ-TIME-004 (also exercises REQ-TIME-002, REQ-LOG-003 and REQ-TRANS-004)
- **Level:** Unit (steps 1 through 5: `tests/unit/test_timestamp_pacer.cpp` on `FakeClock`), Integration (steps 6 through 9: `tests/integration/test_replay_pacing.py`)
- **Objective:** Verify that the pacer maps each recorded timestamp to a wall-clock deadline measured from the first frame, sleeps until it through the `Clock` seam, never sleeps on a deadline that has passed, reports lateness without accumulating it, honours gaps as recorded, and that a paced replay's output is byte-identical to the unpaced one while its frames leave at the recorded cadence.
- **Preconditions:** The fake clock of TC-009 for the unit steps; the session fixture's 500-frame log for the integration steps; for step 9 a receiver listening before the run (TC-005 step 1).
- **Steps:**
  1. Call `wait_for` with 0, 20, 40 and 100 ms on a punctual fake clock.
  2. Make the fake clock wake 3 ms late on every sleep; call `wait_for` with 0, 20, 40 and 60 ms.
  3. Call `wait_for` with 0, 100, then 50 ms.
  4. Call `wait_for` with 0, then 1000 ms.
  5. Call `wait_for` first with 3000 ms on a clock reading T, then with 3020 ms.
  6. *(pytest)* Replay the session log with `--pace --cycles 150` and, separately, with `--cycles 150`; time both runs.
  7. *(pytest)* Read `logs/replay_jitter.csv` of the paced run; list the log directory.
  8. *(pytest)* Run `telemetry --pace` without `--replay`.
  9. *(pytest)* With a receiver listening, replay the session log with `--pace --cycles 100 --zmq <endpoint>`; record each frame's arrival time.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | sleeps requested at origin + 0, 20, 40 and 100 ms, in that order; `scheduled_ns` equal to them; `jitter_ns` 0 on every call; `cycle` 0, 1, 2, 3; `missed_cycles` 0 |
  | 2 | `jitter_ns` 3 ms on every call and every `scheduled_ns` still origin-relative (20, 40, 60 ms after the first): lateness does not accumulate |
  | 3 | the third call sleeps on nothing and returns at once with a positive `jitter_ns` (actual minus the past deadline); the frame is not skipped |
  | 4 | one sleep, to origin + 1 s |
  | 5 | the origin is (T, 3000 ms): the first deadline is T, the second T + 20 ms |
  | 6 | the two replay logs byte-identical, and equal to the original's first 150 frames; the paced run about 3 s (150 frames at 20 ms), the unpaced under 1 s |
  | 7 | 150 rows, `jitter_ns` never negative, its mean under one period (20 ms) and at most one frame in twenty a period or more late, the `Timing:` line's count agreeing; min, mean and max printed and recorded in FTS-VR-001; `logs/telemetry_jitter.csv` absent |
  | 8 | exit 1, stderr naming `--pace` and `--replay`; no `logs/` written |
  | 9 | 100 frames, 0 gaps, each byte-identical to the log, arrivals spanning about 2 s from first to last; then the ENDED status of TC-021 |

- **Pass/fail:** Steps 1 through 5 exact. Step 6 fails on any byte difference. Step 7 fails on a negative lateness, a mean lateness of a period or more, or more than one frame in twenty a period or more late: never early is the pacer's property, how late is the machine's (the Mac's `nanosleep` fallback under load has woken a frame 62 ms late, FTS-VR-001 Entry 16), as the 1 ms bound of REQ-TIME-003 is the Pi's and is not claimed here. Steps 8 and 9 as listed.

### TC-021: Session Status on the Wire

- **Requirement:** REQ-TRANS-004 (also exercises REQ-TRANS-001, REQ-TRANS-002 and REQ-TRANS-003)
- **Level:** Unit (steps 1 through 4: `tests/unit/test_session_message.cpp`, `test_zmq_publisher.cpp`, `test_udp_publisher.cpp`), Integration (steps 5 through 8: `tests/integration/test_session_status.py`), Hardware (step 9)
- **Objective:** Verify that the status message is eight bytes under its own magic that no frame reader accepts, that both transports send it through the same seam and count it apart from frames, that a run announces its mode before the first frame, repeats it about once a second and reports the end after the last frame, that the receiver reads it and ends on it, and that the status messages change no byte of the log.
- **Preconditions:** The in-process subscriber of TC-005 over `inproc://`; a datagram socket bound on the loopback; the receiver harness of TC-005 for the integration steps.
- **Steps:**
  1. Build `session_message(REPLAY, RUNNING)`; copy its eight bytes over a zeroed `LogFileHeader` and run `header_problem`; name every mode and state.
  2. Publish a status over `inproc://` to a settled subscriber; read the publisher's counters.
  3. Publish a status to a bound datagram socket; read it.
  4. Publish a status through a publisher that is not ok.
  5. *(pytest, each transport)* Run the simulator for 120 cycles with the receiver listening; record every status with its position among the frames and the receiver's time of exit.
  6. *(pytest)* Parse the eight bytes with `frame_format.parse_message`; parse seven bytes; parse eight bytes whose mode byte is 9.
  7. *(pytest)* Replay a log with the receiver listening; read the statuses.
  8. *(pytest)* Run the simulator for 120 cycles with `--zmq` and without; compare the two logs.
  9. *(Hardware)* `telemetry --live --zmq <endpoint> --cycles 250` on the Raspberry Pi with the receiver, then the bridge and the page, on the Mac.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | `46 54 53 53 01 03 01 00`; "bad magic: not a telemetry log"; LIVE, SIMULATED, REPLAY; RUNNING, ENDED |
  | 2 | the subscriber receives exactly those eight bytes; `status_published` 1, `published` 0, `send_failures` 0 |
  | 3 | one datagram of eight bytes |
  | 4 | false, `send_failures` 1 |
  | 5 | a RUNNING SIMULATED status before frame 0 (UDP: before any frame; ZeroMQ: before the first frame received, within the join), another before frames 50 and 100, ENDED after frame 119 (three copies over UDP, at least one over ZeroMQ); the receiver exits on ENDED well inside its 1 s quiet timeout, printing `session: mode=SIMULATED state=ENDED`; `status messages = 6` on the transport line |
  | 6 | `SessionStatus(mode="SIMULATED", state="RUNNING")`; `FormatError` naming the length; `FormatError` naming mode 9 |
  | 7 | mode REPLAY on every status, ENDED last |
  | 8 | byte-identical |
  | 9 | mode LIVE before the first frame and ENDED at exit, shown by the receiver and by the page |

- **Pass/fail:** Steps 1 through 8 exact as listed; a status read as a frame, a frame changed by a status, or a receiver that waits out its timeout after ENDED fails. Step 9 is recorded in FTS-VR-001.

### TC-022: The Bridge and the Page

- **Requirement:** REQ-DISP-003, REQ-DISP-004, REQ-DISP-005 (also exercises REQ-PROC-007 and REQ-TRANS-004)
- **Level:** Integration (steps 1 through 7: `tests/integration/test_bridge.py`, in-process but for step 7, which runs the binary), Page (steps 8 through 14: `dashboard/src/useTelemetryFeed.test.ts` and `dashboard/src/App.test.tsx`, Vitest with jsdom, a fake socket and fake timers)
- **Objective:** Verify that the bridge listens on the loopback unless told otherwise, greets each client with the threshold and the last session status, forwards every frame and status as JSON by name without absolute coordinates, serves several clients, and carries a paced replay end to end; and that the page shows no mode until a status arrives and never shows SIM or REPLAY as LIVE, renders each validity value by name, marks the feed stale from its own arrival clock and recovers, shows an ended stream as ended, and reports a lost bridge.
- **Preconditions:** A `ByteSource` fed from a list, as TC-005 step 6's; a `websockets` client; for step 7 the session fixture's log and a free ZeroMQ port; the page tests inject a fake `WebSocket` and a fake clock through the hook's options.
- **Steps:**
  1. Start the bridge with no `--listen`; read the bound address. Start it with `--listen 0.0.0.0:0`; read the bound address.
  2. Connect a client; read its first message.
  3. Feed one frame with every field set to a distinct value; read the client's next message.
  4. Feed a RUNNING REPLAY status; read the message; connect a second client and read its first message.
  5. Feed an ENDED status; read both clients.
  6. Feed ten frames with two clients connected; disconnect one after five; feed ten more.
  7. Run `telemetry --replay <session log> --pace --cycles 100 --zmq <endpoint>` with the bridge reading that endpoint and a client connected; record each message's arrival time.
  8. Render the page with no message received.
  9. Deliver a session status with mode SIMULATED; then one with mode REPLAY; then one with mode LIVE.
  10. Deliver frames whose five validity bytes are, in turn, NORMAL, FAILURE_WARNING, NO_COMPUTED_DATA and NOT_JUDGED, and read each field's cell; deliver 50 frames over one simulated second and read the rate and the last frame's timestamp.
  11. Advance the fake clock past `stale_ms` with no frame; read the badge, the age and every cell; deliver a frame.
  12. Deliver an ENDED status; advance the clock past `stale_ms`.
  13. Close the fake socket; advance the clock past the backoff.
  14. Search the rendered DOM for "latitude", "longitude" and any decimal degree.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | `127.0.0.1`; `0.0.0.0` |
  | 2 | `{"type":"hello", ...}` with `stale_ms` 1000, `period_ms` 20, `session` null, the three counts 0 |
  | 3 | `type` frame; `frame` holds every name of `FRAME_FIELDS` but `latitude` and `longitude`, each with its value; `validity` and `status` by name from `frame_format`'s tables; `received_at_ms` present |
  | 4 | `{"type":"session","mode":"REPLAY","state":"RUNNING",...}`; the second client's `hello` carries that session |
  | 5 | both clients receive the ENDED session message |
  | 6 | the remaining client receives all twenty; the bridge keeps running |
  | 7 | 100 frames in order, arrivals spanning about 2 s, then a session message with state ENDED |
  | 8 | the label "MODE UNKNOWN", the badge WAITING, no frame data |
  | 9 | SIM; REPLAY; LIVE; the text "LIVE" absent from the page for the first two |
  | 10 | NORMAL: the value and the name; FAILURE_WARNING: the value, the name, flagged; NO_COMPUTED_DATA: dashes and the name; NOT_JUDGED: dashes, the name and a pipeline-fault annunciator; each distinguishable by text and by class; the rate about 50 per second and the last timestamp the newest frame's |
  | 11 | STALE with the age counting, every value cell carrying the stale class with its last number still shown, the rate 0; the next frame clears all of it |
  | 12 | ENDED with the last frame's time; the badge stays ENDED and never reads STALE |
  | 13 | BRIDGE DISCONNECTED; a new socket constructed after the backoff |
  | 14 | nothing found |

- **Pass/fail:** Exact as listed. A coordinate on the wire or in the DOM, a mode shown before a status, SIM or REPLAY rendered as LIVE, a value left unmarked past the threshold, or an ended stream shown as stale fails.

### TC-023: The Instruments Under Each Validity, Status and Feed State

- **Requirement:** REQ-DISP-001, REQ-DISP-002, REQ-DISP-004 (also exercises REQ-DISP-003 and REQ-DISP-006)
- **Level:** Page (`dashboard/src/pfd/instrumentState.test.ts`, the six instrument test files under `dashboard/src/pfd/`, `dashboard/src/pfd/Pfd.test.tsx`; Vitest with jsdom)
- **Objective:** Verify that one rule decides what every instrument shows (the value, dashes, or a flag naming the instrument); that the rule follows each computed field's validity and each raw instrument's channel status with the precedence FTS-DS-001 §2.1 states; that the feed's state overrides every field; that no instrument ever shows a removed value struck through or dimmed; that the strip names the mode and the cause; and that the instruments are labelled TRK and GS, never heading or airspeed.
- **Preconditions:** Frames built by `testMessages.frame()` with any value and validity; the feed's state built directly for `instrumentState`, and through the hook with the fake socket and fake clock for the rendered panel.
- **Steps:**
  1. Call `instrumentState` for one computed field under each validity, NORMAL, NO_COMPUTED_DATA, FAILURE_WARNING and NOT_JUDGED, with the feed running.
  2. Call it for one raw field under each channel status, NOMINAL, NO_DATA and DEGRADED.
  3. Call it for two fields together: NORMAL with FAILURE_WARNING; NORMAL with NOT_JUDGED; NOT_JUDGED with NO_COMPUTED_DATA.
  4. Call it for a NORMAL field with the feed stale; ended; lagging beyond the threshold; the socket closed after frames were seen; with no frame yet; and with several of these at once.
  5. Render each of the six instruments under steps 1, 2 and 4; read its text, its geometry element and its flag.
  6. Render the panel with no status; with SIM; with REPLAY; with LIVE; then under each feed state of step 4; then with a NOT_JUDGED field.
  7. Search the panel's text for "HDG", "heading", "airspeed" and "IAS".
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | NORMAL: `value`; NO_COMPUTED_DATA: `dashes`; FAILURE_WARNING: `flag` with the cause `fail`; NOT_JUDGED: `dashes` with `pipeline` set |
  | 2 | NOMINAL: `value`; NO_DATA: `dashes`; DEGRADED: `flag`, cause `fail` |
  | 3 | `flag`; `dashes` with `pipeline`; `dashes` with `pipeline` |
  | 4 | stale: `flag` `stale`; ended: `flag` `ended`; lag: `flag` `lag`; closed: `flag` `link`; no frame: `dashes`; together, the precedence link, ended, stale, lag |
  | 5 | `value`: the number's text and the geometry element present and no flag; `dashes`: `---` and no geometry; `flag`: a box with the instrument's name (ATT, ALT, VS, GS, TRK, POS), no number, red for every cause but `ended` and grey for `ended`; no element inside the panel carries a line-through or the `stale` class |
  | 6 | MODE UNKNOWN in amber; SIM and REPLAY each in an amber box with "LIVE" absent from the panel; LIVE plain; the state text WAITING, RUNNING, STALE with the age, ENDED, LAG with the lag, BRIDGE DISCONNECTED as FTS-DS-001 §5 lists; PIPELINE shown only with the NOT_JUDGED field |
  | 7 | nothing found |

- **Pass/fail:** Exact as listed. A value shown under FAILURE_WARNING, DEGRADED or any feed-level cause, a struck-through or dimmed value anywhere in the panel, a flag that does not name its instrument, a mode shown before a status, SIM or REPLAY rendered as LIVE, or a heading or airspeed label fails.

### TC-024: The Attitude Indicator's Signs, the Tapes and the Display Units

- **Requirement:** REQ-DISP-002 (also exercises REQ-DISP-001)
- **Level:** Page (`dashboard/src/pfd/geometry.test.ts`, `dashboard/src/pfd/units.test.ts`, `AttitudeIndicator.test.tsx`, `AltitudeTape.test.tsx`, `VerticalSpeed.test.tsx`)
- **Objective:** Verify that a nose-up pitch moves the horizon down and a right roll rotates it counterclockwise, with the translation applied before the rotation; that the altitude tape and the vertical speed indicator place their ticks, pointer and readouts where FTS-DS-001 §4 says; and that units convert at the display only, by exact constants.
- **Preconditions:** The geometry functions called directly; the instruments rendered from `testMessages.frame()` values; the hook's state read for the units check.
- **Steps:**
  1. `horizonTransform(10, 0)`; `horizonTransform(-10, 0)`.
  2. `horizonTransform(0, 30)`; `horizonTransform(0, -30)`.
  3. `horizonTransform(10, 30)`.
  4. Read the pitch ladder's lines and the roll pointer's transform at roll 30.
  5. Render the tape at 121.92 m (400 ft) and at 2,000 m; read the labels' positions, the ticks' range and the readout.
  6. Render the vertical speed indicator at +3 m/s, −2 m/s and +15 m/s; read the readout and the pointer's position.
  7. Convert 1 m and 1 m/s to feet, feet per minute and knots; deliver a frame with `fused_altitude_m` 121.92 and read `feed.lastFrame.frame.fused_altitude_m`.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | `rotate(0 400 260) translate(0 40)` (down); `rotate(0 400 260) translate(0 -40)` (up) |
  | 2 | `rotate(-30 400 260) translate(0 0)` (counterclockwise for a right roll); `rotate(30 400 260) translate(0 0)` |
  | 3 | `rotate(-30 400 260) translate(0 40)`: the translation first |
  | 4 | lines at y = 260 − 40, 260 − 80 and 260 − 120 for +10°, +20° and +30°, and their mirrors below for the negative ones, inside the horizon group; the pointer's group `rotate(-30 400 260)` |
  | 5 | the 400 label at y 260, 300 at y 300, 500 at y 220; ticks every 20 ft from 0 to 800 and none outside; the readout `400`; at 2,000 m the readout `6,560` |
  | 6 | `+600` with the pointer 35.4 px above the centre; `−400` with the pointer 23.6 px below; `+2,950` with the pointer pinned 120 px above |
  | 7 | 3.28084, 196.850 and 1.94384 to five places; `121.92`, the metres the message carried |

- **Pass/fail:** Exact as listed, the transforms as strings and the positions within 0.1 px. A horizon that moves up for nose up, rotates clockwise for a right roll, or applies the rotation before the translation fails; a converted value in the feed's state fails.

### TC-025: The Relative Position and the Lag Check

- **Requirement:** REQ-DISP-005, REQ-DISP-006, REQ-DISP-002 (the track's speed threshold)
- **Level:** Integration (steps 1 through 4: `tests/integration/test_bridge.py`, in-process), Page (steps 5 through 7: `dashboard/src/useTelemetryFeed.test.ts`, `dashboard/src/pfd/GroundTrackPlot.test.tsx`, `dashboard/src/pfd/TrackIndicator.test.tsx`), Manual (step 8, Chrome)
- **Objective:** Verify that the bridge forwards a position relative to the first NOMINAL fresh fix and never the fix itself, resets the origin as REQ-DISP-006 resets the lag's, and delivers both thresholds in `hello`; that the page measures the lag from elapsed times on each clock, flags it past the threshold, resets its origins, and keeps a trail of fresh fixes; that the plot draws the trail on a grid with a scale bar and never an image; and that the track is withheld below the speed threshold.
- **Preconditions:** A `QueueSource` and a `websockets` client as TC-022; a frame at latitude 51.234567 and longitude −0.987654 and a second one 1 m north and 1 m east of it by the bridge's own formula (`METERS_PER_DEGREE`, R = 6,371,000 m); the hook driven by the fake socket and clock; for step 8, the demo of TC-026 step 9 replaying to a Chrome tab.
- **Steps:**
  1. Start the bridge with no flags and read `hello`; start it with `--track-min-speed 2.5 --lag-ms 250` and read `hello`.
  2. Feed a frame with `gps_status` NO_DATA; then the first frame with NOMINAL and `gps_read_ok` true; then the frame 1 m north and east; then the same position with `gps_read_ok` false.
  3. Search each forwarded message's text for "latitude", "longitude", "51.23" and "0.9876".
  4. Feed a frame whose timestamp precedes its predecessor's, then a fix; feed ENDED, then a fix.
  5. Deliver frames with timestamps 0, 20 and 40 ms arriving at 0, 20 and 400 ms on the fake clock, with `received_at_ms` 0, 20 and 300; read `lagMs`, `bridgeLagMs` and `lagExceeded` with `hello`'s threshold 500, then with 250; close and reopen the socket, deliver ENDED then a frame, deliver a backwards timestamp, reading the origins each time; deliver 3,700 fresh fixes and read the trail's length.
  6. Render the plot with three fixes at (0, 0), (1, 0) and (1, 1) m with the track 045° at 2 m/s; then at 0.5 m/s; then with fixes 3 km apart; then under a `fail` state; search the plot for `<image>` elements.
  7. Render the track indicator with `ground_speed_mps` 0.5 and `heading_deg` 90; then with 1.0.
  8. Manual, in Chrome: with the demo replaying and the page in a Chrome tab, switch to another tab for two minutes and return; read the lag in the strip, whether LAG flagged and cleared, and the two lag numbers in the diagnostic view.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | `track_min_speed_mps` 1.0 and `lag_ms` 500; then 2.5 and 250 |
  | 2 | `position` null; `{"east_m": 0.0, "north_m": 0.0}`; east and north each 1.00 within 0.01; the same position again |
  | 3 | nothing found in any message |
  | 4 | the fix after the backwards timestamp reads (0, 0); the fix after ENDED reads (0, 0) |
  | 5 | `lagMs` 0, 0, 360; `bridgeLagMs` 0, 0, 260; `lagExceeded` false at 500 and true at 250; the origins re-taken after the reconnect, after ENDED and after the backwards timestamp (the next frame's lag 0); the trail holds 3,600 |
  | 6 | a `points` string of three pairs scaled into the viewBox; the marker a triangle rotated 45°; a dot at 0.5 m/s; the scale bar `2 m` for the metre-scale trail (the 1-2-5 step nearest a quarter of the 10 m minimum half-extent) and `1 km` for the kilometre one; the POS flag with the trail still drawn; no `<image>` |
  | 7 | `TRK ---` with no rose; `TRK 090°` with the rose rotated by −90 |
  | 8 | recorded in FTS-VR-001 Entry 17 §17.5 as observed: the lag on return, whether the flag showed and cleared, the bridge-side lag |

- **Pass/fail:** Steps 1 through 7 exact as listed, positions within 1 cm. A coordinate or its digits in any message, an origin that survives ENDED or a backwards timestamp, a lag computed by subtracting one clock from the other, a track shown below the threshold, or an image in the plot fails. Step 8 records; it does not pass or fail.

### TC-026: The Scripted Flight and the Replay Demo

- **Requirement:** REQ-SENS-007 (also exercises REQ-SENS-006, REQ-TIME-004, REQ-LOG-003, REQ-PROC-001 and REQ-PROC-002)
- **Level:** Unit (steps 1 through 4: `tests/unit/test_altitude.cpp`, `test_motion_profile.cpp`, `test_simulated_data.cpp`), Integration (steps 5 through 8: `tests/integration/test_motion_profile.py`), Manual (step 9)
- **Objective:** Verify that the profile's readings are the pipeline's inverse, so that the pipeline recovers the script; that a profile run is deterministic and its replay byte-identical; that `--motion` is refused where it has no meaning; and that the stationary default's bytes do not change.
- **Preconditions:** The built binary; the session fixture (the seed-42 run, TC-004); `ComplementaryFilter` and `pressure_to_altitude` as oracles.
- **Steps:**
  1. `altitude_to_pressure` at 0, 2, 122, 1,000 and 2,000 m, each back through `pressure_to_altitude`; 898.76 hPa through `pressure_to_altitude` and back.
  2. `MotionProfile::named("demo")` at 0 s, 36 s, 77 s and 200 s; `named("nope")`.
  3. Generate the demo's frames with a 1e-9 noise and feed them to a `ComplementaryFilter` at the frame interval; invert the pressure; read the fix at 60 s.
  4. Generate the first frame of a generator without a profile; generate 1,500 frames from two profile generators with one seed.
  5. Run `telemetry --motion demo --cycles 1000` (20 s); read the log's last frame and the exit summary.
  6. Replay that log unpaced; compare the bytes.
  7. Run `telemetry --motion demo --replay <log>`, `--motion demo --live` and `--motion nope`.
  8. The session fixture's log, with and without `--zmq` (the existing checks of TC-004 and TC-021).
  9. Manual: record the 7,500-frame demo, hash it, replay it paced and compare, then replay it paced through the bridge to the page.
- **Expected result:**

  | Step | Expected |
  |---|---|
  | 1 | each altitude back within 1 cm; 1,000 m back to 898.76 hPa within 0.01 hPa |
  | 2 | 0 s: 2 m, 0°, 0°, 090, 0 m/s; 36 s: 59 m, 8°, 0°, 090, 30 m/s, pitch rate 0; 77 s: 122 m, 2°, 20°, 180, 30 m/s, track rate 6°/s; 200 s: the 150 s state held with zero rates; `nullopt` |
  | 3 | pitch and roll within 0.2° of the script at 30 s and 80 s; the altitude within 5 cm; the fix at 60 s about 1,500 m east, within 2 m, and within 2 m north of the origin |
  | 4 | the first frame equals the stationary literals the existing `SimulatedDataTest` pins; the two profile sequences byte-identical |
  | 5 | pitch within 0.3° of 8, ground speed 30, every validity NORMAL on the last frame, the GPS more than 200 m east of the first fix by the test's own conversion; `Motion:    profile demo` in the summary |
  | 6 | byte-identical; `Replay:    1000 frames, end of log reached` |
  | 7 | exit 1 each, the first two naming the rule, the third naming `demo`; nothing built |
  | 8 | `6d43f343…` (FTS-VR-001 Entry 12 §12.4), unchanged |
  | 9 | recorded in FTS-VR-001 Entry 17 §17.4: the hash, the paced replay byte-identical, its lateness, the page's phases as FTS-DS-001 §7 lists |

- **Pass/fail:** Steps 1 through 8 exact as listed. Any change to the session fixture's bytes, a profile run whose replay differs, a refused flag that built anything, or a filter output more than 0.3° from the script fails.

## 6. Coverage Strategy

### 6.1 Statement versus Branch Coverage

**Statement coverage** reports whether each executable line ran at least once. It is necessary but weak: an `if` with no `else` reaches 100% statement coverage when the condition is true once, even though the false path was never exercised.

**Branch coverage** reports whether each decision outcome ran. Every `if`, loop condition, and ternary counts as two branches, and both must execute. For a fault detector, the false path is the interesting one: "sensor is healthy, do nothing" is the branch most likely to hide a bug that only appears when the condition flips.

This project measures both and gates on branch coverage. Statement coverage is reported for completeness but does not block a build.

The figure is taken after both the Google Test suite and the pytest suite have run the instrumented build: the integration tests drive `build/telemetry`, whose code is `telemetry_core`'s, so their counts merge into the same files the unit tests wrote. Both levels are requirements-based tests, which is what structural coverage analysis is performed over (DO-178C 6.4.4.2), and a count can only rise. verification_results.md names the branches only the integration level reaches.

### 6.2 Why Not 100%

Some branches cannot be reached from automated tests in CI:

- Hardware error paths in the I2C driver (a failed `ioctl`, a bus that returns garbage) need physical hardware to trigger; the serial port's success path runs on a pseudo-terminal in CI, and only `cfsetspeed`, `tcsetattr` and `tcflush` failing on a terminal that opened, and a read that fails, stay hardware-bound. They are exercised on the Raspberry Pi manually and documented in verification_results.md, but CI runs on a machine with no sensors.
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

The gate measures `src/` (REQ-TEST-003). The Python modules under `pipeline/` are not under it and never were; the page in `dashboard/` is not either: its line and branch figures from Vitest's coverage run in CI are recorded per entry in FTS-VR-001 beside the C++ scopes, reported and not gated, because the coverage tooling of record is gcov/lcov over the C++ and a second gate with a second tool would need its own exclusion policy.

### 6.4 MC/DC

Modified Condition/Decision Coverage is the structural coverage criterion DO-178C requires for Level A software. It extends branch coverage to compound conditions: for a decision such as `if (a && b)`, MC/DC requires test cases showing that each of `a` and `b` independently changes the outcome while the other is held fixed. Branch coverage is satisfied by two test cases (true, false). MC/DC needs at least three for two conditions, and N+1 for N conditions.

MC/DC is out of scope for this project. It requires tool support (gcov does not report it) and the DAL A rigor is not proportionate to a development system. Where compound conditions appear in fault detection logic, they are kept short and each operand is tested in isolation, which approaches MC/DC intent without claiming the criterion.

---

## 7. Open Items

- Nine requirements are Partial in FTS-TM-001, the four I2C sensor requirements and the GPS (REQ-SENS-005) among them (on-target evidence pending). The Notes column there states what each is missing.
- TC-015's Hardware and manual steps (9 through 12) wait for the stationary capture of 2026-10-13. REQ-PROC-004 stays Partial until then, and after it with the GPS variance as its one named gap (a capture with the receiver fixing, now that REQ-SENS-005 delivers one).
- The Hardware steps of TC-002 (step 8), TC-012 (step 8), and TC-014 (steps 11 through 15) wait for the Raspberry Pi; TC-012's step 8 needs the real chip's register dump. Results go to FTS-VR-001 Entry 8 §8.6. The addresses the live source starts from (`sensors.barometer.address` 0x76 and `sensors.imu.address` 0x68 in `config/telemetry_config.yaml`, the driver named for the BMP280) are provisional until step 11 reads the real values (FTS-DD-001 Section 12); a change lands in the file and the struct defaults together.
- Both transports are implemented; TC-005 and TC-010 steps 6 and 7 are `tests/integration/test_transport.py`, over both, and the UDP destination comes from the configuration file (TC-016 step 20). TC-004's Python comparison step is `tests/integration/test_replay_identity.py`.
- REQ-TIME-003 is judged on the Raspberry Pi (TC-009 step 7). Jitter numbers measured on macOS use the `nanosleep` fallback and are recorded in FTS-VR-001 as a preview only.
- The session status message's LIVE mode (TC-021 step 9) and a paced replay's lateness on the Raspberry Pi are recorded on the next hardware day. The end-of-stream signal on `DataSource` is built (REQ-LOG-006, TC-019).
- GPS fix-quality detection at the detector (FAULT-003b) is deferred until the frame carries a fix-quality byte (FTS-DD-001 Section 11.2); the live source withholds a no-fix epoch, so the condition reaches the detector as the timeout (TC-018 step 13). The Hardware steps of TC-018 (20 through 24) and its capture fixture (step 17) wait for the Raspberry Pi and the receiver; results go to FTS-VR-001 Entry 13 §13.8. GPS stuck detection is not covered by any FAULT entry and is not implemented.
- The display's instruments (REQ-DISP-001, REQ-DISP-002, REQ-DISP-006, REQ-SENS-007; TC-023 to TC-026) are in work on the display's branch (2026-10-15). TC-025 step 8, a Chrome background tab, is a manual record and not a gate; TC-026 step 9 is the demo's hand measurement; both go to FTS-VR-001 Entry 17.
