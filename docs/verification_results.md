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
| Test suites | AltitudeTest (8), KalmanTest (8), ComplementaryTest (9), FaultDetectionTest (27), FaultInjectingSourceTest (4) |
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

---

## Entry 2 — 2026-09-09, logger and replay commit

### 2.1 Test results

| Item | Value |
|---|---|
| Platform | macOS 26, Apple clang 21 (preview; CI run pending) |
| Compiler warnings | 0 |
| Test suites | AltitudeTest (8), KalmanTest (8), ComplementaryTest (9), FaultDetectionTest (27), FaultInjectingSourceTest (4), LoggingTest (22), ReplayTest (6) |
| Tests passed | 84 of 84 |
| Replay identity | `ReplayTest.ReplayReproducesLiveSession`: 500 frames, 2 injected faults, every frame and event bit-identical. File level: `logs/telemetry_000.bin` and `logs/replay_000.bin` identical by `cmp`. |

### 2.2 Structural coverage

| Scope | Lines | Branches | Threshold (branch) | Result |
|---|---|---|---|---|
| `src/processing/` | 100.0% | 100.0% | 90% | pass |
| `src/drivers/` | 100.0% | 100.0% | 70% | pass |
| `src/logging/` | 100.0% | 83.3% (25/30) | 80% | pass |
| `src/replay/` | 100.0% | 100.0% | 80% | pass |
| **Overall** | **100.0%** (584/584) | **93.1%** (149/160) | 80% | **pass** |

The five untaken branches are all in `log_reader.cpp` on lines whose true and false paths are each exercised by a named test (`ReaderRejectsMissingFile`, `ReaderRejectsTruncatedHeader`, `ReaderRejectsBadMagic`, `ListLogFilesOnMissingDirectoryIsEmpty`, `ListLogFilesIsSortedAndFiltered`) and whose line coverage is 100%. They are clang-specific edges (constructor initializer and range-for over `directory_iterator`) that `--filter branch` does not remove on this toolchain. The CI GCC figures are the measurement of record; these are not chased on the preview.

### 2.3 Exclusions

As Entry 1, plus one `LCOV_EXCL_BR_LINE` on the record-tag `switch` in `LogReader::next`, which has an explicit `default` that is itself tested (`UnknownTagStopsReader`); the exclusion covers only the compiler's no-match edge.

### 2.4 Static analysis

cppcheck 2.21.0, same flags as Entry 1: 0 findings.

### 2.5 Open

- Replace the preview numbers with the first CI run after this commit.

---

## Entry 3 — 2026-09-09, timing commit

### 3.1 Test results

| Item | Value |
|---|---|
| Platform | macOS 26, Apple clang 21 (preview; CI run pending) |
| Compiler warnings | 0 |
| Test suites | AltitudeTest (8), KalmanTest (8), ComplementaryTest (9), FaultDetectionTest (27), FaultInjectingSourceTest (4), LoggingTest (22), ReplayTest (6), TimingTest (14) |
| Tests passed | 98 of 98 |
| Replay identity with pacing on | `logs/telemetry_000.bin` (paced live run) and `logs/replay_000.bin` (unpaced replay) identical by `cmp` |

### 3.2 Structural coverage

| Scope | Lines | Branches | Threshold (branch) | Result |
|---|---|---|---|---|
| `src/processing/` | 100.0% | 100.0% | 90% | pass |
| `src/drivers/` | 100.0% | 100.0% | 70% | pass |
| `src/logging/` | 100.0% | 83.3% | 80% | pass |
| `src/replay/` | 100.0% | 100.0% | 80% | pass |
| `src/timing/` | 100.0% | 100.0% | 80% | pass |
| **Overall** | **100.0%** (669/669) | **93.8%** (167/178) | 80% | **pass** |

The five untaken branches are the same `log_reader.cpp` clang edges recorded in Entry 2. On macOS only the `nanosleep` fallback branch of `MonotonicClock::sleep_until_ns` is compiled; the Linux `clock_nanosleep` branch is compiled and measured on CI only. One `LCOV_EXCL_BR_LINE` sits on the Linux `EINTR` retry loop: signal delivery during a sleep cannot be reproduced in a unit test.

### 3.3 Timing preview (REQ-TIME-002 measurement; REQ-TIME-003 NOT satisfied here)

500 cycles at 20 ms on the real clock, macOS `nanosleep` fallback, scratch harness linking the timing modules, machine otherwise idle:

| Statistic | Value |
|---|---|
| Jitter min | 52 µs |
| Jitter mean | 3 381 µs |
| Jitter max | 5 755 µs |
| Cycles over 1 ms | 468 of 500 |
| Missed cycles | 0 |

The 5-cycle run from `./telemetry` shows the same shape (min 172 µs, mean 3.6 ms, max 5.0 ms). This is the macOS sleep primitive's timer slack, several milliseconds by default, not the scheduler: `missed_cycles` is zero, every deadline is on the grid, and the fake-clock tests show the scheduler adds no lateness of its own. **These numbers do not satisfy REQ-TIME-003 and are not claimed to.** The 1 ms bound is judged on the Raspberry Pi's `clock_nanosleep(TIMER_ABSTIME)` path (TC-009 step 7), where sub-millisecond wake latency is the normal case. The preview is recorded because it is the first real measurement and because it makes the platform distinction in FTS-DD-001 §10 concrete.

### 3.4 Static analysis

cppcheck 2.21.0, same flags as Entry 1: 0 findings.

### 3.5 Open

- Replace preview numbers with the first CI run after this commit (Linux; first execution of the `clock_nanosleep` path).
- TC-009 step 7 on the Pi: 60 s at 50 Hz, max jitter below 1 ms, `missed_cycles` 0. Until then REQ-TIME-003 stays Partial.

---

## Entry 4 — 2026-09-09, requirements revision commit (FTS-SRD-001 v0.2.0)

### 4.1 Test results

| Item | Value |
|---|---|
| Platform | macOS 26, Apple clang 21 (preview; CI run pending) |
| Compiler warnings | 0 |
| Test suites | as Entry 3 plus SimulatedDataTest (5), SimulatedSourceTest (2) |
| Tests passed | 105 of 105 |

### 4.2 Structural coverage

Unchanged from Entry 3 in every scope: lines 100.0% (670/670), branches 93.8% (167/178), all thresholds met. The one source change (below) adds no branch.

### 4.3 Finding: uninitialized padding in simulated frames

The requirements revision reworded REQ-SENS-006 to say the simulator produces frames of raw sensor readings, and a direct test of that contract was added. `SimulatedDataTest.SameSeedSameSequence` compares two same-seed frames with `memcmp` and failed on first run: bytes 98 and 99 differed. Those are tail padding after `gps_read_ok` (offset 97) in the 104-byte `TelemetryFrame`. The simulator declared its frame without an initializer, assigned every field, and left the compiler's padding bytes holding stack garbage, which the binary logger then wrote verbatim.

Consequence before the fix: two identical sessions could produce logs differing in bytes that carry no data. In practice two live runs of the binary compared identical, because both processes happened to see the same stack contents; that is luck, not a property. Every field-level comparison in the suite passed throughout, which is why the defect survived until a byte-level test existed.

Fix: `TelemetryFrame frame{};` in `SimulatedDataGenerator::generate` (one token). Value-initialization zeroes the whole object, padding included, before the fields are assigned. No data value changes; every existing numeric assertion is unaffected. Verified after the fix: the padding probe reports no differing bytes; two separate live runs produce byte-identical logs; the live and replay logs remain identical. The rule "sources value-initialize their frames" is recorded in FTS-DD-001 Section 9 and this test is its guard on both toolchains.

### 4.4 Static analysis

cppcheck 2.21.0, same flags as Entry 1: 0 findings.

### 4.5 Open

- Replace preview numbers with the first CI run after this commit.

---

## Entry 5 — 2026-09-10, first CI run of the logger, replay, and timing commits

### 5.1 CI result

GitHub Actions run 34442203740 (Ubuntu, GCC 13) on commit `5ec7027`: **failed** at the unit test step. 104 of 105 tests passed. `ReplayTest.ReplayReproducesLiveSession` failed at `test_replay.cpp:198`, "event 0 differs", "event 1 differs". The frame comparison at line 193 passed: every replayed frame was byte-identical to the live one on GCC. The coverage and static analysis steps did not run because the test step failed first, so the first GCC coverage numbers are still pending.

### 5.2 Finding: unspecified padding in brace-built fault events on GCC

`FaultEvent` is 16 bytes with two padding bytes between `type` (offset 9) and `value` (offset 12). The detector built each event as a braced temporary, `events_.push_back({now_ms, channel, type, value})`. Aggregate initialization from a braced list does not require padding to be zeroed. Clang zero-fills the temporary, which is why every local run passed; GCC leaves the padding as whatever was on the stack. The live and replay detectors run with different call histories (a logger between calls on one side, a reader on the other), so their stack contents differed and the two padding bytes differed. Consequence on the target platform: every fault event record in a Pi-recorded log would carry two garbage bytes, and two identical sessions would produce different logs.

This is the same defect class as Entry 4's uninitialized frame, one struct over, and it was caught by the same test the moment it ran on the other toolchain. The rule in FTS-DD-001 Section 9 is generalized: every struct that reaches the logger is value-initialized and then assigned.

Fix: `FaultDetector::record_event` is now the only place an event is constructed; it value-initializes `FaultEvent event{}` and assigns the four fields. Guard added: `FaultDetectionTest.EventPaddingBytesAreZero` reads the padding bytes of produced events directly through `offsetof` and asserts zero, so it fails on any toolchain that leaves them unspecified, without depending on stack coincidences. 106 tests.

### 5.3 Open

- Confirm the rerun on this commit is green on GCC and record its run number and coverage numbers here, replacing the macOS previews in Entries 1 through 4.
- Design decision recorded: replace implicit padding with explicit reserved fields at the next format version (FTS-DD-001 Section 12).
