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
| Exception-unwind edges on lines with a conditional, 6 sites (added in Entry 5) | `// LCOV_EXCL_EXCEPTION_BR_LINE` | GCC attaches unwind edges to `if`/`for`/`while` lines that build `std::string` temporaries or call the standard library; `--filter branch` cannot remove them there, and lcov 2.0's `no_exception_branch` setting is broken (Entry 5 §5.6). The marker removes only the exception edges; the line's real decision branches remain counted and are exercised by named tests (§5.4). |
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

### 5.3 Measurement of record — GitHub Actions run 34443038655, commit `2083343`, Ubuntu, GCC 13

First green run. This supersedes the macOS preview tables in Entries 1 through 4 as the coverage measurement of record; those entries remain as the per-commit history.

| Item | Value |
|---|---|
| Tests passed | 106 of 106 |
| Compiler warnings under `-Werror` | 0 |
| cppcheck findings | 0 |

| Scope | Branches (GCC) | Branches (Mac preview, Entry 4) | Threshold | Result |
|---|---|---|---|---|
| `src/processing/` | 98.6% | 100.0% | 90% | pass |
| `src/timing/` | 100.0% | 100.0% | 80% | pass |
| `src/logging/` | 80.6% | 83.3% | 80% | pass, 0.6 points of margin |
| `src/replay/` | 93.8% | 100.0% | 80% | pass |
| `src/drivers/` | 100.0% | 100.0% | 70% | pass |
| **Overall** | **91.6%** (174/190) | 93.8% (167/178) | 80% | **pass** |

Lines 99.7% (575/577), functions 97.4% (76/78). The two toolchains do not count the same things: GCC reports 190 branches and 577 lines where clang reported 178 and 670, because they differ in how they split compound conditions, attribute multi-line statements, and treat compiler-generated code. The threshold rule is evaluated against GCC only.

**Logging margin.** 80.6% against an 80% threshold means one more untaken branch in `src/logging/` fails the build. The GCC-only untaken branches are listed in 5.4 so the next logging change knows where it stands.

### 5.4 GCC-only untaken branches (from the run's coverage artifact)

Sixteen branch outcomes untaken, on eight lines, plus two closing braces GCC marks as unexecuted lines:

| File:line | Source | Marks | Reading |
|---|---|---|---|
| `log_reader.cpp:17` | `if (!header_is_valid(header_))` | `+-++` | both `if` outcomes taken (`ReaderRejectsBadMagic` and the happy path); the untaken edge is the exception path of the `std::string` assignment that follows |
| `log_reader.cpp:64` | `while (next(record))` | `+-++` | loop enters and exits; untaken edge is an exception path |
| `log_reader.cpp:76` | `for (... directory_iterator(directory, ec))` | `+-+-++` | iterator begin/end taken; two untaken edges are exception paths of iterator construction |
| `log_reader.cpp:78` | `if (name.rfind(prefix + "_", 0) == 0 && ... == ".bin")` | 24 outcomes, 10 untaken | both operands true and false are tested (`ListLogFilesIsSortedAndFiltered` has a wrong-prefix file and a wrong-extension file); the ten untaken edges belong to the `std::string` temporaries and comparisons on that line |
| `log_reader.cpp:22`, `:84` | `}` | line not executed | function-exit cleanup landing pads GCC emits for the exception path |
| `fault_detector.cpp:110` | `std::find_if(...)` | `+-` | both find/no-find outcomes taken by tests; untaken edge is the exception path through the iterator call |
| `log_replay_source.cpp:36` | `if (!reader_.has_value() && !open_next_file())` | `+++-++++` | all three real outcomes taken (`ReplaySpansRotatedFiles`, `EmptyFileListIsExhaustedImmediately`); untaken edge is an exception path |

Every real decision outcome on these lines is exercised by a named test. The untaken outcomes are compiler-generated exception-handling edges that `lcov --filter branch` removes on clang but not fully on GCC 13. This is the category Entry 1 §1.3 already excludes by policy; the tool is not applying the policy completely on the Linux toolchain. Consequence: `src/logging/` reads 80.6% for reasons unrelated to its tests, with 0.6 points of headroom before a build fails on noise.

### 5.5 Attempted fix for the noise, and its reversal

`--filter branch` removes branches only from lines that contain no conditional, so exception-unwind edges on `if`, `while`, and `for` lines survive it on GCC (5.4). lcov's documented setting for removing all identified exception branches is `no_exception_branch`, present in the runner's lcov 2.0 and the Mac's 2.5 (the newer `exception` filter keyword exists only in 2.5). Commit `6ef2010` added `--rc no_exception_branch=1` to the capture, remove, summary, and report steps, plus `--filter brace` for the closing-brace lines.

**Result: CI run 34444099506 failed.** Tests passed (106 of 106) and line coverage was reported (100%, 557 of 557 after the brace filter), but the branch summary read "no data found" and the threshold script found no branch data in any scope. On lcov 2.0 with GCC 13 data, the setting removed every branch, not only the exception ones. On the Mac (lcov 2.5, clang data) the same flags left the 178 branches untouched, which is why the local check passed. The runner's log does not print a per-stage branch summary, so whether the data was dropped at capture or at the remove step was not isolated.

**Reverted** in the following commit to the exact flags of run 34443038655. No threshold and no exclusion policy changed at any point. The workflow comment now says not to re-add the setting without a local lcov 2.0 reproduction. The threshold script now reports "NO BRANCH DATA" explicitly instead of printing "none%", so a tooling failure is not mistaken for a coverage result.

Lesson recorded: a coverage tooling change must be exercised on the toolchain that produces the measurement of record before it reaches the gate. Locally passing on clang and lcov 2.5 said nothing about GCC and lcov 2.0.

### 5.6 Resolution, reproduced offline on the runner's toolchain

The candidates were tried in an Ubuntu 24.04 container with GCC 13.3 and lcov 2.0 (the runner's versions) against the same commit. The baseline reproduced the runner exactly: 174 of 190 branches, 91.6%, logging 80.6%.

| Experiment | Result | Reading |
|---|---|---|
| Raw capture, no filter | 1766 of 5600 branches, 2035 of them flagged `e` (exception) | lcov 2.0 does identify exception edges; `--filter branch` keeps them on lines that contain a conditional |
| `--rc no_exception_branch=1` at capture | 35 of 46 branches | the setting discards almost every branch, not only exception ones; after the remove step, none remain. This is the wipe run 34444099506 saw |
| `--rc no_exception_branch=1` at remove only | no branch data | same |
| `--rc geninfo_no_exception_branch=1` | 35 of 46 | same defect under the geninfo-scoped name |
| `LCOV_EXCL_EXCEPTION_BR_LINE` on the six lines of 5.4 | **142 of 142, 100%**; every scope 100% | the marker lcov 2.0 documents for exactly this: exception edges on that line are dropped, the line's real decision branches stay counted |
| lcov 2.5 from source | build failed in the container | not pursued; the marker makes it unnecessary |

**Applied:** `// LCOV_EXCL_EXCEPTION_BR_LINE` on the six lines listed in 5.4, each with a one-line reason. This is the same class of justified exclusion as the `LCOV_EXCL_BR_LINE` markers on exhaustive `switch` statements (Entry 1 §1.3), applied where the tool's generic filter cannot. Rule going forward: a line that combines a conditional with a `std::string` temporary, a standard-library call, or a range-`for` over an iterator will show untaken exception edges on GCC; mark it the same way, with the reason, and list it here. If the markers proliferate, the systemic alternative is pinning lcov 2.5 in the workflow and using `--filter branch,exception`.

The workflow flags stay exactly as run 34443038655. The next run re-measures with the markers; its numbers replace 5.3 as the record.

### 5.7 Open

- Re-record 5.3 from the first green run with the markers.
- Design decision recorded: replace implicit padding with explicit reserved fields at the next format version (FTS-DD-001 Section 12).
- REQ-TIME-003 remains judged on the Pi (TC-009 step 7); CI measures jitter nowhere.
