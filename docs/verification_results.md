# Flight Telemetry System — Verification Results

> **Document ID:** FTS-VR-001
> **Version:** 0.1.0
> **Status:** Draft
> **Last Updated:** 2026-10-16
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
| `LCOV_EXCL_EXCEPTION_BR_LINE` on the six lines of 5.4 | **142 of 142, 100%**; every scope 100% | the marker lcov 2.0 documents for exactly this: exception edges on that line are dropped, the line's real decision branches stay counted (Entry 11 §11.7: not so; 190 to 142 is the six lines' 48 edges entire, and the record carries no branch on a marked line) |
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

- The pytest step's captured prints (the join index, the stall counts, the ECONNREFUSED count) are not in the CI log (§10.6); the step gains `-rA` with the configuration topic, and Entry 11's CI section reports them. (Reported in Entry 11 §11.7: join index 0 on both transports, UDP stall gaps 141 of 150, ECONNREFUSED on 25 of 50 sends.)
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

**Found by the first preview, which read 87.1% (237 of 272) with every line hit.** The 35 untaken edges were read one by one against clang's branch listing (`llvm-cov gcov -b -c`), and they were of two kinds, not one. Twenty-five were unwind edges: five on the marked loop and twenty on lines that hold no decision but that lcov 2.5 keeps: a `std::find` compared with `end()` on its own line (`!=`), the `inet_pton` and `isdigit` calls compared on their lines (`==`, `!=`, and a `static_cast<unsigned char>` whose brackets the filter does not strip), the `read_int` call for `max_file_bytes` carrying `static_cast<long long>`, and the closing lines of six `problem(...)` calls that spanned lines. Both lcov versions strip strings, comments, `->`, `<<`, `::` and single-word template arguments and then keep any line holding `? ! < > == != && ||` or a loop or branch keyword; the one difference is that lcov 2.5 keeps a continuation line whose parentheses do not balance where lcov 2.0, the runner's, drops it. Ten were real decisions no test took: gcov counts a template's branches once per instantiation, `read_int` is four functions (`uint64_t`, `uint8_t`, `int`, `uint32_t`) and `read_positive` two (`float`, `double`), and each fault had been proven on one key, so the missing-key outcome was untaken in three integer instantiations, the non-scalar outcome in three, the unparsable-text outcome in two, `seed < 0` in one (no test had a negative seed), and a non-finite value in the `double` instantiation (the two coordinate sigmas). GCC counts instantiations the same way, so those ten would have reached the record. (§11.7: it does not; the runner's reader reports one decision per line, 196 branches to clang's 232, and the record could not have shown them.)

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

### 11.7 Measurement of record — GitHub Actions run 38490312035, commit `797f06b`, Ubuntu 24.04, GCC 13.3

The first CI run with the `src/config/` scope gated and the pytest step under `-rA`, on this entry's own commit, 2026-10-14 00:27 UTC, green in 2 min 39 s: Install tools 30 s, Configure 6 s, Build 26 s, Unit tests 3 s, Set up Python 4 s, Install the Python side 10 s, Integration tests 65 s, Coverage report 3 s, thresholds 2 s, cppcheck under 1 s (Entry 10's run: 1 min 43 s, Build 10 s, Integration tests 53 s). Image `ubuntu-24.04` (20261004.327.1): GCC 13.3.0, CMake 3.28.3, lcov 2.0-4ubuntu2, cppcheck 2.13.0, libzmq 4.3.5, Python 3.13.16 with pytest 9.1.1. This is the measurement of record for REQ-TEST-002, REQ-TEST-003 and the config scope; the README verification table and the FTS-TM-001 TEST-002 to TEST-004 rows cite it.

| Item | Value |
|---|---|
| Unit tests (Google Test) | 212 of 212 passed, 2.33 s |
| Integration tests (pytest) | 78 passed in 64.77 s (66 in 52.54 s before; the twelve configuration tests are the growth) |
| Branch coverage (gcov/lcov) | 94.2% (488 of 518), every per-scope threshold met |
| Line coverage | 98.6% (1313 of 1332); functions 96.2% (152 of 158) |
| Per scope | processing 100.0%, config 100.0% (threshold 90%), timing 100.0%, logging 100.0%, transport 96.2%, replay 100.0%, drivers 81.3% (threshold 70%), overall 94.2% (threshold 80%) |
| Compiler warnings under `-Werror` | 0, yaml-cpp's thirty sources included: `-Wall -Wextra -Wpedantic` and `-Werror` are `add_compile_options` at directory level, so the fetched sources compile under them |
| cppcheck findings | 0 on 24 files |

**The config scope on GCC: 100%, as §11.3 predicted; 196 branches to clang's 232.** `telemetry_config.cpp`: 397 of 397 lines, 28 of 28 functions, 196 of 196 branches on the same 77 lines the preview counted; no marker added, none removed. The branch total rose from Entry 10's 322 to 518, by exactly the loader's 196, so no other file's count moved through the parameter structs. The thirty untaken branches are Entry 10's thirty: `i2c_bus.cpp` 15, `mpu6050.cpp` 7 and `bmp280.cpp` 6 (the Linux device-file error paths), `udp_publisher.cpp`'s `connect` failure, and the DEGRADED outcome of `status_name` in `src/telemetry_frame.cpp`, which no test prints and which sits outside every gated scope (§10.6 did not name it). The six unhit functions are the deleting destructors GCC emits beside the complete ones for `DataSource`, `Publisher`, `I2cBus`, `Clock`, `LinuxI2cBus` and `UdpPublisher`, the class §10.6 named for one of them: an abstract interface's deleting destructor is unreachable, and nothing deletes the two concrete ones through a pointer.

The thirty-six branches between the two counts were read line by line, the run's HTML report against the preview's `.info`. The marked range-`for` over the YAML node (line 158) carries twelve in clang's data, seven taken and the five unwind edges untaken, and none on GCC with the marker in place. GCC splits the four `&&`/`||` chains (lines 49, 53, 58 and 600) into two more edges each and counts the `hex ? 16 : 10` ternary (line 69) that clang folds into a select: ten more on GCC. And the six conditional lines inside `read_int<T>` and `read_positive<T>` (180, 186, 189, 193, 218 and 221) carry one decision each on the runner, 2, 2, 2, 4, 2 and 2 edges, where the preview counted every instantiation, 8, 8, 8, 16, 4 and 4: thirty-four fewer.

**What that last difference corrects in §11.3.** The per-instantiation counting that exposed the ten real decisions belongs to the Mac path (clang's `gcov` text output, which lists a template's line once per function, read by lcov 2.5), not to gcov everywhere: the runner's path (GCC 13's `gcov` read by lcov 2.0) reports one decision per source line, so an outcome taken in any one instantiation reads as taken. §11.3's sentence "GCC counts instantiations the same way, so those ten would have reached the record" was wrong; the record could not have shown them. `EveryKeyReportsEachKindOfFault` stands on its own ground, that every key's rule refuses every kind of fault, which the object code decides once per instantiation whatever a coverage reader reports; its comment and TC-016 step 6 give the counting as the reason and overstate it (§11.8).

**A second correction, to Entry 5 §5.6: on lcov 2.0 the marker removes the marked line's own decision too.** The run's HTML report carries no branch data on any of the seven `LCOV_EXCL_EXCEPTION_BR_LINE` lines (`telemetry_config.cpp` 158; `log_reader.cpp` 17, 64, 76 and 78; `fault_detector.cpp` 110; `log_replay_source.cpp` 36), lines whose `if`, `while`, `for` and `&&` outcomes §5.4 showed taken on GCC. The record's own arithmetic had said so: §5.4's six lines carried 48 edges (4, 4, 6, 24, 2 and 8) and §5.6's marker run went from 190 branches to 142, all 48 gone, not the 16 exception edges alone. The mechanism is in lcov 2.0's `removeExceptionBranches`: it strips the flagged edges, which on GCC's data take the whole block of a call that may throw, the call's normal continuation with its throw edge, and when a single block is left on the line, the one holding the decision's own outcomes, lcov judges the line no conditional (its lone-block rule) and removes its branch data. lcov 2.5's source reader notes that per-line exception exclusion is "not implemented at present", and clang flags nothing, so the Mac preview keeps every edge on those lines (12, 4, 2, 8, 16 and 4 on six of them in this entry's preview, none on `fault_detector.cpp` 110) and the two measurements differ by the marked lines' decisions as well as by their unwind edges. The seven source comments ("exception-unwind edges only; the decision itself is still counted"), §5.6's reading of the marker and the CLAUDE.md rule that rests on it overstate what the record measures: the decisions on those lines are proven by the named tests of §5.4, not by the coverage number. The loader's style rule (a call on a statement line of its own, the branch on the next line testing a local) needs no marker and loses no decision; whether to apply it to the seven lines, pin lcov 2.5 and filter `exception` instead, or keep the markers with this note, is the author's decision (§11.8).

**yaml-cpp on the runner.** The Configure step's 6 s includes the fetch of the pinned tag. In the Build step yaml-cpp's thirty objects were compiled and archived as `libyaml-cppd.a` (the library's Debug postfix, from the workflow's `CMAKE_BUILD_TYPE=Debug`) within 5 s of the step's start, in parallel with Google Test; `telemetry_core` was archived at 18 s and both executables by 26 s. The Build step's growth from 10 s is the library, the loader and its 38-test file together. Zero warnings under GCC 13.3's `-Werror`, the fact §11.8 could not verify on the Mac.

**The pytest line, with the captured prints §10.6 could not record** (`-rA`, this entry's workflow change): TC-005, 200 frames: join index 0 on both transports, 200 received, 5.01 s each. TC-010 step 6, 500 frames with a 3 s stall after 50: ZeroMQ received 500, gaps 0 (the TCP buffers held the stall, as on the Mac); UDP with the 4 KB receive buffer received 359, gaps 141 of the 150 stalled frames (the Mac: 373 and 127); missed cycles 0 on both, 11.01 s each. TC-010 step 7, 50 frames with nobody listening: ZeroMQ published 50, refused 0; UDP published 25, refused 25, the exact half that alternation produces: the pattern §10.5 predicted for Linux from the kernel's documented behaviour (the ICMP unreachable reported as ECONNREFUSED on the next send, loopback exempt from the ICMP rate limits), against macOS's 9 of 50 under its 50 ms suppression. These are the join index, the stall counts and the ECONNREFUSED count §10.6 listed as unrecorded.

**The runner image moves.** The run's annotations say `ubuntu-latest` migrates to Ubuntu 26 from 2026-10-19, so the toolchain above (GCC 13.3, lcov 2.0, cppcheck 2.13) changes under later entries unless the workflow pins `ubuntu-24.04`; and the three actions (`checkout@v4`, `setup-python@v5`, `upload-artifact@v4`) are flagged for Node 20's deprecation and ran on Node 24, with no effect on the job. Both are open in §11.8.

### 11.8 Open

- The measured constants from the stationary capture (Entry 9 §9.6) land in `config/telemetry_config.yaml` and the struct defaults together; `analysis/noise_profile.py` still carries `ALPHA`, `PERIOD_S`, `STUCK_COUNT` and the Kalman placeholders as Python constants and does not read the file.
- `main` does not check `LogReplaySource::exhausted()`: a bad or missing replay log is skipped silently and the run logs the zero frame `--cycles` times (pre-existing, FTS-DD-001 §12). (Closed by Entry 12: `main` probes the input's header before anything is built and refuses an unreadable log by name, and prints how many cycles repeated the last frame when a replay outlasts its log; REQ-LOG-005.)
- Deliberately not built: a flag that disables a file-enabled transport, name resolution for `transport.udp.host`, a barometer read schedule below the processing rate, keys for the ZeroMQ tuning.
- Unverifiable here (no GCC, no Docker): the config scope's GCC branch count and yaml-cpp under GCC 13's `-Werror`; §11.7 settles both. (Settled: 196 of 196, zero warnings.)
- TC-016 step 6 and the comment above `ConfigTest.EveryKeyReportsEachKindOfFault` give gcov's per-instantiation branch counting as the sweep's reason; §11.7 shows that counting belongs to the Mac preview, and that the record folds a template's instantiations into one decision per line. The sweep's ground is REQ-CFG-002's "each" (every key's rule against every fault); the two sentences await rewording.
- The seven `LCOV_EXCL_EXCEPTION_BR_LINE` lines carry no branch data on the record (§11.7): their decisions are tested, not measured. For the author: apply the loader's style rule to the seven lines and drop the markers (the health topic rewrites `log_reader.cpp` 17 and 64 in any case); or pin lcov 2.5 in the workflow with `--filter branch,exception`, the systemic alternative §5.6 named; or keep the markers with this note. The seven source comments, §5.6 and the CLAUDE.md rule say the decision is still counted, and await that decision.
- The runner image: `ubuntu-latest` becomes Ubuntu 26 from 2026-10-19, and with it the toolchain of record (§11.7); pinning `ubuntu-24.04` in the workflow, or recording the new versions in the next entry, is a decision for the next push.

## Entry 12 — 2026-10-13, validity of computed fields, NO_DATA, format version 2, the byte checks (macOS preview)

REQ-PROC-007, REQ-LOG-005 and REQ-FAULT-006 (new in FTS-SRD-001 0.7.0) at unit and process level; REQ-PROC-005 closed; the `SystemHealth` sidecar decision of FTS-DD-001 §12 closed in favour of the frame. Built from the Obsidian plan `Plan - Health and Validity` (2026-10-13), after the configuration topic's four commits and §11.7. No hardware. The first CI run with these commits is the measurement of record and goes in §12.7; everything else here is a macOS preview.

### 12.1 What was built

- `src/telemetry_frame.h/.cpp`: `enum class Validity : uint8_t { NOT_JUDGED = 0, NO_COMPUTED_DATA, FAILURE_WARNING, NORMAL }`, ARINC 429's sign/status vocabulary with zero deliberately none of the three judgments and the order worst first; `ChannelStatus::NO_DATA = 2`; five `Validity` fields at offsets 98 to 102, `uint32_t reserved_a` at 52 and `uint8_t reserved_b` at 103 where the compiler used to pad, `sizeof` 104 unchanged; the proof `static_assert(sizeof(TelemetryFrame) == 8 + 11*4 + 4 + 2*8 + 5*4 + 3 + 3 + 5 + 1)` with `offsetof` pins; `status_name` as a switch with `NO_DATA`, and `validity_name`, both answering `UNKNOWN` to a byte outside the enumeration.
- `src/processing/fault_detector.h/.cpp`: `FaultEvent::reserved` (`uint16_t`, offset 10) with the same proof; `ChannelState::status` starts NO_DATA; `transition` promotes to DEGRADED from NO_DATA or NOMINAL alike (`!= DEGRADED`), and a NO_DATA channel's first successful reading makes it NOMINAL with no event.
- `src/logging/log_format.h/.cpp`: `kLogFormatVersion` 2, `kOldestReadableLogFormatVersion` 1, `header_problem` naming the first mismatch (the twin of the Python `check_header`), `header_is_valid` kept as its one-line form. `src/logging/log_reader.cpp`: the refusal names the problem, with the call on its own line and the branch on a local (the marker that line carried is gone); `next()` sets a version-1 frame's validity to NOT_JUDGED and its reserved fields, and a version-1 event's, to zero. `src/replay/log_replay_source.cpp`: `strip_to_raw` zeroes the validity and reserved bytes too.
- `src/processing/processor.h/.cpp`: the barometric altitude converted only from an accepted reading and held otherwise (0 before the first, the last accepted conversion while DEGRADED); `baro_ever_fresh_`, `gps_ever_fresh_`, `attitude_ever_seeded_`; `judge()`, the only writer of `Validity`, called last in `process()`; `ComplementaryFilter::seeded()`.
- `src/main.cpp`: the replay input's header probed after the configuration is loaded and before anything is built, a refusal by name, the `Replay input:` line with the input's version and a notice when it differs; `print_frame` with the validity beside each computed value; `ValidityTally` and the `Validity:` line at exit; the `Replay: exhausted after N frames; M cycles repeated the last frame` line, read from the replay source through a `static_cast` in the composition root.
- Python: `pipeline/frame_format.py` (`@Q11fI2d5f3B3?5BB`, `@QBBHf`, the new field names, `VALIDITY_NAMES`, `COMPUTED_VALIDITY`, `STATUS_NAMES` with NO_DATA, `check_header` on the range 1 to 2), `pipeline/log_reader.py` (`I` and `H` in the numpy map, `invalid_as_nan`, the summary's no-data and validity lines), `pipeline/receiver.py` (`format_frame` with the names beside the values), `analysis/noise_profile.py` (`output_report` over NORMAL frames only, with `n` and `excluded_not_normal`), `tests/integration/synthetic_log.py` (integer zeros, NORMAL by default, a `version` argument).
- Tests: `tests/unit/test_telemetry_frame.cpp` (6, new) and `test_validity.cpp` (7, new); `LoggingTest.Version1HeaderIsReadAndSanitized`, the named refusals in `ReaderRejectsWrongVersion` and `ReaderRejectsWrongFrameSize`; `ReplayTest.Version1LogReplaysAsRawFrames`, `Version1LogReplaysJudged`, the validity assertions in `ReplayReproducesLiveSession` and `ReplayStripsComputedFieldsAndStatus`; `FaultDetectionTest.NoDataUntilFirstReading`, `FirstReadingMakesNominalWithoutEvent`, `FaultOnFirstReadingDegradesFromNoData`, `DeadFromPowerOnStillTimesOut` extended, `EventPaddingBytesAreZero` renamed `EventReservedIsZero`; validity assertions in four `ComplementaryTest` and two `KalmanTest` cases and in the two raw-only source tests; `tests/integration/test_validity.py` (7, new, TC-017 steps 11 to 15); `test_log_reader.py` (`LOG_FORMAT_VERSION + 1`, the new offsets), `test_replay_identity.py` and `test_config.py` (the validity names among the computed fields).
- Documents: FTS-SRD-001 0.7.0 (REQ-PROC-007, REQ-LOG-005, REQ-FAULT-006; the definitions line; rationales for FAULT-004 and LOG-003), 37 statements; TC-017 and corrections to TC-002, TC-004, TC-008, TC-011 and TC-014; FTS-TM-001 28 Verified, 8 Partial, 1 Not started of 37 (PROC-005 Verified); FTS-DD-001 §3, §5, §6, §7 rewritten, §9, §11.1, new §11.2 (the display drafts and what the PFD will ask for), §12; FTS-FM-001 0.2.0 (every response in validity terms, the start-up and validity section, the degradation table); README; CLAUDE.md.

### 12.2 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 231 of 231 (220 after the layout and the readers, 231 after the judgments), 2.3 s; `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings |
| pytest | 85 of 85 in 65.6 s (78 before; `test_validity.py` 7 in 14 s) |
| cppcheck 2.21.0 | 0 findings, the reserved members and the two name switches included |
| `--replay` of the version-1 reference log | exit 0; `Replay input:  v1/telemetry_000.bin (format version 1; this build writes version 2, so the replay log is version 2 with the computed fields and their validity recomputed from the raw fields)`; the replay log version 2, 500 frames, every raw field equal to the input's, every validity NORMAL |
| `--replay` of a file whose header says version 3 | exit 1, `error: cannot replay ../bad/telemetry_000.bin: header mismatch: format version 3; this build reads 1 to 2`; no `logs/` created |
| `--replay` of a 5-frame log for 12 cycles | `Replay:    exhausted after 5 frames; 7 cycles repeated the last frame` |
| The `Validity:` line, 500-frame session and its replay | `every computed field NORMAL on 500 of 500 frames (first on frame 0); NO_COMPUTED_DATA on 0 frames, FAILURE_WARNING on 0` |

Found by the tests while building: the identity test's first validity assertion expected NORMAL from the frame an injected window ended, where the detector keeps the channel DEGRADED for M more valid readings (the recovery latency of REQ-FAULT-005), so the bound is now derived from `recovery_count`; the version-1 replay pytest compared the Python floats it packed with the float32 values the file holds (9.81 against 9.8100004), so it compares the file's own read-back; and the preview showed one untaken outcome in `judge`, the `baro_ever_fresh_` operand of `baro_contributes`, which the filter's existence implies (it is born from a barometer reading), so the operand went, with the reason in a comment, rather than a marker.

### 12.3 Coverage preview (clang, the Mac recipe of Entry 8, unit tests only)

| Scope | After the layout (commit 1) | After the judgments (commit 2) | Threshold |
|---|---|---|---|
| processing | 100.0% | 100.0% (99.1% before the `baro_contributes` change: 45 of 46 in `processor.cpp`) | 90% |
| config | 97.8% | 97.8% | 90% |
| timing | 100.0% | 100.0% | 80% |
| logging | 86.1% | 86.1% | 80% |
| transport | 89.3% | 89.3% | 80% |
| replay | 100.0% | 100.0% | 80% |
| drivers | 100.0% | 100.0% | 70% |
| overall | 96.7% | 96.6% | 80% |

Per file after commit 2: `telemetry_frame.cpp` 9 of 9 (the no-match outcome of both name switches taken by the `UNKNOWN` tests, so neither needs the `LCOV_EXCL_BR_LINE` the detector's name switches carry), `fault_detector.cpp` 52 of 52 (the NO_DATA branch and the reordered promotion), `processor.cpp` 44 of 44, `log_format.cpp` 12 of 12 (every refusal taken), `log_replay_source.cpp` 12 of 12, `log_reader.cpp` 38 of 48. The logging scope rose from Entry 11's 83.3%: the reader's header check moved its call onto its own line (the loader's style rule), so the four clang unwind edges that line carried are gone with its marker. The ten untaken edges left in `log_reader.cpp` are clang's unwind edges on the `is_open` and `gcount` conditionals of the constructor and on the two marked lines of `list_log_files`, which the record does not count at all (§11.7). No marker was added by this topic: every new conditional sits on a call-free line or tests a local. Prediction for §12.7: processing, logging and replay at 100% on GCC, the rewritten reader line showing its two decision edges and no unwind edge; if any new line reads otherwise, §12.7 names it.

### 12.4 The byte checks

The reference is the 500-frame seed-42 log of the build before this topic (`15e8c35`, the same bytes as `9e68222`'s, §11.4), run from a scratch directory with `--config config/telemetry_config.yaml --cycles 500`; each candidate is the same run on the same Mac. The comparison script reads both files record by record and reports the header versions, the set of frame offsets that differ, the validity patterns seen and whether the reserved bytes are zero and every event identical.

| Log | SHA-256 | Against the reference |
|---|---|---|
| Reference, format version 1 | `3124895fdf8997907aa43343ae41ce735cedc4ad8219a38b6829e50af1e795e8` | — |
| After the layout and the readers (commit 1) | `91136212b21692e3012ed8f7f901c47e2b34ca360ce93e274b2e8fa0fdefe74b` | `cmp -l` prints one line, `5 1 2`: the header's version byte. The script: differing frame offsets none, validity pattern `00 00 00 00 00` on all 500 frames, reserved bytes zero, 0 events. The layout changed no value. |
| After the judgments and NO_DATA (commit 2) | `6d43f343311b77c107a1af4ff40bbdaa827b7efd4eea5b5c12055f29f80da345` | `cmp -l`: offset 5 and, in every one of the 500 frames, exactly offsets 98 to 102, each 0 becoming 3 (2,500 bytes). The script: differing frame offsets `[98, 99, 100, 101, 102]`, validity pattern `03 03 03 03 03` only, reserved bytes zero. No status byte changed: the simulator is fresh on every channel at t = 0, so every channel is NOMINAL from frame 0 and every judgment NORMAL. |

The replay of the commit-2 log through the same build `cmp`s silent against it (identity within a version, REQ-LOG-003). The version-1 reference log replayed through the commit-2 build: 500 frames, every raw field equal to the input's, every validity NORMAL, and every computed field byte-equal to the version-1 log's own recorded value, because the one arithmetic change (the barometric altitude held instead of converted) is a no-op on a session where every barometer reading is accepted. That is the claim of REQ-LOG-005's rationale in bytes: a change that keeps the raw bytes is compatible, and the pipeline recomputes the rest.

### 12.5 Decisions taken, and deviations from the plan

- Decision 7, `ChannelStatus::NO_DATA`, is included as recommended; `transition` reads `!= DEGRADED` for the promotion and has a NO_DATA branch for the first reading. The nine first-frame-fault cases the plan named as the regression net all passed unchanged, as did every source test asserting NOMINAL from a source.
- The marker question of Entry 11 §11.8 was not decided by this topic. The one marked line it had to rewrite, `log_reader.cpp` 17, now follows the loader's style rule and carries no marker; line 64 and the other five marked lines are untouched. No new marker anywhere.
- `header_is_valid` stays as a one-line wrapper over `header_problem` (one caller, `LoggingTest.FrameMessageIsHeaderTagAndFrame`) rather than being removed.
- The replay source's `exhausted()` and `frames_read()` reach `main` through a `static_cast` from the `DataSource` pointer, guarded by the `--replay` flag that built it: the composition root is the one place that knows the concrete type, and `create_source`'s signature is unchanged. The exit line prints only when the log was outlasted.
- The `Validity:` line counts frames (every field NORMAL; any field NO_COMPUTED_DATA; any field FAILURE_WARNING) and the first all-NORMAL frame; the Python summary counts frames and field-frames.
- `synthetic_log.make_frame` defaults every validity to NORMAL, so a synthetic log reads as a processed one and the noise oracles' numbers do not move; the compatibility tests set NOT_JUDGED explicitly. `output_report` reports `n` and `excluded_not_normal` per field and survives a field with no NORMAL sample (`mean` None), which no log today produces.
- `baro_contributes` dropped its `baro_ever_fresh_` operand (§12.2). The flag itself stays: the barometric altitude's own judgment needs it.
- Plan flag 5 (`kMinAccelMagnitude`, a constant no requirement names) is unchanged and noted here; flags 10 and 11 (the Roadmap's REQ-RED-004 wording, the Map's TC numbering) are annotated in the Obsidian notes, not in a controlled document.

### 12.6 Library and toolchain facts this entry rests on

- `std::has_unique_object_representations_v<TelemetryFrame>` is false for any type with a floating-point member, on Apple clang and on GCC 13 (checked during planning), so the proof of "no unspecified byte" is the arithmetic `static_assert` on the sum of the members plus `offsetof` pins. `offsetof` on the standard-layout frame raises nothing under `-Wall -Wextra -Wpedantic -Werror`.
- `struct.Struct("@Q11fI2d5f3B3?5BB").size` is 104 with no trailing `0Q`, and numpy's `align=True` dtype agrees at every offset (the reader's import-time assertion). `struct.pack` refuses `0.0` for the `B`, `H` and `I` codes, so the synthetic zero frame needs integer zeros.
- A brace-built `FaultEvent{ts, channel, type, 0.5f}` fails to compile once `reserved` sits before `value` (a narrowing conversion into `uint16_t`), so every brace site had to name the reserved field: the compiler enforcing the layout change at three test sites.
- cppcheck 2.21.0 reports nothing on the reserved members, the trailing `return "UNKNOWN"` of the name switches, or the `static_cast` in `main`.
- lcov 2.5 on clang data counts the two marked `list_log_files` lines at 8 and 16 edges; §11.7 recorded that the runner's lcov 2.0 drops both lines entirely.

### 12.7 Measurement of record — GitHub Actions run 38499690270, commit `e0a8792`, Ubuntu 24.04, GCC 13.3

The first CI run with this entry's commits (`0101e0a`, the judgments, and `e0a8792`, this record), 2026-10-14 02:11 UTC, green in 2 min 20 s: Install tools 21 s, Configure 7 s, Build 18 s, Unit tests 4 s, Set up Python 2 s, Install the Python side 7 s, Integration tests 67 s, Coverage report 4 s, thresholds 2 s, cppcheck 1 s. Image `ubuntu-24.04` (20261004.327.1), Entry 11's toolchain unchanged: GCC 13.3.0, CMake 3.28.3, lcov 2.0-4ubuntu2, cppcheck 2.13.0, libzmq 4.3.5, Python 3.13.16 with pytest 9.1.1. Written on the night of 2026-10-14 from the run's log and HTML report, after Entry 14's push, the section having stayed reserved through Entries 13 and 14; the README verification table and the FTS-TM-001 TEST rows cite Entry 14 §14.6, the run that supersedes this one.

| Item | Value |
|---|---|
| Unit tests (Google Test) | 231 of 231 passed, 2.22 s |
| Integration tests (pytest) | 85 passed in 67.44 s (78 in 64.77 s in Entry 11; `test_validity.py`'s seven are the growth) |
| Branch coverage (gcov/lcov) | 94.8% (534 of 563), every per-scope threshold met |
| Line coverage | 98.7% (1392 of 1410); functions 96.3% (157 of 163) |
| Per scope | processing 100.0%, config 100.0%, timing 100.0%, logging 100.0%, transport 96.2%, replay 100.0%, drivers 81.3%, overall 94.8% |
| Compiler warnings under `-Werror` | 0 |
| cppcheck findings | 0 on 24 files |

**§12.3's prediction held: processing, logging and replay at 100% on GCC.** Per file on the record: `telemetry_frame.cpp` 9 of 9, `fault_detector.cpp` 54 of 54 (the preview's 52), `processor.cpp` 54 of 54 (44), `log_format.cpp` 12 of 12, `binary_logger.cpp` 12 of 12, `log_reader.cpp` 18 of 18 (the preview's 38 of 48), `log_replay_source.cpp` 8 of 8 (12), `telemetry_config.cpp` 196 of 196 as in Entry 11, `live_sensor_source.cpp` 16 of 16. The reader's eighteen edges sit on nine `if` lines, two each: the rewritten header-check line (`if (!problem.empty())`, 7 refusals and 35 passes) carries its two decision edges and no unwind edge, and the three marked lines of `list_log_files` carry none, as §11.7 said they would. The branch total rose from Entry 11's 518 to 563 and the untaken count fell from thirty to twenty-nine: `i2c_bus.cpp` 15, `mpu6050.cpp` 7 and `bmp280.cpp` 6 (the Linux device-file error paths, compiled on the runner and unreachable without a bus) and `udp_publisher.cpp`'s `connect` failure, as before; the DEGRADED outcome of `status_name` in `telemetry_frame.cpp`, untaken since Entry 10, is taken now that `test_telemetry_frame.cpp` names every status. The six unhit functions are Entry 11's six deleting destructors (`Clock`, `DataSource`, `I2cBus`, `Publisher`, `LinuxI2cBus`, `UdpPublisher`).

**The pytest line.** The captured prints are Entry 11's to the number: TC-005, join index 0 and 200 received in 5.01 s on both transports; TC-010 step 6, ZeroMQ 500 received with 0 gaps, UDP 359 received and 141 gaps, 0 missed cycles, 11.01 s each; step 7, ZeroMQ 50 published and 0 refused, UDP 25 and 25. The replay-identity, log-reader, noise and configuration tests ran on the version-2 layout, and `test_validity.py` (TC-017 steps 11 to 15) ran for the first time against GCC's bytes: the validity bytes the Python reader finds at offsets 98 to 102 are the ones the GCC-built processor wrote, which is §12.4's byte check on the other compiler. Build 18 s (Entry 11's run: 26 s), no new dependency.

### 12.8 Open

- §12.7. (Written 2026-10-14 from run 38499690270.)
- The six remaining `LCOV_EXCL_EXCEPTION_BR_LINE` lines and the rule they rest on (Entry 11 §11.8), unchanged by this topic.
- Parked in FTS-DD-001 §11.2 and §12 for the PFD and the voter: a sequence number and a frame age, the Kalman covariance in the frame, GPS fix quality in a reserved byte, the run mode on the wire, pacing a replay.
- The Kalman filter is born from the barometer only (FTS-DD-001 §12): a session whose barometer never reads has NO_COMPUTED_DATA fused altitude although the GPS reads.
- `LogReplaySource` still skips an unreadable continuation file in a list silently (`ReplayTest.UnreadableFileIsSkipped`); `main`'s single-file `--replay` never reaches it.
- Unverifiable here (no GCC): the branch counts of the new code on the record, and whether the rewritten reader line keeps its two decision edges there. (Settled in §12.7: the three scopes at 100%, the reader line two edges, both taken.)

## Entry 13 — 2026-10-14, the GPS receiver: the serial port seam, the NMEA parser, the `NmeaGps` driver, the live source at the receiver's rate (macOS preview)

REQ-SENS-005 at unit level (FTS-SRD-001 0.8.0, which also reworded REQ-FAULT-001's GPS clause and REQ-CFG-001's list); FTS-FM-001 0.3.0 (FAULT-003); the new card TC-018. Built from the Obsidian plan `Plan - GPS` (approved 2026-10-13, built 2026-10-14) in the worktree branch cut from `e0a8792`, after the validity topic's Entry 12. No hardware: the board's own capture and the Pi runs are pending (§13.8). The first CI run with these commits is the measurement of record and goes in §13.7; everything else here is a macOS preview.

### 13.1 What was built

- `src/drivers/serial_port.h/.cpp`: `SerialPort`, one virtual `read(buffer, length)` returning `std::optional<size_t>` (the bytes waiting, 0 for none, nullopt on an error), never blocking; `PosixSerialPort(path, baud_rate)`: `open(O_RDONLY | O_NOCTTY | O_NONBLOCK)`, `tcgetattr`, `cfmakeraw`, `CLOCAL | CREAD`, `CRTSCTS` cleared, `VMIN` and `VTIME` zero, `CSTOPB` cleared and `IGNBRK` set (one stop bit; a BREAK ignored), `cfsetspeed` from one of six named rates (`supports_baud_rate`, the list `kSupportedRates` shared with the loader's message), `tcsetattr(TCSANOW)`, `tcflush(TCIFLUSH)` once; RAII on the descriptor, copying deleted, nothing thrown, `ok()`/`error()`/`last_errno()` as `LinuxI2cBus`; an empty read is 0 bytes (`VMIN` and `VTIME` zero); each read first asks `poll(2)` and a hung-up device (POLLHUP, POLLERR or POLLNVAL) is an error, as is a read that fails, `EAGAIN` included, both counted by the driver. termios is POSIX, so the class has no `#if`: the codebase keeps exactly two platform splits (FTS-DD-001 §3, §10).
- `src/drivers/nmea.h/.cpp`: `namespace nmea`, free functions with no state: `checksum_ok` (XOR of the body, two hex digits of either case, nothing after), `fields`, `formatter` (the last three letters of a five-letter address, any talker), `to_degrees` (degrees plus minutes over sixty, S and W negative; sixty minutes, 91° and a negative text refused), `knots_to_mps` (1852/3600), `parse_gga` and `parse_rmc` into `Gga` and `Rmc` (a field read in full, to its last byte, or not at all, so an embedded NUL, invisible to the XOR, cannot truncate a number; an empty field is absent and reads 0; a fix without a position, a count out of range, `nan`/`inf`, a status other than A, V or empty, a speed below 0 or a course outside 0 to 360 are malformed and the sentence is dropped whole).
- `src/drivers/nmea_gps.h/.cpp`: `NmeaGps(SerialPort&)`, `poll()` reading 256-byte chunks up to 4096 per call, a line assembler (a sentence starts at `$` and ends at a newline, `\r` stripped; bytes outside a sentence dropped and counted once per run; a line past 128 bytes dropped as overlong), the checksum check, dispatch on the formatter, and the epoch: a GGA and an RMC with the same UTC time field, in either order, a newer sentence replacing a stale pending one (counted as incomplete); a fix only when GGA quality is at least 1 and RMC status is A, a no-fix pair counted and dropped; two fixes in one poll deliver the later and count the earlier as skipped; `GpsFix` and `GpsCounters` (bytes, sentences, used, bad_checksum, malformed, overlong, resyncs, incomplete_epochs, no_fix_epochs, fixes, fixes_skipped, read_errors).
- `src/drivers/live_sensor_source.h/.cpp`: a sixth constructor parameter `std::unique_ptr<SerialPort> gps_port` (default null), `std::optional<NmeaGps> gps_` emplaced over it after `clock_` in declaration order, one `poll()` per `read_frame()`, the last fix held in `GpsFix last_gps_{}` and copied into the five GPS fields every frame, `gps_read_ok` true only on the cycle a fix completed, `gps_counters()`, `last_gps_fix()` for the exit line, and a one-second start-up grace (`kGpsStartupGraceMs`): a fix completing before the frame clock reaches 1000 ms is the device's backlog, counted (`gps_startup_discarded()`) and discarded, so the first fix delivered is live.
- `src/config/telemetry_config.h/.cpp`, `config/telemetry_config.yaml`: `GpsConfig { enabled false, device "/dev/serial0", baud_rate 9600 }` under `sensors.gps`; the loader reads the switch, non-empty text, and a whole number 4800 to 115200 that must then be one of the six rates (`PosixSerialPort::supports_baud_rate`, included as `mpu6050.h` is for the divider); 39 keys. The switch line carries no trailing comment so the sweep's two-line marker matches, the transports' convention.
- `src/main.cpp`: the usage text; `create_source` builds `PosixSerialPort` when `sensors.gps.enabled` and refuses startup by name (`error: GPS: <device>: <strerror> (sensors.gps.device; set sensors.gps.enabled to false to run without a receiver)`); `const LiveSensorSource* live`, the `replay` pointer's pattern; `print_gps_summary` at exit for live runs (`GPS:       N sentences (M GGA/RMC) on <device> (<baud> baud): fixes, epochs without a fix, incomplete, skipped, bad checksums, malformed, overlong, resyncs, read errors, bytes; last fix: quality, satellites, UTC time`, or `disabled (sensors.gps.enabled)`); nothing printed for simulated and replay runs.
- Tests: `tests/unit/nmea_examples.h` (the manual's sentences and the variants), `test_nmea.cpp` (22), `test_serial_port.cpp` (9, on a pseudo-terminal: round trip, control bytes unaltered, the stale-bytes flush, each rate, a missing device, `/dev/null`, an unsupported rate refused before `open`, the six rates, the ownership traits), `fake_serial_port.h/.cpp` (chunks served one per read, `queue_pause`, `queue_error`, `fail_every_read`, a log of every read), `test_nmea_gps.cpp` (16), five new cases in `test_live_sensor_source.cpp` (`FixIsFreshOnlyOnTheCycleItCompleted`, `EpochSplitAcrossCyclesIsFreshWhenItCompletes`, `WithoutAPortTheGpsIsAbsent`, `PortErrorLeavesTheGpsNotFresh`, `NoFixEpochLeavesTheGpsNotFresh`), and in `test_config.cpp` the drift test's three fields, three sweep rows (39), `sensors.gps` in `EverySectionIsRequired`, `GpsBaudRateMustBeStandard`, and `MissingKeyIsNamed`'s expectation (its `"    enabled: false"` marker now meets the receiver's switch first). Every test was written before the code it tests and watched fail on the missing header or member.
- Documents: FTS-SRD-001 0.8.0 (REQ-SENS-005 reworded with a rationale: GGA and RMC on a configurable serial device at a configurable baud rate, one reading per valid fix; REQ-FAULT-001's GPS clause "no valid fix has been read"; REQ-CFG-001 names the sensors' bus device and addresses and the receiver's presence, device and rate; the definitions line; the REQ-PROC-004 rationale's wording), 37 statements; FTS-FM-001 0.3.0 (FAULT-003 retitled, both conditions reaching the detector as the timeout, the counters telling them apart, the tests named); TC-018 (unit steps 1 to 19, hardware 20 to 24) and TC-014 step 12 conditional, TC-016's counts, §2.1, §2.4, §6.2, §7; FTS-TM-001 (SENS-005 Partial with its lists, FAULT-001, FAULT-003, FAULT-006, CFG-001, CFG-002, TEST-001 rows, the reverse-trace rows for the three new files, the config file at 39 keys; 28 Verified, 9 Partial, 0 Not started of 37); FTS-DD-001 §3 (the table row, the live source, "the serial port seam", "GPS driver"), §7, §10 ("the receiver's time and the frame's"), §11.2, §12 (the born-from-the-barometer bullet; "GPS fix latency" and "2D fixes" added); FTS-NP-001 and the "until REQ-SENS-005" wording family (design §5, the matrix's PROC-004 note, the YAML comment, `analysis/noise_profile.py`'s source string, `processor.h`'s comment); README; CLAUDE.md.

### 13.2 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 287 of 287 (231 before: 23 parser, 10 serial port, 16 driver, 6 live source, 1 configuration), 2.6 s; `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings |
| pytest | 85 of 85 in 66 s, run twice: after the configuration keys and after `main` |
| cppcheck 2.21.0 | 0 findings on 29 files |
| `--live` on the Mac | exit 1 on `/dev/i2c-1`, as before; `--help` names the receiver |
| The seed-42 500-frame log | SHA-256 `6d43f343311b77c107a1af4ff40bbdaa827b7efd4eea5b5c12055f29f80da345` after steps 0, 5, 6 and 7, the Entry 12 §12.4 reference: no byte of a simulated run changed; the recorded `telemetry_config.yaml` beside it carries the `sensors.gps` section; no `GPS:` line in a simulated run |

Found by the tests while building: `tcgetattr` on `/dev/null` answers ENODEV on macOS, not the ENOTTY the plan assumed for both platforms (the test asserts a nonzero errno that is not ENOENT, as `test_i2c_bus.cpp` does for the same file; TC-018 row 8 names both); the sweep's two-line marker `enabled: false\n    device` did not match while the switch line carried a trailing comment (the edited file was broken YAML), so the comment moved above the section; `MissingKeyIsNamed` removed the first `    enabled: false` line without a uniqueness check, which became the receiver's (the review had predicted it); a test helper took a C string where a corrupt `std::string` was passed. None changed a decision.

### 13.3 Coverage preview (clang, the Mac recipe of Entry 8, unit tests only)

| Scope | Preview | Entry 12 | Threshold |
|---|---|---|---|
| processing | 100.0% | 100.0% | 90% |
| config | 97.9% (233 of 238) | 97.8% (227 of 232) | 90% |
| timing | 100.0% | 100.0% | 80% |
| logging | 86.1% | 86.1% | 80% |
| transport | 89.3% | 89.3% | 80% |
| replay | 100.0% | 100.0% | 80% |
| drivers | 96.1% | 100.0% | 70% |
| overall | 96.2% | 96.6% | 80% |

Per new file, after the review's fixes: `nmea_gps.cpp` 56 of 56 branches, 140 of 140 lines; `live_sensor_source.cpp` 26 of 26 (16 of 16 before the receiver; the start-up grace's branches both taken), 85 of 86 lines, the unhit line a closing brace clang counts as a line; `nmea.cpp` 137 of 144, 195 of 195 lines; `serial_port.cpp` 27 of 33 branches, 69 of 75 lines, the hang-up check's branches both taken on the pseudo-terminal; `telemetry_config.cpp` 233 of 238, the five untaken still the marked `for` line's unwind edges (Entry 11), every branch of the `gps` section taken. The six untaken edges of `serial_port.cpp` are `cfsetspeed`, `tcsetattr` and `tcflush` failing on a terminal that opened (lines 65 to 67) and `read` itself failing (line 114; the hang-up a closed master causes is now caught by `poll` before the read, so no portable test reaches the read's own error): hardware-bound on both platforms, no exclusion, the same standing as `i2c_bus.cpp`'s device-file edges (Entry 8 §8.4). The seven untaken edges of `nmea.cpp` sit on five lines whose every logical outcome a test takes: `hemisphere == "S" || hemisphere == "W"` with N, S, E and W and the ternary that applies the sign (lines 204 and 205), the RMC status's `status == "A" || status == "V" || status.empty()` and `rmc.valid = status == "A"` with A, V, an empty field and X (lines 258 and 262), and `!ok || course < 0.0 || course > 360.0` with `fast`, `-1.0`, `361.0` and `360.0` (line 281); clang emits more edges than outcomes for a `||` chain, a `?:` and a `string_view` comparison, as §11.7 recorded for the loader. Prediction for §13.7: every decision of `nmea.cpp` and `nmea_gps.cpp` taken on GCC, `serial_port.cpp` with the same six hardware-bound edges, drivers above 96%; if any line reads otherwise, §13.7 names it.

The first preview read drivers 89.0% (`serial_port.cpp` 23 of 31, `nmea.cpp` 123 of 146, `nmea_gps.cpp` 54 of 60). Reading the 29 untaken edges one by one, as Entry 11 did, found ten real decisions no test took beside the hardware-bound and clang-only ones: the hex digits below `0`, between `9` and `A` and above `f`, a second checksum digit bad with the first good; `nan` and `inf` where a number is read; a quality of 10 and a satellite count of -1; a position with either half missing; an RMC whose latitude does not convert; a negative speed; a course of -1 and 361 (360.0 allowed); a malformed GGA and RMC reaching the driver; an RMC arriving twice; a pending GGA displaced by a newer epoch's RMC. Each is now a test. Three guards proved unreachable and went: `parse_count`'s empty check (an empty field now reads 0 inside it, so `parse_gga` calls it unconditionally and the rule "empty reads as absent" is tested on the quality and satellite fields), the assembler's non-empty check before `line_.back()` (a complete line always holds its `$`), and the `has_position` operand of the fix rule (`parse_gga` refuses a quality of 1 or more without a position, so the operand was a tautology, as `baro_contributes`' was in Entry 12). One ternary in `fields()` put two `substr` calls, which may throw, on a line with `?`, which the coverage style rule forbids (CLAUDE.md); it is an `if`/`else` with each call on its own line. No marker was added anywhere.

### 13.4 The oracles and the byte checks

The parser's oracle is the set of example sentences printed in the u-blox 6 Receiver Description (GPS.G6-SW-10018) with their printed checksums: GGA `*5B`, RMC `*57`, VTG `*06`, GLL `*60`, GSA `*0D`, GSV `*7F`, TXT `*50`, and the receiver's no-fix forms `$GPGGA,,,,,,0,00,99.99,,,,,,*48` and `$GPRMC,,V,,,,,,,,,,N*53`. Every checksum was recomputed by an independent XOR in Python before any test was written: eight of nine matched at the first attempt, and the ninth, the VTG, matched once its speed unit letter was corrected from `K` to `N`, which is the oracle doing its job on a sentence recalled from memory. The variants (another talker, a shifted time, a second epoch, a latitude of 91°, sixty minutes, a non-numeric count, a fix without a position, a status of `X`) carry checksums computed by the same XOR; the two no-fix sentences and the time-matched RMC of the GGA's epoch (`*5E`) are the driver tests' fixtures. The serial port's oracle is a pseudo-terminal: the bytes written to the master, every one, `\r\n` and the control bytes 0x11, 0x13, 0x03 and 0x00 included, come back through the port unaltered; a sentence written to a held-open slave before the port is constructed, and confirmed readable there by `poll(2)`, is gone after the constructor's flush.

The board, from its vendors' pages (checked 2026-10-13): a GT-U7, sold as NEO-6M-compatible and described as a u-blox 7th-generation chip, 9600 baud by default, up to 5 Hz if reconfigured, an EEPROM keeping a configuration, a micro-USB port that shows the sentences to a phone data cable; the listed supply voltage disagrees between sellers (3.3 to 5 V on one, 3.6 to 5 V on another), so the header-UART path in `Plan - GPS` powers it from 5 V.

### 13.5 Decisions taken, and deviations from the plan

- Derek's four decisions before the plan, all kept: no fix-quality byte and no format bump (the byte stays in FTS-DD-001 §11.2 with the 2D-fix question beside it); `sensors.gps.enabled` shipped false, a run with the receiver passing a configuration copy; the capture to be committed redacted (positions shifted by one constant pair onto the manual's Thalwil example, checksums recomputed by the script in the plan); the build notes in `Day 11 - 10-14` (the plan's Day 10 choice was conditioned on building the night of 10/13).
- The design review's three corrections, all applied: an empty NMEA field reads as absent rather than malformed (the no-fix GGA has empty time, position and altitude); the `MissingKeyIsNamed` marker; the traceability row moved with each commit, never left as Implemented.
- `ChannelStatus`, the detector and the processor are untouched: a receiver without a fix reaches the detector as the timeout, which REQ-FAULT-001's reworded clause says, and the exit line's counters distinguish it from a silent receiver. Entry 8 §8.7's last bullet (and Entry 7 §7.6's) asked that `gps_read_ok` be set only on a cycle in which a sentence completed; the rule landed stricter, a valid fix, and the bullet is closed here.
- The exit line prints the read-error count without a `(last port error: …)` suffix: `SerialPort`'s interface carries no errno and the live source owns the port. One errno the operator reads from the count.
- `SerialPort::read` returns `std::optional<size_t>` rather than the `int` the review sketched: nullopt is the error, 0 is nothing waiting, and no sign comparison enters the driver.
- `FakeSerialPort` gained `queue_pause()`, a queued "nothing waiting" that ends a poll between two epochs, because the fake returns 0 only when its queue is empty.
- The coverage-driven refinements of §13.3 (three unreachable guards, one ternary); behaviour unchanged, 284 of 284 green before and after.
- Not done, by the plan's own sequencing: TC-018 step 17 (the board's capture streamed three ways) waits for the file; the row and the card say so. The redaction script in `Plan - GPS` section 5 B is the one that produces the fixture.
- The whole-branch review (a fresh reviewer on the uncommitted tree) found no Critical issue and three Important ones, all applied: the RMC status compared as a `std::string_view` so the two lines carry no GCC exception edge; the port's `EAGAIN` story, which was false (an empty read is 0 bytes on both kernels with `VMIN` and `VTIME` zero, measured here; `EAGAIN` on Linux means a second reader), corrected in the code, design §3 and §13.6, with a failed read now counted whatever its errno; and TC-018 step 23's USB expectation, which Linux would contradict (a USB disconnect hangs up the open descriptor and every later read returns 0, so the channel stays DEGRADED until a restart; only the header UART can show RECOVERY), rewritten for both paths. Of its nine minors, eight applied: a number is read to its last byte so an embedded NUL cannot truncate it (`NmeaTest.EmbeddedNulInANumberIsMalformed`, red first), `CSTOPB` cleared and `IGNBRK` set, the last fix's quality, satellites and time on the exit line (`last_gps_fix()`), the card's wording for nine oracle sentences and the error-between-epochs step, the step-18 test asserting every status and validity, two comments still saying "FAULT-003a", the fake's `Read` with default member initializers, §6.2 naming `cfsetspeed` and the read path, and the six-rate list spelled once (`PosixSerialPort::kSupportedRates`). Deferred: pairing on the time field's text rather than its value, which the capture decides.
- Two more changes the same evening, from questions in the Opus chat, both test-first. The flush at open covers only what the kernel had queued, and the kernel receives from a USB device only while the port is open, so a receiver's backlog arrives after the flush and would have parsed as current fixes: the live source now discards and counts any fix completing in the first second of frame time (`FixesInTheFirstSecondAreDiscarded`; the fresh-fix test moved its epoch to 1000 ms). And a hung-up terminal reads 0 bytes forever, like an idle one, so the port asks `poll(2)` before each read and a hang-up is an error the driver counts (`HungUpDeviceIsAnError`, the pty's master closed): an unplugged USB receiver now shows as read errors climbing, which TC-018 row 23 says.

### 13.6 Library and toolchain facts this entry rests on

- termios, `posix_openpt`, `grantpt`, `unlockpt`, `ptsname`, `cfmakeraw`, `cfsetspeed` and `CRTSCTS` exist on macOS and on glibc; under CMake's default `-std=gnu++17` g++ defines `_GNU_SOURCE`, so `cfmakeraw` and `CRTSCTS` need no feature macro on Linux (do not add `CMAKE_CXX_EXTENSIONS OFF`). `CRTSCTS` is cleared through `static_cast<tcflag_t>(~CRTSCTS)` so the mask has the flag's own width.
- A pseudo-terminal slave in raw mode passes `\r`, 0x11, 0x13, 0x03 and 0x00 through; before raw mode its default discipline would translate and echo, so the tests write to the master only after the port's constructor has run, except the stale-bytes test, which holds its own slave descriptor open and waits with `poll(2)` until the bytes are readable before constructing the port. `ptsname` returns a static buffer, copied at once. With `VMIN` and `VTIME` zero an empty read returns 0 bytes, with or without `O_NONBLOCK`, measured on this Mac on a raw pty slave; on Linux `n_tty_read` returns on a zero timeout before its `EAGAIN` check, and `EAGAIN` there means another reader holds the tty (the review's finding; the port's first draft mapped `EAGAIN` to 0 on the belief that a non-blocking empty read reports it). After the master closes, a slave reads EIO on Linux and 0 on macOS, and `poll(2)` reports POLLHUP on both (measured here: POLLIN and POLLHUP set, the read 0 bytes), which the port checks before every read, so a pty whose master closed is the test of a hung-up device. A USB disconnect on Linux hangs up the open descriptor the same way; a pulled UART lead does not, and reads 0 bytes. Each pty write in the tests stays under a kilobyte (macOS drops input past 1024 bytes when the slave has not drained); the 4096-byte flood is on the fake.
- `tcgetattr` on `/dev/null`: ENOTTY on Linux, ENODEV on macOS. `open` of a nonexistent path: ENOENT on both. `strtod` reads `nan`, `inf` and hex floats, so `std::isfinite` guards the first two; a receiver sends neither.
- `std::string_view::substr` may throw `std::out_of_range`, so it is a call that may throw under the coverage style rule and never shares a line with a conditional.
- lcov 2.5 on clang data counts six edges for a two-term `||` and four for a `?:` (§13.3); the record's GCC 13 through lcov 2.0 counts two per decision (§11.7). The GitHub `ubuntu-latest` image moves to Ubuntu 26 after 2026-10-19 (Entry 11), so §13.7 may run on a newer GCC and cppcheck.

### 13.7 Measurement of record — GitHub Actions run 38668973387, commit `4783b9d`, Ubuntu 24.04, GCC 13.3

The first CI run with this entry's commits (`37e1fad`, the receiver, and `4783b9d`, this record), 2026-10-15 02:03 UTC, green in 2 min 33 s: Install tools 19 s, Configure 6 s, Build 28 s, Unit tests 5 s, Set up Python 1 s, Install the Python side 9 s, Integration tests 71 s, Coverage report 5 s, thresholds 3 s, cppcheck 1 s. Image `ubuntu-24.04` (20261012.334.1, a newer image than the runs before and after it drew from the pool, 20261004.327.1), the same toolchain: GCC 13.3.0, CMake 3.28.3, lcov 2.0-4ubuntu2, cppcheck 2.13.0, libzmq 4.3.5, Python 3.13.16 with pytest 9.1.1. Written on the night of 2026-10-14 from the run's log and HTML report, together with Entry 12 §12.7 and Entry 14 §14.6; the README verification table and the FTS-TM-001 TEST rows cite §14.6.

| Item | Value |
|---|---|
| Unit tests (Google Test) | 287 of 287 passed, 4.11 s, the pseudo-terminal tests included |
| Integration tests (pytest) | 85 passed in 70.68 s |
| Branch coverage (gcov/lcov) | 95.4% (792 of 830), every per-scope threshold met |
| Line coverage | 98.6% (1805 of 1831); functions 95.9% (188 of 196) |
| Per scope | processing 100.0%, config 100.0%, timing 100.0%, logging 100.0%, transport 96.2%, replay 100.0%, drivers 91.0%, overall 95.4% |
| Compiler warnings under `-Werror` | 0: termios, `cfmakeraw`, `CRTSCTS` and the pseudo-terminal calls compile clean under `-std=gnu++17`, the §13.6 fact |
| cppcheck findings | 0 on 27 files, `serial_port.cpp`, `nmea.cpp` and `nmea_gps.cpp` the three new |

**The new code on the record: 267 branches more than Entry 12's run, nine of them untaken, five of those real.** `nmea.cpp` 154 of 158, `nmea_gps.cpp` 59 of 60, `serial_port.cpp` 27 of 31, `live_sensor_source.cpp` 28 of 28 (16 before the receiver), `telemetry_config.cpp` 202 of 202 (Entry 11's 196 plus the `gps` section's six). The pseudo-terminal behaved on the runner as on the Mac: every serial-port test passed, and the port's untaken edges are the hardware-bound ones §13.3 predicted, `cfsetspeed`, `tcsetattr` and `tcflush` failing on a terminal that opened (lines 65 to 67) and `read` itself failing (line 114), one edge each where clang counted six: no exclusion, the standing of `i2c_bus.cpp`'s device-file edges. Drivers read 91.0% rather than the preview's 96.1% because the runner compiles the Linux I2C implementation, whose error paths no test reaches (28 edges across `i2c_bus.cpp`, `mpu6050.cpp` and `bmp280.cpp`, Entry 11 §11.7), where the Mac compiles the stub. Build 28 s (Entry 12's run: 18 s): three translation units and the new tests, no new dependency.

**§13.3's prediction failed on five outcomes, and its reading of the preview was wrong on two of them.** The preview's surplus edges on lines 258 and 281 were dismissed as clang's way of counting a `||` chain whose every outcome a test takes; GCC counts one decision per operand, and the record shows one outcome of each untaken, with three more the preview had not shown at all. Read from the report's per-edge counts: `nmea.cpp` line 100, `latitude.has_value() && longitude.has_value()`, the second operand never false, a longitude that fails to convert beside a latitude that converts (the four malformed-latitude sentences take the first operand's false edge; nothing takes the second); line 239, `position == Position::PRESENT && altitude_present`, a GGA with a position and an empty altitude field (the no-fix fixture has neither, six times); line 258, `status == "A" || status == "V" || status.empty()`, the empty status field, which §13.3 named as tested and is not: the no-fix RMC fixture says V, and the two refusals line 259 counts are the X sentence's; line 281, `!ok || course < 0.0 || course > 360.0`, a course that is not a number: the `fast` of §13.3 is fed as the speed (`test_nmea.cpp` line 205) and takes line 272's edge, never line 281's, while −1.0 and 361.0 take the other two; and `nmea_gps.cpp` line 163, `gga.fix_quality >= 1 && rmc.valid`, a fix-quality GGA paired with a status-V RMC (the three no-fix epochs have quality 0, so the second operand was never false). Each is a behaviour the code defines (MALFORMED, `has_position` false, a known status, a dropped RMC, no fix) that no test asserts. Five tests to add, red first against the branch data, in their own commit (§13.9); until then those five outcomes are defined by the code and unproven. Entry 11 found the preview counting decisions the record folds; this run shows the reverse, the preview's surplus hiding real gaps inside lines explained away as a whole. The rule that follows: an untaken edge on a `&&` or `||` line is read per operand on the record, never dismissed by the preview's total for the line.

**The pytest line.** The prints are Entry 11's to the number (join index 0 and 200 received on both transports; the stall 500 and 0 gaps on ZeroMQ, 359 and 141 on UDP; 50 and 0, 25 and 25 refused with nobody listening), on a build whose configuration carries the three `sensors.gps` keys and whose recorded copy the suite's `--config` runs read back. The eight unhit functions are Entry 12's six deleting destructors plus `SerialPort`'s and `PosixSerialPort`'s: the interface's is unreachable, and the concrete port is destroyed as a complete object in every test and through the base pointer only inside a live source, which no test builds with a real port.

### 13.8 On-target results

Reserved for the Raspberry Pi with the GT-U7: which port the board offers (`/dev/ttyACM0`, `/dev/ttyUSB0` or the header UART, and which UART), the 30-second capture's provenance and the redaction applied to it before it became `tests/unit/data/gt_u7_capture.nmea` (TC-018 step 17, the three-way streaming test), the run with the receiver enabled and no fix and the run with a fix (steps 21 and 22: the statuses and events, the first fix's frame, the `GPS:` line, the replay `cmp`, the reader's `fresh` counts), the receiver unplugged and replugged (step 23), the port left closed for two minutes before a run so the discarded count gives the backlog's depth and the first delivered fix's UTC time is checked live, the barometer's SDA pulled while the receiver fixes (step 24), and the jitter summary of a live run with the receiver polled. SENS-005 moves to Verified on step 22.

### 13.9 Open

- §13.8. (§13.7 written 2026-10-14 from run 38668973387.)
- Five parser and driver outcomes the record shows untaken (§13.7): a longitude that fails to convert beside a good latitude, a GGA with a position and an empty altitude, an RMC with an empty status field, a course that is not a number, a fix-quality GGA paired with a status-V RMC. Five tests, red first, in their own commit; TC-018's unit rows and the matrix's SENS-005 test list grow with them, and §13.3's sentence on lines 258 and 281 stands corrected by §13.7. (Closed 2026-10-14, Entry 15: the five tests, each proven against its line by breaking that line and watching the test fail, in their own commit; TC-018 steps 2, 3, 5 and 13 and the SENS-005 row carry them.)
- The capture fixture and TC-018 step 17's test, which land when the file does.
- The six remaining `LCOV_EXCL_EXCEPTION_BR_LINE` lines and the rule they rest on (Entry 11 §11.8), unchanged by this topic.
- FTS-DD-001 §12: GPS fix latency (the frame keeps the cycle's time; a fix is up to half a second old at 9600 baud); 2D fixes (quality 1 for 2D and 3D alike; GSA or a satellite count, parked with the fix-quality byte); the Kalman filter born from the barometer only, now decidable once a capture with a fix exists; the GPS variance retained at 25 m² until such a capture is analysed.
- FAULT-003b's own event, with the fix-quality byte at the next format bump.
- Unverifiable here (no GCC): the branch counts of the new code on the record, and whether the runner's pseudo-terminal behaves as the Mac's did. (Settled in §13.7: the counts above, five real gaps among them; the pseudo-terminal behaved the same.)

---

## Entry 14 — 2026-10-14, FAULT-010 at both rails: the accelerometer saturation limits derived from the driver (macOS preview)

One defect, parked since Entry 8 §8.7 (2026-10-12): positive accelerometer saturation never tripped. FTS-SRD-001 0.9.0 (REQ-FAULT-003 reworded with a rationale), FTS-FM-001 0.4.0 (FAULT-010), TC-013 step 7, the matrix, design §3 and §12; the key `fault.accel_limit_mps2` removed (38 keys). Built from the Obsidian plan `Plan - FAULT-010 Positive Saturation` (written from the chat's outline and executed in one session on 2026-10-14) in the worktree branch cut from `4783b9d`. The first CI run with this commit is the measurement of record and goes in §14.6.

### 14.1 The defect, and how the tests missed it

`Mpu6050::read()` converts each axis as `count / 16384 × 9.81`. A 16-bit two's-complement count runs from −32768 to +32767, so the negative rail converts to exactly −2 g (−19.62 m/s²; 19.62 is the same float as `2.0f * 9.81f` and as the parsed key, 19.6200008) and the positive rail to 32767/16384 × 9.81 = 19.619402 m/s², one count (9.81/16384, 0.000599 m/s²) short. The detector compared `fabs(a) >= 19.62`: the negative rail satisfied it; the positive rail never could. The one test, `AccelSaturationDegradesImu`, fed `2.0f * 9.81f` itself, a value the driver cannot produce, so it passed against a detector that could not see a saturated positive axis, and nothing in the pytest suite or the simulator approaches a rail. Checked in IEEE single precision before any edit (numpy float32): 32767 → 19.619402, 32766 → 19.618803, −32767 → −19.619402 (minus the positive rail, bit for bit), −32768 → −19.620001; 32767/16384 is exact in a float, so the gap is the sensor's, not rounding's.

No other current threshold has this kind of mismatch: the pressure and temperature limits are plausibility bounds inside the sensor's output range, the timeouts and counts are not sensor ranges, and the GPS fix-quality limit is read from the sentence. The one other full-scale comparison the system could have, the gyroscope's (FTS-FM-001 TODO, design §12), has no entry; written as a nominal 250 deg/s it would err the other way, since 131 LSB per deg/s puts the rails at +250.13 and −250.14.

### 14.2 What was built

- `src/drivers/mpu6050.h`: `kAccelPositiveRailMps2` and `kAccelNegativeRailMps2`, `INT16_MAX` and `INT16_MIN` through the same three constants in the same order (divide, then multiply) as `read()`, so each is the float `read()` returns for 0x7FFF and 0x8000.
- `src/processing/fault_detector.cpp`: the `find_if` predicate is `a >= kAccelPositiveRailMps2 || a <= kAccelNegativeRailMps2`, two signed tests, inclusive at each rail; no `fabs`, because −32767 converts to minus the positive rail and is a legitimate reading one count inside the negative rail; the file includes the driver header, as the loader does for the divider rule. `FaultDetectorConfig` loses `accel_limit_mps2`.
- `config/telemetry_config.yaml`, `src/config/telemetry_config.cpp`: the key and its `read_positive` line removed; a comment in the file says where FAULT-010's limits live; 38 keys. `test_config.cpp`: the drift-test line, `AccelLimitPositive` and the sweep row removed, the sweep's count 38.
- Tests: `FaultDetectionTest.AccelAtPositiveRailDegradesImu` (written first and watched fail against the unchanged detector), `AccelAtNegativeRailDegradesImu`, `AccelOneCountInsideEitherRailIsValid` (+32766 and −32767 on each axis in turn: NOMINAL, no event; the test first asserts that −32767's value equals minus the positive rail), replacing `AccelSaturationDegradesImu`; `FaultOnFirstReadingDegradesFromNoData` feeds the positive rail instead of `2.0f * 9.81f`; `Mpu6050Test.ExtremeCountsAreTheRails` (TC-013 step 7: `read()` of 0x7FFF and 0x8000 equals the constants under `EXPECT_EQ`, the positive rail below 2 × g, the negative rail equal to −2 × g, the gap one count).
- Documents: FTS-SRD-001 0.9.0 (REQ-FAULT-003: pressure and temperature against configurable limits, the accelerometer at either rail of the full scale the driver configures, GPS fix quality; a rationale), 37 statements; FTS-FM-001 0.4.0 (the "every threshold is a key" sentence gains its exception; FAULT-010's Detection, Test and the gyroscope TODO); FTS-TP-001 (TC-013's objective, step 7, its expected row and pass/fail; TC-016 steps 4 to 6 at 38 keys); FTS-TM-001 (the FAULT-003 row: the driver header, eight detector tests and the driver test, TC-013 step 7, the note; the CFG-001 and CFG-002 rows; the reverse trace for `mpu6050.h` and the file; the SRD pin 0.9.0; 28 Verified, 9 Partial, 0 Not started of 37, unchanged); FTS-DD-001 (§3's MPU6050 paragraph, and the configuration paragraph's count and not-keys list; §12 "Accelerometer saturation rails", decided, and the gyroscope bullet); README and CLAUDE.md (38 keys).

### 14.3 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 289 of 289 (287 before: four added, two removed with the key), 2.95 s; `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings |
| The red run | `AccelAtPositiveRailDegradesImu` against the unchanged detector: `test_fault_detection.cpp:367` `imu_status` NOMINAL where DEGRADED was expected, `:369` 0 events where 1; the negative-rail, one-count-inside and driver tests green against it, because the negative rail already tripped and the values one count inside were already NOMINAL |
| pytest | 85 of 85 in 66.0 s, the main checkout's venv against this worktree's binary |
| cppcheck 2.21.0 | 0 findings on 29 files, CI's flags |
| The seed-42 500-frame log | SHA-256 `6d43f343311b77c107a1af4ff40bbdaa827b7efd4eea5b5c12055f29f80da345` before and after, the Entry 12 §12.4 reference: the simulated accelerations never approach a rail and the run logs 0 events; its replay at `--cycles 500` `cmp`s silent with the same hash; the `telemetry_config.yaml` recorded beside it no longer carries `accel_limit_mps2` |

### 14.4 Coverage preview (clang, the Mac recipe of Entry 8, unit tests only)

| Scope | Preview | Entry 13 | Threshold |
|---|---|---|---|
| processing | 100.0% | 100.0% | 90% |
| config | 97.9% (233 of 238) | 97.9% (233 of 238) | 90% |
| timing | 100.0% | 100.0% | 80% |
| logging | 86.1% | 86.1% | 80% |
| transport | 89.3% | 89.3% | 80% |
| replay | 100.0% | 100.0% | 80% |
| drivers | 96.1% | 96.1% | 70% |
| overall | 96.2% | 96.2% | 80% |

`fault_detector.cpp` 54 of 54 branches: clang puts the predicate's one decision on the `||` line (118), both edges taken, and executes the second operand's line 5007 times of 5009, the two short-circuits being the positive-rail frames; GCC will count the second operand as a decision of its own, and both of its outcomes are taken (the negative rail, and everything else). `mpu6050.cpp` 28 of 28; the constants add no branch. The removed `read_positive` line carried no branch of its own (the template's branches belong to its other instantiations), so the config scope's count is Entry 13's, the five untaken still the marked `for` line's unwind edges (Entry 11).

The first capture read processing 98.1%, config 0.0% and overall 67.5%, with the profiler complaining at the test run that it could not merge `telemetry_config.cpp.gcda` ("cannot merge previous GCDA file: corrupt arc tag"). Cause: `gtest_discover_tests` runs the instrumented `unit_tests` at link time to list the tests, which writes a `.gcda` for every object; the tree was then rebuilt with the fix, and the new objects could not merge into the files the old ones had written, so one object's data was discarded and another's was stale. Deleting every `.gcda` under `build-cov` before the test run gave the table above. Not a property of the code; a step of the recipe from now on (§14.5).

### 14.5 Decisions taken, and deviations from the outline

- The key `fault.accel_limit_mps2` is gone, which reverses `Plan - YAML Configuration`'s decision to make it one. A stated number is exactly what drifted from the sensor, and "the threshold can't drift from what the sensor can produce" cannot hold while a file can say 19.7; the rails are a fact of the driver's `ACCEL_CONFIG` write, like the register maps and chip identities design §3 lists as "facts, not tuning". The alternative, kept open in §12: a key for the full-scale range in g that the driver would write and both rails would derive from, a REQ-SENS-003 change for the day the range needs configuring. Consequence: a configuration recorded beside a log older than this change carries the key and is refused on replay as unknown; `--config` with a current file is the remedy, as after the `sensors.gps` keys.
- The gap lived in Entry 8 §8.7, not in design §12 as the outline said; §12 now carries the decision.
- Two signed tests rather than `fabs` against the positive rail, which would flag −32767, one count inside the negative rail (`AccelOneCountInsideEitherRailIsValid` is the guard). `std::find_if` and its marker stay as they were; only the predicate changed.
- The rails are constants in the class rather than a shared `constexpr` function: a static data member's initializer inside the class body is not a complete-class context, so it cannot call a static member function declared in the same class; the exact-equality driver test is the proof of the link instead.
- The test plan and this entry are touched beyond the outline's four documents: TC-013 step 7 so the driver test has a card step as its siblings do, TC-016's counts, and this record.
- The Mac coverage recipe gains a step: delete `build-cov/**/*.gcda` before running the instrumented tests (§14.4).

### 14.6 Measurement of record — GitHub Actions run 38674948292, commit `c988cf9`, Ubuntu 24.04, GCC 13.3

The first CI run with this entry's commits (`b9dce78`, the rails; `72edb7f`, this record; `c988cf9`, §14.7's close-out and the design §12 bullet), pushed together, 2026-10-15 03:11 UTC, green in 2 min 33 s: Install tools 28 s, Configure 6 s, Build 24 s, Unit tests 3 s, Set up Python 3 s, Install the Python side 8 s, Integration tests 68 s, Coverage report 4 s, thresholds 1 s, cppcheck 1 s. Image `ubuntu-24.04` (20261004.327.1): GCC 13.3.0, CMake 3.28.3, lcov 2.0-4ubuntu2, cppcheck 2.13.0, libzmq 4.3.5, Python 3.13.16 with pytest 9.1.1. This is the measurement of record for REQ-TEST-002, REQ-TEST-003 and REQ-TEST-004; the README verification table and the FTS-TM-001 TEST-002 to TEST-004 rows cite it. Written the same night as Entry 12 §12.7 and Entry 13 §13.7, from the run's log and HTML report.

| Item | Value |
|---|---|
| Unit tests (Google Test) | 289 of 289 passed, 2.86 s |
| Integration tests (pytest) | 85 passed in 67.54 s |
| Branch coverage (gcov/lcov) | 95.4% (796 of 834), every per-scope threshold met |
| Line coverage | 98.6% (1805 of 1831); functions 95.9% (188 of 196) |
| Per scope | processing 100.0% (threshold 90%), config 100.0% (90%), timing 100.0%, logging 100.0%, transport 96.2%, replay 100.0% (80% each), drivers 91.0% (70%), overall 95.4% (80%) |
| Compiler warnings under `-Werror` | 0 |
| cppcheck findings | 0 on 27 files; 2.13.0 reports nothing on the constants or the lambda, as 2.21.0 did not (§14.3) |

**The predicate on GCC: four edges, all taken, and the first time FAULT-010's decision has been measured on the record.** `fault_detector.cpp` 58 of 58 branches (143 of 143 lines), against 54 of 54 on Entry 13's run: the `||` line carries two decisions, `a >= kAccelPositiveRailMps2` true twice (the positive-rail test and the first-reading test, each on the x axis, where `find_if` stops) and false 17,043 times, `a <= kAccelNegativeRailMps2` true once (the negative-rail test, on z, after x and y failed both tests) and false 17,042 times. The growth is four, not the two §14.4 predicted, because the old predicate had contributed nothing: `return std::fabs(a) >= limit;` on the record's `-O0` build is a compare and a `setcc`, no conditional jump, so gcov saw no branch on that line in any earlier run (Entry 13's report shows it executed 17,024 times with no branch data), and the one decision this fault rests on was never inside the 94.2% or the 95.4%. The same shape sits on `nmea.cpp` line 262, `rmc.valid = status == "A";`, which carries no branch either. Not a defect of the record, a property of what branch coverage measures: a comparison that becomes a value has no edge, so a decision of that shape is proven by the test that asserts it or not at all, as §11.7 said of the marked lines; `AccelAtPositiveRailDegradesImu` fails on the old code (§14.3), measured or not.

**Everything else is Entry 13's run to the edge.** `telemetry_config.cpp` 202 of 202 on one line fewer (410 of 410; the removed `read_positive` line carried no branch of its own, as §14.4 said), and 202 on GCC where the preview counted 238: the thirty-six are Entry 11's per-instantiation and marked-line differences (232 against 196 there), unchanged by the key's removal. `mpu6050.cpp` 37 of 44 with the same seven device-file edges; the constants add no branch. The thirty-eight untaken edges are §13.7's thirty-eight: twenty-eight on the I2C device-file paths, four on the port's hardware-bound calls, the `connect` failure, and the five parser and driver outcomes no test takes. The eight unhit functions are the same eight deleting destructors. The pytest prints are the same to the number (join index 0 and 200 received; 500 and 0 gaps, 359 and 141; 50 and 0, 25 and 25), and the configuration the suite's runs record and read back carries 38 keys and no `accel_limit_mps2`.

### 14.7 Open

- §14.6. (Written 2026-10-14 from run 38674948292.)
- The gyroscope saturation entry, still parked (design §12), now with its derivation rule written beside it.
- A recorded configuration older than this change is refused on replay (§14.5); whether the stationary capture of 2026-10-13 has a record beside it decides whether FTS-NP-001's replay runs need `--config`. (Closed 2026-10-14, on Derek's review of this entry: there is no stationary capture of 2026-10-13. The noise capture has not run, because the sensors are not yet soldered, so no real recording carries the old key; the first one is recorded with the 38-key file and FTS-NP-001's replay runs need no `--config`. Nothing in the repository is a recording either: `logs/` is ignored and regenerated per run, and no `captures/` exists. The general gap this item stood in front of is the last bullet below.)
- The key's removal is Derek's to confirm or reverse before the v0.1.0 tag; nothing else in this entry depends on it except the key count. (Closed 2026-10-14: confirmed by Derek on review of this entry, the key stays removed as built; 38 keys is the configuration of record and `Plan - YAML Configuration`'s key is superseded.)
- The recorded configuration carries no schema version the way the log carries a format version, so a key change refuses every older record on replay without `--config`: the three `sensors.gps` keys (Entry 13) and this entry's removal each did that. Parked in FTS-DD-001 §12 (2026-10-14), not built; to be decided before a key change would break the replay of a real capture.

---

## Entry 15 — 2026-10-14, the five GPS outcomes the record showed untaken: a test for each, proven against its line (macOS preview)

Five unit tests and no code change: REQ-SENS-005 at unit level, TC-018 steps 2, 3, 5 and 13 extended, the FTS-TM-001 SENS-005 row; closes Entry 13 §13.9's second item. Built from the chat's outline (saved as the Obsidian plan `Plan - GPS Parser Tests`) in the same session and worktree as Entry 14, on `c17bafa`, which is `main` after the three CI sections of 2026-10-14 were committed. The first CI run with this commit is the measurement of record and goes in §15.4.

### 15.1 What was built

- `tests/unit/nmea_examples.h`: eight sentences, one field changed from the manual's in each, checksums recomputed by the same XOR as the other variants after it had matched the printed GGA, RMC and no-fix RMC: `kGgaLongitude181` and `kRmcLongitude181` (`18100.00000,E`), `kGgaNoAltitude` (quality 1) and `kGgaNoAltitudeQuality0`, `kRmcEmptyStatus` and `kNoFixRmcEmptyStatus` (`$GPRMC,,,,,,,,,,,,N*05`), `kRmcBadCourse` (`east`), `kRmcStatusVSameEpoch` (the oracle GGA's epoch, status V, mode N as the two no-fix forms).
- `tests/unit/test_nmea.cpp`, four tests, each asserting `checksum_ok` on its sentence first so the sentence could have reached the branch through the driver: `PositionWithoutAltitudeIsNoPosition` (step 2: at quality 0 a `Gga` with `has_position` false, altitude 0, quality 0, 8 satellites; at quality 1 refused), `RmcEmptyStatusIsNotAFix` (step 3: parsed, `valid` false, the oracle's position, speed and course; the all-empty form parsed, not valid, no position), `LongitudeThatDoesNotConvertIsMalformed` (step 5: refused as a GGA and as an RMC), `NonNumericCourseIsMalformed` (step 5).
- `tests/unit/test_nmea_gps.cpp`, one test: `FixQualityGgaWithStatusVRmcIsNoFix` (step 13: the oracle GGA and the V RMC of its epoch pair and no fix is delivered, `sentences 2`, `used 2`, `malformed 0`, `no_fix_epochs 1`, `incomplete_epochs 0`, `fixes 0`; then the oracle epoch delivers its fix with `incomplete_epochs` still 0, so the pending pair was consumed).
- Documents: FTS-TP-001 TC-018 steps 2, 3, 5 and 13 and their expected rows (the existing steps grew; no new numbers, so the hardware steps stay 20 to 24 in every document that cites them); FTS-TM-001 SENS-005 (`NmeaTest.*` 27, `NmeaGpsTest.*` 17, the note; Partial as before, 28 Verified, 9 Partial, 0 Not started of 37); this entry; Entry 13 §13.9 closed. `src/` untouched by the tests' commit: `git diff --stat -- src/` clean after every run below. On Derek's review of this entry (2026-10-14, after the push): a trailing comment on `nmea.cpp:258` stating the null-field rule, so the line numbers cited here stand; `FixQualityGgaWithStatusVRmcIsNoFix` named in FTS-FM-001 FAULT-003's Test list and the FTS-TM-001 FAULT-003 row, the line it covers being FAULT-003's fix rule; one touch-up commit.

### 15.2 The five, and the line each is proven against

A test that pins behaviour the code already has passes on its first run, and all five did. Each was therefore proven the other way round: the line it covers was broken, the test run against it, the line restored, by a script that restores the line whatever happens and ends by checking `src/` clean.

| Test | Line | The break | Against it |
|---|---|---|---|
| `LongitudeThatDoesNotConvertIsMalformed` | `nmea.cpp:100`, `latitude.has_value() && longitude.has_value()` | the longitude operand removed | fails: `parse_gga` and `parse_rmc` accept both sentences (`test_nmea.cpp:300`, `:302`) |
| `PositionWithoutAltitudeIsNoPosition` | `nmea.cpp:239`, `position == Position::PRESENT && altitude_present` | the altitude operand removed | fails: `has_position` true at quality 0, the quality-1 sentence accepted (`:143`, `:148`) |
| `RmcEmptyStatusIsNotAFix` | `nmea.cpp:258`, `status == "A" \|\| status == "V" \|\| status.empty()` | the empty operand removed | fails: the sentence refused (`:201`) |
| `NonNumericCourseIsMalformed` | `nmea.cpp:281`, `!ok \|\| course < 0.0 \|\| course > 360.0` | `ok && (course < 0.0 \|\| course > 360.0)`: a parse failure no longer refuses | fails: accepted with course 0 (`:310`) |
| `FixQualityGgaWithStatusVRmcIsNoFix` | `nmea_gps.cpp:163`, `gga.fix_quality >= 1 && rmc.valid` | the RMC operand removed | fails: a fix delivered, `no_fix_epochs` 0, `fixes` 1 (`test_nmea_gps.cpp:219`, `:223`, `:225`, `:232`) |

The scripted run reported the second break as passing. The Mac's GNU make 3.81 compares timestamps in whole seconds, and the restore of the first break and the write of the second landed in the second the object had been compiled in, so the object was not rebuilt and the test ran against the unbroken parser; rerun by hand after a pause it failed on both assertions. The same artifact left the driver's broken object in `build` after the last break (source and object stamped the same second), which the full suite caught at once: `FixQualityGgaWithStatusVRmcIsNoFix` failed under `ctest` until the two sources were touched and rebuilt. Both are the build's timestamps, not the code's; the instrumented tree, built after every restore, passed all 294 the first time.

**Why an empty RMC status is no fix rather than malformed.** NMEA 0183 calls an empty field a null field, data not available. The parser's rule (`nmea.h`) is that an empty field is absent and never an error, and §13.5 made that rule apply to every field, because a no-fix GGA has empty time, position and altitude. The status is the receiver's verdict and `rmc.valid` is true for an explicit `A` only, so a missing verdict can never make a fix; nothing in such a sentence is corrupt, so `malformed`, the counter the operator reads as a parser refusal, would misreport it. The sentence pairs with its GGA and the driver counts the epoch as what it is, a no-fix epoch (step 13's row). `X` is non-empty text with no meaning, the general malformed rule. The code did not say this; since Derek's review it does, in a trailing comment on line 258 (§15.5), beside the test's comment and this entry.

### 15.3 Test results and coverage (preview)

| Check | Result |
|---|---|
| Google Test | 294 of 294 (289 before), 3.07 s; `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings |
| The five alone, first run | 5 of 5 green, as expected of tests that pin existing behaviour; the proof is §15.2 |
| cppcheck 2.21.0 | 0 findings on 29 files, CI's flags (`src/` only, so unchanged by construction) |
| pytest | not rerun: no file the suite drives changed |

| Scope | Preview | Entry 14 | Threshold |
|---|---|---|---|
| processing | 100.0% | 100.0% | 90% |
| config | 97.9% (233 of 238) | 97.9% | 90% |
| timing | 100.0% | 100.0% | 80% |
| logging | 86.1% | 86.1% | 80% |
| transport | 89.3% | 89.3% | 80% |
| replay | 100.0% | 100.0% | 80% |
| drivers | 96.4% | 96.1% | 70% |
| overall | 96.3% (788 of 818) | 96.2% | 80% |

One edge more in the preview, five expected on the record. `nmea.cpp` 138 of 144 (137 before): the one is line 281's `!ok`, the only gap of the five that clang's accounting could see. Lines 100, 239 and `nmea_gps.cpp` 163 read two of two before and after, because clang puts a two-operand `&&` on one decision whose two outcomes other sentences already took, and line 258 keeps two of its eight surplus edges untaken with every outcome now fed, as `||` chains and `string_view` comparisons do here (§13.3). That is §13.7's finding seen from the other side: four of the five gaps are invisible in the preview and visible only on GCC, which counts one decision per operand. The six untaken edges of `nmea.cpp` are lines 204 (two), 205, 258 (two) and 262, all clang's surplus on lines whose every outcome is tested; `nmea_gps.cpp` 56 of 56, 140 of 140 lines. Prediction for §15.4: `nmea.cpp` 158 of 158 (154 on Entry 14's run), `nmea_gps.cpp` 60 of 60 (59), the untaken count 38 to 33 (the I2C device-file paths, the port's four hardware-bound calls, the `connect` failure), drivers 379 of 411 (92.2%, from 374 of 411), overall 801 of 834 (96.0%), unit tests 294.

### 15.4 Measurement of record

Reserved for the first CI run with this commit: GCC's count on the five lines against the prediction above, the per-scope table, the unit count.

### 15.5 Decisions taken, and deviations from the outline

- No change to `nmea.cpp` or `nmea_gps.cpp`, as the outline required. The empty status is explained in the test's comment and §15.2 rather than in the code; a one-line comment at `nmea.cpp:258` (an empty field is a null field: no verdict, not a fix, not an error) is proposed, Derek's to approve. (Approved on review, 2026-10-14: added as a trailing comment on the line, so 258, 262 and 281 stay where every record cites them.)
- The tests sit in their TC-018 step sections (2, 3, 5 and 13) and the card's existing steps grew rather than gaining numbers, so the hardware steps keep theirs.
- The quality-1 twin of the no-altitude sentence is in the step-2 test although it is refused rather than parsed: it shows what `has_position` false costs a fix-quality sentence, the rule at `nmea.cpp:242`, through the same operand.
- The V-status RMC carries mode `N`, as the two no-fix forms do; the empty-status RMC keeps the manual's `A` with one field changed, as `kRmcStatusX` does. Neither field is parsed.
- Beyond the outline, flagged and not edited: FTS-FM-001 FAULT-003's Test list names `NoFixEpochIsNotAReading` at the source, and the new driver test is its sibling. (Closed on review, 2026-10-14: named there and in the FTS-TM-001 FAULT-003 row, whose note now says both halves of the fix rule.)
- Method, for the next proof of this kind on this Mac: touch the source or wait a second between restoring a line and the next build, and rebuild after the last restore before trusting `build` (§15.2).

### 15.6 Open

- §15.4.
- The comment at `nmea.cpp:258` (§15.5), Derek's call. (Closed 2026-10-14: added on review.)
- Everything in Entry 13 §13.9 but its second item: §13.8, the capture fixture and step 17, the marked lines, the design §12 items, FAULT-003b.

## Entry 16 — 2026-10-14, the display's data path: a replay that ends and paces, the session status on the wire, the bridge and the page (macOS preview)

Six requirements (REQ-TIME-004, REQ-LOG-006, REQ-TRANS-004, REQ-DISP-003, REQ-DISP-004, REQ-DISP-005; SRD 0.9.0 → 0.10.0, 37 → 43 statements), four test cards (TC-019 to TC-022), two new C++ modules, one Python module, a page. Built from `Plan - PFD Data Path` (plan mode, approved 2026-10-14) on the branch `claude/pfd-data-path-part-1-457f04`, cut from `363b02b` (= `main`), in ten commits: the seventh closed the topic, the eighth adds `dashboard/src/badge.ts`, which the page's commit left out, the ninth reads an unpaced replay's burst over UDP in one status test, which the runner's ZeroMQ join lost (§16.5), and the tenth records the STALE flip's measurement and two notes for part 2; the branch stays off `main` until after the v0.1.0 tag, and the CI run on its head, on the draft pull request (number 1), is the measurement of record (§16.7).

### 16.1 What was built

- `src/data_source.h`: `virtual bool has_next()`, true by default; `src/replay/log_replay_source.h/.cpp`: the one-frame look-ahead (`has_next`) and `read_frame` consuming it, `exhausted` unchanged in meaning; `src/drivers/fault_injecting_source.*`: the forward. `src/main.cpp`: the loop asks the source before every read, `--cycles` is `std::optional<int>` (5 for a live or simulated run, no cap for a replay), the `Replay:` line names which ended the run (REQ-LOG-006).
- `src/transport/session_message.h/.cpp`: `RunMode`, `StreamState`, `session_message` (eight bytes: `FTSS`, version 1, mode, state, reserved), `mode_name`, `state_name`, `kStatusIntervalMs` 1000, `kEndedRepeats` 3; `src/transport/publisher.h`: `publish_status`, `PublishStats::status_published`; both publishers: `publish_status` through a shared `send_bytes`; `src/main.cpp`: the mode chosen with the source, RUNNING before frame 0 and every `1000 / period_ms` cycles, ENDED three times after the loop, `status messages = N` on the transport line (REQ-TRANS-004). `pipeline/frame_format.py`: `SESSION`, `SessionStatus`, `parse_session`, `parse_message` dispatching on the magic; `pipeline/receiver.py`: `session`, `ended`, the iteration ending on ENDED, `format_session`, main ending on ENDED and the exit line's session.
- `src/timing/timestamp_pacer.h/.cpp`: `TimestampPacer::wait_for`, the origin from the first frame, the signed deadline `origin + (t − t0)` clamped at the epoch, `sleep_until_ns` through the `Clock` seam, lateness in `CycleTiming` with whole periods late in `missed_cycles`; `src/main.cpp`: `--pace` (refused without `--replay`), the pacer after the read in `run`, the jitter log for a paced replay, the CSV named by the run's prefix (`<prefix>_jitter.csv`, which the matrix's REQ-TIME-002 note had described since the UDP topic while the code wrote `telemetry_jitter.csv`), the paced `Timing:` line (REQ-TIME-004). `config/telemetry_config.yaml`: the flag comment names `--pace` (no key, no value).
- `pipeline/bridge.py`: `Bridge` (a reader thread on `FrameReceiver.recv()`, `call_soon_threadsafe` to the loop, `websockets.asyncio.server.serve`, `broadcast`), `frame_message` without `WITHHELD_FIELDS`, `session_message`, `hello`, `DEFAULT_HOST` 127.0.0.1, `--listen`, `--period-ms`, `--stale-ms`; SIGINT and SIGTERM end it with a summary (REQ-DISP-005). `requirements.txt`: `websockets==17.2`.
- `dashboard/`: `package.json` (pinned, §16.6) and its lockfile, `tsconfig.json`, `vite.config.ts` (jsdom, coverage), `index.html`, `src/main.tsx` (`?bridge=` for a bridge elsewhere), `src/feed.ts` (the JSON contract typed field for field, `parseMessage`, `modeLabel`, `SocketLike`), `src/useTelemetryFeed.ts` (the hook: the socket, arrival time, the rate over the trailing second, `judge`, the ticker, the reconnect), `src/badge.ts` (`badgeFor`), `src/App.tsx` and `App.css` (the view), `src/fakeSocket.ts` and `src/testMessages.ts` (test support), `src/useTelemetryFeed.test.ts` (8), `src/App.test.tsx` (9), `README.md` (REQ-DISP-003, REQ-DISP-004).
- `.github/workflows/ci.yml`: the `page` job (Node 22, `npm ci`, the tests with coverage, the build) in parallel with the C++ job; the Python step's comment. `.gitignore`: `dashboard/node_modules/`, `dist/`, `coverage/`, `.claude/launch.json`.
- Tests: `tests/unit/test_replay.cpp` (+6, the thesis loop driven by `has_next`), `test_session_message.cpp` (4), `test_zmq_publisher.cpp` (+2, the shut-down context also refusing a status), `test_udp_publisher.cpp` (+2, the kernel's report also refusing a status), `test_timestamp_pacer.cpp` (6); `tests/integration/test_replay_pacing.py` (8), `test_session_status.py` (10), `test_bridge.py` (6), `test_validity.py` (the end-of-log line; its past-the-end test moved), `conftest.py` (the third record kind; the receiver closed on every exit path), `test_transport.py` (statuses on the UDP socket filtered).
- Documents: SRD 0.10.0 (the six statements and rationale, the Display section, REQ-CFG-001's flag rule); FTS-TM-001 (six rows Verified, the `main.cpp`, seam, replay, injector, publisher, Python and page reverse traces, TEST-001 and TEST-002, 34/9/0 of 43); FTS-TP-001 (TC-019 to TC-022, §2.2, §2.5, §3, §6.3, §7, TC-017 step 13); FTS-DD-001 (§3, §9: end of stream, the session status, the bridge, the page; §10: pacing, the CSV by prefix; §11.2; §12: real-time replay and end-of-stream decided, the sequence number's trigger moved to the voter, the rotated replay's number); README; CLAUDE.md; this entry.

### 16.2 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 314 of 314 (294 before), 3.2 s; `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings |
| pytest | 108 of 108 (85 before) in about 100 s on the plain build; 108 on the instrumented build too, once the paced lateness bound was amended (§16.5) |
| Page (Vitest 4.1.11, jsdom) | 17 of 17; statements 98.2%, branches 98.9%, lines 100%, functions 92.9% (the two real defaults, a `WebSocket` and `performance.now`, run in no test; one defensive `?? 0` in `badge.ts`) |
| `npm run build` | the TypeScript check and the bundle, clean (224 kB, 70 kB gzipped) |
| cppcheck 2.21.0 | 0 findings, CI's flags, 29 files |
| The hand run (plan §7) | the page in the built-in browser: SIM, RUNNING, 50 to 51 frames a second (the trailing-second window keeps its boundary frame, so a 50 Hz feed reads 50 or 51), every field NORMAL, the GPS held between fixes; ENDED at the run's end; REPLAY at the same rate through a paced replay of that run's 1500-frame log, then ENDED; STALE after the binary was killed mid-run (1,012 ms after the last frame by the measurement in §16.5; the 3.1 s and 3.7 s first read were the age when the page was looked at), the rate 0, every number struck through and legible; BRIDGE DISCONNECTED when the bridge was stopped, reconnected within a second of its restart (MODE UNKNOWN and STALE, the restarted bridge having seen no status) |

Red first throughout: the unit files were watched failing on the missing members and headers, the pytest files on the missing module or flag, the page tests on the missing modules; the end-to-end bridge test and the status tests then passed on their first run against the code, and the page's two rate tests failed on a boundary (§16.5) before passing.

### 16.3 Coverage preview (clang, the Mac recipe of Entry 8, unit and integration tests on the instrumented build)

| Scope | Preview | Entry 15 | Threshold |
|---|---|---|---|
| processing | 100.0% | 100.0% | 90% |
| config | 97.9% | 97.9% | 90% |
| timing | 100.0% | 100.0% | 80% |
| logging | 86.1% | 86.1% | 80% |
| transport | 88.9% | 89.3% | 80% |
| replay | 100.0% | 100.0% | 80% |
| drivers | 96.4% | 96.4% | 70% |
| overall | 96.2% | 96.3% | 80% |

The new and changed files: `timestamp_pacer.cpp` 6 of 6 branches, 30 of 30 lines; `log_replay_source.cpp` 16 of 16, 48 of 48; `fault_injecting_source.cpp` 16 of 16; `zmq_publisher.cpp` 8 of 8; `udp_publisher.cpp` 27 of 30 (the three edges Entry 10 lists, kernel-bound); `session_message.cpp` 5 of 7 before the two `switch` no-match edges were marked as the five sites of Entry 1 are (`LCOV_EXCL_BR_LINE` on the switch, `LCOV_EXCL_LINE` on the trailing return), which is the transport scope's 0.4 point. The page's figures are Vitest's (§16.2); Python coverage is not measured in this project, and the bridge's branches are listed against its tests by hand: the loopback and the explicit bind, `hello` with and without a session, a frame, a status, a client leaving, the end-to-end run; untested by any test, the `FormatError` count in the reader thread, the `dropped` count for a record kind the page has no use for, and `main` (the flags, the signal handlers, the summary), exercised by hand in §16.2's run.

### 16.4 The byte checks

| Check | Result |
|---|---|
| The seed-42 500-frame log, after every step | `6d43f343…` (Entry 12 §12.4), with and without `--zmq` |
| Its replay, capped at 500, uncapped, and capped at 9999 | each byte-identical to the original; `Replay:    500 frames, end of log reached` on all three |
| Its replay paced, against the unpaced replay and against the original | byte-identical; 500 frames paced in 9.99 s, lateness 0 / 3243 / 11526 us, none a period late |
| A 5-frame log for 12, 3 and no cycles | 5, 3 and 5 frames; the 12-cycle and the 5-cycle logs byte-identical, no timestamp repeated (TC-019 steps 6 and 7) |
| The demo run's 1500-frame log, replayed paced over ZeroMQ to the page | byte-identical; lateness 0 / 5477 / 16304 us, none a period late; `status messages = 33` (30 RUNNING, before frames 0, 50, ..., 1450, and 3 ENDED) |
| The pytest's 150-frame paced replay | byte-identical to the unpaced and to the original's first 150 frames; lateness 0 / 4844 / 14623 us, none a period late |

### 16.5 Decisions taken, and deviations from the plan

- All as recommended in the plan, with the answers to the trust test's flags folded in (three mode labels; no format change; deadlines from the recorded timestamps through the `Clock` seam; a replay to its end with `--cycles` as a cap; the CSV by prefix; REQ-DISP-003 and -004 now, -001 and -002 with part 2; the branch name kept).
- **The pytest harness closes the receiver on every exit path.** A test that handed `run_published` a directory that did not exist raised before the harness's `try`, the receiver was never closed, and the interpreter hung at exit for ten minutes: a pyzmq `Context` whose socket is still open blocks its termination forever. Fixed in the test (the directory created) and in the harness (`receiver.close()` in a `finally` around the whole run), so no later test can hang the suite that way.
- **The paced lateness bound.** The plan bounded the maximum under one period. On the instrumented build, under the load of the demo and the dev server, one frame of 150 woke 62 ms late on the Mac's `nanosleep` fallback, and again on a rerun. Never early is the pacer's property and byte identity its proof; how late is the machine's, as REQ-TIME-003's jitter is the Pi's. TC-020 step 7 now bounds the mean under a period and the frames a period or more late at one in twenty, and records the maximum; the `Timing:` line's count must agree.
- **The rate window keeps its boundary frame.** Fifty frames at 20 ms judged on the grid read 49 with a half-open window; the window is closed on its old side, so the page reads 50, and 51 at times, at 50 Hz.
- **`badge.ts`.** Vite's Fast Refresh reloads a page whose component module also exports a plain function, and a reload loses the feed's state (seen twice in the hand run: the frame counter back to 0, the ended state recovered from `hello`). `badgeFor` moved to its own module so an edit to the view hot-swaps it and keeps the state.
- React 19's types have no global `JSX` namespace; `App.tsx` imports the type from React.
- **The lockfile.** npm 10.9.3's resolver fails on this manifest with `Cannot read properties of null (reading 'edgesOut')` while walking jsdom's optional `canvas` peer, with a fresh cache too; `npm install --legacy-peer-deps` produced the lockfile, and `npm ci` from it installs cleanly (121 packages in 0.8 s), which is what CI runs. Recorded in CLAUDE.md's prerequisites.
- jsdom 30.1.2 declares Node `^22.22.2 || ^24.15.0 || >=26`; this Mac runs 22.18.0, npm warns, the tests run. CI's `setup-node` '22' resolves to a newer patch.
- `CycleIndexIncrements` is asserted inside `DeadlinesFollowRecordedTimestamps`; `DeadlineBeforeTheClockEpochIsClampedToZero` was added beyond the plan, because the clamp is a branch and needed a test.
- Lateness of a whole period or more rides `CycleTiming::missed_cycles` as whole periods, so `JitterLog` and its summary serve the pacer unchanged; the paced `Timing:` line prints it as "late by a period or more".
- The bridge handles SIGTERM like SIGINT: the demo's bridge, ended by `kill`, printed no summary.
- The two `switch` no-match edges in `session_message.cpp` are marked as Entry 1's five sites are (seven sites now).
- **The STALE flip, measured (2026-10-15, on Derek's question).** The hand run's reading of 3.7 s after the kill was the age when the page was looked at, not when it changed: each tool round trip after the kill took seconds. Measured with a `MutationObserver` on the page recording each frame's arrival and every change of the badge, the badge went STALE 1,012 ms after the last frame's arrival, the 1,000 ms threshold plus one 100 ms tick; the binary's exit, the receiver and the bridge add nothing, since the judgment is the page's own clock. In the same two runs all 544 and 2,500 frames arrived, the page's clock within −4 to +33 ms of the frame timestamps and no arrival gap over 60 ms. With the page hidden for 15.8 s of the second run (the pane hid it), the 444 frames of that window arrived at the recorded cadence (maximum gap 28 ms, lag −2 to +9 ms) and ENDED 1 ms after the last; the pane does not throttle a hidden page as Chrome does, so Chrome's background tab is recorded as untested, with the lag measurement part 2 adds for it, in FTS-DD-001 §12.
- **Found by the final run, a test defect of this topic's making:** `test_no_receiver_counts_and_continues[udp]` asserted `published + send_failures == 50`, which assumed every refusal the kernel reports is a frame's. Since the status rides the same socket, the port-unreachable report can land on a status send instead (`send_failures` counts frames and statuses alike), and the sum read 51 once in three full runs. The invariant is now over every attempt: frames published plus statuses published plus failures equals 50 frames plus the RUNNING status before frame 0 plus three ENDED, 54; the ZeroMQ variant still expects no refusal at all. Five consecutive runs over both transports pass.
- **The page's commit left out `dashboard/src/badge.ts`.** The commits were handed over as incremental patches taken with `git diff`; `badge.ts` was created after the page's patch, and the patch for its refactor was taken before the new file was marked for adding, so `git diff` did not see it, and the check that the nine patches rebuild the working tree could not either (`git diff --quiet` ignores an untracked file). The first push therefore carried an `App.tsx` that imports a module the branch does not hold; the branch's first CI run fails the `page` job on that import while the C++ job stands. Found when Derek asked for the commits to be checked; the eighth commit adds the file, and its run (38691853458) was the branch's first green one; the record is the run on the head (§16.7).
- **Found by the branch's first CI run (38691300022, the C++ job):** `test_replay_announces_replay` replayed a 60-frame log unpaced with a ZeroMQ receiver, and on the runner the whole burst, frames and statuses, went out before the subscriber's join landed, so the receiver saw nothing (`run.statuses` empty); on the Mac the join had won the race every time. That is the slow-joiner effect FTS-DD-001 Section 9 describes, and the lesson the pacing work already states: an unpaced replay that publishes is a burst. The test now reads the burst over a bound datagram socket, which has no join, and asserts the positions of the statuses (RUNNING before frames 0 and 50, ENDED three times after frame 59). The other 107 passed on the runner, the paced lateness bound among them; the `page` job's failure on that run was the missing `badge.ts`. The second run (38691853458, on the eighth commit) passed the same test unchanged over ZeroMQ, which is what a race looks like from the outside; the ninth commit removed the race rather than rely on winning it.
- Not done, by the plan's own order: the CI run of record (§16.7); TC-021 step 9 (the LIVE mode's status) and the paced replay's lateness on the Pi.

### 16.6 Library and toolchain facts this entry rests on

- `websockets` 17.2 (2026-10-03; Python 3.11 or later): the asyncio implementation (`websockets.asyncio.server.serve`, `broadcast`, `ServerConnection.wait_closed`); `broadcast(raise_exceptions=...)` is keyword-only since 17.0; `broadcast` is synchronous and writes without backpressure.
- pyzmq 27.2: a `Context` whose socket is still open blocks `term()` and therefore the interpreter's exit; `close(linger=0)` then `term()` in the receiver's `close()` is what the harness now guarantees.
- Node 22.18.0, npm 10.9.3. Pinned (the newest release of each package whose major was at least three months old on the build day; TypeScript 7.0, two months old, and Vitest 5.0, six weeks, skipped): react and react-dom 19.3.1, vite 8.3.4 (8.0 since 2026-03-12), @vitejs/plugin-react 6.1.2 (requires vite ^8), typescript 5.9.3, vitest 4.1.11 (accepts vite ^6 || ^7 || ^8), @vitest/coverage-v8 4.1.11, jsdom 30.1.2, @testing-library/react 16.3.3 with its peer @testing-library/dom 10.4.2, @types/react and @types/react-dom 19.3.0.
- Vite's Fast Refresh (plugin-react) hot-swaps only modules that export components alone; any other export means a full page reload on edit.
- jsdom provides `Event`, `MessageEvent` and `CloseEvent`, so the fake socket constructs real events; React Testing Library 16 needs an explicit `afterEach(cleanup)` when Vitest runs without globals.
- `vi.useFakeTimers()` drives `setInterval` and `setTimeout`; the hook's clock is injected rather than taken from the faked `performance`, so a test advances both together.

### 16.7 Measurement of record

CI run 38695063952 on `aab6772`, the branch's head, on the draft pull request (number 1), 2026-10-15 06:52 to 06:55 UTC: the fourth run on the branch (the first, on `62460db`, failed both jobs as §16.5 records; the second and third, on `0fb47f2` and `ccd1730`, passed). Both jobs green, and the repository's GitGuardian check with them.

| Check | Result |
|---|---|
| Google Test (GCC, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 314 of 314 |
| pytest (Python 3.13, against the instrumented binary) | 108 of 108 in 94.9 s |
| The paced replay's lateness on the runner (TC-020 step 7, `clock_nanosleep` with `TIMER_ABSTIME`) | min/mean/max 69 / 104 / 156 µs over 150 frames, none a period late; the Mac's `nanosleep` fallback gave 0 / 4,844 / 14,623 µs for the same test (§16.4) |
| lcov over `src/` | lines 98.6% (1,880 of 1,906), functions 96.2% (200 of 208), branches 95.7% (827 of 864) |
| cppcheck 2.13.0 (the runner's Ubuntu 24.04 package), 29 files | one style report, below; the Mac's 2.21.0 reports none (§16.2) |
| `page` job (Node 22, `npm ci`, Vitest 4.1.11 with coverage, `npm run build`) | 17 of 17; statements 98.24% (112 of 114), branches 98.94% (94 of 95), functions 92.85% (26 of 28), lines 100% (107 of 107), the same figures as the Mac; the build clean in 125 ms; 18 s for the whole job |
| Wall time | the C++ job 3 min 2 s; the workflow 3 min 4 s |

Per-scope branch coverage on GCC, from `scripts/coverage_check.sh` (thresholds from FTS-TP-001 §6.3), beside the Mac preview of §16.3:

| Scope | Record (GCC) | Preview (clang) | Threshold |
|---|---|---|---|
| processing | 100.0% | 100.0% | 90% |
| config | 100.0% | 97.9% | 90% |
| timing | 100.0% | 100.0% | 80% |
| logging | 100.0% | 86.1% | 80% |
| transport | 88.6% | 88.9% | 80% |
| replay | 100.0% | 100.0% | 80% |
| drivers | 92.2% | 96.4% | 70% |
| overall | 95.7% | 96.2% | 80% |

The files this entry added or changed, from the run's `coverage-html` artifact: `timestamp_pacer.cpp` 26 of 26 lines, 6 of 6 branches, 2 of 2 functions; `session_message.cpp` 17 of 17 lines, 3 of 3 functions and no branch counted at all, its only branches being the two marked `switch` lines (§16.5), so the markers hold on the runner's lcov as on the Mac's; `log_replay_source.cpp` 44 of 44 lines, 14 of 14 branches; `fault_injecting_source.cpp` 67 of 67 lines, 16 of 16 branches; `zmq_publisher.cpp` 40 of 40 lines, 10 of 12 branches (the shut-down context's two edges, Entry 10); `udp_publisher.cpp` 52 of 53 lines, 29 of 32 branches (the kernel-bound edges Entry 10 lists). The record agrees with the preview everywhere the two measure the same thing: every test green on both toolchains, the per-scope figures within the lines the two compilers count differently (GCC counts fewer lines in the pacer, 26 against clang's 30, with the same six branches).

**cppcheck's one report (REQ-TEST-004):** `src/drivers/fault_injecting_source.h:31:10: style: The function 'has_next' overrides a function in a base class but just delegates back to the base class. [uselessOverride]`. The override's body is `return inner_->has_next();` (`fault_injecting_source.cpp:9`): the call goes to the inner source, another object, not to the base class, and the forward is the override's whole purpose, since a replay wrapped by the injector must end when the replay ends (`ReplayTest.FaultInjectorForwardsHasNext`). cppcheck 2.13.0, the runner's package, reads a same-named call as a call to the base; 2.21.0 on the Mac does not report it, so the report is the older checker's. It fails nothing today, because the step reports on every push and gates only on `v*` tags (FTS-TP-001 §4 criterion 4), but the tag gate runs the same level with `--error-exitcode=1` and would exit on it as the runner's package stands. The remedy the workflow already provides is `--inline-suppr`: one `// cppcheck-suppress uselessOverride` line above the declaration, with this reason beside it, the first inline suppression in `src/`. Left open in §16.8 for the decision rather than taken inside a record.

### 16.8 Open

- cppcheck 2.13's `uselessOverride` report on `FaultInjectingSource::has_next` (§16.7): the inline suppression with its reason, or another remedy, before any `v*` tag that carries this branch. Taken in Entry 17's first commit: `// cppcheck-suppress uselessOverride` above the declaration, with the reason beside it; the runner's verdict is §17.7's.
- On the Pi: TC-021 step 9 (a live run's LIVE status reaching the receiver, the bridge and the page on the Mac), and a paced replay's lateness on `clock_nanosleep`.
- Parked in FTS-DD-001 §12 with this entry: replay across rotated files (part 2's demo decides), the sequence number (with the voter, on the same bump as the fix-quality byte).
- Part 2: the instruments (REQ-DISP-001 and -002 entering the SRD), `display_spec.md`, the deterministic replay demo; heading has no direct source on this hardware.

## Entry 17 — 2026-10-15, the Primary Flight Display's instruments: the flag rule, the six instruments, the lag check, the simulator's scripted flight and the replay demo (macOS preview)

Four requirements (REQ-DISP-001, REQ-DISP-002, REQ-DISP-006, REQ-SENS-007; SRD 0.10.0 → 0.11.0, 43 → 47 statements), four test cards (TC-023 to TC-026), one new controlled document (FTS-DS-001, `docs/display_spec.md`, the eighth), one new C++ module and an inverse function, one bridge class and two flags, twelve page modules. Built from `Plan - PFD Instruments` (plan mode, approved 2026-10-15, the five flags at its top as recommended) on the branch `claude/pfd-data-path-part-1-457f04`, after part 1's ten commits and the Entry 16 §16.7 commit, in seven commits (the twelfth to the eighteenth on the branch); the branch stays off `main` until after the v0.1.0 tag, and the CI run on its head is the measurement of record (§17.7).

### 17.1 What was built

- Documents: SRD 0.11.0 (REQ-DISP-001 and REQ-DISP-002 entered from the FTS-DD-001 §11.2 drafts, 002 reworded for the GPS track, no outside air temperature, no held cue; REQ-DISP-006, the lag; REQ-SENS-007, the scripted flight; the Display preamble; DISP-005's rationale); FTS-DS-001 0.1.0 (the conventions, the layout in viewBox units, one subsection per instrument, the feed's states, the plot, the demo's keyframes, the diagnostic view); FTS-TP-001 (TC-023 to TC-026, §7); FTS-TM-001 (four rows Verified, DISP-005's row, the reverse traces, TEST-001 and TEST-002, 38/9/0 of 47); FTS-DD-001 (§3 the profile seam, §6 the display's sign rule, §9 "At the display: the instruments", §11.2 and §11.3, §12: the scripted flight, `heading_deg`, the track threshold, the receiver's counter, the rotated replay decided, the two part-2 notes closed); README, CLAUDE.md, `dashboard/README.md`; this entry.
- `src/drivers/motion_profile.h/.cpp`: `MotionState`, `MotionProfile::named`, `state_at` (keyframes linear in between, the segment's slopes as the rates, held after the last), the demo's table; `src/processing/altitude.h/.cpp`: `altitude_to_pressure`; `src/drivers/simulated_data.h/.cpp`: `BaseReadings`, `kStationary`, `scripted_readings` (the pipeline's models inverted; the position integrated from the scripted velocity once per generated frame), `generate` over the base readings with the seven draws in the same order; `src/drivers/simulated_source.h/.cpp`: the profile owned for the generator, copies deleted; `src/main.cpp`: `--motion <name>`, refused with `--live` or `--replay` and for an unknown name, the `Motion:` line; `src/drivers/fault_injecting_source.h`: the inline suppression of Entry 16 §16.8; `CMakeLists.txt` (REQ-SENS-007, REQ-TEST-004).
- `pipeline/bridge.py`: `RelativeOrigin` (the first NOMINAL fresh fix; metres north and east by the spherical approximation, `METERS_PER_DEGREE`; re-taken after ENDED and on a backwards timestamp), `position` in `frame_message`, `--track-min-speed` and `--lag-ms` in `hello` and on the start line (REQ-DISP-005, REQ-DISP-002, REQ-DISP-006).
- `dashboard/src/feed.ts` (`Position`, the two thresholds), `useTelemetryFeed.ts` (the lag from elapsed times, `bridgeLagMs`, the origins, the trail), `testMessages.ts` (`runningFeed`), `App.tsx` and `App.css` (the panel and the plot above the diagnostic view, the lag row); `dashboard/src/pfd/`: `units.ts`, `instrumentState.ts` (`instrumentState`, `feedCause`, `trackShown`, `feedStateText`, `lagLabel`), `geometry.ts` (the horizon transform, the ladder, the roll pointer, the tape's ticks and readout, the pointer, the rose and its readout, the plot's scale, points and marker), `Flag.tsx`, `AttitudeIndicator.tsx`, `AltitudeTape.tsx`, `VerticalSpeed.tsx`, `GroundSpeed.tsx`, `TrackIndicator.tsx`, `GroundTrackPlot.tsx`, `Pfd.tsx` (REQ-DISP-001, REQ-DISP-002, REQ-DISP-004, REQ-DISP-006).
- Tests: `tests/unit/test_motion_profile.cpp` (5), `test_altitude.cpp` (+1), `test_simulated_data.cpp` (+3); `tests/integration/test_motion_profile.py` (4), `test_bridge.py` (+3, the `hello` test amended for the thresholds); `dashboard/src/pfd/units.test.ts` (2), `instrumentState.test.ts` (5), `geometry.test.ts` (8), `Pfd.test.tsx` (5), `AttitudeIndicator.test.tsx` (4), `AltitudeTape.test.tsx` (3), `VerticalSpeed.test.tsx` (3), `GroundSpeed.test.tsx` (3), `TrackIndicator.test.tsx` (4), `GroundTrackPlot.test.tsx` (5), `useTelemetryFeed.test.ts` (+4), `App.test.tsx` (+3; the bar's queries scoped to the bar, since the panel's strip repeats its words).

### 17.2 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 323 of 323 (314 before), 0.3 s for the new files; `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings |
| pytest | 115 of 115 in about 110 s (108 before), on the plain build and on the instrumented one |
| Page (Vitest 4.1.11, jsdom) | 66 of 66 in 12 files (17 before); statements 99.02% (the three uncovered lines in §17.3), branches 98.44%, functions 97.59%, lines 99.65% |
| `npm run build` | the TypeScript check and the bundle, clean |
| cppcheck 2.21.0 | 0 findings, CI's flags, 30 files (29 before); the suppression raises no unmatched-suppression report at the workflow's level |
| The hand run (plan §7), the demo replayed paced through the bridge to the page in the desktop app's browser pane | REPLAY boxed in amber in the strip and the bar; parked: ALT 10 ft, VS settling to 0, GS 0 kt, `TRK ---` with no rose, the plot a dot; the takeoff roll: GS to 58 kt, TRK 090° under the rose, the marker east; the climb: the horizon down 8°, VS +600, ALT rising through 80 to 400 ft, the plot's scale bar growing 50 m, 100 m, 500 m; the turn: TRK 090 → 270; the descent: the horizon up 4°, VS −400, ALT 320 ft; ENDED: every instrument's name in a grey box (ATT, ALT, VS, GS, TRK, POS), the trail kept, east then a half circle through south to west. 7,499 of 7,500 frames reached the page: the first was published before the subscriber's join landed, the slow-joiner effect of FTS-DD-001 §9; the receiver counted no gap, since it saw no frame before the second |
| The lag check (plan §7 step 7) | a 2 s busy loop on the page's thread, with a `MutationObserver` on the strip's state: LAG 2.0 s 4 ms after the thread returned, RUNNING 11 ms later when the queue had drained, the lag back to −4 ms on the next frames |
| The kill (plan §7 step 5) | the binary killed 8 s into a second replay: STALE with the age counting, every instrument under a red flag with its name, the trail kept, the diagnostic view struck through below, read at 2.5 and 3.0 s; a third replay: RUNNING, the plot's origin re-taken (a fresh 2 m grid), `TRK ---` while parked |
| The bridge (plan §7 step 6) | the bridge stopped: BRIDGE DISCONNECTED, red flags, the trail kept; restarted: RUNNING within 5 s, REPLAY from the next status, the plot's origin re-taken |

Red first throughout: the C++ files watched failing on the missing header and function, the pytest on the missing flag and the `hello` dictionary, each page module on its missing import, the page's layout test on the missing bar id; then green against the code, with one amendment, the scale bar's number (§17.5).

### 17.3 Coverage preview (clang, the Mac recipe of Entry 8, unit and integration tests on the instrumented build)

| Scope | Preview | Entry 16 | Threshold |
|---|---|---|---|
| processing | 100.0% | 100.0% | 90% |
| config | 97.9% | 97.9% | 90% |
| timing | 100.0% | 100.0% | 80% |
| logging | 86.1% | 86.1% | 80% |
| transport | 92.1% | 88.9% | 80% |
| replay | 100.0% | 100.0% | 80% |
| drivers | 95.8% | 96.4% | 70% |
| overall | 96.1% | 96.2% | 80% |

The new and changed files: `motion_profile.cpp` 40 of 40 lines, 7 of 8 branches (the untaken edge is `wrap_degrees`' negative side: the demo's track never crosses north); `simulated_data.cpp` 81 of 81, 11 of 12 (a scripted frame whose timestamp does not advance, which no caller produces); `simulated_source.cpp` 11 of 11, 3 of 4 (clang's unwind edge on the constructor's initializer); `altitude.cpp` 4 of 4, no branches; `fault_injecting_source.cpp` 70 of 70, 16 of 16, unchanged by the suppression. The page's three uncovered lines: `badge.ts:29` (the defensive `?? 0` of Entry 16), `Pfd.tsx:21` (the strip's colour for a `fail` cause, which `feedCause` never returns), `geometry.ts:110` (a tick outside the window, which the tick range never produces). The bridge's new branches against its tests by hand: the origin taken only on a NOMINAL fresh fix (a NO_DATA frame and a held frame leave it), the reset after ENDED and on a backwards timestamp, the two thresholds by default and by flag; untested by any test, `main`'s two new arguments and the start line, exercised by hand in §17.2's runs.

### 17.4 The byte checks

| Check | Result |
|---|---|
| The seed-42 500-frame log, after the simulator's change and at the end | `6d43f343…` (Entry 12 §12.4); its unpaced replay byte-identical, `Replay:    500 frames, end of log reached` |
| The demo, `--motion demo --cycles 7500` | 7,500 frames in 787,516 bytes, every validity NORMAL from frame 0, `caba9aeb5ad06634771b8ed0646964fbe99351156c6c968afb1a405dfcc62889` on this Mac (libc++; the runner's libstdc++ draws differently, FTS-DD-001 §3), `Motion:    profile demo, 150 s scripted flight`, missed cycles 0 |
| Its unpaced replay | byte-identical; `Replay:    7500 frames, end of log reached` |
| Its paced replay over ZeroMQ through the bridge to the page | byte-identical to the recording; lateness min/mean/max = 1 / 5,702 / 9,959 µs, none a period late; `status messages = 153` (150 RUNNING, 3 ENDED) |
| The pytest's 20 s flight (`--cycles 1000`) | pitch 7.97° on the last frame, 30 m/s, course 090, every validity NORMAL, the 19 s fix 269 m east of the first; its unpaced replay byte-identical |

### 17.5 Decisions taken, and deviations from the plan

- All as recommended in the plan, the five flags answered by the approval: the display's sign rule (roll positive is a right roll, pitch positive is nose up, the bench's signs; FTS-DD-001 §6 and FTS-DS-001 §2.3 record that the driver's mapping, not the display, moves if the bench disagrees); `heading_deg` kept and TRK mapped, the rename parked; REQ-SENS-007 taken and the ADS-B drafts relabelled; REQ-DISP-002 without the draft's outside air temperature, GPS altitude and held cue; Entry 16's commit count and the Day 11 note's three numbers corrected.
- **The ground track plot's scale bar.** The plan's card expected `10 m` for a metre-scale trail; the quarter-of-half-extent rule gives the 1-2-5 step nearest 2.5 m, which is 2 m. The card follows the rule; the kilometre case is 1 km as planned.
- **The track indicator's band.** Moved a few viewBox units from the plan (y 448 to 520, the lubber line at 462, the readout at 434) so that the readout box clears the attitude ball; FTS-DS-001 records what was built.
- **The pytest count is 115.** The plan's estimate of 112 left out the bridge's three new tests.
- **The lcov filter step needs the capture's ignore set.** The profile's keyframe table is a global with a dynamic initializer, which clang's gcov reports at line 0; the capture ignored the `format` error as the recipe says, the filter step did not, and lcov 2.5 stopped writing at that record and left a truncated `lcov.info` that read as "no files" for most scopes. `format` (with `corrupt,unsupported`) in the filter's `--ignore-errors` fixed it; the preview above is the second run.
- **Found in the hand run:** the receiver's out-of-order counter counts every frame of a restarted run until its timestamps pass the previous run's (1,488 over two restarts: the first bridge's exit line read `received 8987 frames, gaps 0 frames, out of order 1488`), because its expectation is not reset on a backwards step. Parked in FTS-DD-001 §12 with the receiver; the bridge and the page re-take their origins on that step.
- **The lag's transient.** React committed one LAG render for the hundred messages the stall had queued, then RUNNING: a transient queue is visible for as long as it lasts and no longer, which is the measurement's meaning; a sustained queue (a throttled tab) would hold it. Chrome's background tab remains TC-025 step 8, a hand step.
- **The patch snapshots** were taken against a scratch git index (`GIT_INDEX_FILE`) advanced at each step's end, so the real index stayed free for the §16.7 commit and no `git add -N` was needed on it; the check that the seven patches rebuild the working tree is in the closing commands.
- Not done, by the plan's own order: the CI run of record (§17.7); TC-025 step 8 (Chrome, Derek's); TC-021 step 9 and the paced lateness on the Pi (from Entry 16); the bench's TC-014 step 13.

### 17.6 Library and toolchain facts this entry rests on

- SVG applies the rightmost transform first, and a positive `rotate` is clockwise in its y-down frame; jsdom's SVG elements carry `dataset`, and React renders a numeric attribute as its plain decimal string, so the tests read transforms and positions as strings (`rotate(-30 400 260) translate(0 40)`, a `y` of `260`).
- Node 22's `toLocaleString("en-US")` gives the thousands separator; the readouts use a typographic minus (U+2212) so that a negative value cannot be mistaken for the dashes.
- Vitest 4.1 with 3,700 React state updates in one test (the trail's cap) runs in well under a second; the whole page suite in about 3 s.
- lcov 2.5's `--remove`/`--filter` step aborts on a `format` error it was not told to ignore and leaves a partial output file without a non-zero exit, so a preview must be checked for the scopes it names (§17.5).
- cppcheck 2.21.0 honours `// cppcheck-suppress uselessOverride` on the line above a declaration without an unmatched-suppression report at `--enable=warning,style,performance,portability`; 2.13.0's reading of the same line is the runner's to show (§17.7).

### 17.7 Measurement of record

Reserved for the CI run on the branch's head: the C++ job's per-scope table on GCC (the profile's eight branches and `simulated_data.cpp`'s twelve on the runner's gcov), the pytest line (115, the scripted flight's 20 s run on the runner), the `page` job's Vitest table (66) and build, and the cppcheck step's report with the suppression in place.

### 17.8 Open

- §17.7.
- TC-025 step 8: the demo replaying to a Chrome tab, the tab in the background for two minutes, the lag on return and whether LAG flagged and cleared, the bridge's share; Derek's hand step, into §17.5.
- On the Pi: TC-021 step 9 and a paced replay's lateness on `clock_nanosleep` (Entry 16 §16.8); the bench's TC-014 step 13, which decides the roll sign's mapping in the driver.
- Parked in FTS-DD-001 §12 with this entry: the `heading_deg` rename (with the fix-quality byte and the sequence number); the track threshold until a fix capture; the receiver's out-of-order counter across runs; the rotated replay, decided for this demo and parked for a longer one.

## Entry 18 — 2026-10-15, ADS-B Phase A: real SFO arrivals through the same pipeline, from the OpenSky Network API (macOS preview, and the live run)

Seven requirements (REQ-SENS-008 to REQ-SENS-011, REQ-FAULT-007, REQ-CFG-004, REQ-DISP-007; SRD 0.11.0 → 0.12.0, 47 → 54 statements) and eight amended (REQ-PROC-003, REQ-PROC-004, REQ-TRANS-004, REQ-DISP-001, REQ-DISP-004, REQ-FAULT-001, REQ-FAULT-006, REQ-CFG-001), seven fault entries (FAULT-012 to FAULT-018; FTS-FM-001 0.4.0 → 0.5.0), four test cards (TC-027 to TC-030) and six amended, FTS-DS-001 0.1.0 → 0.2.0, nine new C++ modules and the project's one thread, a fourth channel state, the session status at version 2, the ADS-B run mode, the bridge's identity and the page's; ten commits on `claude/ads-b-phase-a-opensky-87da4f`, stacked on the PFD branch at `dc989d6`, off `main` until after the v0.1.0 tag, and an eleventh the same night for §18.7, the control run of §18.4, the signed archive of §18.6 and the replayed tape's datum (design §12).

### 18.1 What was built

- Documents (commit 1): the SRD statements above; FTS-FM-001's *External Data Source Failures* (FAULT-012 the service unreachable or in error, 013 the token refused or expired, 014 the credit budget, 015 the selected aircraft gone, 016 an incomplete vector, 017 a malformed response) and FAULT-018 (a channel the source does not provide) under *Start-up and validity*; TC-027 (the client), TC-028 (selection), TC-029 (the source, the run, the live run as its hand step), TC-030 (the display); FTS-DS-001 §2.1, §2.2, §3, §4.1, §4.2, §5, §6 and §8; FTS-DD-001 §3, §7, §9, §10, §11.3 and §12; FTS-NP-001's sentence on the birth source's variance. The OpenSky terms of use, read the same day: non-profit research and education, no redistribution, de-identified publications, the citation (README, *Data sources*).
- `src/processing/processor.cpp` (commit 2): the Kalman filter born from the first fresh reading of either altitude source, the barometer first, with that source's measurement variance as its initial altitude variance; `baro_ever_fresh_` so a NOMINAL barometer that never read contributes nothing to the validity (REQ-PROC-003, REQ-PROC-004, REQ-PROC-007).
- `src/drivers/adsb_config.h`, `src/config/telemetry_config.*`, `config/telemetry_config.yaml` (commit 3): the optional `adsb` block, ten keys, 38 → 48, with the box's ordering rules and the timeout above the interval; a file without the block is refused only by `--adsb` (REQ-CFG-001, REQ-CFG-002).
- `ChannelStatus::NOT_AVAILABLE` (commit 4): the one status a source asserts; the detector leaves it alone, the replay's strip keeps it, the live source reports it for the GPS without a receiver, `frame_format.py`, `feed.ts`, `instrumentState.ts` (the `unavailable` cause), the outlined flag (REQ-FAULT-007, REQ-FAULT-006, REQ-DISP-001).
- `src/drivers/http_client.*`, `curl_http_client.*`, `opensky_json.*`, `opensky_client.*` (commit 5): the HTTP seam and libcurl behind it (the timeouts, the body cap, no redirects, never verbose), the parsers that never throw, the credentials file by path and key with the mode check, the OAuth2 client on the Clock seam; `tests/unit/fake_http_client.*`, `opensky_examples.h` (synthetic, nothing from the service); CMake: `CURL::libcurl`, nlohmann/json 3.12.0 by release archive and SHA-256, CMP0135 NEW, Threads, `-fprofile-update=atomic`; CI's apt line (REQ-SENS-008, REQ-CFG-004).
- `src/drivers/aircraft_selector.*` (commit 6): the nearest descending airborne aircraft to the reference point, the margin, the miss tolerance, every change with its reason (REQ-SENS-009).
- `src/timing/periodic_worker.*`, `src/drivers/adsb_source.*`, `src/drivers/selection_log.*` (commit 7): the thread, the slot, sample-and-hold, the absent channels, the switch records at the frame's timestamp, the CSV (REQ-SENS-008, REQ-SENS-010, REQ-SENS-011).
- `src/transport/session_message.*`, both publishers, `src/main.cpp`, `pipeline/frame_format.py`, `receiver.py`, `bridge.py` (commit 8): the status at version 2 with the identity, `--adsb`, `--adsb-credentials`, `--adsb-origin`, the `ADSB` mode, the status after the read and at once on a change, the detector's timeout by mode and for a replayed ADS-B log, the `ADS-B:` exit line, the receiver's identity, the bridge's identity and `--hide-identity` (REQ-TRANS-004, REQ-CFG-004, REQ-FAULT-001, REQ-DISP-007).
- `dashboard/src/feed.ts`, `useTelemetryFeed.ts`, `pfd/Pfd.tsx`, `pfd/AltitudeTape.tsx`, `App.tsx` (commit 9): `ADS-B` plain with the identity after it, `REPLAY · ID UNAVAILABLE`, `ft HAE`, the trail cleared on a change, the attribution line (REQ-DISP-007, REQ-DISP-004).
- Tests: unit +72 (`test_kalman` +3, `test_validity` +2, `test_config` +3, `test_telemetry_frame` +1, `test_fault_detection` +1, `test_live_sensor_source` +1, `test_replay` +1, `test_opensky_json` 9, `test_opensky_client` 14, `test_curl_http_client` 8, `test_aircraft_selector` 11, `test_periodic_worker` 4, `test_adsb_source` 12, `test_selection_log` 2, `test_session_message` +1); pytest +16 (`test_adsb.py` 13 over a local stand-in for the service, `test_bridge.py` +3; `test_session_status.py` rewritten for the nineteen bytes); page +7.

### 18.2 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 395 of 395 (323 before), `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings |
| pytest | 131 of 131 in 126 s (115 before); the ADS-B run over the local stand-in: 12 polls in 12 s, 11 readings, one 401 repaired, one 429 honoured, two switches, the replay byte-identical, no secret or token in anything written |
| Page (Vitest 4.1, jsdom) | 73 of 73 in 12 files (66 before); `npm run build` clean |
| cppcheck 2.21.0 | 0 findings with CI's flags, after five style findings in commit 5's first draft (`variableScope`, `useStlAlgorithm` twice, `functionStatic`, `knownConditionTrueFalse`) were fixed, not suppressed |
| Mutations | commit 5: four (the 401 repair, the row length, the bearer header, the expiry margin), each caught by one to three tests; commit 7: nine, eight caught at once, the ninth in §18.5 |
| The seed-42 500-frame log | `6d43f343…` unchanged after the Kalman birth, the fourth status and the status after the read; replay identical |
| The live run (TC-029 step 19) | §18.4 |

Red first where a header or a function was missing; the one expectation the plan did not name that changed is the PFD's replay label, `REPLAY` → `REPLAY · ID UNAVAILABLE` (REQ-DISP-007 itself).

### 18.3 Coverage preview

Not run on the Mac this session: the pytest suite's ADS-B run would have to be re-run on an instrumented build, and §18.7 takes the CI figures on GCC. Lines expected to show exception-unwind edges, marked with the project's marker and listed here for §18.7: `src/timing/periodic_worker.cpp` (the `wait_for` line, a call that can throw on a loop condition) and `src/main.cpp` (the replay probe's `probe.next(first)` line). The one conditional with string temporaries in the new code, `describe()` in `main.cpp`, was written as statements instead.

### 18.4 The live run

`./build/telemetry --adsb --cycles 15000 --zmq tcp://127.0.0.1:5555` from a scratch directory with `--config` naming the shipped file, 2026-10-15 21:24 to 21:29 PDT (the evening arrival bank; the plan said daytime for traffic density, and the evening sufficed: 46 aircraft in the box and 5 candidates on the last response), the bridge on the loopback and the page in the desktop app's browser pane; FlightAware beside it for the hand check. The credentials file at the default path was read by the binary alone and its path printed once.

| Item | Observed |
|---|---|
| The first selection | `UAL2080 (a441ac)` at frame 0, 7.2 km from the reference point, descending on the 28R final at 720 ft HAE, GS 150 kt, TRK 298°; FlightAware: United 2080, Kahului to San Francisco, a 737 MAX 8, landed 21:27 PDT, gate E3 |
| The switches (the console, `telemetry_adsb.csv`) | eight: t=41.5 s `nearer` to `WJA1504 (c05da4)` at 2.4 km (§18.5); t=77.8 s `nearer` back to UAL2080 at 1.2 km; t=98.5 s `landed` → `UAL2817 (a12b2d)` 2.2 km; t=124.4 s `landed` → WJA1504 8.9 km; t=134.8 s `nearer` → `UAL276 (a12797)` 8.2 km; t=243.7 s `landed` → `QXE2112 (a817b1)` 2.4 km; t=285.2 s `landed` → `JBU933 (acca19)` 3.1 km; each row's timestamp is the first frame carrying the new aircraft |
| The `ADS-B:` line | 58 polls, all ok (0 auth, 0 token refused or unavailable, 0 rate-limited, 0 http, transport or malformed), 1 token request, 50 readings, 8 selection changes, 46 aircraft and 5 candidates in the last response, 0 rows skipped, 58 reports taken, 0 overwritten, position age min/mean/max 0 / 1.14 / 3 s, last HTTP 200 |
| The log | 15,000 frames in two files (rotation at 1 MiB: 9,986 and 5,014 frames), no fault event; every frame `baro=NOT_AVAILABLE imu=NOT_AVAILABLE gps=NOMINAL`, the attitude and barometric altitude NO_COMPUTED_DATA and the fused fields NORMAL on all 15,000; the Validity line says so |
| The statuses | 310 on the ZeroMQ transport: 300 periodic, 7 at switches not on a second's boundary, 3 ENDED |
| Timing | 15,000 cycles at 20 ms, jitter min/mean/max 19 / 5,383 / 12,842 µs (p50 6.0, p90 8.1, p99 9.7 ms), 0 missed, with the thread, the publisher and every frame printed to a file. The control the same night (§18.5): the simulator for 15,000 cycles on the same Mac with the bridge, the page and the ZeroMQ publisher up and every frame printed to a file as in the live run, 25 / 3,376 / 14,977 µs (p50 3.9, p90 4.4, p99 4.9 ms), 0 missed. Neither Mac number judges REQ-TIME-003, which is the Pi's `clock_nanosleep` figure (Entry 8, TC-009 step 7, under 1 ms); they measure the macOS `nanosleep` fallback under the demo's load |
| The PFD | `ADS-B · UAL2080` plain in the strip and the bar, RUNNING, the attitude indicator under the outlined `ATT` no-source flag with no cause in the strip, `ALT ft HAE` reading 720 then 420 on short final and −70 at touchdown, VS −400 to −850 fpm on final, GS 150 then 144 kt, TRK 298° under the rose, the ground track a line to the north-west; later `ADS-B · UAL276`; BARO and IMU `NOT_AVAILABLE held`, GPS `NOMINAL held` between polls; `Traffic data: The OpenSky Network (opensky-network.org)` at the foot |
| The datum (plan flag 6) | the GNSS altitude at touchdown −22.9 m (−75 ft, the broadcast's 25 ft quantization) against SFO's 4 m above mean sea level: a geoid offset of about −27 m, the −32 m the design expected within one quantum; an aircraft on the runway reads below zero on the tape, as FTS-DS-001 §4.2 says |
| The replay | the first file replayed unpaced from beside its record: byte-identical (`2971db60…`), the `Replay input:` line naming an ADS-B run by its first frame and the GPS timeout; replayed paced to the bridge and the page: `REPLAY · ID UNAVAILABLE` boxed in amber, `ALT ft`, the instruments live, no attribution line |

### 18.5 Decisions taken, and deviations from the plan

- **The Kalman birth from either source (flag 3, approved).** No recorded log replays differently: the simulator and the scripted flight read the barometer on frame 0 (the seed-42 hash and the demo's bytes are unchanged), no bench log exists (Entry 14 §14.7), and the one log that would differ is a bench log whose barometer failed its first reads while the receiver already had a fix. In ADS-B mode the filter is born from the first GPS reading, at that reading's value (`fused_altitude_m == gps_altitude_m` on frame 0 in the pytest).
- **NOT_AVAILABLE built, not parked (flag 8).** The size rule held: the enumerator, three skips in the detector, two lines in the sources, the strip, the names and one verdict on the page.
- **The replay of an ADS-B log takes the ADS-B channel timeout, decided from the log's first frame.** Not in the plan. Without it a 5 s poll interval replays as a 2 s UART timeout the run never had, and the replay is not byte-identical; the mode is not in the log, so `main` reads the barometer and IMU statuses of the first frame, which only the ADS-B source writes NOT_AVAILABLE, and says so on the `Replay input:` line. Design §12 parks the honest fix, a mode byte in the next format version.
- **A mutation found a flaw.** Commit 7's ninth mutation, a replaced report's switches dropped, was uncaught; the test written for it found that every switch in a merged report was stamped with the last poll's number, fixed by letting each switch carry its own poll (`SwitchRecord` in the report).
- **The status moved after the read.** The periodic status was published before `read_frame()`; it now follows it, so a status carries the identity of the frame it precedes; the pytest's positions among the frames are unchanged.
- **The live run in the evening.** 21:24 PDT, not daytime; recorded as such.
- **Finding: the selection measures horizontal distance only.** At t=41.5 s `WJA1504`, descending through 3,498 m HAE on an arrival route 2.4 km from the reference point (TRK 140°, GS 122 kt), displaced `UAL2080` on short final for 36 s by the nearest-descending rule, which is judged on the ground distance; the fused altitude followed it from 152 m to 2,934 m and back, with vertical speeds of +152 and −154 m/s in the log. An altitude term, a ceiling on candidates or a slant distance, is the fix; it changes REQ-SENS-009's words and adds a tunable, so it is parked in design §12 for Phase B.
- **Finding: a switch is a discontinuity the filter is not told about.** The identity is not in the frame, so the Kalman filter takes a new aircraft's altitude as a step of the old one's and reports vertical speeds of ±60 to ±185 m/s for a few polls after every switch (−64 m/s at the landing switch at t=98.5 s), pegging the vertical speed indicator until the hold's growing covariance lets the new readings win; between polls on one aircraft the indicator read −400 to −850 fpm on final, plausible. The fix, a re-birth at a switch, needs the identity in the frame or a discontinuity flag; parked in design §12 with the vertical rate.
- **The selection's hand check passed:** every aircraft the strip named was a real arrival; the identities are in the run's record under `logs/` only and are de-identified in anything published.
- **The jitter on this Mac, and what moved it.** The live run's 5.4 ms mean against the Pi's sub-millisecond bar needed a control before it could stand in this record: the simulator for the same 15,000 cycles on the same Mac, the bridge, the page and the publisher up, every frame printed to a file, so the only difference is the source. The control's median is 3.9 ms, the macOS `nanosleep` slack Entry 8 first measured (a 5-cycle run: mean 3.6 ms), and its spread is narrow (p90 4.4 ms, sd 1.2 ms); the ADS-B run's median is 6.0 ms with twice the spread (p90 8.1 ms, sd 2.4 ms), and both are flat minute by minute (5.5, 5.3, 5.3, 5.3, 5.5 ms against 3.4, 3.4, 3.4, 3.5, 3.2 ms). The maxima are the same slack on both (12.8 and 15.0 ms). So the ADS-B source does move the loop's jitter on this Mac, by about 2 ms at the median, and not through the HTTP work, which runs for a few milliseconds every 5 s and would show as outliers, not as a flat shift: a second thread parked in a timed wait changes how macOS coalesces the main thread's sleeps, and the loop's `nanosleep` fallback inherits the coarser slack. The mechanism is a reading of the shape, not a measurement of the kernel; the measurement that matters is TC-009 step 7 with `--adsb` on the Pi, whose `clock_nanosleep` with `TIMER_ABSTIME` is what REQ-TIME-003 is judged on, and it is open below. Both runs missed no cycle.

### 18.6 Library and toolchain facts this entry rests on

- libcurl 8.7.1 from the macOS SDK (`find_package(CURL)` with no Homebrew package; the runner takes `libcurl4-openssl-dev`); `CURLOPT_PROTOCOLS_STR`, `CURLOPT_NOSIGNAL`, the write callback's short return as `CURLE_WRITE_ERROR` (23) for the body cap; never `CURLOPT_VERBOSE`.
- nlohmann/json 3.12.0 by `URL` and `URL_HASH SHA256=42f6e95c…` (the release's `json.tar.xz`, 114,576 bytes): the digest was computed from the first fetch and pins every later one to the same bytes. The pin was tied to the release itself the same night, after the push: GnuPG 2.5.24 installed with Homebrew, the maintainer's public key taken from the location the project's README names (keybase.io/nlohmann, fingerprint `797167AE41C0A6D9232E48457F3CEA63AE251B69`, the README's), imported into a scratch keyring, and the release's detached signature `json.tar.xz.asc` verified against a fresh download of the archive: a good signature by that key, made 2025-04-07, over bytes whose SHA-256 equals the pin and CMake's own copy. `json::parse(text, nullptr, false)` for a parse that never throws; CMake's CMP0135 set NEW under `if(POLICY …)` so a CMake older than 3.24 is untouched.
- `std::condition_variable::wait_for` with a predicate as the worker's one wait; `std::count_if` over a static member function; C++17's guaranteed elision lets a test return a source that owns a thread and a mutex by value.
- The OpenSky REST API: OAuth2 client credentials at `auth.opensky-network.org`, tokens of 1,800 s, one credit per call for this box, `X-Rate-Limit-Retry-After-Seconds` on 429, state vectors of 17 elements (18 with `extended=1`), the position time nulled past 15 s.
- The FlightAware page in the desktop app's browser pane for the hand check; Vite 8.3's dev server in the same pane through `.claude/launch.json`.

### 18.7 Measurement of record

CI run 38852667084 on `4dabebd`, the branch's head, on the draft pull request (number 2, to `main`, stacked on the PFD branch), 2026-10-16 04:50 to 04:54 UTC, the first run on the branch; both jobs green.

| Check | Result |
|---|---|
| Google Test (GCC, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 395 of 395 in 11.4 s |
| pytest (Python 3.13, against the instrumented binary) | 131 of 131 in 127.8 s, the ADS-B run over the local stand-in among them |
| lcov over `src/` | lines 98.1% (2,747 of 2,799), functions 97.1% (301 of 310), branches 92.7% (1,286 of 1,387) |
| cppcheck (the runner's package), 40 files | 0 findings |
| `page` job (Node 22, Vitest with coverage, `npm run build`) | 73 of 73 in 12 files; statements 99.09%, branches 98.91%, functions 97.67%, lines 99.68%; the build clean in 93 ms; 18 s for the job |
| Wall time | the C++ job 4 min 2 s; the page job 18 s |

Per-scope branch coverage on GCC, from `scripts/coverage_check.sh` (thresholds from FTS-TP-001 §6.3), beside the last Mac preview taken, Entry 17's:

| Scope | Record (GCC) | Entry 17 preview (clang) | Threshold |
|---|---|---|---|
| processing | 100.0% | 100.0% | 90% |
| config | 100.0% | 97.9% | 90% |
| timing | 100.0% | 100.0% | 80% |
| logging | 100.0% | 86.1% | 80% |
| transport | 88.8% | 92.1% | 80% |
| replay | 95.0% | 100.0% | 80% |
| drivers | 89.0% | 95.8% | 70% |
| overall | 92.7% | 96.1% | 80% |

The new and changed files on GCC:

| File | Lines | Branches |
|---|---|---|
| `timing/periodic_worker.cpp` | 39 of 39 | 4 of 4 |
| `drivers/adsb_source.cpp` | 146 of 147 | 48 of 52 |
| `drivers/selection_log.cpp` | 20 of 20 | 6 of 6 |
| `drivers/aircraft_selector.cpp` | 79 of 79 | 55 of 64 |
| `drivers/opensky_client.cpp` | 151 of 164 | 95 of 108 |
| `drivers/opensky_json.cpp` | 145 of 150 | 108 of 130 |
| `drivers/curl_http_client.cpp` | 90 of 97 | 26 of 32 |
| `drivers/http_client.cpp` | 6 of 6 | 3 of 4 |
| `transport/session_message.cpp` | 48 of 48 | 32 of 36 |
| `config/telemetry_config.cpp` | 481 of 481 | 242 of 242 |
| `processing/processor.cpp`, `processing/fault_detector.cpp` | 77 of 77, 149 of 149 | 60 of 60, 70 of 70 |
| `replay/log_replay_source.cpp` | 44 of 44 | 19 of 20 |
| `drivers/live_sensor_source.cpp` | 74 of 74 | 30 of 30 |

The two lines §18.3 named: the worker's marked `wait_for` line contributes no branch after lcov's exception filter, so the file's four branches are the two `running_` tests, and the loop's own decision rests on the two tests that stop a wait early and on time; `main.cpp` is outside lcov's scope on the runner, as it has been since Entry 5, so its probe line is not measured.

The drivers scope's fall from Entry 17's 95.8% to 89.0% is the ADS-B source's parsers and clients, whose untaken branches are of three kinds. First, exception-unwind edges on lines the marker rule names but which carry no marker yet: `aircraft_selector.cpp:103` and `http_client.cpp:6` (a `find_if` over an iterator range), `opensky_client.cpp:27` (a string temporary in a conditional) and `opensky_json.cpp:121` (a range-for over the document's array); these four lines take the marker in the next commit that touches their files, and are listed here as the rule asks. Second, legs of short-circuit chains no fixture produces: `AircraftSelector::usable`'s absences of latitude, longitude, velocity, track and position time (the examples null only the geometric altitude, besides `on_ground`), `candidate`'s null vertical rate, the per-field row guards in `opensky_json.cpp` (`index >= row.size()`, which the 13-element row meets at the length check instead), `hex_value`'s never-false digit tests and `operator==`'s callsign leg in `session_message.cpp`, and four legs of the slot's and the freshness rule's chains in `adsb_source.cpp` that no scripted sequence reaches (a vector whose aircraft is not the one selected, a replaced report carrying a vector the new one lacks). Third, failure paths no test provokes: libcurl's global init and handle allocation failing (`curl_http_client.cpp:65`, `:70`, `:97`), a token endpoint refusing or vanishing after a 401 inside one poll (`opensky_client.cpp:158`, `:164`), and an origin override with no path (`:80`). The transport scope's 88.8% is the publishers' send-failure legs of earlier entries, unchanged; the replay scope's one untaken leg is the strip's NOT_AVAILABLE ternary on the GPS line, which `ReplayTest.NotAvailableSurvivesTheStrip` exercises on the other two channels.

### 18.8 Open

- Yours: the link to the published work for the OpenSky Network (its terms ask for it); the demo video's ADS-B segment with the bridge's `--hide-identity`.
- TC-009 step 7 with `--adsb` on the Pi: whether the worker thread moves `clock_nanosleep`'s jitter as it moves the Mac's `nanosleep` slack (§18.5).
- The four exception-edge lines of §18.7 without a marker (`aircraft_selector.cpp:103`, `http_client.cpp:6`, `opensky_client.cpp:27`, `opensky_json.cpp:121`) take it in the next commit that touches their files.
- Phase B (design §12): the vertical rate as the filter's third measurement and the pressure altitude, the mode in the log, the selection's altitude term, the re-birth on a switch, all with format version 3.
- Entry 17's open items stand: §17.7 from the CI run, TC-025 step 8, the Pi.

## Entry 19 — 2026-10-15 and 16, Format version 3: the run mode in the header, the sequence number, the new-vehicle flag and the re-birth, the ADS-B vertical rate and pressure altitude, the fix quality, the voter's status (macOS preview)

Three requirements (REQ-LOG-007, REQ-LOG-008, REQ-PROC-008; SRD 0.12.0 → 0.13.0, 54 → 57 statements) and five amended (REQ-SENS-005, REQ-SENS-008, REQ-LOG-005, REQ-TRANS-004, REQ-DISP-007), FTS-FM-001 0.5.0 → 0.6.0, FTS-DS-001 0.2.0 → 0.3.0, TC-031 and nine cards amended (TC-004, TC-008, TC-017, TC-021, TC-022, TC-023, TC-026, TC-029, TC-030), one new header pair (`src/run_mode.h`), the frame from 104 to 120 bytes with version 2 as its prefix, the session status at version 3; eight commits on `claude/ads-b-phase-a-opensky-87da4f` after Entry 18's eleven, off `main` until after the v0.1.0 tag. The voter's status field was assigned on Derek's amendment the evening of 2026-10-15, before commit 2 was committed, so the voter never needs a format version 4.

### 19.1 What was built

- Documents (commit 1): the SRD statements above; FAULT-015's re-birth and FAULT-003's bytes in place; FTS-DS-001 §3, §4.2, §4.5 and §5 by source mode; FTS-DD-001 §3, §7 (the flags byte), §9 (the version-3 layout, the header's two modes, the prefix rule, the status at version 3), §11.1, §11.2 and §12 (five bullets decided, the 2D-fix bullet amended); TC-031, seventeen steps.
- `src/telemetry_frame.*`, `src/run_mode.*`, `src/logging/*`, `src/replay/log_replay_source.cpp`, both publishers, `src/main.cpp` (commit 2): `sequence` at 52, `course_deg` at 80, `flags` at 103 (`FrameFlag`: NEW_VEHICLE, VERTICAL_RATE, PRESSURE_ALTITUDE, FIX_QUALITY), the tail `gps_vertical_rate_mps` 104, `gps_pressure_altitude_m` 108, `gps_fix_quality` 112, `gps_satellites` 113, `voter_status` 114 (`VoterStatus`: VOTED, LANE_1_FAULTED to LANE_3_FAULTED, NO_MAJORITY; zero, no voter ran), `reserved_d` 116; `RunMode` with UNKNOWN as its zero; the header's `run_mode` and `source_mode`, `frame_size_for`, the prefix read, the header handed to the logger and both publishers; the replay's source mode from its input's header, the first-frame rule of Entry 18 §18.5 deleted (REQ-LOG-005, REQ-LOG-007).
- `pipeline/frame_format.py`, `log_reader.py`, `receiver.py`, `tests/integration/synthetic_log.py`, the page's types (commit 3): `FRAME` at 120 and `FRAME_V2` as its prefix, one `Frame` shape for every version, the header's modes, the name tables, the summary's header and sequence lines, `seq=` on the receiver's line, `course_deg` on the page; the file-level identity stated by its one header byte (§19.5) (REQ-LOG-005, REQ-LOG-007, REQ-TEST-002).
- `src/data_source.h` and the three sources, the injector (commit 4): `next_sequence()`, the count from zero; the live source's GGA quality and satellites under FIX_QUALITY, held with the fix; the simulator's 1 and 8 on every frame; a held GPS reading held whole by the injector (REQ-LOG-008, REQ-SENS-005).
- `src/transport/session_message.*`, the publishers, `main.cpp`, the Python status, the receiver, the bridge, the page (commit 5): `SessionStatus`, the source mode in byte 7 at version 3; the tape's `ft HAE`, a replay's `ID UNAVAILABLE` and the attribution by the source mode (REQ-TRANS-004, REQ-DISP-007, REQ-LOG-007).
- `src/drivers/adsb_source.cpp`, `src/processing/processor.*` (commit 6): NEW_VEHICLE on the first frame of a newly selected aircraft; the re-birth before the gates (REQ-PROC-008).
- `src/drivers/adsb_source.*` (commit 7): the vertical rate and the pressure altitude with their presence bits, held with the vector (REQ-SENS-008).
- Tests: unit +17, 395 → 412 (`test_telemetry_frame` +2, `test_logging` +3, `test_replay` +3, `test_simulated_data` +1, `test_live_sensor_source` +2, `test_adsb_source` +3, `test_fault_detection` +1, `test_kalman` +1, `test_validity` +1); pytest +11, 131 → 142 (`test_log_reader` +5, `test_adsb` +2, `test_validity` +1, `test_replay_identity` +1, `test_session_status` +1, `test_bridge` +1); page 73, unchanged in number, its label, datum and attribution tests extended by source mode.

### 19.2 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 412 of 412 (395 before), `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings |
| pytest | 142 of 142 in 127 s (131 before) |
| Page (Vitest 4.1, jsdom) | 73 of 73 in 12 files; `npm run build` clean |
| cppcheck 2.21.0 | 0 findings with CI's flags at every commit |
| The byte checks | §19.4 |

Red first at every commit: commit 2's tests failed to compile against the old layout; commit 3's suite could not import the twin's new names and the page's track read `rotate(0)`; commit 4's ten, commit 5's three layers, commit 6's five and commit 7's six failed before their code. The expected values that changed beyond the plan's list are in §19.5.

### 19.3 Coverage preview

Not run on the Mac; §19.7 takes the CI figures on GCC. No new conditional line combines a decision with a string temporary, a standard-library call that can throw, or an iterator range: `has_flag` in `processor.cpp` is tested in a function that holds no object with a destructor, the ADS-B source's `set_flag` calls sit on lines of their own, and `optional::has_value()` cannot throw. Any exception edge the record shows goes into §19.7 under the marker rule. One outcome no test reached was pinned before it could show: `frame_size_for` answering 0 for a version neither twin defines (commit 3).

### 19.4 The byte checks

| Item | Observed |
|---|---|
| The seed-42 500-frame session (TC-031 step 17) | `7720966cb87473bc429f975794ffb4fdfcba1652b7c1a0bec54bc15da2f15ae3` (format version 2: `6d43f343…`, Entry 12 §12.4). Against the kept version-2 reference, frame by frame by a script: the first 104 bytes of every frame identical but offsets 52 and 53, the sequence, the frame's index (498 and 244 frames: frames 0 and 256 share byte 52's zero, and only frames 256 to 499 have a second byte), and 103, `08`, the FIX_QUALITY bit (all 500); the tail `00 00 00 00 00 00 00 00 01 08 00 00 00 00 00 00` on every frame, quality 1 and 8 satellites; no event in either; the sequence equal to the frame's index on all 500. The bytes moved at commit 2 only in the header and the zero tail (`c23357c4…`) and at commit 4 as above; commits 5 to 7 change no byte of a simulated log |
| Its replay | `703f621cca12153e267be4794a28dd28a8be4df58a929d8ce40c1c9a55b01269`: every record identical to the session's; the header differing in byte 10 alone, the run mode, SIMULATED (2) against REPLAY (3), the source mode SIMULATED in both; `cmp -l` prints `11 2 3` and `cmp` from offset 16 prints nothing |
| A version-2 log replayed (TC-031 step 13) | the version-2 reference itself, beside its record: `Replay input:` names format version 2, run mode UNKNOWN, source mode UNKNOWN and the version-3 replay log; the replay's header REPLAY over UNKNOWN; every frame's first 104 bytes equal to the reference's, the sequence 0 and the tail zero; NORMAL on all 500 |
| A version-1 log replayed | the same file with its header's version set to 1, the two versions sharing every byte position: the same result, byte for byte, the reader having set the version-1 frames' validity and former reserved bytes to what version 2 means by them |
| The live run's log of Entry 18, replayed (version 2, kept under the scratchpad and never committed, its first file of 9,986 frames) | the source mode UNKNOWN by its header, so the GPS channel takes the UART timeout, 2,000 ms, against the run's 5 s polls: one COMM_TIMEOUT at about 2 s and the channel DEGRADED from there, never recovering, since every poll's reading is followed by more than 2 s of silence; the fused fields FAILURE_WARNING on 9,885 of 9,986 frames, where the run had no event and NORMAL throughout; frames 0 to 100 identical to the recording. The first-frame rule this replaces would have taken the ADS-B timeout. The remedy: `--config` naming a copy of the record with `fault.uart_timeout_ms` set to the ADS-B timeout, 20,000 ms, which replays all 9,986 frames byte for byte in their 104 bytes with no event; the `Configuration:` line says identity is not claimed, which is right, since the configuration differs. The only version-2 ADS-B logs are this run's |
| The ADS-B stand-in run (pytest, TC-031 step 14) | NEW_VEHICLE on frames 0 and 151 (t = 0 and 3,025 ms), exactly the two rows of `telemetry_adsb.csv`; on each the fused altitude born at the GPS altitude, 480 m and 730 m, the vertical speed 0, both NORMAL; the largest vertical speed over the 600 frames 0.0 m/s across the 250 m step from TEST001 to TEST002, the kind of step the live run read as up to ±185 m/s (Entry 18 §18.5); the vertical rate −3.5 m/s and the pressure altitude 450 m then 700 m, both bits set, on every frame; no fix quality. Its replay: every record identical, the header REPLAY over the recording's ADSB, and every status's source mode ADSB |
| The rotation | a frame record is now 121 bytes, so a 1 MiB file holds 8,665 frames where it held 9,986 (the live log's replay rotated into two files of 8,665 and 1,321); the demo's 7,500-frame recording is 907,516 bytes, still one file |

### 19.5 Decisions taken, and deviations from the plan

- **The voter's status (Derek's amendment, 2026-10-15).** `reserved_c` became `voter_status`, zero meaning no voter ran, with bits for the approved voter decisions; every writer puts zero; the replay's strip zeroes it as a verdict the voter will recompute, as it zeroes validity; `reserved_d` stays reserved. It traces to REQ-LOG-001 until the REQ-RED drafts enter the SRD (FTS-DD-001 §11.1).
- **The run-mode byte and the file-level identity (found in commit 3).** REQ-LOG-007 records the run mode of the session that wrote each file, so a session's log and its replay's differ in exactly one header byte, the run mode, where four pytests compared whole files. The plan's deviation rule (an existing test's expected value changing) applied; Derek had asked to carry on, so it was built as approved: the four tests compare every record and the header but for that byte (`conftest.assert_replay_identical`), and TC-004, TC-026, TC-029, TC-031, design §9, README.md and CLAUDE.md say so. The alternative, dropping the run-mode byte, which nothing reads, would have restored whole-file identity at the cost of REQ-LOG-007's words; on 2026-10-16 Derek continued with the byte as built. REQ-LOG-003 claims identical processing outputs, which are the records; it holds.
- **The attribution follows the source mode (commit 5).** Not in the plan. With the datum moved to the source mode, a replayed ADS-B recording would have shown the network's data under `ft HAE` without the attribution the data license requires; under the standing instruction to attribute OpenSky as its terms require, the attribution follows the source mode too (FTS-DS-001 §8, TC-030 step 7).
- **The injector holds a GPS reading whole (commit 4).** The plan said only that the sequence passes through. A held or failed GPS now holds the fix quality, the satellites and the two ADS-B fields with their presence bits, as the live source holds its last fix; NEW_VEHICLE and the sequence pass through.
- **A new vehicle gets a new attitude filter (commit 6).** `ComplementaryFilter::reset()` keeps the angles for an outage's re-seed (design §6), which on a new vehicle would have shown the old one's pitch under NO_COMPUTED_DATA; a new object reads 0, as REQ-PROC-007 asks of a field that never existed.
- **The matrix summary after commit 2** counted REQ-LOG-007 as Not started while its row said Partial; commit 3 corrected it.
- **The first-frame rule deleted (decision 3).** §19.4 shows the cost for the one kind of log it served and the remedy.
- **The receiver prints the source mode only where it differs from the run mode**, so TC-021's `session: mode=SIMULATED state=ENDED` line is unchanged.

### 19.6 Library and toolchain facts this entry rests on

- Python's `struct.Struct.format` is a `str`, so the prefix rule is asserted at import as `FRAME.format.startswith(FRAME_V2.format)`; numpy's `align=True` dtype is 120 bytes, a multiple of the 8-byte alignment; `np.frombuffer` reads an older file's frames packed to version 3.
- C++17 aggregates keep aggregate initialization with default member initializers (`SessionStatus{mode, source, state, identity}`); the 120-byte frame stays standard-layout, so `offsetof` pins every field.
- macOS's BSD `cmp`: `-l` prints one line per differing byte, its 1-based position and the two values in octal; `cmp a b 16 16` skips 16 bytes of each.

### 19.7 Measurement of record

Reserved for the CI run on the branch's head after the push.

### 19.8 Open

- §19.7 from the CI run.
- Phase B (design §12): the vertical rate, recorded since commit 7, as the filter's third measurement; the pressure altitude; the selection's altitude term.
- FAULT-003b: the detector's judgment of the fix quality and the satellite count the frame now carries.
- The voter (design §11.1), whose status field waits for it.
- The demo's recording at version 3 (FTS-DS-001 §7): its hash is recorded when it is made.
- Entry 18's open items stand: the OpenSky publication link, TC-009 step 7 with `--adsb` on the Pi, the four exception-edge lines; and Entry 17's: §17.7, TC-025 step 8, the Pi.
