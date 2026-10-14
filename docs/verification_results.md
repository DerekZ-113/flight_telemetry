# Flight Telemetry System — Verification Results

> **Document ID:** FTS-VR-001
> **Version:** 0.1.0
> **Status:** Draft
> **Last Updated:** 2026-10-12
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
| Exception-unwind edges on lines with a conditional, 7 sites (six added in Entry 5, the `for` over a YAML node in `telemetry_config.cpp` in Entry 11) | `// LCOV_EXCL_EXCEPTION_BR_LINE` | GCC attaches unwind edges to `if`/`for`/`while` lines that build `std::string` temporaries or call the standard library; `--filter branch` cannot remove them there, and lcov 2.0's `no_exception_branch` setting is broken (Entry 5 §5.6). The marker removes only the exception edges; the line's real decision branches remain counted and are exercised by named tests (§5.4). |
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

### 5.3 First green run — GitHub Actions run 34443038655, commit `2083343`, Ubuntu, GCC 13

*Superseded as the measurement of record by 5.8 after the exception-edge markers landed; kept as the record of what the runner reported before them.* This run superseded the macOS preview tables in Entries 1 through 4; those entries remain as the per-commit history.

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

### 5.7 Sequence of CI runs in this entry

| Run | Commit | Result | Why |
|---|---|---|---|
| 34442203740 | `5ec7027` | failed | `FaultEvent` padding differed between live and replay on GCC (5.2) |
| 34443038655 | `2083343` | green | padding fix; first GCC numbers, logging at 80.6% (5.3, 5.4) |
| 34444099506 | `6ef2010` | failed | `no_exception_branch` wiped all branch data on lcov 2.0 (5.5) |
| next | revert | green | flags restored, same numbers as 34443038655 |
| 34445017604 | `c69d13d` | green | six `LCOV_EXCL_EXCEPTION_BR_LINE` markers (5.6); record below |

### 5.8 Measurement of record — GitHub Actions run 34445017604, commit `c69d13d`, Ubuntu, GCC 13

Matches the container prediction in 5.6 exactly.

| Item | Value |
|---|---|
| Tests passed | 106 of 106 |
| Compiler warnings under `-Werror` | 0 |
| cppcheck findings | 0 |
| Lines | 99.7% (575 of 577) |
| Functions | 97.4% (76 of 78) |
| Branches | **100.0%** (142 of 142) |

| Scope | Branches | Threshold | Result |
|---|---|---|---|
| `src/processing/` | 100.0% | 90% | pass |
| `src/timing/` | 100.0% | 80% | pass |
| `src/logging/` | 100.0% | 80% | pass |
| `src/replay/` | 100.0% | 80% | pass |
| `src/drivers/` | 100.0% | 70% | pass |
| **Overall** | **100.0%** | 80% | **pass** |

The two uncovered lines are the closing-brace landing pads GCC reports in `log_reader.cpp` (5.4); line coverage is reported, not gated. The two uncounted functions are the compiler-generated destructor variants noted in Entry 1 §1.2. The branch denominator fell from 190 to 142 because the markers removed 48 exception edges on six lines, 16 of which were untaken and 32 taken; none was a decision.

### 5.9 Open

- Design decision recorded: replace implicit padding with explicit reserved fields at the next format version (FTS-DD-001 Section 12).
- REQ-TIME-003 remains judged on the Pi (TC-009 step 7); CI measures jitter nowhere.
- Rule for new code (5.6): a conditional on a line with a `std::string` temporary, a standard-library call, or an iterator range will show exception edges on GCC; mark it and list it here.

---

## Entry 6 — 2026-10-11, ZeroMQ transport commits

REQ-TRANS-001 and REQ-TRANS-003 at unit level; TC-005 and TC-010 step 7 executed by hand. Everything in this entry is a macOS preview; 6.7 is reserved for the CI measurement of record once the commits are pushed.

### 6.1 Test results

| Item | Value |
|---|---|
| Platform | macOS 26, Apple clang 21 (preview; CI run pending) |
| Build | `-DENABLE_COVERAGE=ON -DWARNINGS_AS_ERRORS=ON`, `-O0 -g`; libzmq 4.3.5 (Homebrew), cppzmq v4.11.0 (FetchContent, pinned) |
| Compiler warnings | 0 |
| Test suites | AltitudeTest (8), KalmanTest (8), ComplementaryTest (9), FaultDetectionTest (28), FaultInjectingSourceTest (4), SimulatedDataTest (5), SimulatedSourceTest (2), LoggingTest (22), ReplayTest (6), TimingTest (14), ZmqPublisherTest (8) |
| Tests passed | 114 of 114 |
| Replay identity with publishing on | `logs/telemetry_000.bin` (750 paced frames, published) and `logs/replay_000.bin` (unpaced replay of it, published as a burst) identical by `cmp`; the 5-frame default pair likewise |

### 6.2 Structural coverage (preview)

| Scope | Lines | Branches | Threshold (branch) | Result |
|---|---|---|---|---|
| `src/processing/` | 100.0% (279/279) | 100.0% (66/66) | 90% | pass |
| `src/drivers/` | 100.0% (125/125) | 100.0% (16/16) | 70% | pass |
| `src/logging/` | 100.0% (145/145) | 83.3% (55/66) | 80% | pass |
| `src/replay/` | 100.0% (43/43) | 100.0% (12/12) | 80% | pass |
| `src/timing/` | 100.0% (85/85) | 100.0% (18/18) | 80% | pass |
| `src/transport/` | 100.0% (39/39) | 100.0% (4/4) | 80% | pass |
| **Overall** | **100.0%** (717/717) | **94.0%** (171/182) | 80% | **pass** |

Functions 94.7% (126 of 133). The seven uncounted are the compiler-generated destructor variants of `DataSource`, `FaultInjectingSource`, `LogReplaySource`, `Clock`, and `MonotonicClock`, the same class of entry as Entry 1 §1.2; none is reachable code.

The 11 untaken branches are all in `log_reader.cpp`, the clang exception edges every Mac preview has shown since Entry 2 (Entry 3 counted five with that day's tools; this capture used lcov 2.5 with Xcode 26's `llvm-cov gcov`). The measurement of record for that file is 100% on GCC with the Entry 5 markers (§5.8). The transport scope has four branches, both outcomes of `if (!ok_)` and of `if (!sent)` in `ZmqPublisher::publish`, all taken: the first pair by `BindFailureIsReportedNotThrown`, the second by `SendErrorIsCountedNotThrown`. The constructor's and `publish()`'s `catch` blocks execute under the bind-failure, invalid-endpoint, and `ETERM` tests, so the scope needed no marker of any kind.

### 6.3 Exclusions

No new exclusion. The existing `lcov --remove` patterns already drop cppzmq (`build/_deps/`) and `zmq.h` (`/usr/*` on CI); this preview added the Mac's equivalents, `/opt/homebrew/*` and `/Applications/*`, to the same list. No source marker was added in `src/transport/`.

### 6.4 TC-005 and TC-010 step 7, executed by hand

Six runs on `tcp://127.0.0.1:5555` with `./build/telemetry` and `.venv/bin/python -m pipeline.receiver` (pyzmq 27.2.0, Python 3.13.0). The receiver counts a gap whenever consecutive frame timestamps differ by more than one period.

| Run | Procedure | Observed |
|---|---|---|
| a | Receiver started first, 300 ms settle, then `--zmq --cycles 500` (TC-005 steps 1 to 6, the log as the reference) | Publisher: 500 published, 0 send failures, `missed_cycles` 0 on all 500 lines, process exit 10.02 s after start. Receiver: 500 frames, timestamps 0 to 9980 ms, gaps 0. Log 52 516 bytes = 16 + 500 × 105. |
| b | No subscriber, `--zmq --cycles 500` (TC-010 step 7) | 500 published, 0 send failures, `missed_cycles` 0 on all 500 lines, exit at 10.01 s: no linger wait with an empty queue. Jitter mean 3221 µs against 3218 µs in run a: publishing to nobody and publishing to a subscriber cost the same, nothing measurable. |
| c | Receiver started 5 s into a `--cycles 750` run (slow joiner) | Receiver's first frame at 5080 ms; 496 frames, gaps 0 after joining. Publisher 750 published. |
| d | Receiver first; `--replay logs/telemetry_000.bin --zmq --cycles 750` (the log from run c) | 750 published in 0.04 s. Receiver's first frame at 3700 ms: 185 frames were sent before its 10 ms reconnect completed; 562 frames received, gaps 3 (the burst against the 100-message mark). `cmp` of the two logs silent: publishing changed nothing in the record. |
| e | A second instance on the same endpoint while the first ran `--cycles 150` | Second exits 1: `cannot bind ZeroMQ publisher to tcp://127.0.0.1:5555: Address already in use`. First unaffected: 150 published. |
| f | Default `./build/telemetry` then `--replay` (the README pair) | `cmp` silent. |

Jitter in runs a and b (min/mean/max 103/3218/15462 µs and 143/3221/15818 µs) is the macOS `nanosleep` timer slack recorded in Entry 3 §3.3, a preview that does not bear on REQ-TIME-003. What these runs show for REQ-TRANS-003 is that the publisher added no missed cycle and no measurable mean in either case.

### 6.5 Static analysis

cppcheck 2.21.0, same flags as CI: 0 findings. One `constParameter` style note on the new `parse_args` in `main.cpp` appeared during development and was resolved (`const char* const argv[]`) before this entry.

### 6.6 Open

- CI run of record for these commits: 6.7, and the README verification table, which cited run 34445017604 until Entry 10 §10.6. (6.7 was never written; the transport scope's first GCC measurement is Entry 10 §10.6, which also lists the runs that went unrecorded in between.)
- TC-005 automation and TC-010 step 6 (a subscriber stalled at loop level) wait for the pytest suite (REQ-TEST-002); REQ-TRANS-001 and REQ-TRANS-003 stay Partial until then. (Closed by Entry 10: `tests/integration/test_transport.py`, over both transports; both rows Verified.)
- The receiver's 10 ms reconnect interval is what lets a subscriber started first catch frame 0 of a paced run; a burst still loses what is sent inside that window (run d). A publisher-side start delay was considered and not added: no requirement asks for it, and the pytest suite can start the receiver first and wait for the first frame.

---

## Entry 7 — 2026-10-11, freshness gating commit

### 7.1 Finding: stale readings re-applied as measurements

`TelemetryProcessor::process` gated each filter update on channel status alone and read `read_ok` nowhere. A frame whose read failed inside the timeout window (`read_ok` false on a channel still NOMINAL), or a 1 Hz GPS on the 49 cycles between fixes, carries the previous sample, and the processor applied it as a new measurement every cycle. A Kalman filter cannot tell a copy from a new sample: each update multiplies the altitude variance by (1 − k), so one reading applied N times shrinks the covariance as if N sensors had agreed and pins the state to a value that is no longer current. With the project's constants, a 1 Hz fix applied 50 times carries the information weight of R/50 = 0.5 m², a 0.7 m sensor in place of the 5 m sensor it is; a 500 ms barometer dropout re-applied 25 times leaves the altitude variance at 0.061 m² where prediction alone gives 0.101 m², so the next real reading is trusted with gain 0.020 instead of 0.033; in a 2 m/s climb the stale window leaves the estimate 0.25 m low with the velocity at 1.875 m/s, where prediction alone is right to the millimeter. The complementary filter had the same gate: a stale IMU frame moved the attitude, and a gap of stale frames lost its rotation.

No test could see it. The simulator produced a fresh GPS fix on every frame, so the pipeline never met a stale NOMINAL reading in a test; `ReplayTest.ReplayReproducesLiveSession` injects a 40-frame barometer dropout and passed, because live and replay misbehaved identically. The defect was found by reading the processor against FTS-DD-001 §7's definition of `read_ok`, not by a failing test, which is the gap the simulator change closes: from now on every test that runs the simulator meets the real GPS cadence.

Fix (REQ-PROC-006, FTS-SRD-001 0.3.0): `process()` predicts on every cycle the filter exists and applies a channel's reading only when the channel is NOMINAL and the reading is fresh; the gyroscope integrates over the time since the last accepted IMU sample (`imu_last_fresh_ms_`); the Kalman filter is constructed on the first fresh barometer frame rather than on the first frame, because a real driver reports `read_ok` false until its first successful transaction. The simulator fixes GPS once per second (REQ-SENS-006 reworded), holding the fields with `gps_read_ok` false between fixes.

Guards (TC-011): six processor tests written before the fix and failing on the old code, plus two simulator tests.

| Test | Old code | Required |
|---|---|---|
| `ComplementaryTest.StaleImuFrameHoldsAttitude` | pitch moved 0.475° → 1.366° on a stale frame carrying 45° of tilt | unchanged |
| `ComplementaryTest.GyroIntegratesOverStaleGap` | 0.0196° (one 20 ms step) | 0.0784° (the 80 ms gap) |
| `KalmanTest.StaleBaroInsideTimeoutIsPredictOnly` | fused 2.1776 m against predict-only 2.1830 m on the first stale frame; velocity pinned at 0.006 m/s while predict-only reached 0.034 m/s | identical to the DEGRADED twin |
| `KalmanTest.StaleGpsIsNotApplied` | a stale 9999 m GPS field pulled the fused altitude to 67 m, then 128 m | identical to the held fix |
| `KalmanTest.OneGpsUpdatePerFix` | frame 1 already differed from the explicit schedule (1.8797 against 1.9270 m) | identical; parts from the first-fix-only twin exactly at the second fix |
| `KalmanTest.FilterWaitsForFirstFreshBaro` | filter born at 2.134 m from an unmeasured pressure | 0 until the first fresh reading, then that reading |
| `SimulatedDataTest.GpsFixOncePerSecond`, `GpsIntervalIsConfigurable` | every frame a fix; no rate parameter | fixes at 0 and 1000 ms only; period configurable |

### 7.2 Re-baselined numbers

GPS noise is no longer drawn on the 490 frames between fixes, so the seed-42 sequence shifts from frame 1 on. No test asserted a number; these records did, and were updated (FTS-DD-001 §5, the `ConvergenceReducesNoise` comment, the Kalman knowledge-base note):

| Quantity (seed 42, 20 ms, 500 frames) | Before | Now |
|---|---|---|
| Barometric altitude sigma / mean | 1.647 / 1.961 m | 1.734 / 2.058 m |
| Fused altitude sigma / mean | 0.244 / 2.060 m | 0.228 / 2.038 m |
| Fused over barometric sigma (TC-007 step 5 bound 0.5) | 0.15 | 0.13 |
| GPS updates in 500 frames | 500 | 10 (mean 2.716 m) |
| Vertical speed startup transient | peak near 1 m/s around frame 49, settling near 0.05 m/s | peak 0.78 m/s at frame 31, settling 0.04 m/s |

### 7.3 Test results (preview)

| Item | Value |
|---|---|
| Platform | macOS 26, Apple clang 21 (preview; CI run pending) |
| Compiler warnings | 0 |
| Test suites | AltitudeTest (8), KalmanTest (12), ComplementaryTest (11), FaultDetectionTest (28), FaultInjectingSourceTest (4), SimulatedDataTest (7), SimulatedSourceTest (2), LoggingTest (22), ReplayTest (6), TimingTest (14), ZmqPublisherTest (8) |
| Tests passed | 122 of 122 |
| Replay identity | the default 5-frame pair and a 100-frame pair identical by `cmp`; the unit-level identity test's dropout now contains 25 stale-NOMINAL frames and still reproduces |
| End to end | 100 frames published to the Python receiver: 100 received, gaps 0, every channel NOMINAL on every frame |

### 7.4 Structural coverage (preview)

| Scope | Lines | Branches | Threshold (branch) | Result |
|---|---|---|---|---|
| `src/processing/` | 100.0% | 100.0% (`processor.cpp` 20 of 20) | 90% | pass |
| `src/drivers/` | 100.0% | 100.0% (`simulated_data.cpp` 6 of 6) | 70% | pass |
| `src/logging/` | 100.0% | 83.3% (55 of 66) | 80% | pass |
| `src/timing/`, `src/replay/`, `src/transport/` | 100.0% | 100.0% | 80% | pass |
| **Overall** | **100.0%** (739 of 739) | **94.4%** (187 of 198) | 80% | **pass** |

The 11 untaken branches are the `log_reader.cpp` clang edges of every Mac preview (Entry 6 §6.2). `processor.cpp` now carries 20 branches, each `&&` operand and each `if` of the gate among them, all taken: the false side of every `*_fresh` by the stale tests, the DEGRADED side by the existing status tests, the deferred-construction path by `FilterWaitsForFirstFreshBaro`. No marker was added.

### 7.5 Static analysis

cppcheck 2.21.0, same flags as CI: 0 findings.

### 7.6 Open

- CI run of record for this commit, and for the ZeroMQ commits if theirs has not been recorded.
- On recovery from a long DEGRADED span the complementary filter integrates one rate sample over the whole span (FTS-DD-001 §6); a re-seed from the accelerometer after a long gap would be a refinement, not a correction, and is not planned.
- The GPS driver (Week 2 map, Day 14) must set `gps_read_ok` only on a cycle in which a sentence completed; that is now what the pipeline assumes, and the simulator models.

## Entry 8 — 2026-10-12, live sensor source commits (I2C seam, BMP280 and MPU6050 drivers, `LiveSensorSource`, gyro-gap re-seed)

Built and verified on the Mac before the first hardware day; §8.6 is reserved for the on-target results. The driver's name, the barometer address, and the IMU identity are provisional until the chips are read (FTS-DD-001 §12).

### 8.1 Finding: one rate sample integrated across an outage (supersedes Entry 7 §7.6)

Entry 7 §7.6 recorded that on recovery from a long DEGRADED span the complementary filter integrates one rate sample over the whole span, and judged a re-seed from the accelerometer "a refinement, not a correction, and not planned". Reviewing that for the live source changed the judgment. The accepted sample's rate is multiplied by the whole gap with no bound, and a real MPU6050's uncalibrated gyroscope offset is several degrees per second, so a 10 s outage would put tens of degrees of false tilt on the display at the moment of recovery, which the accelerometer term would take about a time constant to undo. That is a defect for the hardware the filter is about to meet, not a refinement. REQ-PROC-002 gained a second sentence (FTS-SRD-001 0.4.0): a gap longer than the IMU communication timeout re-initializes pitch and roll from the accelerometer on the next accepted reading. `ComplementaryFilter::reset()` drops the seed and keeps the angles; `TelemetryProcessor` takes the timeout (default 500 ms, pinned to `FaultDetectorConfig::i2c_timeout_ms` by a `static_assert` in `main.cpp`, where one config object now feeds the detector and the processor) and resets the filter before an update whose gap exceeds it, strictly greater like the detector's own comparison.

Failing values on the old code (TC-006 steps 5 and 7): a 5 deg/s sample accepted after a 520 ms gap of stale frames read 2.548° of pitch where 0° was measured; the same frame carrying 45° of accelerometer tilt read 3.448° where 45° was measured. Step 6, the boundary, passed before and after the change: a gap of exactly 500 ms integrates to 2.45°.

Two asymmetries are recorded in FTS-DD-001 §6: the detector judges a timeout at the failing frame and the processor at the next accepted one, so a dropout the detector tolerated followed by a good read at 520 ms re-seeds although the channel never left NOMINAL; and the processor's gap includes DEGRADED time, so any DEGRADED span longer than the timeout re-seeds on the first NOMINAL frame after recovery, whatever the fault was.

### 8.2 Drivers verified against the manufacturers' arithmetic

**Barometer (TC-012).** The worked example on page 23 of BMP280 datasheet revision 1.14 is reproduced by the driver's compensation: t_fine 128422, temperature 2508 (25.08 °C), pressure 25767233 in 1/256 Pa, which is 100653.254 Pa against the page's double-precision 100653.27 (the page prints the integer 25767236 and notes that the integer result may deviate from the double by rounding). Through the register protocol, with the example's bytes served in the chip's real order (the page's measurement table swaps UT and UP), the driver reports 1006.5325 hPa and 25.08 °C. The datasheet's signed left shifts became multiplications by the same powers of two (shifting a negative value left is undefined behavior in C++17, and the example's own `var1 * dig_P2` is negative); the right shifts stay as written, and this entry's clang result equals the datasheet's, with GCC's to be confirmed by the CI run. For the drill: `var1 * var1 * dig_P6` overflows int32 once |var1| exceeds 17,515, which with the example's trimming set is any temperature outside 21.6 to 28.4 °C; the example at 25.08 °C has var1 = 422 and would pass in int32 by luck, while `(1048576 − adc_P) × 2^31` is 1.36 × 10^15 and never fits.

**MPU6050 (TC-013).** Sensitivities from the register map, 16384 LSB/g and 131 LSB per deg/s: 0x4000 → 9.81 m/s², 0x0083 → 1.000 deg/s, the negatives through the int16 cast; swapped bytes give 0.0383 m/s² and 246.2 deg/s, never the right number with the wrong sign. Divider: 100 Hz → 9, 50 → 19, 8 → 124, 4 → 249, 1000 → 0; 1, 3, 30, 0, −50, and 2000 refused, which is the arithmetic behind REQ-SENS-003's new 4 Hz floor.

**Seam (TC-014 steps 1 to 4).** The fake's semantics, and `LinuxI2cBus` refusing `/dev/i2c-99` and failing a transaction on `/dev/null`. On the Mac both go through the ENOTSUP stub; the Linux branch of `i2c_bus.cpp` compiles for the first time on the CI runner, which is also the first time its open-failure and ioctl-failure paths execute.

### 8.3 Test results (preview)

`ctest`: 157 of 157 pass, Apple clang on macOS.

| Suite | Tests |
|---|---|
| AltitudeTest | 8 |
| KalmanTest | 12 |
| ComplementaryTest | 14 (3 new: TC-006 steps 5 to 7) |
| FaultDetectionTest | 28 |
| FaultInjectingSourceTest | 4 |
| SimulatedDataTest | 7 |
| SimulatedSourceTest | 2 |
| LoggingTest | 22 |
| ReplayTest | 6 |
| TimingTest | 14 (the `FakeClock` moved to `tests/unit/fake_clock.h`, shared) |
| ZmqPublisherTest | 8 |
| FakeI2cBusTest | 4 (new) |
| LinuxI2cBusTest | 3 (new) |
| Bmp280Test | 9 (new) |
| Mpu6050Test | 10 (new) |
| LiveSensorSourceTest | 6 (new) |
| **Total** | **157** |

End to end on the Mac: the default 5-cycle run and its replay `cmp` identical; `telemetry --live` exits 1 with `/dev/i2c-1: I2C device files are not available on this platform`; `--live` with `--replay` is refused as a usage error.

### 8.4 Structural coverage (preview)

macOS, Apple clang, `llvm-cov gcov` through lcov 2.5 with the workflow's flags:

| Scope | Lines | Branches | Threshold (branch) | Result |
|---|---|---|---|---|
| `src/processing/` | 100.0% | 100.0% | 90% | pass |
| `src/drivers/` | 100.0% | 100.0% (`bmp280.cpp` 26 of 26, `mpu6050.cpp` 28 of 28, `live_sensor_source.cpp` 14 of 14; `i2c_bus.cpp` is the stub here and carries no branches) | 70% | pass |
| `src/logging/` | 100.0% | 83.3% (55 of 66) | 80% | pass |
| `src/timing/`, `src/replay/`, `src/transport/` | 100.0% | 100.0% | 80% | pass |
| **Overall** | **100.0%** (986 of 986) | **95.9%** (257 of 268) | 80% | **pass** |

The 11 untaken branches are the `log_reader.cpp` clang edges of every Mac preview (Entry 6 §6.2). Two edges in the live source's constructor were untaken on the first measurement: clang's exception landing pads on `if (!baro_.init())` and `if (!imu_.init())`, the pattern Entry 5 §5.6 describes on GCC. The calls were moved onto their own lines, which also lets one startup message name both chips, and the file measures 14 of 14 without a marker. The barometer's division guard (`var1 == 0`, an all-zero calibration) is reached by a test rather than excluded. Expected on the CI run of record: the Linux branch of `i2c_bus.cpp` will show about six untaken edges, the successful write and read paths after `I2C_SLAVE` that only a bus can take; they are the hardware error paths FTS-TP-001 §6.2 names, to be listed here when the run exists, not marked.

### 8.5 Static analysis

cppcheck 2.21.0, same flags as CI: 0 findings. Two style findings during the build were resolved by restructuring rather than suppressed: `knownConditionTrueFalse` on the divider's bounds check (now explicit rate bounds, 4 to 1000 Hz) and `useStlAlgorithm` on the MPU6050's configuration loop (now five short-circuited writes through one helper).

### 8.6 On-target results

Reserved for the first hardware day: the Pi's OS, kernel, and GCC; the chip IDs (TC-014 step 11) against the provisional constants; TC-012 step 8 with the real chip's dump; the first live frames and the tilt signs (TC-014 steps 12 and 13); the replay of a live log and frames to the Mac (steps 14 and 15); TC-002 step 8, the wire pull, with the four events, their timings, the re-seed on IMU recovery, and the missed-cycle count; and TC-009 step 7's timing numbers. The noise capture moved to 2026-10-13 and is recorded in Entry 9 §9.6.

### 8.7 Open

- CI run of record for these commits, and for the ZeroMQ and freshness commits if theirs are not yet recorded.
- The Linux branch of `src/drivers/i2c_bus.cpp` has not been compiled on this Mac; its first compile is the CI runner's, with GCC and `-Werror`. A warning there is a finding for this entry.
- The BME280 rename and any address change, if the chip IDs read on the Pi differ from the provisional values (FTS-DD-001 §12).
- FAULT-010's positive-rail gap: at ±2 g the count 32767 converts to 19.6194 m/s², below the 19.62 limit the detector tests with `>=`, so positive saturation never faults. Parked for the faults topic.
- FAULT-004's single bus-level event: the bus's `errno` stops at the live source. Parked for the faults topic.
- The GPS driver (Week 2 map, Day 14) must set `gps_read_ok` only on a cycle in which a sentence completed (carried from Entry 7).

---

## Entry 9 — 2026-10-12, log reader, replay-identity pytest, noise analysis oracles (macOS preview)

The Python side of REQ-LOG-003 and the measurement behind REQ-PROC-004, built and verified against simulated logs before the stationary capture exists; §9.6 is reserved for the capture of 2026-10-13. No C++ changed beyond a comment in `tests/unit/test_replay.cpp`.

### 9.1 What was built

- `pipeline/log_reader.py`, the Python twin of `LogReader`: files, the torn-tail rule (complete records kept, the remainder counted), rotated sequences as one run (consecutive indices, increasing timestamps, a boundary step under a second), a numpy layout derived from the struct format with every offset asserted at import, and a summary command that runs on the Pi without the venv.
- `tests/integration/`: `conftest.py` (the binary at `build/telemetry` or `$TELEMETRY_BIN`, failing rather than skipping when absent; one 500-frame seed-42 session and its replay per run, in a temporary directory), `synthetic_log.py` (logs written with the same Structs), `test_log_reader.py` (18 tests, TC-008 steps 8 to 14), `test_replay_identity.py` (6 tests, TC-004 steps 3 to 6), `test_noise_profile.py` (26 tests, TC-015 steps 1 to 8). `pytest.ini`; `requirements.txt` pins numpy 2.5.4, matplotlib 3.11.2, pytest 9.1.1 beside pyzmq.
- `analysis/noise_profile.py`: per-channel mean, population sigma, overlapping Allan deviation, lag-1 autocorrelation, quantization step, longest identical run, first-minute and last-minute means; the ISA slope; the gyro bias and its 0.98 × b tilt; the accelerometer offsets; the five constants each marked measured, retained, or chosen by replay; the pipeline's outputs after a settle window and the startup transient; the jitter CSV; Markdown, JSON, four figures.
- `docs/noise_profile.md` 0.1.0 (FTS-NP-001): the method, the estimators, the expectations and decision rules written before the data, the provenance table with blanks. REQ-PROC-004 reworded (FTS-SRD-001 0.5.0): measurement variances measured, the initial altitude variance equal to the barometric one, process noise and the initial velocity variance documented tuning choices. The map's recipe for process noise (the variance of the accelerometer's own noise) was rejected with a number: at a barometric variance of 0.07 m² the filter would see 90% of a 1 m/s climb after 9.7 s instead of 1.4 s, because that formula belongs to an IMU-driven predict step this filter does not have.
- The R rule gained its second branch the same evening, at Derek's question: ADEV(τ₀)² = σ²(1 − ρ₁), so positively correlated samples would make it understate the per-sample variance and leave the filter overconfident. Decided by ρ₁ of the linearly detrended barometric altitude against 3/√N: white, R = ADEV(τ₀)²; significantly positive, R = the detrended variance, which removes slow drift without understating correlated noise; significantly negative, or pressure repeats far beyond float32's chance count, the analysis stops. The first draft had stated the ratio σ/ADEV(τ₀)'s direction wrongly (positive correlation raises it, like a trend; it does not lower it) in FTS-NP-001, the plan, and the notes; corrected in all of them. Two tests pin the new branch: AR(1) noise of ρ₁ = 0.3 plus drift recovers the true variance within 3% where ADEV(τ₀)² understates it by 30%, and anti-correlated samples and repeated conversions each raise their warning.

### 9.2 Oracles (TC-015 steps 1 through 8, this Mac)

Synthetic logs of 45,000 frames at 20 ms from numpy's seeded generator: every planted sigma recovered within 3/√(2N) = 1.0%, every mean within 3σ/√N, a planted gyro_y bias of 2.5 deg/s recovered with its predicted tilt 2.45°; the Allan deviation of white noise follows τ^−½ within 10% at m = 1, 4, 16, 64, 256; white noise of sigma 1 plus a drift rising √12 over the run gives a plain sigma of √2 within 2% while ADEV(τ₀) stays 1 within 1% and ADEV at 225 s equals dτ/√2 within 10%; a planted run of ten identical samples reads as 10 and the accelerometer's LSB grid returns its step within 1%; held copies with `read_ok` false enter neither n nor the runs.

The binary's own 500-frame log: pressure, temperature, accelerometer and gyroscope sigmas within 15% of the simulator's literals; means centered; ten fresh GPS fixes; the barometric sigma in metres equal both ways within 1%; ADEV(τ₀)²/σ² within [0.85, 1.15] and |ρ₁| under 0.15 on every channel; no identical run over 3; the derived barometric variance within 25% of 2.774 m² = (0.2 hPa × 8.327 m/hPa)². The 3.0 m² placeholder was that number about 1.3 standard errors high.

Preview on a 1500-frame simulated run (30 s, seed 42):

| Channel | Sigma | ADEV(τ₀) | σ/ADEV | Simulator sigma |
|---|---|---|---|---|
| pressure_hpa | 0.2023 hPa | 0.1999 | 1.01 | 0.2 |
| baro_altitude_m | 1.684 m | 1.664 | 1.01 | 1.665 |
| accel_x / y / z | 0.0494 / 0.0498 / 0.0501 m/s² | 0.0487 / 0.0493 / 0.0498 | 1.01 | 0.05 |
| gyro_x / y / z | 0.1009 / 0.1021 / 0.0987 deg/s | 0.102 / 0.100 / 0.099 | 0.99 to 1.02 | 0.1 |

Derived: R_baro 2.77 m², the ISA slope −8.327 m/hPa, gyro bias under 0.005 deg/s, longest run 1 everywhere, fused altitude sigma 0.20 m after 5 s against 1.68 m raw.

### 9.3 Finding: the startup transient belongs to the initial velocity variance

FTS-DD-001 §5 recorded a startup transient of about 1 m/s in vertical speed on a stationary simulated board and attributed it to the initial velocity variance, to be revisited here. The preview measures it at 0.78 m/s at 0.62 s (frame 31). A Monte Carlo of the filter as written shows the measured barometric variance will not remove it (median peak 0.74 m/s today, 0.67 m/s at R = 0.07 m²) and that an initial velocity variance of 0.01 (m/s)² would (0.17 m/s), at the cost of seeing a vehicle already climbing at birth about a second later. The constant is chosen by replaying the capture through 1.0, 0.1 and 0.01 under the rule in FTS-NP-001 §3.5, written before the data.

### 9.4 Test results (preview)

| Item | Value |
|---|---|
| Platform | macOS, Apple clang, Python 3.13.0 |
| C++ suite | unchanged: 157 of 157 |
| pytest | 50 of 50 in 14 s, of which 10 s is the one simulated session |
| TC-004 steps 3 to 6 | 500 frames in each log, zero field differences, the files byte-identical, every frame's bytes equal to its repacking (padding zero), timestamps 0 to 9980 ms |
| TC-008 steps 8 to 14 | every header corruption refused naming the field; a 41-byte torn tail keeps the first frame and counts 41; restart, index gap, large step, and repeated timestamps refused; offsets 0, 8, …, 56, …, 97 equal to the struct's |
| Reader on the 5-frame and 1500-frame logs | 5 frames 0 to 80 ms; 1500 frames 0 to 29,980 ms, step 20 ms, 30 fresh GPS fixes |

### 9.5 Coverage and static analysis

No C++ source changed; the C++ figures are Entry 8's. No Python coverage policy exists (FTS-TP-001 §4 criterion 3 scopes coverage to `src/`), so none is claimed.

### 9.6 On-target results

Reserved for the capture of 2026-10-13 (TC-015 steps 9 to 12): the provenance, the reader's summary (45,000 frames in five files, one GPS event, zero torn bytes), the measured table and the derived constants, the before/after replay of file 000 with the initial-velocity-variance candidates, the re-baselined seed-42 numbers after the simulator's sigmas change, the C++ suite and pytest after the constants commit, cppcheck, and the coverage preview.

### 9.7 Open

- The capture and everything in §9.6.
- Python in CI: the suite runs locally; the CI topic adds the step (FTS-TP-001 §7). (Closed by Entry 10: the "Integration tests" step.)
- `--replay` takes one file, so a rotated capture is replayed one file at a time (FTS-DD-001 §12).
- A stale rotated file from an aborted earlier capture would join a later run's sequence seamlessly; the frame total, the mtime warning, and clearing `logs/` before the capture are the guards (FTS-DD-001 §3).
- The BMP280 datasheet's per-mode noise figure at ×4 oversampling is still to be confirmed for FTS-NP-001 §3.1.

---

## Entry 10 — 2026-10-12, UDP transport, the transport pytest, the suite in CI (macOS preview)

REQ-TRANS-002 at unit and process level, REQ-TRANS-003 over both transports, REQ-TEST-002 with TC-005 and TC-010 automated, and the pytest suite in the workflow. The first CI run with the "Integration tests" step is the measurement of record and goes in §10.6; everything else here is a macOS preview. No hardware.

### 10.1 What was built

- `src/transport/publisher.h`: the `Publisher` seam (`publish`, `stats`, a virtual destructor) and `PublishStats`. `run()` in `main.cpp` takes `const std::vector<Publisher*>&` and never learns which transports it has; `--zmq` and `--udp` may be combined, one `Transport:` line each at exit.
- `src/logging/log_format.h/.cpp`: `kFrameMessageBytes` and `frame_message()`, the 121-byte one-record log file both publishers send; `ZmqPublisher::publish` now calls it, its eight tests unchanged; `LoggingTest.FrameMessageIsHeaderTagAndFrame`.
- `src/transport/udp_publisher.h/.cpp`: the port (1 to 65535) and the IPv4 dotted quad checked before any system call, `INADDR_ANY` and `INADDR_BROADCAST` refused by name, `socket`, `connect` (an unroutable destination is refused at startup), `send` with `MSG_DONTWAIT`, every refusal counted with its errno and never retried, the descriptor closed in the destructor; no platform split. `main.cpp` parses `--udp <ipv4>:<port>` at the last colon and exits 1 with the reason when the publisher refuses the destination. Ten tests in `test_udp_publisher.cpp` (TC-005 unit steps, TC-010 steps 8 to 12), among them `SocketFailureIsReportedNotThrown`, which lowers `RLIMIT_NOFILE` to zero around the construction so `socket()` failing is a test rather than an exclusion.
- `pipeline/receiver.py`: `ZmqSource` (pyzmq imported inside it) and `UdpSource` (standard library, runs on the Pi without the venv) behind one `FrameReceiver`; `FrameAccounting` gains `out_of_order`; `--udp HOST:PORT`; the exit line reports gaps and out-of-order frames.
- `tests/integration/test_transport.py` (16 tests: four on the accounting without a binary, TC-005 and TC-010 steps 6 and 7 over each transport, five startup refusals, both transports at once) and the `run_published` harness in `conftest.py` (the binary's stdout to a file, the receiver read in the test thread, collection until the process exits and the receiver has been quiet for one timeout).
- `.github/workflows/ci.yml`: `actions/setup-python@v5` on 3.13 with the pip cache, `pip install -r requirements.txt`, "Integration tests" between "Unit tests" and "Coverage report" (section 10.3 on why there), `timeout-minutes: 20`.
- Documents: FTS-SRD-001 0.5.1 (the TRANS-002 rationale; one sentence in TRANS-003's); TC-005 and TC-010 over both transports (section 10.4 on what changed in them); FTS-TM-001 19 Verified, 11 Partial, 3 Not started, TRANS-002 Partial for REQ-CFG-001 alone, since the SRD defines *configurable* as its configuration file; FTS-DD-001 §9 (the second transport, the seam, the kernel's buffers between the high-water marks), §10, §12 (the sequence-number deferral the Week Plan already cited); FTS-TP-001 §2.2, §6.1, §7.

### 10.2 Test results (preview)

| Item | Value |
|---|---|
| Platform | macOS 27.2, Apple clang, Python 3.13.0, pyzmq 27.2.0 / libzmq 4.3.5 |
| C++ suite | 168 of 168 (157, plus one logging test, plus ten UDP tests); the ten UDP tests passed 30 of 30 repeats |
| pytest | 66 of 66 in 52 s (50 in 14 s before); `test_transport.py` 16 in 38 s, of which the two 10 s stall runs are the cost |
| TC-005, 200 frames | ZeroMQ: join index 0, 200 received; UDP: 200 received; every frame byte-identical to the log, gaps 0, out of order 0; 200 published and 0 refused on both |
| TC-010 step 6, 500 frames, a 3 s stall after 50 | ZeroMQ: 500 received, gaps 0 (the kernel's TCP buffers held the stall); UDP with a 4 KB receive buffer: 373 received, gaps 127 of the 150 stalled frames; missed cycles 0 on both; 500 published, 0 refused on both |
| TC-010 step 7, 50 frames, nobody listening | ZeroMQ: 50 published, 0 refused; UDP: 41 published, 9 refused with ECONNREFUSED (the port-unreachable the kernel reports on later sends, about one per 100 ms here); exit 0 and 50 frames in the log on both |
| Startup refusals | `--udp not-an-address`, `127.0.0.1:99999`, `0.0.0.0:5556`, `localhost:5556`, `--zmq not-an-endpoint`: exit 1, the reason named, no frame logged |
| Both transports at once, 100 frames | both receivers complete and byte-identical to the log; two `Transport:` lines, 100 published and 0 refused each |
| cppcheck 2.21.0 | 0 findings |

### 10.3 Structural coverage (preview)

macOS, Apple clang, `llvm-cov gcov` through lcov 2.5 with the workflow's flags, captured twice: after the unit tests alone, and after the pytest suite was run against the same instrumented binary (`TELEMETRY_BIN=build-cov/telemetry`).

| Scope | Branches, unit tests | Branches, unit tests plus pytest | Threshold | Result |
|---|---|---|---|---|
| `src/processing/` | 100.0% | 100.0% | 90% | pass |
| `src/timing/` | 100.0% | 100.0% | 80% | pass |
| `src/logging/` | 83.3% | 83.3% | 80% | pass |
| `src/transport/` | 89.3% (25 of 28) | 89.3% | 80% | pass |
| `src/replay/` | 100.0% | 100.0% | 80% | pass |
| `src/drivers/` | 100.0% | 100.0% | 70% | pass |
| Overall | 95.2% | 95.2% | 80% | pass |

Per file in the transport scope: `zmq_publisher.cpp` 4 of 4, `udp_publisher.cpp` 21 of 24, and `log_format.cpp` 8 of 8. The three untaken branches in `udp_publisher.cpp` are at two lines: the `connect` conditional (line 45), whose failure outcome macOS cannot reach because it routes the loopback broadcast like unicast (section 10.5; the runner's kernel did the same, so GCC did not reach it either, §10.6), together with its clang unwind edge; and one clang unwind edge at the `inet_pton` conditional (line 25), whose two logical outcomes are both taken (five addresses refused, three accepted). These are the clang exception edges every Mac preview has shown since Entry 2; the record is GCC's (§10.6): 21 of 22, the `connect` failure untaken there too, no marker. The logging scope's 83.3% is Entry 8's figure (`log_reader.cpp`'s clang edges, 100% on GCC with the Entry 5 markers).

The pytest runs did merge into the counters (the UDP publisher's line execution count rose from 17,169 to 24,106 between the two captures) and took no branch the unit tests had not already taken: every file's branch set is identical in the two captures. Placing the step before the capture is therefore a measurement choice with no effect on today's figures, documented in FTS-TP-001 §6.1; it would matter for a branch reachable only through a real run of `main`, and `main.cpp` is excluded by design (§6.2).

### 10.4 TC-005 and TC-010: what the cards got wrong, and the measurements that corrected them

- **TC-005 step 1** named the wrong mechanism. A settle delay before starting the publisher cannot act on a publisher that does not exist yet; what catches frame 0 is the subscriber's reconnect interval against the loop's 20 ms first deadline (Entry 6 §6.6 had it right). Measured 2026-10-12 on this Mac with a PUB binding late and the first frame exactly 20 ms after the bind, 40 trials each: at the shipped 10 ms reconnect interval the subscription reached the publisher 2.4 to 22 ms after the bind (median 9.5) and frame 0 was lost 3 times; at 1 ms, 1.2 to 4.7 ms (median 1.7) and never. Entry 6's hand run (500 of 500) was the 92% case. **Step 5** now asserts exactly 200 over UDP, where a bound socket has no join, and over ZeroMQ contiguity from the first frame received with the join bounded to five frames and recorded; the pytest's receiver reconnects every 1 ms (`ZmqSource(reconnect_ivl_ms=1)`), the tool's default stays 10 ms. REQ-TRANS-001's rationale already says a late join is not loss.
- **TC-010 step 6** expected "gaps greater than 0" from a 3 s stall. Over `tcp://` the kernel's TCP buffers on both ends sit between the two ZeroMQ high-water marks and hold minutes of 121-byte frames (a 3 s stall is 18 KB), so the expectation would have failed against correct code: the first run received 500 of 500 across the stall. The step now asserts the requirement (published 500, missed cycles under 75 against the 150 a blocked 3 s send would cost, contiguity after the stall, out of order 0) and records the gaps. The UDP variant shrinks the receiver's kernel buffer to 4 KB and asserts the drop on Linux, where the limit is strict, recording it on macOS, where the kernel held 22, 104 and 301 of 1000 datagrams past that buffer in different runs. The drop at the high-water mark stays proven at unit level over `inproc://` (step 4), where there are no kernel buffers.
- **TC-010 step 11**, the UDP stalled receiver, asserts "at least 1 and fewer than 1000" delivered of 1000 for the same reason; the plan's "at most 100" failed 2 of 40 repeats on this Mac.
- **TC-010 step 12** expected a directed broadcast to be refused at connect (Linux) or at the first send; macOS routes `127.255.255.255` to `127.0.0.1` like unicast (its route table lists the address as a host route on `lo0`) and delivers it, so the step accepts that third outcome, and the limited broadcast `255.255.255.255` is refused by name in step 8 on both kernels.
- **TC-010 step 7** is a pytest step at 50 frames per transport; the 500-frame manual run of Entry 6 stands.

### 10.5 Socket behaviour measured on this Mac, the design's inputs

| Case | Result |
|---|---|
| Connected datagram socket, nobody bound on the loopback port, six sends 10 ms apart | ok, ECONNREFUSED, ok, ok, ok, ok: one refusal per ICMP, reported on the next send, that datagram not transmitted |
| Same, 20 sends 5 ms apart; 1000 back to back; sends 50 ms apart | 2 refused; 1 refused (on the send after the burst); every one refused: macOS suppresses an unreachable that follows another within about 50 ms on the same socket |
| Unconnected `sendto` to the same dead port, four sends | no error, ever |
| Never-reading receiver, `SO_RCVBUF` 2048, 500 sends | 500 accepted at the sender in 2.3 ms; 11 kept |
| 150 datagrams (a 3 s stall at 50 Hz) into a default receiver (786,896 bytes) | all 150 held |
| 1000 datagrams into a 4 KB receiver | 22, 104 and 301 kept in different runs; no error at the sender |
| `connect` to 255.255.255.255 without `SO_BROADCAST`, then `send`, 30 fresh sockets, default SIGPIPE | `send` EACCES 30 of 30, no signal; an unconnected `sendto` the same |
| `connect` to 127.255.255.255, 10.255.255.255, 192.168.255.255 | ok, and the send succeeds: routed like unicast |
| `connect` to 0.0.0.0 then `send` | arrives on 127.0.0.1 (the reason the publisher refuses it by name) |
| Oversize datagram (70,000 bytes) | EMSGSIZE; a datagram send never returns a short count |
| `inet_pton` | accepts 127.0.0.1 and 0.0.0.0; refuses localhost, 256.1.1.1, 1.2.3 |
| A PUB binding late, first frame 20 ms after the bind, a SUB connected first, 40 trials each | 10 ms reconnect: frame 0 lost 3 times; 1 ms: never |

Linux, from the kernel's documented behaviour (§10.6 states which of these the run confirmed, and that the broadcast prediction was wrong for the runner's kernel): the ICMP unreachable on a connected socket is reported as ECONNREFUSED on the next send and loopback is exempt from the ICMP rate limits, so a burst to a dead port alternates ok and refused; `connect` to a broadcast address without `SO_BROADCAST` fails with EACCES; `SO_RCVBUF` is doubled on set with a floor near 2.3 KB and each 121-byte loopback datagram charged about 1.2 KB, so a 4 KB request keeps a handful of 150; no SIGPIPE path for a datagram socket on either kernel.

### 10.6 Measurement of record — GitHub Actions run 38329662749, commit `9e68222`, Ubuntu, GCC 13

The first CI run with the "Integration tests" step, on this entry's own commit, 2026-10-13 00:55 UTC, green in 1 min 43 s (the job took 50 to 70 s before the step: Install tools 18 s, Build 10 s, Unit tests 1 s, Set up Python 2 s, Install the Python side 7 s, Integration tests 53 s, Coverage report 2 s, thresholds and cppcheck 1 s each). This is the measurement of record for REQ-TEST-002 and REQ-TEST-003 and the first GCC measurement of the transport and driver scopes on record; the README verification table and the FTS-TM-001 TEST-003 row cite it.

| Item | Value |
|---|---|
| Unit tests (Google Test) | 168 of 168 passed, 0.81 s |
| Integration tests (pytest) | 66 passed in 52.54 s on Python 3.13.16 (pytest 9.1.1, pyzmq 27.2.0, numpy 2.5.4, matplotlib 3.11.2) |
| Branch coverage (gcov/lcov) | 90.7% (292 of 322), every per-scope threshold met |
| Line coverage | 98.0% (909 of 928); functions 95.4% (124 of 130) |
| Per scope | processing 100.0%, timing 100.0%, logging 100.0%, transport 96.2%, replay 100.0%, drivers 81.3% (threshold 70%), overall 90.7% (threshold 80%) |
| Compiler warnings under `-Werror` | 0 |
| cppcheck findings | 0 |

**The transport scope on GCC.** `zmq_publisher.cpp` 4 of 4 branches; `udp_publisher.cpp` 21 of 22 branches, 38 of 39 lines, 3 of 4 functions (the unhit function is the deleting destructor GCC emits beside the complete one for a class with a virtual destructor; nothing deletes a publisher through a pointer). §10.3 expected 24 of 24 on GCC with the `connect` failure reached through `DirectedBroadcastIsRefusedOrRouted`; the runner did not take that branch either, so its kernel routed 127.255.255.255 like macOS did rather than refusing it at `connect` with EACCES as §10.5 predicted from the kernel's documented behaviour (the test accepts refusal at `connect`, refusal at `send`, or delivery, §10.4, and passed). The one untaken branch is therefore the `connect` failure on both platforms, unreachable from a test; it stays unmarked, 96.2% against the scope's 80%. The log does not list lines; the run's coverage artifact (the HTML report) names it. No `LCOV_EXCL` marker was added for this entry.

**The driver scope on GCC**, first measured in a recorded run here: `i2c_bus.cpp` 11 of 26 branches (31 of 47 lines), `bmp280.cpp` 34 of 40, `mpu6050.cpp` 37 of 44, `live_sensor_source.cpp` 16 of 16, `simulated_data.cpp` 8 of 8, `fault_injecting_source.cpp` 16 of 16: the untaken branches are the Linux device-file error paths that only hardware reaches, the reason FTS-TP-001 §6.3 gives for the 70% threshold; no exclusion. Every other scope is at 100% with the Entry 5 markers in place (six `LCOV_EXCL_EXCEPTION_BR_LINE`), as the four unrecorded runs below already showed.

**What this run confirms of §10.5's Linux expectations, through the assertions that passed on the runner:** the kernel reported ECONNREFUSED on a connected datagram socket within 100 ms of 1 ms-spaced sends to a dead port (`PortUnreachableIsCountedNotThrown`), a 4 KB `SO_RCVBUF` dropped at least 50 of the 150 frames sent during a 3 s stall (`test_stalled_receiver_never_blocks_the_loop[udp]`, the Linux-only assertion) and fewer than 1000 of 1000 at unit level, and a broadcast address was not refused at `connect` (above). **What it does not record:** the ZeroMQ join index, the stall test's missed cycles, the exact UDP gap count, and the kernel's ECONNREFUSED count with nobody listening. The step runs plain `python -m pytest`, which captures a passing test's prints, so those numbers exist on the runner only inside the bounds the assertions checked (join within five frames, missed cycles under 75, published plus failures 50). The step gains `-rA` with the configuration topic so the captured prints reach the log, and Entry 11's CI section reports them.

**The four green runs that went unrecorded** between Entry 5 §5.8 (run 34445017604, the measurement of record every document cited until this entry: 106 tests, no transport or driver scope) and this one: 38204693995 on `7f216e1` (Entry 6), 38210190313 on `233f965` (Entry 7), 38291216217 on `79afc7e` (Entry 8), and 38307718743 on `a0d088d` (Entry 9), the last reporting 157 tests, branches 90.6% (270 of 298), processing, timing, logging, transport and replay at 100%, and drivers at 81.3%. Entry 6 reserved §6.7 for the transport scope's first GCC measurement and it was never written; this section is it.

### 10.7 Open

- The pytest step's captured prints (the join index, the stall counts, the ECONNREFUSED count) are not in the CI log (§10.6); the step gains `-rA` with the configuration topic, and Entry 11's CI section reports them.
- REQ-TRANS-002 stays Partial until REQ-CFG-001 carries the destination; no name resolution until then either (`--udp` takes a dotted quad).
- A sequence number on the wire: deferred with its three placements and its trigger (FTS-DD-001 §12).
- The `connect` failure branch of `udp_publisher.cpp` is unreachable on macOS; the record is GCC's.
- Entry 9 §9.6, the stationary capture, is unchanged by this entry.

## Entry 11 — 2026-10-13, YAML configuration, the recorded configuration, the byte-identity check (macOS preview)

REQ-CFG-001, REQ-CFG-002 and the new REQ-CFG-003 at unit and process level; REQ-TRANS-002 and REQ-SENS-006 closed through the file; the parameter structs and the injection tests behind them. The first CI run with the `src/config/` scope gated is the measurement of record and goes in §11.7; everything else here is a macOS preview. No hardware. Built from the Obsidian plan `Plan - YAML Configuration` (2026-10-13).

### 11.1 What was built

- `CMakeLists.txt`: yaml-cpp 0.9.0 fetched and pinned by tag (0.8.0 declares `cmake_minimum_required(VERSION 3.4)`, which the Mac's CMake 4.4.3 refuses; 0.9.0 declares `3.5...3.30` and configures on the Mac, the runner and the Pi), tools, contrib, tests, install rules and formatter off, linked `PRIVATE` to `telemetry_core`; `src/config/telemetry_config.cpp` in the library; the shipped file's path compiled into `unit_tests`. yaml-cpp's thirty sources compiled clean under `-Wall -Wextra -Wpedantic -Werror` on clang, about 2 s on ten cores; the runner's figure goes in §11.7.
- The parameter structs, behaviour-preserving (commit 1): `ProcessorConfig` in `processor.h` (the four Kalman variances, alpha, the sea-level reference; the initial altitude variance is the barometric one, REQ-PROC-004), taken by `TelemetryProcessor(const ProcessorConfig&, uint64_t imu_timeout_ms)`; `SimulationNoise` in `simulated_data.h` (the seven sigmas), taken by the generator and `SimulatedSource`; `LiveSensorSource` takes the IMU sample rate and hands it to `Mpu6050`. The anonymous-namespace constants in `processor.cpp` are gone and every default member initializer equals the shipped file. Six injection tests: `KalmanTest.TuningIsHonored`, `SeaLevelReferenceIsHonored`, `ComplementaryTest.AlphaIsHonored`, `SimulatedDataTest.NoiseIsConfigurable`, `LiveSensorSourceTest.ImuSampleRateReachesTheDriver`, `UnreachableImuRateRefusesStartup`.
- `src/config/telemetry_config.h/.cpp`: the typed structs (`TimingConfig`, `SensorConfig`, `LoggingConfig`, `TransportConfig`, `SimulationConfig`, aggregated with `FaultDetectorConfig`, `ProcessorConfig` and `SimulationNoise` into `TelemetryConfig`); `ConfigLoad` (ok, config, problems, text); `load_config_text`, `load_config_file`, `log_prefix`, `recorded_config_path`. yaml-cpp is the document parser only; the four scalar rules are the project's (decimal or `0x` hex with no leading zero, finite numbers through `strtod`, `true`/`false`, non-empty text). A `Section` wrapper reads each mapping's keys in document order, reports a duplicate once and the unknown keys at the end, and every `read_*` appends at most one problem under the dotted key; 36 keys with their ranges, and three rules between sections (the UART timeout against the GPS period, the IMU rate against the processing rate, the UDP host through `inet_pton`). One `try` around `YAML::Load`; no exception leaves the module.
- `config/telemetry_config.yaml`: the configuration of record, a header stating the rules and one comment per section citing its requirement and fault IDs.
- `src/main.cpp`: `--config <file>`, default `config/telemetry_config.yaml` relative to the working directory; the five file-scope constants gone (`kDefaultCycles` and the `static_assert` on the two timeout defaults stay); the file loaded before any object is built, every problem printed under `error: configuration <path> has N problems:`, exit 1; the `Configuration:` line; the record `<logging.directory>/<prefix>_config.yaml` written once the logger has opened; a replay reads its record by default and, with `--config`, says whether the named file equals the record by content, differs, or has no record to compare with; the refusal to replay a `replay_NNN.bin` from the output directory; `--zmq` and `--udp` enable a transport and override the file's address; the period, the log directory and size, the sensors' bus, addresses and IMU rate, the simulator's seed, GPS period and noise, and the fault thresholds all from the file.
- Tests: `tests/unit/test_config.cpp` (38 tests, TC-016 steps 1 to 9, every message compared whole); `tests/integration/test_config.py` (10 tests, 12 collected, TC-016 steps 11 to 20); `conftest.py` passes `--config` with the shipped file on every run and the session fixture records the two configuration copies; `scripts/coverage_check.sh` gains the `config` scope at 90%; the workflow's pytest step gains `-rA` (§10.6).
- Documents: FTS-SRD-001 0.6.0 (REQ-CFG-003; rationales for CFG-001, CFG-002, SENS-001, SENS-006, TRANS-002, LOG-003); TC-016; FTS-TM-001 24 Verified, 9 Partial, 1 Not started of 34; FTS-DD-001 §3 (Configuration), §5, §6, §9 (the recorded configuration, the corrected open-order sentence), §10, §12 (bullets 221, 228 and 229 resolved, 231 and 233 updated); FTS-FM-001 (every threshold named by its key, the 2000 ms UART timeout stated); FTS-NP-001 §1; README; CLAUDE.md.

### 11.2 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 212 of 212 (174 after the structs, then 38 configuration tests), 2.0 s; configured with `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings |
| pytest | 78 of 78 in 61 s (66 before; `test_config.py` 12 in 20 s) |
| cppcheck | 0 findings on `src/`, the config module included |
| `./build/telemetry` from the repository root | reads `config/telemetry_config.yaml`, writes `logs/telemetry_config.yaml` beside the log |
| `--config nope.yaml`; no file in a bare directory | exit 1, the path named, nothing written; the default path's message adds `pass --config <file>` |
| `--replay logs/telemetry_000.bin` | `Configuration: logs/telemetry_config.yaml (recorded with logs/telemetry_000.bin)`; the replay log and the replay's record byte-identical to the session's |
| `--replay logs/replay_000.bin` from the log directory | refused: "this run would write logs/replay_NNN.bin over it"; the input untouched |

Found by the tests while building: the first integer rule read `010` as decimal 10 where the test expected a refusal (the rule now refuses a leading zero, as the file's header says); a test edit that inserted a section in the middle of the `udp` block moved `host` and `port` out of it, three problems instead of one (the test, not the loader); and the session fixture's replay, handed the shipped file by `--config`, was reported as "not the configuration recorded" because the comparison was by path, so it is now by content (§11.5).

### 11.3 Coverage preview (clang, the Mac recipe of Entry 8, unit tests only)

| Scope | Branches | Threshold |
|---|---|---|
| processing | 100.0% | 90% |
| config | 97.8% (227 of 232) | 90% |
| timing | 100.0% | 80% |
| logging | 83.3% | 80% |
| transport | 89.3% | 80% |
| replay | 100.0% | 80% |
| drivers | 100.0% | 70% |
| overall | 96.4% | 80% |

`telemetry_config.cpp`: 430 of 430 lines, every function, 227 of 232 branches. The five untaken are the unwind edges of the one range-`for` over a `YAML::Node` (the marked line, the `log_reader.cpp` line 76 class). clang's gcov data does not flag exception edges, so the marker, which tells lcov to drop flagged edges, has no effect in a Mac preview; it acts on the GCC run.

**Found by the first preview, which read 87.1% (237 of 272) with every line hit.** The 35 untaken edges were read one by one against clang's branch listing (`llvm-cov gcov -b -c`), and they were of two kinds, not one. Twenty-five were unwind edges: five on the marked loop and twenty on lines that hold no decision but that lcov 2.5 keeps: a `std::find` compared with `end()` on its own line (`!=`), the `inet_pton` and `isdigit` calls compared on their lines (`==`, `!=`, and a `static_cast<unsigned char>` whose brackets the filter does not strip), the `read_int` call for `max_file_bytes` carrying `static_cast<long long>`, and the closing lines of six `problem(...)` calls that spanned lines. Both lcov versions strip strings, comments, `->`, `<<`, `::` and single-word template arguments and then keep any line holding `? ! < > == != && ||` or a loop or branch keyword; the one difference is that lcov 2.5 keeps a continuation line whose parentheses do not balance where lcov 2.0, the runner's, drops it. Ten were real decisions no test took: gcov counts a template's branches once per instantiation, `read_int` is four functions (`uint64_t`, `uint8_t`, `int`, `uint32_t`) and `read_positive` two (`float`, `double`), and each fault had been proven on one key, so the missing-key outcome was untaken in three integer instantiations, the non-scalar outcome in three, the unparsable-text outcome in two, `seed < 0` in one (no test had a negative seed), and a non-finite value in the `double` instantiation (the two coordinate sigmas). GCC counts instantiations the same way, so those ten would have reached the record.

**Applied.** The ten decisions: `ConfigTest.EveryKeyReportsEachKindOfFault` walks all 36 keys and takes every outcome of every key's rule (missing, a sequence, text the rule refuses, one past each edge), so every instantiation meets every fault on every platform and one key per field type is no longer a floor anyone has to remember. The twenty edges: the loader's coverage note now states the style rule precisely (a call sits on one statement line of its own, carrying none of the characters the filter reads as a conditional) and the eight lines that broke it were refined: the two `std::find` calls into a local, the `inet_pton` and `isdigit` calls into an `int` compared on the next line, a `kOneRecordBytes` constant in place of the cast on the `read_int` line, and a local for the message of each of the six wrapped `problem(...)` calls. No message or behaviour changed: every unit test compares messages whole, and pytest ran 78 of 78 after the change. No marker was added: none of those lines holds a conditional, so the §5.6 rule does not name them, and on GCC with lcov 2.0 none of them keeps a branch. Prediction for §11.7: the scope's exception edges on GCC fall on the marked loop (dropped by the marker) and on the `node->` lines (dropped by the filter, which strips `->`); `inet_pton`, `isdigit`, `strtoll` and `strtod` are `__THROW` on glibc, so the kept ternary and comparison lines carry no unwind edge there; the record should read 100%, and if it does not, §11.7 names the line.

### 11.4 The byte-identity check

A build of `9e68222` (`build-ref/`, the tree before this entry) ran the seed-42 simulated source for 500 frames and for the 5-frame default in scratch directories:

| Log | SHA-256 |
|---|---|
| 500 frames, reference build | `3124895fdf8997907aa43343ae41ce735cedc4ad8219a38b6829e50af1e795e8` |
| 5 frames, reference build | `b94bf0331ab0afe6c487349baaac91b36e928ac8ba06359f7a1caadea84d6475` |

After the parameter structs (commit 1) the same 500-frame run `cmp`ed silent against the reference. After the file and `main` (commit 2) the run with `--config config/telemetry_config.yaml` `cmp`ed silent again, the same hash. The record beside that log equals the shipped file; its replay from the record is byte-identical to it, and the replay's record equals the shipped file too. Identity across a code change is evidence, not a requirement (REQ-LOG-003 claims identity for one build); it held. Permanently: `ConfigTest.ShippedFileEqualsDefaults`, the determinism tests, and the pytest replay identity, which now runs on the file.

### 11.5 Decisions taken, and deviations from the plan

- The scalar rules are the project's, not yaml-cpp's (plan decision 7): yaml-cpp's stream conversion reads `010` as octal 8 and accepts `yes`/`no`/`on`/`off`; the loader reads `node.Scalar()` and applies four printable rules, so a value two readers read differently is refused. Found while building: the first rule accepted a leading zero as decimal; `010` and `0020` are now refused and the file's header says so.
- Sigmas must be positive, not non-negative as the plan's table said: `std::normal_distribution` requires `stddev > 0`, so the loader's rule, the shipped file's comment and TC-016 steps 6 and 10 say "positive", and `NoiseIsConfigurable` uses sigmas of 1e-6 and a tenfold sigma rather than zero.
- Replay identity is judged on the configuration's text, not its path (plan decision 6 compared paths): `--replay` with `--config` naming a file whose text equals the record reports "equal to the configuration recorded with"; a differing text reports "not the configuration recorded with ...; replay identity is not claimed"; a log with no record says so. The session fixture, which hands every run the shipped file, exercises the first form; `test_config.py` the others, and the plain `--replay` form.
- The barometer sample rate is the processing period (decision 9): `timing.period_ms` is bounded to 20 to 1000 ms, REQ-SENS-001's 50 to 1 Hz, and that requirement's new rationale says why; no decimation code.
- The simulator's frame interval is `timing.period_ms` (decision 10); the ZeroMQ high-water mark and linger stay constants (decision 11); `--cycles` stays a flag; one `ProcessorConfig`; alpha moves because the SRD names it.
- The matrix had applied "configurable" unevenly: PROC-001, FAULT-001, FAULT-002, FAULT-003, FAULT-005, TIME-001 and LOG-002 were Verified with constants while SENS-001, SENS-003, SENS-004, SENS-006 and TRANS-002 were held Partial for the same clause. Every row now names its key.
- The Week 2 Map's Day 14 sketch differed in six points (TC-015 for the card; `cycles` and `prefix` keys; three filter structs; five Kalman keys; `std::optional<Config>`; `config.h`); the Done note there records them.
- Commits: the structs and yaml-cpp as one behaviour-preserving commit, then the loader, the file, `main` and the tests; the matrix changes all land in the second (the plan put a CFG-001 "Partial" note in the first).
- The plan's coverage rule for the loader (decision 13, "no library call on a conditional line") was not precise enough: lcov's branch filter keeps a line for a comparison or a surviving template bracket as well as for a keyword, and lcov 2.5 keeps a continuation line that 2.0 drops. The rule is now written in the file's coverage note, eight lines were refined to it, and the ten real gaps the preview exposed, one decision per template instantiation, are closed by one table-driven test (§11.3). The file has 36 keys, not the 35 the plan counted (the `sensors` section has four leaf keys); the documents now say 36 and `EveryKeyReportsEachKindOfFault` asserts the count.

### 11.6 Library facts this entry rests on

yaml-cpp 0.9.0: `Load` throws `ParserException` with a zero-based mark; a repeated key is kept as two map entries and iteration sees both (`DuplicateKeyIsNamed` passes on that); `operator[]` on a const map node returns an undefined node for an absent key and never inserts. `std::strtod` reads `inf` and `nan`, which the number rule then refuses through `std::isfinite`; `.inf` and `.nan` do not parse at all. A file written then `chmod 0` makes `ifstream` fail with `Permission denied` for an ordinary user (`UnreadableFileIsReported`; the test skips, saying so, as root).

### 11.7 Measurement of record

Reserved for the first CI run with this entry's commits: the config scope on GCC (232 branches in clang's count, one marker, 100% predicted in §11.3), yaml-cpp's build time on the runner, the pytest line (78 tests) with `-rA`'s captured prints, which now carry the transport tests' join index, stall counts and ECONNREFUSED count that §10.6 could not record, and every other scope. The README verification table and the FTS-TM-001 TEST-003 row move to that run.

### 11.8 Open

- §11.7.
- The measured constants from the stationary capture (Entry 9 §9.6) land in `config/telemetry_config.yaml` and the struct defaults together; `analysis/noise_profile.py` still carries `ALPHA`, `PERIOD_S`, `STUCK_COUNT` and the Kalman placeholders as Python constants and does not read the file.
- `main` does not check `LogReplaySource::exhausted()`: a bad or missing replay log is skipped silently and the run logs the zero frame `--cycles` times (pre-existing, FTS-DD-001 §12).
- Deliberately not built: a flag that disables a file-enabled transport, name resolution for `transport.udp.host`, a barometer read schedule below the processing rate, keys for the ZeroMQ tuning.
- Unverifiable here (no GCC, no Docker): the config scope's GCC branch count and yaml-cpp under GCC 13's `-Werror`; §11.7 settles both.
