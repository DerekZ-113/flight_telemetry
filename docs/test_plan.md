# Flight Telemetry System — Software Test Plan

> **Document ID:** FTS-TP-001
> **Version:** 0.1.0
> **Status:** Draft
> **Last Updated:** 2026-09-09
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
- **Isolation:** Hardware dependencies are replaced with test doubles. The `DataSource` abstraction is the primary seam: a test-only `FakeSource` returns hand-built frames, so the pipeline can be exercised without sensors or the simulator. Time-dependent logic (fault timeouts, fixed-rate scheduling) shall accept an injectable clock so tests do not sleep.
- **Location:** `tests/unit/`, one test file per module (`test_altitude.cpp`, `test_fault_detection.cpp`, and so on).
- **Executed:** Every commit, in CI and locally.

### 2.2 Integration Tests

- **Scope:** The full pipeline across the C++/Python boundary. C++ processing publishes over ZeroMQ and UDP; the Python receiver consumes and validates.
- **Framework:** pytest.
- **Isolation:** The C++ binary runs with the simulated data source, so integration tests are hardware-independent and reproducible.
- **Location:** `tests/integration/`.
- **Executed:** Every commit, in CI.

### 2.3 Fault Injection Tests

- **Scope:** System behavior under the failure modes in FTS-FM-001. Each FAULT entry maps to at least one injected scenario.
- **Framework:** Parameterized Google Test harness for single faults; pytest scenarios for multi-fault and recovery sequences.
- **Method:** Faults are injected at the `DataSource` boundary. A `FaultInjectingSource` wraps another source and applies a scripted or seeded fault schedule: dropped reads, repeated values, out-of-range values, and recovery after a configurable interval.
- **Randomization:** Randomized schedules use a logged seed so any failing run can be reproduced exactly. A randomized test that cannot be replayed is not evidence.
- **Location:** `tests/fault_injection/`.
- **Executed:** Every commit for the deterministic scenarios. Randomized sweeps run nightly.

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

- **Requirement:** REQ-FAULT-001 (also exercises REQ-FAULT-004)
- **Level:** Fault injection
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
- **Expected result:** Barometer and IMU transition to DEGRADED at a clock time greater than 480 ms and no greater than 500 ms after the first failed read. GPS transitions at a time greater than 1980 ms and no greater than 2000 ms. Healthy channels remain NOMINAL throughout. Processing continues for every frame; no frame is dropped.
- **Pass/fail:** All three channels detected within their bound and not more than one frame period early. Any detection before 480 ms (or 1980 ms for GPS) fails, because it would false-alarm on a single transient error. Any missing or duplicated fault event fails.

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
- **Preconditions:** `BinaryLogger` and `LogReplaySource` implemented. Pipeline uses frame timestamps, not the wall clock, for every time-dependent computation. Same build of the binary used for both runs. Steps 1 through 6 are executed at unit level by `ReplayTest.ReplayReproducesLiveSession` (500 frames with injected faults, every processed frame and fault event compared with `memcmp`) and at file level by comparing `logs/telemetry_000.bin` with `logs/replay_000.bin` after `telemetry --replay`. The Python parse in step 3 is the integration-level execution and lands with the receiver.
- **Steps:**
  1. Run the pipeline in simulated mode with seed 42 for 500 frames at 50 Hz. Log every processed frame to `session_a.bin`.
  2. Run the pipeline in replay mode with `session_a.bin` as the data source. Log every processed frame to `session_b.bin`.
  3. Parse both logs in Python. For each frame index, compare every field.
  4. Assert the frame counts are equal.
  5. Assert every raw field is identical (replay must not alter its input).
  6. Assert every computed field (baro altitude, pitch, roll, fused altitude, vertical speed, channel status) is identical, compared as raw bytes rather than with a floating-point tolerance.
- **Expected result:** 500 frames in each log. Zero field differences.
- **Pass/fail:** Any differing byte in any computed field fails. A tolerance-based comparison is not acceptable here: the requirement says identical, and a drift that is small today becomes a debugging problem when the Kalman filter changes. This test runs on a single toolchain. Cross-platform bit identity between the Mac development build and the Raspberry Pi target is not claimed, because `std::normal_distribution` is not specified by the standard and the two C++ standard libraries differ.

### TC-005: Pipeline Data Integrity over ZeroMQ

- **Requirement:** REQ-TRANS-001 (also exercises REQ-TEST-002)
- **Level:** Integration
- **Objective:** Verify that every frame published by the C++ process arrives at the Python receiver intact, in order, and without loss under normal conditions.
- **Preconditions:** C++ binary built with the ZeroMQ publisher. Python receiver subscribed on the configured local endpoint. Simulated source with seed 42 so the expected frame sequence is known in advance.
- **Steps:**
  1. Start the Python subscriber and allow 200 ms for the subscription to settle. ZeroMQ pub/sub drops messages sent before a subscriber is connected, so the settle delay is part of the procedure, not a workaround.
  2. Start the C++ process configured for 200 frames at 50 Hz, then exit.
  3. Collect all received messages until the publisher exits or 5 s elapses.
  4. Deserialize each message into the frame layout defined in `telemetry_frame.h`.
  5. Assert 200 frames were received.
  6. Assert timestamps are strictly increasing in steps of 20 ms.
  7. Compare each received frame against the binary log written by the C++ process during the same run. Assert every received frame is byte-identical to the corresponding logged frame. (Python cannot regenerate the expected frames from the seed, because its random number generator differs from the C++ one, so the C++ log is the reference.)
- **Expected result:** 200 frames received, ordered, byte-identical to the log.
- **Pass/fail:** Any missing, reordered, or altered frame fails. This test exercises the normal path only. Transport failure behavior (REQ-TRANS-003) is a separate card.

---

### TC-006: Attitude Estimation Accuracy

- **Requirement:** REQ-PROC-002
- **Level:** Unit
- **Objective:** Verify that the complementary filter recovers static tilt from the accelerometer, follows the gyroscope for short-term motion, and bounds gyroscope bias drift.
- **Preconditions:** None. The filter is constructed with a chosen alpha; no hardware or simulator is required.
- **Steps:**
  1. Construct a filter with default alpha. Call `update()` once with accelerometer (0, 0, 9.81) m/s² and zero gyroscope rates. Record pitch and roll.
  2. Construct a fresh filter. Call `update()` once with accelerometer (9.81, 0, 9.81) and zero rates. Record pitch and roll.
  3. Construct a filter with alpha = 1.0 (gyroscope only). Seed flat as in step 1, then call `update()` 100 times with accelerometer (0, 0, 9.81), pitch rate 1.0 deg/s, dt = 0.02 s. Record pitch.
  4. Construct a filter with alpha = 0.98. Seed flat, then call `update()` 500 times with the same inputs as step 3. Record pitch.
- **Expected result:**

  | Step | Expected pitch (deg) | Expected roll (deg) | Tolerance (deg) | Basis |
  |---|---|---|---|---|
  | 1 | 0.0 | 0.0 | 0.01 | gravity on z only |
  | 2 | 45.0 | 0.0 | 0.1 | atan2(9.81, 9.81) |
  | 3 | 2.0 | 0.0 | 0.01 | 100 × 0.02 s × 1 deg/s, pure integration |
  | 4 | 0.98 | 0.0 | 0.05 | steady state alpha·b·dt/(1−alpha) |

- **Pass/fail:** All four within tolerance. Step 3 must show the drift (pitch near 2°, not 0°) and step 4 must show it bounded (pitch below 1.5° after ten time constants). A filter that passes step 4 by ignoring the gyroscope fails step 3.

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
- **Level:** Unit
- **Objective:** Verify that the binary log is self-describing, round-trips frames and fault events bit for bit, rotates at record boundaries, and fails safely on a damaged or foreign file.
- **Preconditions:** A temporary directory per test, removed afterwards. Frames from `SimulatedSource(20, 42)` or built by hand.
- **Steps:**
  1. Log one frame. Reopen with `LogReader`. Assert the header validates and its version, frame size, event size, and endianness marker match this build.
  2. Log 10 simulated frames. Read them back. Compare each with `memcmp` against the frame that was written.
  3. Log frame, event, frame. Assert three records with tags FRAME, FAULT_EVENT, FRAME and that the event compares equal.
  4. Set the file limit to header plus three frame records. Log 10 frames. Assert four files, each with a valid header, none larger than the limit, and 10 frames total in order.
  5. Corrupt a header field (magic, version, frame size, event size, endianness) one at a time. Assert the reader refuses each with a non-empty error.
  6. Truncate a file in the middle of its second frame. Assert the reader returns the first frame, then false, with a "torn record" error and no crash.
  7. Point the logger at a path that cannot be a directory. Assert `ok()` is false and that logging calls return without throwing.
- **Expected result:** All assertions hold. Rotation never splits a record; a limit smaller than one record still yields one record per file.
- **Pass/fail:** Any bit difference in step 2 or 3 fails. Any accepted foreign header in step 5 fails. Any crash or invented frame in step 6 or 7 fails.

## 6. Coverage Strategy

### 6.1 Statement versus Branch Coverage

**Statement coverage** reports whether each executable line ran at least once. It is necessary but weak: an `if` with no `else` reaches 100% statement coverage when the condition is true once, even though the false path was never exercised.

**Branch coverage** reports whether each decision outcome ran. Every `if`, loop condition, and ternary counts as two branches, and both must execute. For a fault detector, the false path is the interesting one: "sensor is healthy, do nothing" is the branch most likely to hide a bug that only appears when the condition flips.

This project measures both and gates on branch coverage. Statement coverage is reported for completeness but does not block a build.

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
| `src/timing/`, `src/logging/`, `src/transport/`, `src/replay/` | 80% |
| `src/drivers/` | 70% |
| Overall | 80% |

Processing carries the highest target because filters and fault detection are pure logic with no hardware dependency, so every branch is reachable from a unit test. Drivers carry the lowest because their error branches are hardware-bound. The overall figure is a floor, not a goal.

### 6.4 MC/DC

Modified Condition/Decision Coverage is the structural coverage criterion DO-178C requires for Level A software. It extends branch coverage to compound conditions: for a decision such as `if (a && b)`, MC/DC requires test cases showing that each of `a` and `b` independently changes the outcome while the other is held fixed. Branch coverage is satisfied by two test cases (true, false). MC/DC needs at least three for two conditions, and N+1 for N conditions.

MC/DC is out of scope for this project. It requires tool support (gcov does not report it) and the DAL A rigor is not proportionate to a development system. Where compound conditions appear in fault detection logic, they are kept short and each operand is tested in isolation, which approaches MC/DC intent without claiming the criterion.

---

## 7. Open Items

- REQ-SENS-006, REQ-PROC-005, REQ-LOG-003, and REQ-TEST-001 are Partial in FTS-TM-001. The Notes column there states what each is missing.
- The transports are not implemented. TC-005 describes the intended procedure and will be revised when the interface is final. TC-004's Python comparison step waits on the receiver.
- `DataSource` has no end-of-stream signal; `LogReplaySource` repeats its last frame after the log ends and exposes `exhausted()`. See FTS-DD-001 open decisions.
- GPS fix-quality detection (FAULT-003b) is deferred until the NEO-6M driver adds a fix-quality field. GPS stuck detection is not covered by any FAULT entry and is not implemented.
