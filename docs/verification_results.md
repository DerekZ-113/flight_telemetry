# Flight Telemetry System — Verification Results

> **Document ID:** FTS-VR-001
> **Version:** 0.1.0
> **Status:** Draft
> **Last Updated:** 2026-09-09
> **Related:** FTS-TP-001 (test plan), FTS-TM-001 (traceability), FTS-SRD-001 (requirements)

This document records the outcome of running the test plan: which tests ran, on what platform, what structural coverage they achieved, and what was excluded from coverage and why. FTS-TP-001 says what should happen; this document says what did. Each entry is a snapshot of one commit; the latest entry is the current state.

Coverage is measured by the CI job on Ubuntu with GCC and gcov, which is the measurement of record. Numbers from a developer Mac (clang, `llvm-cov gcov`) are marked as a preview: the two toolchains instrument code slightly differently and the percentages may not match exactly.

---

## Entry 1 — 2026-09-09, CI pipeline commit

### 1.1 Test results

| Item | Value |
|---|---|
| Platform | macOS 26, Apple clang 21 (preview; CI run pending) |
| Build | `-DENABLE_COVERAGE=ON -DWARNINGS_AS_ERRORS=ON`, `-O0 -g` |
| Compiler warnings | 0 |
| Test suites | AltitudeTest (8), KalmanTest (8), ComplementaryTest (9), FaultDetectionTest (29), FaultInjectingSourceTest (4) |
| Tests passed | 56 of 56 |

### 1.2 Structural coverage

Measured over `src/` after the exclusions in 1.3. Thresholds from FTS-TP-001 §6.3.

| Scope | Lines | Functions | Branches | Threshold (branch) | Result |
|---|---|---|---|---|---|
| `src/processing/` | 100.0% | 100.0% | 100.0% (66/66) | 90% | pass |
| `src/drivers/` | 100.0% | 94.7% | 100.0% (16/16) | 70% | pass |
| `src/timing/`, `logging/`, `transport/`, `replay/` | no files | | | 80% | n/a |
| **Overall** | **100.0%** (396/396) | **94.8%** (55/58) | **100.0%** (82/82) | 80% | **pass** |

Branch coverage is the gating figure (FTS-TP-001 §6.1). Line and function coverage are reported for completeness.

The three uncounted functions are compiler-generated destructor variants for `DataSource` and `FaultInjectingSource` (the "deleting" and "complete object" forms the ABI emits for a class with a virtual destructor). Only one variant runs depending on how the object is destroyed; the other is never dead code, it is the same destructor reached a different way. Not a coverage gap.

### 1.3 Excluded from coverage

Per FTS-TP-001 §6.2, each exclusion is listed with its justification.

| Excluded | Mechanism | Justification |
|---|---|---|
| `src/main.cpp` | `lcov --remove` | Composition root: builds the source, detector, and processor and runs the loop. Has no requirement of its own to test against; every function it calls is tested directly. Named in §6.2. |
| Google Test sources (`build/_deps/`) | `lcov --remove` | Third-party. |
| `tests/unit/*.cpp` | `lcov --remove` | Tests are not the unit under test. |
| Compiler-generated exception branches | `lcov --filter branch` | Every call that may throw (`std::vector::push_back`, `std::make_unique`) compiles to a taken/unwind branch pair. The unwind path cannot be reached without exhausting memory. Counting them measures the standard library's unwind paths, not this system's decisions. |
| `switch` no-match branch, 5 sites | `// LCOV_EXCL_BR_LINE` | Each `switch` is over an `enum class` and lists every enumerator. The implicit no-match branch exists because the compiler cannot prove exhaustiveness; no value of the type reaches it. §6.2: "defensive branches that guard against conditions the type system already excludes." Sites: `fault_detector.cpp` `channel_name`, `fault_type_name`; `fault_injecting_source.cpp` `copy_channel`, `fail_reads`, `hold_values`. |
| `return "UNKNOWN";` after those switches, 2 sites | `// LCOV_EXCL_LINE` | Same reason; exists only to satisfy `-Wreturn-type`. |

### 1.4 Coverage-driven test additions

The first coverage run at this commit reported 72.8% branches overall (processing 77.3%, drivers 60.7%). Seventeen untaken branches were reviewed. Five were the `switch` no-match branches above. Twelve were untested paths in code with an existing requirement, and a test was added for each rather than lowering a threshold:

| Path | Test added | Requirement |
|---|---|---|
| Timeout on a channel that never had a successful read | `FaultDetectionTest.DeadFromPowerOnStillTimesOut` | REQ-FAULT-001 |
| Temperature below −40 °C | `FaultDetectionTest.TemperatureBelowRangeDegradesBaro` | REQ-FAULT-003 |
| Every IMU axis stuck at once | `FaultDetectionTest.AllAxesStuckReportsOnce` | REQ-FAULT-002 |
| Missing read while DEGRADED | `FaultDetectionTest.MissingReadDuringRecoveryNeitherCountsNorResets` | REQ-FAULT-005 |
| Injector fail/hold on IMU and GPS | `FaultInjectingSourceTest.FailReadsImuAndGps`, `HoldValuesImuAndGps` | REQ-TEST-001 |
| Fault-type and channel names | `FaultDetectionTest.EventNamesAreReadable` | REQ-FAULT-004 |
| Complementary filter with no usable gravity | `ComplementaryTest.ZeroAccelCoastsOnGyro`, `UnseededUntilAccelUsable` | REQ-PROC-002 |
| Kalman predict with dt ≤ 0 | `KalmanTest.PredictWithZeroDtIsNoOp` | REQ-PROC-003 |
| GPS DEGRADED skips the GPS update | `KalmanTest.DegradedGpsIsIgnored` | REQ-PROC-003, REQ-FAULT-004 |

### 1.5 Static analysis

| Item | Value |
|---|---|
| Tool | cppcheck 2.21.0 |
| Flags | `--enable=warning,style,performance,portability --std=c++17 --suppress=missingIncludeSystem --inline-suppr` |
| Findings | 0 |

Four style findings were present before this commit and were fixed rather than suppressed: two member functions that read no object state made `static` (`FaultDetector::comm_timed_out`, `FaultInjectingSource::copy_channel`); `SimulatedSource`'s constructor made `explicit` (its second parameter is defaulted, so it was an implicit converting constructor); a raw loop over accelerometer axes replaced with `std::find_if`.

### 1.6 Open

- Replace the macOS preview numbers above with the first GitHub Actions run on Ubuntu/GCC and record the run number.
- Integration tests (pytest, REQ-TEST-002) do not exist yet. Criterion 2 of FTS-TP-001 §4 is not evaluated.
