# Flight Telemetry System — Verification Results

> **Document ID:** FTS-VR-001
> **Version:** 0.1.0
> **Status:** Draft
> **Last Updated:** 2026-10-21
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

Per FTS-TP-001 §6.2, each exclusion is listed with its justification. This table is the standing list. Until Entry 23 it stood as written for Entry 1, with counts that later entries outgrew; since Entry 23 (2026-10-19) it is kept current, and the commit that adds or removes an excluded site updates its row. Sites are named by file and function, which outlive line numbers; `grep -rn LCOV_EXCL src` finds the lines.

| Excluded | Mechanism | Justification |
|---|---|---|
| `src/main.cpp` | `lcov --remove` | Composition root: builds the source, detector, and processor and runs the loop. Has no requirement of its own to test against; every function it calls is tested directly. Named in §6.2. |
| Google Test and the other fetched sources (`build/_deps/`) | `lcov --remove` | Third-party. |
| `tests/`: the unit tests, the integration helpers and the fault-injection campaign's engine | `lcov --remove` | Tests are not the unit under test. The campaign's engine is verification tooling, whose evidence is its own known-answer tests (FTS-FI-001). |
| Compiler-generated exception branches | `lcov --filter branch` | Every call that may throw (`std::vector::push_back`, `std::make_unique`) compiles to a taken/unwind branch pair. The unwind path cannot be reached without exhausting memory. Counting them measures the standard library's unwind paths, not this system's decisions. The filter removes them from a line that holds no conditional, and the source keeps every call that can throw on such a line (CLAUDE.md). |
| The exception-unwind edges of the loop lines whose calls cannot leave the condition | `// LCOV_EXCL_EXCEPTION_BR_LINE` | `--filter branch` keeps every branch on a line that holds a conditional, and a loop makes its condition's calls, or a range-for its iterator calls, itself, so they cannot move to a line of their own as every other such call does (CLAUDE.md; Entry 23 §23.4 decided the twelve lines that were candidates). On the runner's lcov 2.0 the marker removes the line's whole branch record, decision included (Entry 11 §11.7), so each loop's decision is tested by the tests named here, not measured. Sites: `config/telemetry_config.cpp` `Section::Section`, the range-for over a section's entries (entered by `ConfigTest.ShippedFileEqualsDefaults` and left at every section's end; its body's branches taken by `DuplicateKeyIsNamed` and `UnknownKeyIsNamed`); `timing/periodic_worker.cpp` `PeriodicWorker::run`, `wait_for` as the loop's condition (`PeriodicWorkerTest.RunsTheStepRepeatedlyAndFreezesAfterStop` runs the step, `StopInterruptsALongWaitAtOnce` leaves at once); `logging/log_reader.cpp` `LogReader::next_frame`, `next()` as the loop's condition (`LoggingTest.NextFrameSkipsEvents`: records, then the end); `logging/log_reader.cpp` `list_log_files`, the range-for over `directory_iterator` (`LoggingTest.ListLogFilesIsSortedAndFiltered`; `ListLogFilesOnMissingDirectoryIsEmpty` with no entry); `drivers/opensky_json.cpp` `parse_states`, the range-for over the states array (`OpenSkyJsonTest.ParsesTheExampleStateVectors`, `ShortAndNonArrayRowsAreSkippedAndCounted`). |
| `switch` no-match branch | `// LCOV_EXCL_BR_LINE` | Each `switch` is over an `enum class` and lists every enumerator. The implicit no-match branch exists because the compiler cannot prove exhaustiveness; no value of the type reaches it. §6.2: "defensive branches that guard against conditions the type system already excludes." Sites: `processing/fault_detector.cpp` `channel_name`, `fault_type_name`; `drivers/fault_injecting_source.cpp` `copy_channel`, `fail_reads`, `hold_values`; `drivers/aircraft_selector.cpp` `switch_reason_name`; `drivers/opensky_client.cpp` `fetch_status_name`; `transport/session_message.cpp` `state_name`; `processing/lane_fault_injector.cpp` `LaneFaultInjector::perturb_output`. |
| The record tag's `switch` in `LogReader::next` (`logging/log_reader.cpp`) | `// LCOV_EXCL_BR_LINE` | Its explicit `default` is itself tested (`LoggingTest.UnknownTagStopsReader`); the marker covers the compiler's no-match edge (Entry 2 §2.3). |
| The `EINTR` retry of `MonotonicClock::sleep_until_ns` on Linux (`timing/clock.cpp`) | `// LCOV_EXCL_BR_LINE` | A signal delivered during the sleep cannot be reproduced in a unit test (Entry 3 §3.2). |
| Lines after or inside exhaustive switches that no value reaches | `// LCOV_EXCL_LINE` | Same reason as the no-match branch. Sites: `return "UNKNOWN";`, which exists only to satisfy `-Wreturn-type`, in `fault_detector.cpp` `channel_name` and `fault_type_name`, `aircraft_selector.cpp` `switch_reason_name`, `opensky_client.cpp` `fetch_status_name` and `session_message.cpp` `state_name`; `case Channel::VOTER:` and its `break;` in `fault_injecting_source.cpp` `copy_channel`, since `read_frame()` copies the three sensor channels only. |

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
- TC-009 step 7 on the Pi: 60 s at 50 Hz, max jitter below 1 ms, `missed_cycles` 0. Until then REQ-TIME-003 stays Partial. (Run 2026-10-20: step 7 passes, maximum 132.0 µs and 0 missed in 3,000 cycles, Entry 8 §8.6; REQ-TIME-003 stays Partial on a late cycle in TC-009 step 8, Entry 20 §20.10.)

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

Run at the bench on 2026-10-20 on `main` at `c510034`, and on 2026-10-21 for TC-014 step 12 on unit 3. The stationary capture is Entry 9 §9.6's, the GPS steps Entry 13 §13.8's, TC-021 step 9 Entry 16 §16.9's and TC-009 step 8 Entry 20 §20.9's. Every gyroscope reading here and in §8.8 predates §8.9's fix and carries the chip's sign on y: positive for a rotation about the chip's y, nose down.

| Item | Value |
|---|---|
| Target | Raspberry Pi 3 Model B+; Debian GNU/Linux 13 (trixie); kernel `6.18.50+rpt-rpi-v8` (`#1 SMP PREEMPT Debian 1:6.18.50-1+rpt1 (2026-09-11)`, aarch64); `vcgencmd get_throttled` 0x0 at every check, the SoC at 38.6 °C after the capture |
| Build | GCC 14.2.0 (Debian 14.2.0-19), the empty build type (unoptimized, the conservative case for TC-009), no warning; `ctest` 581 of 581 in 92 s on the Pi |
| Sensors | the barometer board at 0x76; three GY-521 (MPU-6050) modules from one pack at 0x68: unit 1 on 2026-10-20, unit 2 tried and withdrawn the same night, unit 3 from 2026-10-21 (§8.8) |

| Step | Result | Verdict |
|---|---|---|
| TC-014 step 11 | `i2cdetect -y 1`: 68 and 76. `i2cget -y 1 0x76 0xD0`: 0x60, a BME280, where the documents name the BMP280 (FTS-DD-001 §12). `i2cget -y 1 0x68 0x75`: 0x68, an MPU-6050, on units 1 and 3; unit 2 passed the driver's own check of it. The provisional addresses stand, and both identities are ones the drivers accept | passes |
| TC-012 step 8 | `i2cdump -y 1 0x76 b`, after the capture. Calibration `a2 6d c1 67 32 00 0a 8f 52 d6 d0 0b 51 1b 9b ff f9 ff b4 2d e8 d1 88 13`: dig_T1 to dig_T3 28066, 26561, 50; dig_P1 to dig_P9 36618, −10670, 3024, 6993, −101, −7, 11700, −11800, 5000. The sample `53 83 00 7f 9b 00`: UP 342064, UT 522672. Baked into `Bmp280Test.RealChipDumpCompensatesToPlausibleValues`, they give t_fine 119357, 23.31 °C and 1011.537 hPa (25895346 in 1/256 Pa), inside the card's 950 to 1050 hPa and 10 to 35 °C; the datasheet's double-precision formula gives 1011.538 hPa and 23.312 °C on the same bytes, and the test fails with the sample's two fields swapped (638.1 hPa, −33.9 °C). The dump also shows `init()`'s configuration on the chip: ctrl_meas 0x2F (temperature ×1, pressure ×4, normal mode) and config 0x00 (0.5 ms standby, the filter off), TC-012 step 4's values, and ctrl_hum 0x00, the BME280's humidity never measured | passes |
| TC-014 step 12 | Unit 1, 50 cycles flat: 1011.5 hPa, 23.29 °C, the gyroscope (0.98, −0.70, −0.94) deg/s, both channels NOMINAL with `read_ok` 1, every computed field NORMAL on 50 of 50, voter lanes 1 2 3 with no majority on 0, jitter 58.3 / 62.2 / 67.3 µs, 0 missed; the accelerometer (−0.55, 0.20, 14.73) m/s², its z 0.5 g above the card's 9.8. Unit 3, 50 cycles flat on 2026-10-21: the accelerometer (0.66, −0.19, 9.70) m/s², magnitude 9.72 (0.99 g), the gyroscope (−1.63, −0.05, −1.45) deg/s, pitch and roll 3.65 and −2.03° (the board about 4° off level), every computed field NORMAL on 50 of 50, voter lanes 1 2 3, jitter 61.8 / 64.5 / 72.4 µs, 0 missed. On both the GPS read NOT_AVAILABLE on every frame with no event: the card's NO_DATA, DEGRADED and timeout at 2,020 ms predate REQ-FAULT-007 and are corrected | fails on unit 1 (§8.8); passes on unit 3 |
| TC-014 step 13 | Unit 1, three tilt runs at 21:11, 21:18 and 21:26, the last by the arrows: lifting the X-arrow end took x from −1.0 to +3.0 m/s² and pitch to +11.4°, and lifting the Y-arrow side, a right roll, took y to +5.2 m/s² and roll to +20 to +27°, each axis answering only its own tilt, both signs, NORMAL throughout. Those are the settled signs the card expects, nose up and a right roll positive, so the roll's mapping stands. The motion is not: every pitch tilt started the wrong way before the accelerometer pulled it round (at 21:26, −4.7, −8.1 and −7.5° before +11.4°; at 21:18, +8.9° before settling near −22°, and −13.8° before +16°), while every roll tilt overshot the right way and sagged back, the gyroscope following the rotation and unit 1's accelerometer angle, compressed by its z offset (65° on edge for 90°, §8.8), pulling it down. The chip's y rate reads the opposite sign to the frame's pitch convention: the finding and its fix are §8.9 | the settled signs pass; the pitch rate's sign fails (§8.9) |
| TC-014 step 14 | The 50-frame log of unit 1's step 12 replayed: `cmp -l` prints one line, `11 1 3`, the header's run mode, LIVE against REPLAY (REQ-LOG-007); every record identical | passes, on the expectation corrected from `cmp` silent, which predates format version 3 |
| TC-014 step 15 | The receiver on the Mac first, then `telemetry --live --zmq tcp://0.0.0.0:5555 --cycles 500` on the Pi, the two on a phone's hotspot: `received 455 frames, gaps 0 frames, out of order 0; session LIVE ended`, the first frame received `seq 45` at 900 ms. The 45 before it went out before the subscription reached the Pi, which a ZeroMQ PUB socket drops (the slow joiner); the Pi's log holds all 500, and nothing was lost after the join | passes, on the wording corrected from `received 500 frames` |
| TC-009 step 7 | `main`, 3,000 cycles with nothing else running: jitter 60.2 / 80.4 / 132.0 µs (median 78.8, 99th percentile 112.6, nearest rank), 0 missed; `Voter:` lanes 1 2 3, no majority on 0 frames, 0 events. REQ-TIME-003's criterion, on the Linux `clock_nanosleep` path | passes; REQ-TIME-003 stays Partial on Entry 20 §20.10 |

**TC-002 step 8.** Unit 1, `telemetry --live --cycles 3000`, the barometer's SDA jumper pulled and pushed back, then the IMU's, the board tilted nose-up while the IMU was out; the pulls in the card's order, not on its clock:

| Event | t (ms) | Value (ms) |
|---|---|---|
| BARO COMM_TIMEOUT | 6,000 | 520 |
| BARO RECOVERY | 20,520 | 14,520 |
| IMU COMM_TIMEOUT | 41,900 | 520 |
| IMU RECOVERY | 56,500 | 14,600 |

Read from the run's log and printed output: no event on the other channel during either pull; 25 frames each with `read_ok` false while the channel was still NOMINAL (5,500 to 5,980 ms and 41,400 to 41,880 ms), so each timeout came 520 ms after the last good read; 726 frames with the barometer DEGRADED and the IMU NOMINAL, 730 the other way, none with both; FAILURE_WARNING on exactly those 1,456 frames (REQ-PROC-007), the barometric and fused fields on the 726 and pitch and roll on the 730; pitch and roll held at −2.81 and 1.72° under FAILURE_WARNING through 56,480 ms, then NORMAL at 5.94 and 0.42° on the first NOMINAL frame, the accelerometer's tilt at that frame (the re-seed of REQ-PROC-002); voter lanes 1 2 3, no majority on 0; jitter 59.2 / 82.7 / 148.7 µs from the run's printed summary (its jitter file went with the next run), 0 missed; the replay one byte different, the run mode. TC-002 step 8 passes on every expectation, the replay's on the wording corrected from byte-identical. An earlier pull of the IMU alone (21:14, the jumper out for about 9 s) gave COMM_TIMEOUT at 11,180 ms (value 520) and RECOVERY at 19,720 ms (value 8,540) after five good reads, the IMU DEGRADED on 427 frames and the barometer on none; stopped with Ctrl-C, it printed no exit summary, `main` having no SIGINT handler.

**The capture on unit 1.** The 15-minute stationary run that followed (`telemetry --live --cycles 45000`): every computed field NORMAL on 45,000 of 45,000 frames, voter lanes 1 2 3 with no majority on 0, 0 events, jitter 60.2 / 81.2 / 187.6 µs, 0 missed, the GPS disabled; six files, five of 8,665 frames and one of 1,675. Its accelerometer z averages 14.69 m/s², unit 1's offset (§8.8), so the capture of record is repeated on unit 3 (Entry 9 §9.6); this one stays as the finding's evidence and as a second barometer capture.

### 8.7 Open

- CI run of record for these commits, and for the ZeroMQ and freshness commits if theirs are not yet recorded.
- The Linux branch of `src/drivers/i2c_bus.cpp` has not been compiled on this Mac; its first compile is the CI runner's, with GCC and `-Werror`. A warning there is a finding for this entry.
- The BME280 rename and any address change, if the chip IDs read on the Pi differ from the provisional values (FTS-DD-001 §12). (2026-10-20: the chip reads 0x60, a BME280, at the provisional 0x76, so no address changes; the rename is Derek's decision, with §12's bullet.)
- FAULT-010's positive-rail gap: at ±2 g the count 32767 converts to 19.6194 m/s², below the 19.62 limit the detector tests with `>=`, so positive saturation never faults. Parked for the faults topic.
- FAULT-004's single bus-level event: the bus's `errno` stops at the live source. Parked for the faults topic.
- The GPS driver (Week 2 map, Day 14) must set `gps_read_ok` only on a cycle in which a sentence completed (carried from Entry 7).
- The y rate's sign in the driver's axis mapping (§8.6, TC-014 step 13). (Fixed 2026-10-21, §8.9; TC-014 step 13's motion criterion on the Pi pending.)
- A SIGINT and SIGTERM handler for a clean stop, a Sprint 3 candidate with a requirement first: a run stopped with Ctrl-C or `kill` would then restore the GPS port's settings (Entry 13 §13.10), print its exit summary, which the wire pull of §8.6 lost, and close the log.

### 8.8 Finding: GY-521 units 1 and 2 of one pack, nonconforming

| | |
|---|---|
| **Found** | 2026-10-20 on the bench: unit 1 at 21:08, on its first live frames (TC-014 step 12); unit 2 at 22:20, on its first 50 frames |
| **Severity** | High for the bench, none for the code. Unit 1's 0.5 g offset on z compresses every accelerometer angle (65° on edge for 90°) and sits in any capture's accelerometer means; unit 2's gyroscope bias of 142 deg/s drives the attitude to 84° on a flat board within a second. Neither trips the detector: unit 1's z is inside the accelerometer's rails and the gyroscope has no range check, so the pipeline reports either unit NOMINAL and its angles NORMAL: a reading in error inside the limits, FTS-FM-001 FAULT-023, with no response by design |
| **Affected** | Two GY-521 modules (MPU-6050) of a pack of three, the chips and not the driver; the IMU readings of every run on unit 1 that night (TC-014 steps 12 to 15, TC-002 step 8, TC-009 steps 7 and 8, TC-018 steps 21 and 22, TC-021 step 9, the capture), of which only TC-014 step 12's verdict rests on the offset |
| **Fixed in** | No code: units 1 and 2 withdrawn; unit 3 of the same pack conforms (TC-014 step 12, §8.6) and replaces unit 1 from 2026-10-21; the capture of record repeated on unit 3 (Entry 9 §9.6) |
| **Evidence** | §8.6; the session's terminal text of 2026-10-20 (the tilt runs at 21:18 and 21:26, one reading a second; unit 2's 50 frames); the capture on unit 1 (45,000 frames) |
| **Guards** | TC-014 step 12, which both units failed and unit 3 passes; nothing in the code judges a sensor's offset or bias |
| **Status** | Closed for the bench 2026-10-21, unit 3 in service; units 1 and 2 bagged and labelled as known-bad, unit 1 kept for a calibration test. Open for Sprint 3: that test, and a known-good reference IMU, since unit 3 comes from the same pack |

**Unit 1: an offset on z, not a gain.** Lying flat, z read 14.88 m/s² at 21:08, 14.73 at 22:26 and 14.69 on average over the capture's 45,000 frames: 1.50 g in all with x and y, where the datasheet allows an initial zero-g offset of ±80 mg on z and ±50 mg on x and y (PS-MPU-6000A-00 §6.2). `ACCEL_CONFIG` read back 0x00 (`i2cget -y 1 0x68 0x1C`: ±2 g, self-test off), so the driver's 16,384 counts per g is the chip's. An offset reads about +5 m/s² on z with the board on its edge, where z feels no gravity, and a gain error about 0: on edge, the Y arrow up, ten readings averaged (0.05, 9.91, 4.73) m/s², z's standard deviation 0.09. Least squares on the magnitude, `‖D⁻¹(a − b)‖ = 9.81` for offsets b and gains D, over the 84 steady readings of the 21:18 and 21:26 runs, one a second, the transitions excluded:

| Model | Fitted (m/s²) | RMS residual (m/s²) |
|---|---|---|
| An offset on each axis | z +5.02 (0.51 g), x +0.16, y +0.14 | 0.111 |
| A gain on z, offsets on x and y | z gain 1.533 | 0.209 |
| An offset on each axis and a gain on z | z +4.26, z gain 1.081 | 0.105 |
| An offset and a gain on each axis | z +4.13, z gain 1.094; x gain 1.010, y gain 1.002 | 0.105 |

The pure gain leaves twice the pure offset's residual and is ruled out, by the edge reading too; a gain on top of the offset improves the fit by 0.006 m/s², so the data allow a z gain of up to about 8 % (9 % with a gain on every axis) without requiring one, and x and y read within 1 % of unit gain. The capture's own z mean gives +4.90 m/s² under the pure offset, 0.12 below the tilt runs' fit an hour earlier. Lying flat, x reads about −1.0 m/s² because the module sits about 6° off level on its single-row header (it reads about 0 lying sideways); a shim levelled it for the capture. The chip, not the board: the readings are digital, five of the six channels read normally, and stress on a board shifts a MEMS offset by tens of mg, not 500. Its gyroscope is clean: (0.92, −0.75, −0.94) deg/s over the capture, y in the chip's sign (§8.9), inside the datasheet's ±20 deg/s (§6.1).

**Unit 2: a gyroscope bias of 142 deg/s.** Eight clean joints; 50 frames flat at 22:20: the accelerometer (−0.67, −0.06, 11.66) m/s², z 0.19 g high, also outside ±80 mg; the gyroscope (−16.1, +141.9, +12.7) deg/s at rest, y in the chip's sign (§8.9), against the datasheet's initial zero-rate tolerance of ±20 deg/s. The filter integrated the y bias to 84° of pitch on the flat board in the run's one second, what its recursion predicts for that bias from the accelerometer's −3.3° (84.1° after 49 updates, on its way to 136°; FTS-DD-001 §6); with §8.9's mapping the same bias drives it the other way.

**The pack.** Two units of three fail, in different ways, so the pack is suspect rather than the soldering or the bench. The MPU-6050 is no longer made for new designs: distributors list it as discontinued, and TDK InvenSense's product page for it is gone (checked 2026-10-21), so a cheap module may carry old stock, a reject or a copy. Unit 3, the pack's last board (silkscreen HW-123), soldered on 2026-10-21, conforms (§8.6) and is the bench's IMU from then. The capture on unit 1 stays as this finding's evidence and as a second barometer capture (Entry 9 §9.6).

### 8.9 Finding: the pitch rate's sign reversed in the driver's axis mapping

| | |
|---|---|
| **Found** | 2026-10-20 on the bench, TC-014 step 13's tilt runs on unit 1 (21:11 to 21:26) |
| **Severity** | High for the live attitude, none for simulated or replayed data. Every pitch rate entered the filter with the wrong sign: a quick pitch tilt first swung the wrong way, by up to the size of the tilt (at 21:18, +8.9° from −4.7° before settling at −22°), then came round within about the filter's time constant of one second, and a steady pitch rate would have been tracked about twice the rate times that time constant behind. The settled angles were right, so the static checks passed; roll was right throughout |
| **Affected** | `src/drivers/mpu6050.cpp`, `Mpu6050::read()`'s axis mapping, the identity; FTS-DD-001 §6's frame line ("right-handed body frame: x forward, y right, z up", a left-handed frame) and the comment in `src/processing/complementary_filter.h`; every live frame's `gyro_y` before the fix, the bench's logs of 2026-10-20 and 21 among them |
| **Fixed in** | The commit that adds this section: `Mpu6050::read()` negates the chip's y rate; FTS-DD-001 §6 corrected, the frame documented per field |
| **Evidence** | §8.6, TC-014 step 13; the session's terminal text of 2026-10-20, pitch and roll printed once a second through the tilt runs |
| **Guards** | `Mpu6050Test.AxisMappingNegatesOnlyTheYRate` (TC-013 step 8); TC-014 step 13's motion criterion, added with the fix |
| **Status** | Fixed in code; closed when TC-014 step 13 passes its motion criterion on the Pi with the fixed driver |

**The cause.** As mounted, the chip is right-handed with x forward along the board's X arrow, y left along its Y arrow and z up. A nose-up rotation turns +x toward +z, a negative rotation about +y, so the chip reports it as a negative y rate, while the filter adds `gyro_y` to pitch (`complementary_filter.cpp:52`) and the accelerometer's pitch is positive nose up: the two terms fought on every pitch motion. A right roll turns +y toward +z, a positive rotation about +x, so the roll terms agreed.

**How it was missed.** FTS-DD-001 §6 called the convention an assumption for the first hardware day and wrote its frame as x forward, y right, z up, a left-handed frame that no chip delivers. The simulator generates `gyro_y` as the scripted pitch rate, from the same assumption, so every simulated test agreed with the filter; TC-006 tests the filter in its own convention; and TC-014 step 13 checked only the settled signs, which the accelerometer alone decides. The tilt runs printed the angle once a second, and the swing showed in the first sample after each lift.

**The fix.** In the driver's mapping, the one place §6 reserved for a sign: `Mpu6050::read()` negates the chip's y count before converting it, so the frame's `gyro_y` is the nose-up pitch rate; negating the count, not the float, keeps a zero count +0.0 and INT16_MIN's rate representable. Nothing simulated moves: the simulator, the filter, the five default hashes of Entry 22 §22.4 (recomputed with the fix: all equal) and the campaign. TC-013 gains step 8 and its step 3's y rate becomes +1.0; TC-014 step 13 gains the motion criterion, which the bench reruns on unit 3 with the fixed driver.

**The convention of the logs before it.** The bench's live logs of 2026-10-20 and 21 carry the chip's sign on `gyro_y`, positive nose down: the capture on unit 1 (its y rate averages −0.75 deg/s in that sign), TC-002 step 8's log and the 50-frame runs. Their replays reproduce their own transients, as replay should, and a gyroscope bias reported from them is named in the chip's sign (§8.6, §8.8). The log format does not change: the layout is the same, and no header says which convention a field carries.

**Not this finding.** On unit 1 every roll tilt also overshot and sagged back. That is the unit's z offset (§8.8), which compresses the accelerometer's roll angle below the angle the gyroscope integrates; a conforming unit shows neither.

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
- The seven `LCOV_EXCL_EXCEPTION_BR_LINE` lines carry no branch data on the record (§11.7): their decisions are tested, not measured. For the author: apply the loader's style rule to the seven lines and drop the markers (the health topic rewrites `log_reader.cpp` 17 and 64 in any case); or pin lcov 2.5 in the workflow with `--filter branch,exception`, the systemic alternative §5.6 named; or keep the markers with this note. The seven source comments, §5.6 and the CLAUDE.md rule say the decision is still counted, and await that decision. (Closed by Entry 23 §23.4: CLAUDE.md's refined rule restructures where the call can leave the condition and marks only a loop's; seven lines restructured, five marked, §1.3 the standing list.)
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

Run on 2026-10-20 (steps 20 to 22) at the bench of Entry 8 §8.6, and on 2026-10-21 (step 22's backlog check, steps 23 and 24, the capture for step 17) with a sky view, the GT-U7 on the Pi's USB both days. Still reserved: step 17's fixture, the capture turned into `tests/unit/data/gt_u7_capture.nmea` with every position shifted by one constant and the checksums recomputed, with its provenance and its redaction. REQ-SENS-005 stays Partial on it and on §13.10.

**The port.** The board's own micro-USB: `cdc_acm` enumerates a "u-blox 7 - GPS/GNSS Receiver" (idVendor 1546, idProduct 01a7) at full speed as `/dev/ttyACM0`, read at 9600 baud through a copy of the configuration with `sensors.gps.enabled` true and `sensors.gps.device` `/dev/ttyACM0`. The first fixes: quality 1 from 4 satellites at a window on 2026-10-20; quality 2 from 8 on 2026-10-21, a differential fix with SBAS corrections, which the driver takes as it takes quality 1.

| Step | Result | Verdict |
|---|---|---|
| 20 | No fix, the antenna under a desk: GGA's quality and satellites `0,00`, RMC's status `V`. Ten seconds counted through `timeout --foreground 10 cat`: 10 each of `$GPGGA`, `$GPGLL`, `$GPGSA`, `$GPRMC` and `$GPVTG`, 7 `$GPTXT`, no `$GPGSV` with no satellite in view. Without `--foreground`, `timeout` signals the pipeline's process group, and the count's `sort` died before it printed | passes |
| 21 | No fix, 250 cycles: one event, `[FAULT] t=2020 ms channel=GPS type=COMM_TIMEOUT value=2020`; `GPS: 32 sentences (10 GGA/RMC) on /dev/ttyACM0 (9600 baud): 0 fixes (0 discarded in the first second), 5 epochs without a fix`, every error counter 0, 1,065 bytes; jitter 61.2 / 64.3 / 92.7 µs, 0 missed. A path that does not exist: `error: GPS: /dev/ttyACM9: No such file or directory (sensors.gps.device; set sensors.gps.enabled to false to run without a receiver)`, exit 1 | passes; `resyncs` 0 where the card predicts 1 or 2 |
| 22 | The antenna at a window; after a replug (§13.10), the first fix about two minutes later. 500 cycles: no GPS COMM_TIMEOUT or RECOVERY, so the first fix came inside the 2 s; `GPS: 79 sentences (21 GGA/RMC) ... 10 fixes (1 discarded in the first second), 0 epochs without a fix`, every error counter 0, 4,598 bytes, the last fix quality 1 with 4 satellites at UTC 064305.00; jitter 61.3 / 74.5 / 125.3 µs, 0 missed; the replay's `cmp -l` one line, the header's run mode; the reader's `fresh: baro 500, imu 500, gps 9`, the ten fixes less the one discarded | passes on every line read |
| 22, the backlog | Two minutes with nothing reading the port, then 500 cycles: `GPS: 87 sentences (20 GGA/RMC) ... 10 fixes (1 discarded in the first second), 0 epochs without a fix`, every error counter 0, 5,305 bytes; the last fix quality 2 with 8 satellites at UTC 185709.00, the run's own time, so no stale fix was delivered. The backlog was one fix | passes |
| 23 | 1,500 cycles, the board's USB pulled about 20 s in and plugged back 10 s later: `[FAULT] t=22240 ms channel=GPS type=COMM_TIMEOUT value=2020`; `GPS: 196 sentences (42 GGA/RMC) ... 21 fixes (1 discarded in the first second), 0 epochs without a fix`, 473 read errors, about one a cycle after the pull (the read-error path's first run on hardware), every other error counter 0, 11,340 bytes; the last fix quality 1 with 9 satellites at UTC 191524.00. No RECOVERY: the board came back as `/dev/ttyACM1` while the program held the hung-up `ttyACM0`, DEGRADED to the end, as the card expects of a USB receiver (reopening a device is the faults topic's, FTS-DD-001 §3). Every computed field NORMAL on 1,500 of 1,500, the barometer carrying the fused altitude; the barometer and IMU never DEGRADED; voter lanes 1 2 3, no majority on 0; jitter 60.1 / 79.4 / 357.7 µs, 0 missed, the highest maximum of a clean run so far, most likely the kernel's work on the disconnect | passes |
| 24 | After a replug and a fix (quality 1, 8 satellites), 1,500 cycles, the barometer's SDA jumper pulled about 10 s in and pushed back 5 s later: BARO COMM_TIMEOUT at 10,660 ms (value 520), BARO RECOVERY at 15,420 ms (value 4,760). The fused altitude, read once a second, rose 1.4 m during the outage toward the GPS altitude, NORMAL on every line, the GPS a NOMINAL altitude source throughout (REQ-PROC-007), and came back within a second of the recovery; FAILURE_WARNING on 238 frames, exactly the barometer's DEGRADED ones (4,760 ms at 20 ms), the barometric altitude flagged while the fused stayed NORMAL; jitter 60.5 / 81.8 / 161.1 µs, 0 missed | passes |

**The capture for step 17.** 30 s with the fix, 277 lines, recorded on 2026-10-21 after a replug, straight to a file and never opened. It holds real positions, so the raw file never enters the repository; the fixture is made from it with every latitude and longitude shifted by one constant.

**What was read.** Each run's output went to a file read only through `grep` lines (the counters, the COMM_TIMEOUT and RECOVERY events, the timing, on 2026-10-21 the `Validity:` line and the fused altitude once a second), so that no position reached a screen. Not read, by that rule: the GPS fields themselves (the card's "a real position and an altitude within tens of metres of the ground's"), the statuses frame by frame, and on 2026-10-20 the `Validity:` line and any event type but COMM_TIMEOUT and RECOVERY. The `GPS:` exit line carries no position.

**A limitation, not a finding.** Step 23's board came back under another name, so a program holding the old one could not recover even once reopening exists: the stable `/dev/serial/by-id/` path in the configuration, and the driver reopening its device after a run of read errors, are candidates for the faults topic (§13.9).

**The card's expectations.** Two are corrected with this entry: step 22's `cmp` silent, which predates format version 3, and its `gps 10`, which forgot the fix discarded in the first second. One is flagged: step 21's `resyncs` 1 or 2, for the tail of a sentence the flush at open cuts, read 0. The receiver most likely sends each epoch's sentences together, about 213 bytes a second without a fix (1,065 in five seconds), some 0.22 s of the line at 9600 baud, so an open that lands in the rest of the second cuts nothing; step 22's open, at about 460 bytes a second, cut nothing either. Whether the card should say 0 to 2 is Derek's (§13.9).

### 13.9 Open

- §13.8. (§13.7 written 2026-10-14 from run 38668973387. Steps 20 to 24 run on 2026-10-20 and 21; step 17's fixture is left.)
- Five parser and driver outcomes the record shows untaken (§13.7): a longitude that fails to convert beside a good latitude, a GGA with a position and an empty altitude, an RMC with an empty status field, a course that is not a number, a fix-quality GGA paired with a status-V RMC. Five tests, red first, in their own commit; TC-018's unit rows and the matrix's SENS-005 test list grow with them, and §13.3's sentence on lines 258 and 281 stands corrected by §13.7. (Closed 2026-10-14, Entry 15: the five tests, each proven against its line by breaking that line and watching the test fail, in their own commit; TC-018 steps 2, 3, 5 and 13 and the SENS-005 row carry them.)
- The capture fixture and TC-018 step 17's test, which land when the file does.
- The six remaining `LCOV_EXCL_EXCEPTION_BR_LINE` lines and the rule they rest on (Entry 11 §11.8), unchanged by this topic.
- FTS-DD-001 §12: GPS fix latency (the frame keeps the cycle's time; a fix is up to half a second old at 9600 baud); 2D fixes (quality 1 for 2D and 3D alike; GSA or a satellite count, parked with the fix-quality byte); the Kalman filter born from the barometer only, now decidable once a capture with a fix exists; the GPS variance retained at 25 m² until such a capture is analysed.
- FAULT-003b's own event, with the fix-quality byte at the next format bump.
- Unverifiable here (no GCC): the branch counts of the new code on the record, and whether the runner's pseudo-terminal behaves as the Mac's did. (Settled in §13.7: the counts above, five real gaps among them; the pseudo-terminal behaved the same.)
- §13.10's finding: fixed in code with REQ-SENS-005's sentence and its test (2026-10-21); TC-018 step 25 on the Pi closes it.
- The configuration on the stable `/dev/serial/by-id/` path and the driver reopening its device after a run of read errors (§13.8, step 23): candidates for the faults topic.
- Step 21's `resyncs` (§13.8): 0 read against the card's 1 or 2; the wording is Derek's.

### 13.10 Finding: the GPS port's settings are not restored at close

| | |
|---|---|
| **Found** | 2026-10-20, twice: after step 21's run (23:34, a loop of `timeout 3 cat` on the port printed nothing) and after step 22's (23:44, the first 30-second capture came out empty); again on 2026-10-21 after the backlog check's run. Explained on 2026-10-21 at 13:00 |
| **Severity** | Low for the system, which reads with `O_NONBLOCK` and is unaffected; any other reader of the device after a run reads nothing until the settings are reset. On the bench it cost an empty capture and two replugs, each of which seemed to cure it |
| **Affected** | `src/drivers/serial_port.cpp`, `PosixSerialPort`: the constructor applies its settings, `VMIN` 0 and `VTIME` 0 among them, and the destructor closes the descriptor without restoring the ones it found |
| **Fixed in** | The commit that adds REQ-SENS-005's second sentence (FTS-SRD-001 0.20.0): `PosixSerialPort` puts back at close the settings it found at open |
| **Evidence** | §13.8; on 2026-10-21 after step 24's run, nothing done to the port since: `stty -F /dev/ttyACM0 -a` read `min = 0; time = 0`, and after `stty -F /dev/ttyACM0 min 1`, `cat` read 3,047 bytes in 5 s with no replug |
| **Guards** | `PosixSerialPortTest.CloseRestoresTheSettingsFoundAtOpen` (TC-018 step 6); TC-018 step 25 on the Pi |
| **Status** | Fixed in code; closed when TC-018 step 25 passes on the Pi |

**The cause.** At open, `PosixSerialPort` reads the device's settings and changes them: raw mode (`cfmakeraw`), `CLOCAL | CREAD`, no flow control, one stop bit, `IGNBRK`, the speed, and `VMIN` 0 with `VTIME` 0, which the read path needs: on Linux an empty non-blocking read then returns 0 bytes, where with `VMIN` 1 it fails with `EAGAIN`, which the driver counts as another process reading the device (`serial_port.h`). At close it restores none of them, and a terminal's settings outlive the descriptors that set them. With `VMIN` 0 and `VTIME` 0 left behind, a blocking `read()` returns 0 at once whenever no byte is waiting, and `cat` takes 0 for the end of the file and exits: the silent loop, the 0 bytes and the empty capture. The receiver never stopped. The replugs only seemed to help because the `stty ... raw` run after each one set min back to 1; the kernel's two `dwc_otg_hcd_urb_dequeue` warnings at 23:29:59 on 2026-10-20 were unrelated.

**The fix.** REQ-SENS-005 gained a sentence first (FTS-SRD-001 0.20.0), as REQ-PROC-002 gained its re-seed in 0.4.0. `PosixSerialPort` keeps the settings `tcgetattr` returned at open and puts them back with `tcsetattr` before the descriptor closes, in the destructor and on the constructor's one failure path after the settings could have taken (the flush); `VMIN` 0 stays while the program holds the port. `PosixSerialPortTest.CloseRestoresTheSettingsFoundAtOpen` (TC-018 step 6) sets known settings on a pseudo-terminal's slave, canonical with `VMIN` 1, and reads them through a second descriptor while the port holds the slave (`VMIN` 0, not canonical) and after it closes: equal field by field, but for `PENDIN`, a state bit macOS's kernel sets when a line returns to canonical mode. Without the restore it fails five assertions. On the Pi, TC-018 step 25: `stty -a` before and after a run with the receiver, the two readings equal, and `cat` reads with no `stty`. The fix covers every normal exit, not a run stopped with Ctrl-C (`main` has no SIGINT handler, Entry 8 §8.6) or killed, after which `stty -F <device> min 1` still repairs the device; a handler for both is a Sprint 3 candidate (Entry 8 §8.7).

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
- On the Pi: TC-021 step 9 (a live run's LIVE status reaching the receiver, the bridge and the page on the Mac), and a paced replay's lateness on `clock_nanosleep`. (TC-021 step 9 run 2026-10-20, §16.9; the paced lateness still open.)
- Parked in FTS-DD-001 §12 with this entry: replay across rotated files (part 2's demo decides), the sequence number (with the voter, on the same bump as the fix-quality byte).
- Part 2: the instruments (REQ-DISP-001 and -002 entering the SRD), `display_spec.md`, the deterministic replay demo; heading has no direct source on this hardware.

### 16.9 On-target results: TC-021 step 9

Run on 2026-10-20 at the bench of Entry 8 §8.6, the Pi and the Mac on a phone's hotspot, `telemetry --live --zmq tcp://0.0.0.0:5555` on the Pi at `c510034`.

- **The receiver** (the 500-cycle run of TC-014 step 15): the first frame it printed was `seq 45` at 900 ms, and five frames printed before its first status line, `session: mode=LIVE state=RUNNING`, the status of the one-second mark: the one sent before frame 0 went out with the 45 frames the subscription's join lost (Entry 8 §8.6). At the end, `session: mode=LIVE state=ENDED` and `received 455 frames, gaps 0 frames, out of order 0; session LIVE ended`.
- **The bridge and the page** (`--cycles 250`, twice; the Mac's venv first needed `pip install -r requirements.txt` for `websockets` 17.2, and `dashboard/` `npm ci`): LIVE, the instruments moving, then ENDED with every instrument withdrawn under a grey flag; 243 frames of the second run's 250, the 7 missing again the join's; RATE 0 frames/s after the end; the lag −14 ms and the bridge's −11 ms, negative by the first frame's own delay (FTS-DS-001 §5). The bridge's exit line counts both runs, 210 frames and 243: `forwarded 453 frames and 14 statuses to 1 client(s); received 453 frames, gaps 0 frames, out of order 243, refused 0; session LIVE ended`, the second run's frames, whose timestamps restart at zero, counted out of order against the first's (the counter across runs, parked in FTS-DD-001 §12).
- **Verdict.** Passes. The page showed the mode as the card expects, and ENDED at the end. The receiver showed ENDED at exit and learned LIVE from the one-second status, five frames after its first, against the card's "before the first frame", which a receiver that joins late cannot meet; the status repeats once a second for exactly that receiver (REQ-TRANS-004), so the card is corrected to the design with this entry, not to the result.
- **Not run:** a paced replay's lateness on `clock_nanosleep` (§16.8).

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

CI run 38802982922 on `dc989d6`, the branch's head, on the draft pull request (number 1), 2026-10-15 20:34 to 20:37 UTC. It was the fifth run on the branch, from the one push that carried §16.7's record and this entry's seven commits (`aab6772..dc989d6`). Both jobs were green, and the repository's GitGuardian check with them.

The run went unread when it landed and is recorded here on 2026-10-19, after the stack merged into `main` (Entry 26). The run on the first merge commit, 39353187063 on `9d2d3dd`, built this tree on `main` and reproduced the C++ job's figures below and the page's 66 tests.

| Check | Result |
|---|---|
| Google Test (GCC 13.3.0, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 323 of 323 in 4.1 s; no warning in the build |
| pytest (Python 3.13.16, pytest 9.1.1, against the instrumented binary) | 115 of 115 in 113.8 s, `test_motion_profile.py`'s four among them; the log gives no time per test, so the scripted flight's share is not separated |
| lcov 2.0 over `src/` | lines 98.7% (1,954 of 1,980), functions 96.3% (210 of 218), branches 95.4% (843 of 884) |
| cppcheck 2.13.0 (the runner's Ubuntu 24.04 package), 30 files | 0 findings. §16.7's `uselessOverride` report is gone under the inline suppression at `fault_injecting_source.h:36`, and 2.13.0 raises no unmatched-suppression report at the workflow's level, which answers §17.6's open point |
| `page` job (Node 22, Vitest with coverage, `npm run build`) | 66 of 66 in 12 files; statements 99.02%, branches 98.44%, functions 97.59%, lines 99.65%, the Mac's figures (§17.2); `tsc --noEmit && vite build` clean in 140 ms |
| Wall time | the C++ job 3 min 11 s; the page job 21 s |

Per-scope branch coverage on GCC, from `scripts/coverage_check.sh` (thresholds from FTS-TP-001 §6.3), beside the Mac preview of §17.3:

| Scope | Record (GCC) | Preview (clang) | Threshold |
|---|---|---|---|
| processing | 100.0% | 100.0% | 90% |
| config | 100.0% | 97.9% | 90% |
| timing | 100.0% | 100.0% | 80% |
| logging | 100.0% | 86.1% | 80% |
| transport | 88.6% | 92.1% | 80% |
| replay | 100.0% | 100.0% | 80% |
| drivers | 91.6% | 95.8% | 70% |
| overall | 95.4% | 96.1% | 80% |

The files this entry added or changed, from the run's `coverage-html` artifact (read 2026-10-19):

- **`motion_profile.cpp`:** 37 of 37 lines, 7 of 8 branches, 5 of 5 functions. These are clang's eight branches, and the untaken one is the same: `wrap_degrees`' negative side, line 37, since the demo's track never crosses north.
- **`simulated_data.cpp`:** 79 of 79 lines, 14 of 16 branches, 3 of 3 functions. §17.3 counted 11 of 12 on clang, and this section's reservation expected twelve. GCC counts four edges on each of two lines where clang counts two:
  - Line 61 (`positioned_ && timestamp_ms > last_timestamp_ms_`): the untaken edge is §17.3's, a timestamp that does not advance.
  - Line 118 (the `?:` that calls `scripted_readings`): one of its four edges is never taken, and no entry has yet said which. The line is unchanged on `main`, every record from Entry 19 §19.7 on carries the same 14 of 16, and Entry 26 §26.9 keeps it open.
- **`simulated_source.cpp`:** 11 of 11 lines, 3 of 4 branches.
- **`fault_injecting_source.cpp`:** 67 of 67 lines, 16 of 16 branches, unchanged by the suppression.
- **`altitude.cpp`:** 4 of 4 lines, no branch.

The record agrees with the preview wherever the two measure the same thing. Every test passed on both toolchains, and the scopes differ by the edges the two compilers count differently, as in §16.7.

### 17.8 Open

- §17.7. (Closed 2026-10-19: recorded from run 38802982922 after the stack merged, Entry 26.)
- TC-025 step 8: the demo replaying to a Chrome tab, the tab in the background for two minutes, the lag on return and whether LAG flagged and cleared, the bridge's share; Derek's hand step, into §17.5.
- On the Pi: TC-021 step 9 and a paced replay's lateness on `clock_nanosleep` (Entry 16 §16.8); the bench's TC-014 step 13, which decides the roll sign's mapping in the driver. (2026-10-20: TC-021 step 9, Entry 16 §16.9; TC-014 step 13, Entry 8 §8.6: the roll's mapping stands, the pitch rate's sign does not, fixed in the driver, Entry 8 §8.9.)
- Parked in FTS-DD-001 §12 with this entry: the `heading_deg` rename (with the fix-quality byte and the sequence number); the track threshold until a fix capture; the receiver's out-of-order counter across runs; the rotated replay, decided for this demo and parked for a longer one.

## Entry 18 — 2026-10-15, ADS-B Phase A: real SFO arrivals through the same pipeline, from the OpenSky Network API (macOS preview, and the live run)

*Revised 2026-10-19 (Entry 23): earlier revisions of this entry named the six aircraft of the live run by callsign and ICAO address, and the first one's flight by its flight number, route, type and gate. The data license requires aircraft identifiers to be de-identified in any publication, and a public repository is one (FTS-DD-001 Section 12). Each aircraft is now a label, A to F in the order it first appears, one label for a callsign and its address, and every number is kept. The earlier revisions remain in the repository's history, which is not rewritten because every later tree hash is pinned; the message of commit `4dabebd` names the first aircraft too.*

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
| The first selection | aircraft A at frame 0, 7.2 km from the reference point, descending on the 28R final at 720 ft HAE, GS 150 kt, TRK 298°; the hand check on FlightAware agreed, the flight landing at 21:27 PDT (its number, route, type and gate withheld since Entry 23) |
| The switches (the console, `telemetry_adsb.csv`) | eight: t=41.5 s `nearer` to aircraft B at 2.4 km (§18.5); t=77.8 s `nearer` back to aircraft A at 1.2 km; t=98.5 s `landed` → aircraft C 2.2 km; t=124.4 s `landed` → aircraft B 8.9 km; t=134.8 s `nearer` → aircraft D 8.2 km; t=243.7 s `landed` → aircraft E 2.4 km; t=285.2 s `landed` → aircraft F 3.1 km; each row's timestamp is the first frame carrying the new aircraft |
| The `ADS-B:` line | 58 polls, all ok (0 auth, 0 token refused or unavailable, 0 rate-limited, 0 http, transport or malformed), 1 token request, 50 readings, 8 selection changes, 46 aircraft and 5 candidates in the last response, 0 rows skipped, 58 reports taken, 0 overwritten, position age min/mean/max 0 / 1.14 / 3 s, last HTTP 200 |
| The log | 15,000 frames in two files (rotation at 1 MiB: 9,986 and 5,014 frames), no fault event; every frame `baro=NOT_AVAILABLE imu=NOT_AVAILABLE gps=NOMINAL`, the attitude and barometric altitude NO_COMPUTED_DATA and the fused fields NORMAL on all 15,000; the Validity line says so |
| The statuses | 310 on the ZeroMQ transport: 300 periodic, 7 at switches not on a second's boundary, 3 ENDED |
| Timing | 15,000 cycles at 20 ms, jitter min/mean/max 19 / 5,383 / 12,842 µs (p50 6.0, p90 8.1, p99 9.7 ms), 0 missed, with the thread, the publisher and every frame printed to a file. The control the same night (§18.5): the simulator for 15,000 cycles on the same Mac with the bridge, the page and the ZeroMQ publisher up and every frame printed to a file as in the live run, 25 / 3,376 / 14,977 µs (p50 3.9, p90 4.4, p99 4.9 ms), 0 missed. Neither Mac number judges REQ-TIME-003, which is the Pi's `clock_nanosleep` figure (Entry 8, TC-009 step 7, under 1 ms); they measure the macOS `nanosleep` fallback under the demo's load |
| The PFD | `ADS-B ·` and aircraft A's callsign plain in the strip and the bar, RUNNING, the attitude indicator under the outlined `ATT` no-source flag with no cause in the strip, `ALT ft HAE` reading 720 then 420 on short final and −70 at touchdown, VS −400 to −850 fpm on final, GS 150 then 144 kt, TRK 298° under the rose, the ground track a line to the north-west; later `ADS-B ·` and aircraft D's callsign; BARO and IMU `NOT_AVAILABLE held`, GPS `NOMINAL held` between polls; `Traffic data: The OpenSky Network (opensky-network.org)` at the foot |
| The datum (plan flag 6) | the GNSS altitude at touchdown −22.9 m (−75 ft, the broadcast's 25 ft quantization) against SFO's 4 m above mean sea level: a geoid offset of about −27 m, the −32 m the design expected within one quantum; an aircraft on the runway reads below zero on the tape, as FTS-DS-001 §4.2 says |
| The replay | the first file replayed unpaced from beside its record: byte-identical (`2971db60…`), the `Replay input:` line naming an ADS-B run by its first frame and the GPS timeout; replayed paced to the bridge and the page: `REPLAY · ID UNAVAILABLE` boxed in amber, `ALT ft`, the instruments live, no attribution line |

### 18.5 Decisions taken, and deviations from the plan

- **The Kalman birth from either source (flag 3, approved).** No recorded log replays differently: the simulator and the scripted flight read the barometer on frame 0 (the seed-42 hash and the demo's bytes are unchanged), no bench log exists (Entry 14 §14.7), and the one log that would differ is a bench log whose barometer failed its first reads while the receiver already had a fix. In ADS-B mode the filter is born from the first GPS reading, at that reading's value (`fused_altitude_m == gps_altitude_m` on frame 0 in the pytest).
- **NOT_AVAILABLE built, not parked (flag 8).** The size rule held: the enumerator, three skips in the detector, two lines in the sources, the strip, the names and one verdict on the page.
- **The replay of an ADS-B log takes the ADS-B channel timeout, decided from the log's first frame.** Not in the plan. Without it a 5 s poll interval replays as a 2 s UART timeout the run never had, and the replay is not byte-identical; the mode is not in the log, so `main` reads the barometer and IMU statuses of the first frame, which only the ADS-B source writes NOT_AVAILABLE, and says so on the `Replay input:` line. Design §12 parks the honest fix, a mode byte in the next format version.
- **A mutation found a flaw.** Commit 7's ninth mutation, a replaced report's switches dropped, was uncaught; the test written for it found that every switch in a merged report was stamped with the last poll's number, fixed by letting each switch carry its own poll (`SwitchRecord` in the report).
- **The status moved after the read.** The periodic status was published before `read_frame()`; it now follows it, so a status carries the identity of the frame it precedes; the pytest's positions among the frames are unchanged.
- **The live run in the evening.** 21:24 PDT, not daytime; recorded as such.
- **Finding: the selection measures horizontal distance only.** At t=41.5 s aircraft B, descending through 3,498 m HAE on an arrival route 2.4 km from the reference point (TRK 140°, GS 122 kt), displaced aircraft A on short final for 36 s by the nearest-descending rule, which is judged on the ground distance; the fused altitude followed it from 152 m to 2,934 m and back, with vertical speeds of +152 and −154 m/s in the log. An altitude term, a ceiling on candidates or a slant distance, is the fix; it changes REQ-SENS-009's words and adds a tunable, so it is parked in design §12 for Phase B.
- **Finding: a switch is a discontinuity the filter is not told about.** The identity is not in the frame, so the Kalman filter takes a new aircraft's altitude as a step of the old one's and reports vertical speeds of ±60 to ±185 m/s for a few polls after every switch (−64 m/s at the landing switch at t=98.5 s), pegging the vertical speed indicator until the hold's growing covariance lets the new readings win; between polls on one aircraft the indicator read −400 to −850 fpm on final, plausible. The fix, a re-birth at a switch, needs the identity in the frame or a discontinuity flag; parked in design §12 with the vertical rate.
- **The selection's hand check passed:** every aircraft the strip named was a real arrival; the identities are in the run's record under `logs/` only and are de-identified in anything published, this entry included since the revision noted at its head.
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
| cppcheck (the runner's package), 38 files | 0 findings |
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
- The four exception-edge lines of §18.7 without a marker (`aircraft_selector.cpp:103`, `http_client.cpp:6`, `opensky_client.cpp:27`, `opensky_json.cpp:121`) take it in the next commit that touches their files. (Closed by Entry 23 §23.4: three restructured, `opensky_json.cpp:121` marked.)
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

CI run 39175951717 on `5bbb79e`, the branch's head, on the draft pull request (number 2, to `main`, stacked on the PFD branch), 2026-10-18 23:18 to 23:22 UTC, the one push that carried the topic's eight commits (`fe62737..5bbb79e`); both jobs green.

| Check | Result |
|---|---|
| Google Test (GCC, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 412 of 412 in 13.9 s; no warning in the build |
| pytest (Python 3.13, against the instrumented binary) | 142 of 142 in 131.2 s, the ADS-B run over the local stand-in among them |
| lcov over `src/` | lines 98.2% (2,809 of 2,861), functions 97.2% (307 of 316), branches 93.0% (1,320 of 1,420) |
| cppcheck (the runner's package, 2.13.0), 39 files | 0 findings |
| `page` job (Node 22, Vitest with coverage, `npm run build`) | 73 of 73 in 12 files; statements 99.09%, branches 98.93%, functions 97.7%, lines 99.68%; the build clean in 134 ms; 21 s for the job |
| Wall time | the C++ job 3 min 56 s; the page job 21 s |

Per-scope branch coverage on GCC, from `scripts/coverage_check.sh` (thresholds from FTS-TP-001 §6.3), beside Entry 18's record, the last measurement taken, since §19.3 took no Mac preview:

| Scope | Record (GCC) | Entry 18 record (GCC) | Threshold |
|---|---|---|---|
| processing | 100.0% | 100.0% | 90% |
| config | 100.0% | 100.0% | 90% |
| timing | 100.0% | 100.0% | 80% |
| logging | 100.0% | 100.0% | 80% |
| transport | 88.8% | 88.8% | 80% |
| replay | 95.0% | 95.0% | 80% |
| drivers | 89.2% | 89.0% | 70% |
| overall | 93.0% | 92.7% | 80% |

The files the eight commits added or changed, on GCC (`main.cpp` outside lcov's scope, as since Entry 5; the other changed headers in the report carry no branch, and every line of theirs ran):

| File | Lines | Branches | Branches at Entry 18 |
|---|---|---|---|
| `run_mode.cpp` (new) | 8 of 8 | 6 of 6 | — |
| `telemetry_frame.cpp` | 34 of 34 | 21 of 21 | 10 of 10 |
| `logging/log_format.h` (`frame_size_for`, new in the report) | 3 of 3 | 6 of 6 | — |
| `logging/log_format.cpp`, `logging/binary_logger.cpp` | 34 of 34, 51 of 51 | 12 of 12, 12 of 12 | the same |
| `logging/log_reader.cpp` | 74 of 75 | 18 of 18 | 18 of 18 |
| `replay/log_replay_source.cpp` | 44 of 44 | 19 of 20 | 19 of 20 |
| `processing/processor.cpp` | 85 of 85 | 62 of 62 | 60 of 60 |
| `drivers/adsb_source.cpp` | 157 of 158 | 54 of 58 | 48 of 52 |
| `drivers/live_sensor_source.cpp` | 79 of 79 | 32 of 32 | 30 of 30 |
| `drivers/fault_injecting_source.cpp` | 72 of 72 | 16 of 16 | 16 of 16 |
| `drivers/simulated_data.cpp`, `drivers/simulated_source.cpp`, `drivers/motion_profile.cpp` | 82 of 82, 12 of 12, 37 of 37 | 14 of 16, 3 of 4, 7 of 8 | the same |
| `transport/session_message.cpp` | 42 of 42 | 32 of 36 | 32 of 36 |
| `transport/udp_publisher.cpp`, `transport/zmq_publisher.cpp` | 53 of 54, 40 of 40 | 29 of 32, 10 of 12 | the same |

The record reconciles with Entry 18's branch by branch. The eight commits added 33 branches and every one is taken: the switches of the name functions in `telemetry_frame.cpp` (11) and `run_mode.cpp` (6), `frame_size_for`'s two conditionals (6), the processor's NEW_VEHICLE test (2), the live source's fix-quality hold (2), and the ADS-B source's new-vehicle flag and two presence bits (6); 1,387 and 33 make the 1,420. The one taken branch beyond them is a leg §18.7 listed as untaken, `AircraftSelector::candidate`'s absent vertical rate (`aircraft_selector.cpp:46`, taken twice), which commit 7's `AdsbSourceTest.VerticalRateAndPressureAltitudeAreRawFieldsWithPresenceBits` reaches by scripting the kept aircraft's vector without one; the selector's file is unchanged. The drivers scope rises from 89.0% to 89.2% on these: the two sources' eight new branches and the selector's leg. The commits' 62 new lines and 6 new functions all ran, so the 52 unrun lines and 9 unhit functions are Entry 18's, the same counts file by file.

No untaken branch and no unrun line sits on a line the commits changed: a script read the report's source pages for the sixteen source files they touched under lcov's scope and the eight changed headers the report covers, and set every untaken branch and unrun line against the lines `git diff fe62737 5bbb79e` changed in them. So §19.3's statement holds on GCC: the topic added no exception edge, and no line of it takes the marker. In the files it touched, the untaken branches are Entry 18's counts file by file, every one on a line the topic left alone. The four exception-edge lines §18.7 named (`aircraft_selector.cpp:103`, `http_client.cpp:6`, `opensky_client.cpp:27`, `opensky_json.cpp:121`) still carry no marker, since no commit of this topic touched their files; they stay open (§19.8).

One correction to Entry 18, made with this record: §18.7's cppcheck row read 40 files, where its run's log reads `38/38 files checked`, the 38 `.cpp` files of that tree; it reads 38 now. This run's 39 are those and `run_mode.cpp`.

### 19.8 Open

- Phase B (design §12): the vertical rate, recorded since commit 7, as the filter's third measurement; the pressure altitude; the selection's altitude term.
- FAULT-003b: the detector's judgment of the fix quality and the satellite count the frame now carries.
- The voter (design §11.1), whose status field waits for it.
- The demo's recording at version 3 (FTS-DS-001 §7): its hash is recorded when it is made. Made 2026-10-18 with this build before the voter's first line, Entry 20 §20.4.
- Entry 18's open items stand: the OpenSky publication link, TC-009 step 7 with `--adsb` on the Pi, the four exception-edge lines; and Entry 17's: §17.7, TC-025 step 8, the Pi.

## Entry 20 — 2026-10-18, The redundancy voter: three identical processing lanes behind one detector, a majority voter, the voter's status on every frame (macOS preview)

Six requirements (REQ-RED-001 to REQ-RED-006) and one for the display (REQ-DISP-008; SRD 0.13.0 → 0.14.0, 57 → 64 statements), five amended (REQ-PROC-007's statement, REQ-CFG-001's, and the rationales of REQ-LOG-003, REQ-LOG-008 and REQ-PROC-008), four fault entries (FAULT-019 to FAULT-022; FTS-FM-001 0.6.0 → 0.7.0), three test cards (TC-032 to TC-034) and five amended (TC-004, TC-009, TC-017, TC-026, TC-031), FTS-DS-001 0.3.0 → 0.4.0, design Section 13, two new C++ pairs and a header-only interface, the voter panel on the page; no format change, no key, no thread. Ten commits on `claude/redundancy-voter-phase-1-3e0ca4`, stacked on the ADS-B branch at `a7461a3` (Entry 19 §19.7), off `main` until after the v0.1.0 tag, the first recording the single-lane references before any code; §20.7 records the CI run.

### 20.1 What was built

- The single-lane references (commit 1): the demo recorded at format version 3 for the first time and the seed-42 session re-recorded, both by a build of `a7461a3` kept aside (§20.4).
- Documents (commit 2): the SRD statements above, with the decisions of the topic applied (exactly three lanes; bit for bit; a lane excluded after three consecutive disagreements and never readmitted; the two-lane case split in two); FTS-FM-001's *Processing Lane Failures* and a lane conditions table; TC-032 lane isolation, TC-033 no majority, TC-034 determinism over 10,000 frames, TC-009 step 8 for the Pi; FTS-DD-001 Section 13 with the honesty note, the persistence count in Section 12, Section 11.1 entered and its format-version-2 validity rule withdrawn; FTS-DS-001 §4.7; CLAUDE.md's two rules refined (a new enumerator in an existing byte is not a struct change; a value a requirement states is a named constant, not a key).
- `src/processing/fault_detector.h` / `.cpp`, the injector, `pipeline/frame_format.py` (commit 3): `Channel::VOTER` (3) and the fault types LANE_DISAGREEMENT, LANE_FAULTED, NO_MAJORITY and LANE_OVERRUN (4 to 7), new values of the event's existing bytes, their names on both sides, and a case in each of the seven switches over `Channel` and in `fault_type_name`'s over `FaultType` (REQ-RED-003, REQ-RED-004, REQ-RED-006).
- `src/processing/voter.h` / `.cpp` (commits 4 to 6): `lanes_agree`, the one comparison, the floats' bits through `memcpy`, a NaN agreeing with nothing (REQ-RED-002); `Voter`: the pairs in order, the representative, the checked frame with its ten computed fields, the count and the exclusion, the bits taken before the counts move, no majority held under `min(held, FAILURE_WARNING)` with one event per run and no count moved (REQ-RED-003, REQ-RED-004); the overrun by the frame's identity and the held vote cleared on a new vehicle (REQ-RED-006, REQ-PROC-008); `VoterTally` for the exit line.
- `src/processing/redundant_processor.h` / `.cpp`, `src/processing/lane_fault_hook.h` (commit 7): three lanes built element by element from the configuration, each on its own copy of the checked frame, all three every cycle, and the lane-fault interface around each (REQ-RED-001, REQ-RED-005); `tests/unit/scripted_lane_faults.h`.
- `src/main.cpp` (commit 8): `RedundantProcessor` where the processor was, the voter's events logged after the detector's, `RunTallies`, the `Voter:` line; the comments that said the voter was to come (REQ-RED-001).
- The page (commit 9): `voterState` and `lanesIn` beside the instruments' rule, `VOTER_BITS`, `VOTER`, `VoterPanel` (REQ-DISP-008).
- Tests: unit +44, 412 → 456 (`test_voter.cpp` 30: `LanesAgreeTest` 5, `VoterTest` 25; `test_redundant_processor.cpp` 11; `test_replay.cpp` +2, `ReplayReproducesLiveSession` through the three lanes; `test_fault_detection.cpp` +1 and `EventNamesAreReadable` extended); pytest +3, 142 → 145 (`test_voter.py` 3; two expectations of `test_validity.py` moved from 0 to VOTED); page +11, 73 → 84 (`VoterPanel.test.tsx` 5, `instrumentState.test.ts` +3, `Pfd.test.tsx` +2, `geometry.test.ts` +1).

### 20.2 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 456 of 456 (412 before), `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings |
| pytest | 145 of 145 in 127.8 s (142 before) |
| Page (Vitest 4.1, jsdom) | 84 of 84 in 13 files (73 in 12 before); `npm run build` clean |
| cppcheck 2.21.0 | 0 findings with CI's flags at every commit, on 41 files; one style finding in commit 5's first draft (`useStlAlgorithm` on the pair search, a raw loop) fixed with `std::find_if`, not suppressed |
| Mutations | sixteen, every one caught by the test named beside it: commit 5, the count's reset on agreement (`TransientDisagreementResetsTheCount`), `>=` the persistence made `>` (`ThreeConsecutiveDisagreementsFaultTheLane`), the degrade under no majority (`TwoDisagreeingLanesHaveNoMajority`), the no-majority run never ending (`NoMajorityEventOncePerRun`), the NaN test in `floats_agree` (`NaNAgreesWithNothing`), the held vote never updated (`TwoDisagreeingLanesHaveNoMajority`); commit 6, the sequence and the timestamp each left out of the identity check (`StaleLaneIsAnOverrun`), stale lanes eligible (`TwoStaleLanesNeverOutvoteACurrentOne`), the held vote kept across a new vehicle (`NewVehicleClearsHeldValuesNotLatches`), one validity left out of the cleared vote (the same test, after the review of §20.5 widened it to all five fields), every onset a disagreement (`StaleLaneIsAnOverrun`), an excluded lane listed by the tally (`VoterTallyCounts`); commit 7, the lanes sharing one input (`PerturbedLaneInputDivergesOnlyThatLane`), the output hook skipped (`PerturbedLaneOutputIsIsolated`), every lane running lane 1's processor (`DeterministicOverTenThousandFrames`). Two first mutants of the degrade and the tally did not compile under `-Werror` (an unused function, a tautology) and were rewritten so they did |
| The byte checks | §20.4 |
| The panel, by eye | the desktop app's browser pane on a paced replay of the voted demo through the bridge: `VOTE` and the cells 1 2 3 under the GS box, clear of the attitude indicator, the strip RUNNING at 51 fps |

Red first at every commit: commit 3's extended name test did not compile before the new enumerators, commit 4's and commit 7's tests before their headers, commits 5 and 6 before the functions they call, commit 8's two `test_validity.py` expectations failed against the single-lane binary (three failures) before `main` was wired, commit 9's page tests before `VoterPanel` and `voterState`.

### 20.3 Coverage preview

Mac, clang, `build-cov` with the unit tests only (the recipe of Entries 6 to 10), so the record is §20.7's on GCC: processing 100.0%, config 98.2%, timing 96.9%, logging 86.8%, transport 91.4%, replay 100.0%, drivers 91.0%, overall 93.9%, every scope above its threshold. The topic's files: `processing/voter.cpp` 157 of 157 lines and 68 of 68 branches, `processing/redundant_processor.cpp` 23 of 23 and 6 of 6, `processing/fault_detector.cpp` 66 of 66 branches, `drivers/fault_injecting_source.cpp` 16 of 16. One new exclusion: `copy_channel`'s VOTER case in `fault_injecting_source.cpp`, two lines marked `LCOV_EXCL_LINE`, unreachable because `read_frame()` copies the three sensor channels only. No new conditional line combines a decision with a call in a function holding an object with a destructor: the one such candidate, the tally's lane bit beside a comparison in a function holding a string stream, was split so the call sits on a line of its own; any exception edge the record shows goes into §20.7 under the marker rule.

### 20.4 The byte checks

**The single-lane references (step 1).** The voter must change the simulator's logs in `voter_status` alone, so the comparison needs the logs of the pipeline without it, recorded by the same toolchain at the same format version. `build-ref/` was configured from the untouched tree at `a7461a3` (`-DWARNINGS_AS_ERRORS=ON`, the `telemetry` target), its binary copied aside as `build-ref/telemetry_a7461a3` and never rebuilt during the topic; both logs are kept beside it in `build-ref/ref_logs/` (`build*/` is ignored, so neither is committed; the hashes below let either be checked or regenerated).

| Item | Observed |
|---|---|
| The seed-42 500-frame session, `--config config/telemetry_config.yaml --cycles 500` | `7720966cb87473bc429f975794ffb4fdfcba1652b7c1a0bec54bc15da2f15ae3`, Entry 19 §19.4's version-3 hash reproduced; every validity NORMAL from frame 0, no event, `voter_status` 0 on every frame. Its replay `703f621cca12153e267be4794a28dd28a8be4df58a929d8ce40c1c9a55b01269`, Entry 19's too: every record identical, `cmp -l` printing `11 2 3` |
| The demo, `--motion demo --cycles 7500` (FTS-DS-001 §7; Entry 19 §19.8's open item) | 7,500 frames in 907,516 bytes (16 + 7,500 × 121), one file, no event, `voter_status` 0 and the sequence equal to the frame's index on every frame, every validity NORMAL from frame 0, missed cycles 0; `d9ee490a17a9c4cc74d4b019b93e14cb55cfc304b9734e9e5b19d41f61ce41e9` on this Mac (libc++; the runner's libstdc++ draws differently, FTS-DD-001 §8). Its unpaced replay `bb9a8af7a979f24ae02d2b201fe41e1732a4a221289c847d8786e6530f9ee89e`: every record identical, the header differing in the run-mode byte alone. Entry 17's `caba9aeb…` is the same flight at format version 2 |

**The voted logs (commit 8), against the references, by `cmp -l` and by a check of every differing byte's position and values.**

| Item | Observed |
|---|---|
| The seed-42 session | `f1dc959aa7583c90b65293470056ce79561b71caac1ef0c7dfed8ba3e0e20ec5`: exactly 500 differing bytes against `7720966c…`, one per frame at the 1-based offset 132 + 121n (the 16-byte header, the tag, offset 114 of the frame), each `0` against `1`, VOTED's low byte; the header, every other frame byte and the events (none) identical. `Voter:     500 frames, lanes in the vote 1 2 3, no majority on 0 frames, voter events 0` |
| Its replay | `189eb162b045fe2b3abe5f9b133c170f909d0d14dcce01c50418bcfc7172b9e3`: every record identical to the session's, the header differing in byte 11 alone (`11 2 3`) |
| The single-lane seed-42 reference replayed by this build | `189eb162…` again: every record equal to the voted recording's, the voter's status recomputed from the raw frames, never copied (the replay's header differs from the voted session's in the run mode alone, `11 3 2`) |
| The demo | `6f877d1de11e83a7f3c37e91325e06070276b880ec5d7fff9c88c3d73d5bfd1c`: exactly 7,500 differing bytes against `d9ee490a…`, every one at 132 + 121n, `0` against `1`; 907,516 bytes, every validity NORMAL from frame 0, missed cycles 0, `Voter:     7500 frames, lanes in the vote 1 2 3, no majority on 0 frames, voter events 0` |
| Its replay, and the single-lane demo replayed by this build | `c14802ab8c6339fb5d0f8b6ca884f102864f6867e228fb70d294c7bb1abca9c0` both: every record identical to the voted recording's |
| Version-1 and version-2 logs replayed (pytest) | raw fields equal to the input's, every frame VOTED (`test_older_log_replays_through_this_build`), and a version-3 log of the voted session's frames rewritten with status 0, as the single lane wrote them, replayed into the voted session's records exactly (`test_pre_voter_version_3_log_replays_voted`) |

The two new hashes are recorded once each: the seed-42 one in TC-026 step 8, the demo's in FTS-DS-001 §7. The single-lane hashes stay here as the references they were.

### 20.5 Decisions taken, and deviations from the plan

- **Answered before the plan was written (2026-10-18).** The voter's events ride new values of `FaultEvent`'s existing channel and type bytes, no tag and no version bump, by the road `NOT_AVAILABLE` took; a lane is one processor, behind one detector, the Roadmap's `ProcessingLane`, so a sensor fault stays one event and a channel's status one writer.
- **The refinements the code forced, built as the plan's item 9 stated them:** a NaN agrees with nothing; "consecutive" counts majority cycles only; an overrun is a miss on a majority cycle; the Health and Validity topic's rule (NO_COMPUTED_DATA with fewer than two NORMAL lanes) is withdrawn; a field never voted reads NO_COMPUTED_DATA under no majority; a lane's bit appears from the frame after its exclusion; CLAUDE.md's two rules refined; the `Voter:` line counted in a tested unit.
- **Commits 4 to 6 split differently from the plan.** No `vote()` can exist without its no-majority branch, so the plan's commit 4 (the comparison and the majority's output) could not stand alone without a branch written to be rewritten: commit 4 is the comparison alone, commit 5 the vote with its majority, count, exclusion and no-majority rule, commit 6 the overrun, the new vehicle and the tally. Eleven commits as planned; the concepts in the same order.
- **One test beyond the plan's list:** `RedundantProcessorTest.StaleInputIsAnOverrun`, the overrun reached through the lane-fault interface the way the harness will reach it.
- **An adversarial review before the commits.** Four read-only reviewers (the code against the requirements, the tests against the cards, the documents against one another, the page against FTS-DS-001), every finding re-checked by a second reviewer told to refute it: thirteen distinct findings confirmed, each closed in the commit that introduced it, by assertions added to existing tests, so no count moved. Four tests asserted less than their cards: TC-033 steps 1 and 6 now check all five computed fields, TC-032 step 9 that the lanes are bit-identical again after the upset, TC-034 step 5 a version-2 log beside the version-1 one with the older logs' raw fields compared, TC-033 step 8 both statuses under every feed cause. `test_voter_event_names` cited TC-032 step 6, which describes another procedure; it is TC-032 step 11 now, an integration step. This entry counted four amended requirements of five and eight switches over `Channel` of seven. "The only writer of `Validity`" stood in CLAUDE.md, FTS-DD-001 §7 and two matrix rows after the voter began degrading held verdicts; they say "the only judge", and REQ-PROC-007's row traces the no-majority clause to the voter and TC-033. REQ-TIME-003's row did not name TC-009 step 8. FTS-DD-001 §13 and REQ-RED-004's rationale said the ADS-B attitude "stays dashes" under no majority, where the page shows its no-source flag (FTS-DS-001 §4.1); they say so now.
- **Flag, pre-existing, not fixed:** `ctest -j` fails twenty logging and replay tests on this Mac, because each test process names its temporary directory from a static counter that starts at 0 in every process (`ftlg_replay_0`, the logging tests' likewise), so parallel processes collide; serially, as CI runs them, all pass.
- **Flag, pre-existing:** the README's documents table had stood at SRD 0.11.0 and 47 statements since the ADS-B topic; it now reads 0.14.0, 64, and the matrix's counts. FTS-TP-001 §7 says nine requirements are Partial where the matrix counts ten (the four I2C sensor requirements, the GPS and REQ-SENS-008, PROC-004, FAULT-003, TIME-003, TEST-004); left for its author.

### 20.6 Library and toolchain facts this entry rests on

- `std::memcpy` into a `uint32_t` is the defined way to read a float's bits in C++17 (a `reinterpret_cast` read breaks strict aliasing; `std::bit_cast` is C++20), and compilers emit a register move for it; `std::isnan` from `<cmath>`; nothing in the build sets `-ffast-math`, under which `isnan` may be folded away.
- Scoped enumerations of one type compare with the built-in relational operators, so `std::min(Validity, Validity)` is the worst-wins combiner the encoding's order was chosen for.
- cppcheck 2.21's `useStlAlgorithm` reads a loop that returns from inside as a `find_if`; `std::find_if` over a `constexpr std::array` of index pairs takes a lambda that captures the lanes by reference.
- `std::array<TelemetryProcessor, 3>` initialized with double braces from three temporaries is clean on clang and GCC; the processor is copyable, its members values only.
- Under `-Werror`, a mutant that leaves a static function unused (`-Wunused-function`) or writes a tautology does not compile; a mutation proof needs a mutant that compiles.

### 20.7 Measurement of record

CI run 39192240395 on `1366efa`, the branch's head, on the draft pull request (number 3, to `main`, stacked on the ADS-B branch), 2026-10-19 02:55 to 02:59 UTC, the one push that carried the topic's ten commits (`a7461a3..1366efa`); both jobs green.

| Check | Result |
|---|---|
| Google Test (GCC 13.3.0, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 456 of 456 in 14.9 s; no warning in the build |
| pytest (Python 3.13.16, against the instrumented binary) | 145 of 145 in 131.2 s, `test_voter.py`'s three among them |
| lcov over `src/` | lines 98.3% (2,979 of 3,031), functions 97.1% (331 of 341), branches 93.3% (1,412 of 1,514) |
| cppcheck (the runner's package, 2.13.0), 41 files | 0 findings |
| `page` job (Node 22.23.3, Vitest with coverage, `npm run build`) | 84 of 84 in 13 files; statements 99.14%, branches 98.99%, functions 97.82%, lines 99.7%; `VoterPanel.tsx` 100% on all four; the build clean in 134 ms |
| Wall time | the C++ job 4 min 6 s; the page job 26 s |

Per-scope branch coverage on GCC, from `scripts/coverage_check.sh` (thresholds from FTS-TP-001 §6.3), beside Entry 19's record and this entry's Mac preview (§20.3):

| Scope | Record (GCC) | Entry 19 record (GCC) | §20.3 preview (clang) | Threshold |
|---|---|---|---|---|
| processing | 99.1% | 100.0% | 100.0% | 90% |
| config | 100.0% | 100.0% | 98.2% | 90% |
| timing | 100.0% | 100.0% | 96.9% | 80% |
| logging | 100.0% | 100.0% | 86.8% | 80% |
| transport | 88.8% | 88.8% | 91.4% | 80% |
| replay | 95.0% | 95.0% | 100.0% | 80% |
| drivers | 89.2% | 89.2% | 91.0% | 70% |
| overall | 93.3% | 93.0% | 93.9% | 80% |

The files the ten commits added or changed under lcov's scope, on GCC (`main.cpp` outside it, as since Entry 5; the other changed headers carry no code the report counts):

| File | Lines | Branches | Functions | At Entry 19 |
|---|---|---|---|---|
| `processing/voter.cpp` (new) | 143 of 143 | 86 of 88 | 20 of 20 | — |
| `processing/redundant_processor.cpp` (new) | 19 of 19 | 6 of 6 | 3 of 3 | — |
| `processing/lane_fault_hook.h` (new) | 1 of 1 | none | 1 of 2 | — |
| `processing/fault_detector.cpp` | 154 of 154 | 70 of 70 | 13 of 13 | 149 of 149, 70 of 70 |
| `drivers/fault_injecting_source.cpp` | 74 of 74 | 16 of 16 | 8 of 8 | 72 of 72, 16 of 16 |
| `replay/log_replay_source.cpp`, `replay/log_replay_source.h` | 44 of 44, 2 of 2 | 19 of 20, none | 5 of 5, 2 of 2 | the same |

The record reconciles with Entry 19's file by file: a script read both runs' reports, and every file but the first five in the table has the counts it had. The ten commits added 170 lines and every one ran: `voter.cpp` 143, `redundant_processor.cpp` 19, `fault_detector.cpp` 5 (the new enumerators' cases in `channel_name` and `fault_type_name`), `fault_injecting_source.cpp` 2 (the VOTER cases of `fail_reads` and `hold_values`; `copy_channel`'s two are excluded, §20.3), `lane_fault_hook.h` 1; 2,809 and 170 make the 2,979. Those cases sit in switches whose lines carry `LCOV_EXCL_BR_LINE` (§1.3), so they add lines and no branch. The commits added 25 functions and 24 ran: the one that did not is `LaneFaultHook`'s deleting destructor (`D0`), which never runs for an abstract class: a `delete` through the base calls the derived class's deleting destructor, which calls the base's `D2` (Entry 11 §11.7). Its `D2` ran 10 times, and the six other interface headers in the report show the same pair (`data_source.h`, `clock.h`, `publisher.h`, `http_client.h`, `i2c_bus.h`, `serial_port.h`), four of them on objects the code does delete through the base, the compiler-generated variants §1.2 describes. They added 94 branches and 92 are taken: `redundant_processor.cpp`'s 6, and 86 of `voter.cpp`'s 88, against the 68 of 68 clang's preview counted in the same file: GCC counts each operand of the short-circuit chains and the ternary as a decision of its own (sixteen more, all taken, as Entry 11 §11.7 describes) and the returning and unwinding edges of the calls on lines 130 and 167 (four); 1,320 and 92 make the 1,412, and 1,420 and 94 the 1,514.

The two untaken branches are exception-unwind edges, each on a line the topic wrote (line numbers here are the run's, `1366efa`), and nothing else untaken or unrun sits on a line it changed: a script set every untaken branch and unrun line in the report's pages for the seven changed files under lcov's scope against the lines `git diff a7461a3 1366efa` changed in them, after checking that its reading of the pages reproduces every file's totals in both runs (102 untaken branches and 52 unrun lines here, 100 and 52 at Entry 19). The other seven changed files are headers the report does not count, and `main.cpp`. The untaken branch in `log_replay_source.cpp` (line 34) is Entry 19's, on a line the topic left alone.

- `voter.cpp:130`, `if (has_flag(checked, FrameFlag::NEW_VEHICLE))`: four branches, the call returning (49,649 times) and unwinding (never), and the condition's two legs (7 and 49,642). `has_flag` is not `noexcept` and is defined in `telemetry_frame.cpp`; the same call in `processor.cpp:37` carries no unwind edge, so what gives this one its edge lies in what GCC builds for `Voter::vote`, which this record does not establish.
- `voter.cpp:167`, `std::count_if` over the lane states: two branches, the call returning (34 times) and unwinding (never). The predicate on line 168 ran 102 times and makes no branch.

§20.3 said an edge this record showed would go under the marker rule, but on the runner's lcov 2.0 the marker removes every branch on its line, the decision's as well as the unwind edges (Entry 11 §11.7; the seven marked lines carry no branch data in this run), so marking line 130 would take the NEW_VEHICLE decision out of the measurement. The commit that records this run applies instead the style rule Entry 11 §11.7 names, which Entry 8 used in the live source and §20.3 in the tally: the call moves to a statement line of its own, `const bool new_vehicle = has_flag(...)`, and the `if` on the next line tests the local. In this run the statement lines in `voter.cpp` that call one of the project's functions without deciding (`find_representative` at 153, `record_event` at 169 and 199, `lane_bit` at 255) carry no branch data, so the call's edges should leave and the decision's two legs stay. Line 167 makes no decision, so it takes the marker, which there removes only the call's two edges: `// LCOV_EXCL_EXCEPTION_BR_LINE` with a one-line reason, the source's eighth. Line 62's `std::find_if`, the same shape, carries no branch data, and why line 167 keeps its edge is not established here either. The run of that commit should read `voter.cpp` 84 of 84 branches over 144 lines, processing 100.0% (230 of 230) and overall 93.4% (1,410 of 1,510), which the next record reconciles.

The page: branches 98.99% against Entry 19's 98.93%. Every branch the topic added is taken (`VoterPanel.tsx` 100% on all four measures, `voterState` and `lanesIn` with no uncovered line), and the uncovered lines the table names in `src/pfd`, `geometry.ts:123` and `instrumentState.ts:147` and `150`, are Entry 19's lines 110, 107 and 110, moved down by the lines the topic inserted above them; `badge.ts:29`, the fourth, is Entry 19's too, in a file the topic did not change.

### 20.8 Open

- §20.7's two remedies, read from the run of the commit that makes them: `voter.cpp` 84 of 84 branches, processing 100.0%, line 130's decision still counted on its new line.
- TC-009 step 8 on the Pi: the three lanes' cost, paired against a build of `a7461a3` (missed cycles 0, jitter within the run-to-run spread, the cost per replayed frame under 2 ms). (Run 2026-10-20: §20.9, passing on the missed cycles and the cost per frame with two expected results failed; the late cycle of §20.10 open; the spread criterion amended on 2026-10-21 for later runs.)
- The fault-injection harness, the next topic: it drives `LaneFaultHook`, builds `Voter(persistence)` directly to sweep the count, and counts isolation latency, healthy lanes thrown out and no-majority events from the public output; its scenarios go under `tests/fault_injection/`, which FTS-TP-001 §2.3 names.
- Entry 19's open items stand: Phase B, FAULT-003b; and Entry 18's: the OpenSky publication link, TC-009 step 7 with `--adsb` on the Pi, the four exception-edge lines without a marker.

### 20.9 On-target results: TC-009 step 8

Run on 2026-10-20 at the bench of Entry 8 §8.6: `main` at `c510034`, and `a7461a3`, the last commit before the voter, built on the Pi in a second worktree (`telemetry` only, the same compiler and build type); four live runs of 3,000 cycles alternating, `a7461a3` first, each printing every frame into a file in the home folder, nothing else running; then the demo's recording (`--motion demo --cycles 7500`, recorded by `main`) replayed unpaced by each build under its own configuration with `time -p`, the output to `/dev/null`. From the runs' exit lines and jitter files, the percentiles nearest-rank over each run's 3,000 cycles, in µs:

| Run | Minimum | Median | Mean | 99th percentile | Maximum | Missed | `Voter:` |
|---|---|---|---|---|---|---|---|
| `a7461a3`, first | 71.3 | 78.9 | 81.3 | 113.0 | 137.7 | 0 | none, before the voter |
| `main`, first | 59.2 | 79.0 | 79.5 | 112.3 | 135.4 | 0 | lanes 1 2 3, no majority on 0, 0 events |
| `a7461a3`, second | 59.7 | 78.7 | 80.1 | 112.9 | 155.4 | 0 | none |
| `main`, second | 60.7 | 78.9 | 87.1 | 113.5 | **16,810.3** | 0 | lanes 1 2 3, no majority on 0, 0 events |

The demo's 7,500 frames replayed in 0.76 s by `a7461a3` (user 0.63, sys 0.12) and in 0.80 s by `main` with three lanes (user 0.68, sys 0.09): 0.107 ms a frame.

Against the card's expectations:
- **Missed cycles 0 on every run of both builds:** met.
- **The voted build's cost per replayed frame below 2 ms:** met, 0.107 ms, a nineteenth of the bound; the lanes add about 5 µs a frame, at `time -p`'s resolution.
- **`Voter:` naming lanes 1, 2 and 3 with no majority on 0 frames:** met on both of `main`'s runs.
- **The maximum below 1 ms:** not met: `main`'s second run's cycle 8 woke 16.81 ms late, 160 ms into the run; its next worst was 124.9 µs (§20.10). That one cycle adds 5.6 µs to the run's mean.
- **Each build's median and 99th percentile within the other build's run-to-run spread:** not met. `a7461a3`'s medians span 78.70 to 78.91 µs, and `main`'s, 78.95 and 78.99, sit 0.04 and 0.08 µs above them; `main`'s 99th percentiles, 112.34 and 113.46, straddle `a7461a3`'s 112.93 and 112.96. Two runs a build cannot estimate a spread: here it is a fraction of a microsecond wide, and the criterion asks two distributions to agree to tens of nanoseconds. All four medians lie within 0.29 µs of one another and all four 99th percentiles within 1.12 µs.

**Verdict.** Passes. The card's pass/fail text rests step 8 on the missed cycles and the cost per frame, since the jitter measures how late the loop wakes, not how long its work takes: 0 missed in every run, and 0.107 ms a frame. Two of its expected results fail as written, the spread and the maximum, the maximum being §20.10's finding. For later runs the spread criterion was amended on 2026-10-21, after this run, which is judged without it: each build's median and 99th percentile within 10 µs of the other's, a hundredth of REQ-TIME-003's bound, set from the bound and not from these figures (FTS-TP-001 TC-009). Step 7, `main` alone, is Entry 8 §8.6's.

### 20.10 Finding: one cycle 16.8 ms late at the start of a run on the Pi

| | |
|---|---|
| **Found** | 2026-10-20, TC-009 step 8, `main`'s second run (§20.9) |
| **Severity** | REQ-TIME-003's 1 ms maximum exceeded on one cycle of the night's 15,000 timed cycles (none in the capture's 45,000, Entry 8 §8.6). The cycle stayed inside its 20 ms period, so nothing was missed and no frame lost; a stall past the period would have missed one |
| **Affected** | The loop's timing on the Pi with every frame printed to a file on the SD card (suspected, not shown). The scheduler did its part: the deadline was absolute and the next cycle on time |
| **Fixed in** | Open |
| **Evidence** | `jitter_m2.csv`: cycle 8, `jitter_ns` 16,810,253, `missed_cycles` 0; that run's next four 124.9, 118.6, 117.5 and 117.4 µs; every other cycle of the night's five TC-009 runs at or under 155.4 µs; no kernel message since the boot at 22:18; `get_throttled` 0x0 |
| **Guards** | TC-009 steps 7 and 8, which caught it |
| **Status** | Open; REQ-TIME-003 stays Partial until it is understood |

**Hypothesis, untested: a file-system stall on the SD card.** The TC-009 runs printed every frame into a file in the home folder, on the card, 19 lines a frame each flushed by `std::endl`, and `main`'s second run started right after `a7461a3`'s output and its jitter file's copy had been written. The capture printed into `/tmp` and never passed 0.19 ms in 45,000 cycles; step 7 printed to the card too and stayed under 0.14 ms, so one run's spike shows no pattern either way. The test: step 8's four runs repeated with the output to `/dev/null`, with `findmnt /tmp` to confirm that the capture's output never reached the card.

## Entry 21 — 2026-10-18 and 19, The fault-injection campaign: a seeded Monte Carlo harness over the detector and the voter, the demonstration lane fault and its replay, the statistics and the report (macOS preview)

Three requirements (REQ-TEST-005, REQ-TEST-006, REQ-RED-007; SRD 0.14.0 → 0.15.0, 64 → 67 statements), one statement amended (REQ-RED-005's, for the option of REQ-RED-007) and two rationales (REQ-LOG-003's and REQ-RED-003's), two fault entries in a new section, *Uncovered Failures* (FAULT-023 and FAULT-024; FTS-FM-001 0.7.0 → 0.8.0), three test cards (TC-035 to TC-037) and FTS-TP-001 §2.3 rewritten, design Section 14 with §12 and §13 amended, a new controlled document (FTS-FI-001, the campaign's report), two new C++ pairs under `src/` and the campaign's engine under `tests/fault_injection/`, two Python modules and a script. The format is unchanged: one new value of the fault event's type byte. No key, no thread. The commits sit on `claude/fault-injection-monte-carlo-694c09`, stacked on the voter's branch at `e23fc87` (Entry 20 §20.7) and kept off `main` until after the v0.1.0 tag:
- the first makes `ctest -j` run clean;
- the second commits the campaign's predictions before any of its code;
- the ninth records the preview;
- the tenth restates the persistence count's reason with the campaign's evidence (§21.6);
- §21.7 records the CI run.

### 21.1 What was built

- The parallel-test fix (commit 1): `tests/unit/scratch_dir.h`, a temporary directory per fixture made by `mkdtemp` and removed by its destructor, in place of the directory names that came from a static counter starting at 0 in every test process (Entry 20 §20.5's flag): `test_logging.cpp`, `test_replay.cpp`, `test_opensky_json.cpp`, `test_selection_log.cpp`, `test_timing.cpp`, `test_config.cpp`. FTS-TP-001 §7's Partial count corrected to ten (Entry 20 §20.5's second flag).
- Documents (commit 2), every prediction committed before a line of campaign code:
  - the SRD statements above, and the convention sentence on an event's channel;
  - FTS-FM-001's *Uncovered Failures*: FAULT-023, a reading in error inside its limits, and FAULT-024, a non-finite reading;
  - TC-035, the harness's known answers and the CI subset; TC-036, the campaign of record; TC-037, the demonstration lane fault;
  - FTS-TP-001 §2.3 rewritten: the campaign runs on demand, there is no nightly job;
  - FTS-DD-001 §14, with §13's hook and replay paragraphs amended and four bullets parked in §12;
  - FTS-FI-001's method and predictions (Sections 1, 2, 4 and 5).
- `src/drivers/fault_injecting_source.h` / `.cpp` (commit 3): `RawField`; `hold_field` (one axis stuck), `override_field` (NaN allowed) and `bias_field` (a step or a drift); `override_pressure` became the field override. The injector still draws no random number, so an uncovered run's clean twin is its exact counterfactual (REQ-TEST-005).
- `src/processing/lane_fault_injector.h` / `.cpp`, `fault_detector`, `pipeline/frame_format.py` (commit 4), for REQ-RED-005, REQ-RED-007 and REQ-TEST-005:
  - `LaneFaultInjector`, the lane-fault interface's one implementation, with four kinds: an output bit flip; an output NaN; a stale output (the lane's previous real output, kept by the injector, so the lane's state stays intact); an input bit flip;
  - `FaultType::LANE_FAULT_INJECTED` (8) and its name on both sides;
  - `demo_lane_fault`, `parse_lane_fault` and `describe_lane_fault`.
- `src/replay/recorded_injections.h` / `.cpp` and `src/main.cpp` (commit 5), for REQ-RED-007 and REQ-LOG-003:
  - `--lane-fault <lane>@<seconds>`, refused with `--replay`;
  - the injector built from the option, or from the injection a replayed log records;
  - its events logged after the detector's and before the voter's;
  - the `Lane fault:` line.
- The engine, under `tests/fault_injection/` (commit 6):
  - `schedule_rng`: the draws over `std::mt19937` and `std::seed_seq`;
  - `run_schedule`;
  - `fault_catalogue`: 33 kinds and 93 cells with their ranges and draws;
  - `campaign_run`: the real pipeline stepped unpaced, the timeline and the twin;
  - `run_outcome`: the classifier;
  - `prediction`: FTS-FI-001 Section 5's formulas and the judge;
  - their known answers.
- `fault_campaign` and the CI subset (commit 7), for REQ-TEST-005 and REQ-TEST-006:
  - `campaign_record`: the CSV row and the manifest;
  - `campaign_main`: the command, which writes the record and copies the configuration beside it;
  - `test_subset.cpp`: 45 cells, ten seeds each, on every build;
  - `replay_vote` and `judge_as_written`, for the finding of §21.6.
- The statistics and the report generator (commit 8), for REQ-TEST-006: `analysis/campaign_stats.py`, `analysis/fault_campaign.py`, `scripts/fault_campaign.sh`, and FTS-FI-001's §6 subsections and §7 table.
- The preview (commit 9):
  - `scripts/fault_campaign.sh --preview` on the tree of commit 8;
  - FTS-FI-001's generated blocks and three figures (`docs/img/`), its findings (Section 7) and blind spots (Section 8);
  - two unit tests the coverage preview showed missing (§21.3);
  - this entry; design §14's record paragraph and refinement sentence; the matrix, the README and CLAUDE.md.
- Tests:
  - unit +24, 456 → 480: `test_fault_detection.cpp` +7; `test_lane_fault_injector.cpp` 12; `test_recorded_injections.cpp` 5;
  - a new binary, `fault_injection_tests`, 89: the 45 subset cells, 14 known answers, 11 classifier tests, 6 catalogue, 6 prediction, 5 draw and 2 record tests;
  - pytest +31, 145 → 176: `test_lane_fault.py` 12, `test_fault_campaign.py` 19; `test_voter.py`'s name table gained type 8.

### 21.2 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 569 of 569: 480 unit and 89 fault-injection (456 unit before); `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings; `ctest -j 10` clean in about 3 s, five runs in a row at commit 1 |
| pytest | 176 of 176 in 132.1 s (145 before); the same against the instrumented binaries in 133.2 s |
| Page | no file under `dashboard/` changed |
| cppcheck 2.21.0 | 0 findings with CI's flags on `src/`, 45 files. A hand preview on `tests/fault_injection/`, outside CI's scan, found six style findings, fixed rather than suppressed: a local shadowing a function, a copy returned where a reference serves, four loops written as `std::count_if`, `std::find_if`, `std::transform` and `std::replace`. Now 0 |
| The CI subset | 450 runs: 450 as predicted, 448 as Section 5 was written (V09's replicate 4 at both counts, §21.6) |
| The campaign | §21.5 |
| The byte checks | §21.4 |

Red first where the plan said so:
- `ctest -j 10` failed before commit 1 (8, 5 and 4 failures of the 47 logging, replay and timing tests in three runs);
- commit 5's pytest file failed against the voter's build before `--lane-fault` existed;
- each C++ test file failed to compile before the header it tests.

### 21.3 Coverage preview

The preview ran on the Mac, with clang, in `build-cov`, configured offline from the build's already-fetched sources. It covers every C++ test and the pytest suite run against the instrumented binaries, as CI runs them, where Entry 20's preview covered the unit tests alone; the record is §21.7's on GCC. Every scope is above its threshold:
- processing 100.0%
- config 98.2%
- timing 96.9%
- logging 86.8%
- transport 91.4%
- replay 100.0%
- drivers 91.2%
- overall 94.3%: 1,382 of 1,466 branches; lines 99.1%, functions 100.0%

The topic's files:
- `processing/lane_fault_injector.cpp`: 113 of 113 lines, 46 of 46 branches, 11 of 11 functions;
- `replay/recorded_injections.cpp`: 25 of 25 lines, 18 of 18 branches;
- `drivers/fault_injecting_source.cpp`: 127 of 127 lines, 32 of 32 branches;
- `processing/fault_detector.cpp`: 66 of 66 branches.

The first capture left two branches of `lane_fault_injector.cpp` untaken (44 of 46, processing at 99.2%):
- line 107: a stale output from the first cycle, which has no previous output to hand over;
- line 165: the finiteness check of `parse_lane_fault`, which no tested input reached, because `inf` and `nan` fail the shape check before the number is read.

`LaneFaultInjectorTest.OutputStaleOnTheFirstCyclePassesTheLanesOwn` and a refused `2@1e999`, which `strtod` reads as infinity, take both; they are in commit 9. The topic's new conditional lines in `src/` keep their calls on lines of their own (`const bool in_time = active(...)`, the loader's style rule), so GCC's record should show no exception edge on them; any it does show goes into §21.7. The engine under `tests/fault_injection/` is outside the measurement (lcov removes `*/tests/*`), as FTS-TP-001 §2.3 says; its evidence is its own known-answer tests.

### 21.4 The byte checks

The default run's bytes must not move, and the demonstration fault must change only what it claims. The seed-42 session, its replay and the demo were recorded by this build at commit 8's tree, with the configuration of record, and each was compared with Entry 20 §20.4's hash. The demo was then recorded again with `--lane-fault 2@60`, compared field by field with the unaltered demo, and replayed.

| Item | Observed |
|---|---|
| The seed-42 session, `--cycles 500` | `f1dc959aa7583c90b65293470056ce79561b71caac1ef0c7dfed8ba3e0e20ec5`, equal to Entry 20's |
| Its replay | `189eb162b045fe2b3abe5f9b133c170f909d0d14dcce01c50418bcfc7172b9e3`, equal to Entry 20's |
| The demo, `--motion demo --cycles 7500` | `6f877d1de11e83a7f3c37e91325e06070276b880ec5d7fff9c88c3d73d5bfd1c`, equal to Entry 20's |
| The demo with `--lane-fault 2@60` | `a2d1d891b85efbaad0440035eacfc66a1177c5ed1a8534454742bb08331bf4cc`. Against the unaltered demo, no frame differs in any field but `voter_status`, which reads 0x0001 up to 60,040 ms and 0x0005 from 60,060 ms. Three events: LANE_FAULT_INJECTED and LANE_DISAGREEMENT, both naming lane 2, at 60,000 ms; LANE_FAULTED (lane 2) at 60,040 ms. The unaltered demo has none. `Lane fault:    lane 2 from 60000 ms, bit 22 of its fused altitude flipped on every cycle (--lane-fault, REQ-RED-007)`; `Voter:     7500 frames, lanes in the vote 1 3, no majority on 0 frames, voter events 2`; missed cycles 0 |
| Its replay | `33b34f6034cfe202460f1c531d74814dece33d9b468ba0c340d886227f02a5e8`: the same length, every record identical, the header differing in byte 11 alone, the run mode; `Lane fault:` says `re-applied: the replay's input records it` |

### 21.5 The campaign (preview)

The provenance (FTS-FI-001 §3 holds the seed ranges):
- 97,100 runs in 93 cells, in 142.6 s on one core of an Apple M1 Max (macOS 27.2, Apple clang 21, libc++, the default unoptimized build);
- the tree of commit 8, `140644b3af539bf5de72ac417f8fe0b2764ea486`, run by `scripts/fault_campaign.sh --preview` before the commits existed (§21.6);
- the configuration of record, SHA-256 `54a18559…`.

The results:
- Every run matches its per-run prediction (the gate).
- 97,006 match Section 5 exactly as written.
- 0 runs are invalid, and 0 break an invariant.
- The record is identical, byte for byte, to a development run of the same code an hour earlier.

The headline numbers against the predictions:

| Prediction (FTS-FI-001 §5) | Measured (§6) |
|---|---|
| D01, D02, D04: declared at exactly 500 ms; recovered 80 ms after removal | 500 ms in all 4,000 injected channels; recovery 80 ms in all |
| D03: 1,020 to 2,000 ms on the operator's clock, uniform (median 1,500, p95 1,960, maximum 2,000); 1,020 ms from the first suppressed fix; recovery 4,000 to 4,980 ms | 1,520 / 1,960 / 2,000 over 50 values; 1,020 in every run; recovery 4,480 / 4,940 / 4,980, and 4,000 in every run from the first fix after removal |
| D05 to D07: 160 ms (140 after a repeat); D08 to D10: 0 ms | 160 in all 3,000; 0 in all 3,000 |
| Missed detections | 0 in 11,000 injected channels, below 0.3% in every cell |
| T01 to T03: never declared | 0 of 3,000 |
| No false alarm anywhere (zero by construction) | 0 in 707.5 channel-hours (below 0.0042 per channel-hour by the rule of three); 0 declarations and 0 voter events in 10,000 clean runs (below 0.03%) |
| U01: about 8.3 m per hPa; the fused altitude "nearly all" | 8.33 / 8.35 / 8.35 m per hPa; the fused altitude's largest error 120% / 123% / 125% of the barometric (a finding) |
| U02: 2.5 to 25 m; a false vertical speed of about 8.3 m/s per hPa/s | 14.1 / 24.1 / 24.9 m; 8.68 m/s per hPa/s (a finding) |
| U03: 0.3° to 2.9°, atan(offset / g) | 1.6 / 2.83 / 2.91°, 1.001 of atan(offset / g) |
| U04: under 1%, about 0.24% | 0.423% / 0.434% / 0.475% (a finding) |
| U06: no majority from the NaN to the end; U07: roll +∞, NORMAL and VOTED | every run, both |
| V01 to V03: excluded (P − 1) × 20 ms after onset; V04 to V06: only at P = 1 | every run, every count |
| V07: 100, 100, 80, 60, 40, 0% | 100, 100, 80.1, 60.2, 40.3, 0% |
| V09: the healthy lane thrown out (P − 1) × 20 ms after onset, the wrong value voted | thrown out in every run, the wrong value voted in every run; in 10 replicates later than predicted, after no-majority episodes (a finding) |
| V11: lane a excluded first when g ≥ P | 92.2, 84.7, 73.6, 64.1, 55.7, 28.8% against 90.9, 81.8, 72.7, 63.6, 54.5, 27.3% (the interval at P = 2 misses: a finding) |
| V12: a wrong majority when g < P, in P of 11 runs | 8.1, 20.0, 28.5, 38.2, 47.6, 73.3% |

FTS-FI-001 Section 7 explains every gap. The filter's own equations reproduce the three uncovered-cell gaps to three digits:
- U01: an overshoot of 20% at 2.9 s;
- U02: the velocity 4% above the ramp's slope;
- U04: 0.42% at the peak after each fix, against 0.24% on average.

The two voter gaps are a property of IEEE-754 and the draw: V09 and V12 come from a flip of bit 30 that is NaN below 2 m, and V11's interval at P = 2 from 847 gaps of two or more where 818 were expected. Section 8 names the blind spots with these numbers.

### 21.6 Decisions taken, and deviations from the plan

- **Answered before the plan was written (2026-10-18):** the trust test, whose predictions became FTS-FI-001 Section 5 and whose flags became the plan's §11. The plan was approved as written, with the parallel-test fix and §7's count first.
- **The V09 finding (the CI subset, 2026-10-18).** V09's replicate 4 (seed 6900004) departed from Section 5 at both of the subset's counts: an identical flip of bit 30 of the fused altitude is NaN while the value is below 2 m, so the run shows no majority before the wrong majority forms. Derek chose to record it and refine the gate:
  - Section 5 stands unchanged.
  - Section 7 records the full campaign's count: 60 V09 runs and 34 V12 runs.
  - The per-run oracle for V09 and V12 replays the vote frame by frame from the lanes' recorded pairwise agreement, labelled in the code and the report as a refinement the subset found on 2026-10-18, not a prediction.
  - `judge_as_written` keeps the closed forms, and the record carries both verdicts.
- **Deviation: `--preview`.** The plan's script refuses a tree with changes. The preview ran before the commits existed, because Derek makes the commits from the topic's patches after the session. `--preview` therefore records HEAD and the tree's hash instead of a commit. That hash, `140644b3…`, equals commit 8's tree: the patch chain, applied to `e23fc87`, rebuilds it exactly, and `git rev-parse <commit 8>^{tree}` will print it. A campaign of record never uses the flag.
- **Deviation: the configuration in the record.** The plan had the script hash the configuration into the manifest. Instead `fault_campaign` copies the file beside the record, as a session keeps its configuration beside its log (REQ-CFG-003), and the generator names the copy's SHA-256.
- **Deviation: the report's shape.** Section 6 gained subsections mirroring Section 5's, with the blocks in Section 5's order, and Section 7 a generated table of the departures; the plan listed six blocks, and there are seven.
- **Deviation: an extra commit.** The plan's flag 5 proposed restating REQ-RED-003's rationale and design §12's reason with the campaign's evidence, and left the choice to Derek. On the overnight instruction to take the recommended option, the restatement was made, as its own commit (commit 10) so that it can be dropped. The CI record becomes commit 11.
- **Two tests the coverage preview found missing**, added in commit 9 rather than in commit 4, so that the preview's recorded tree stays commit 8's (§21.3).
- **Trailing whitespace** on three lines of commit 6's `test_fault_catalogue.cpp` was removed from the patch before any commit was made.
- **The figures** follow a data-visualization method: the two series colours checked by its palette validator, the chrome recessive, every figure looked at before it was kept. Each figure is a light PNG on its own surface, legible on a dark page.
- **Library and toolchain facts:**
  - `std::mt19937`'s 10,000th output from the default seed is 4123659995, by the standard, and `std::seed_seq::generate`'s algorithm is specified, while the distributions' algorithms are not.
  - numpy's `percentile(method="inverted_cdf")` is the nearest-rank rule.
  - Bit 30 is the highest bit of a single's exponent field: a value in [1, 2) has the field 127, and setting the bit gives 255, a NaN whenever the mantissa is non-zero.
  - `strtod` reads `1e999` as `HUGE_VAL`, an infinity, while `inf` never reaches it past the parser's shape check.
  - matplotlib's bundled DejaVu Sans has no semibold weight (bold is used).
  - `FETCHCONTENT_SOURCE_DIR_<NAME>` builds a second tree from the first one's fetched sources without the network.

### 21.7 Measurement of record

**Entry 20 §20.8's open item, from CI run 39200167816 on `e23fc87`** (2026-10-19 04:34 to 04:38 UTC; both jobs green):
- `voter.cpp`: 144 of 144 lines, 20 of 20 functions, 84 of 84 branches.
- The processing scope: 100.0%.
- Overall: branches 93.4% (1,410 of 1,510), lines 98.3% (2,980 of 3,032), functions 97.1% (331 of 341).
- Google Test 456 of 456; pytest 145 passed in 126.3 s; the page 84 of 84; cppcheck 0 findings.

The 84 branches are the 88 of §20.7 less the two unwind edges that moving `has_flag` to its own line removed, and less line 167's two, which the marker removed. The NEW_VEHICLE decision's two legs therefore still count, on their new line: §20.7's two remedies did what §20.7 said they would.

**This branch's run.** It is CI run 39292191411 on `307a69c`, the branch's head, on draft pull request 4 (to `main`, stacked on the voter's pull request 3), 2026-10-19 17:12:56 to 17:17:52 UTC. The run comes from the one push that carried the ten commits (`e745213..307a69c`); the first attempt at that push met an Internal Server Error from GitHub on the branch's creation and created nothing, and the retry created it. Both jobs passed.

| Check | Result |
|---|---|
| Google Test (GCC 13.3.0, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 569 of 569, the 45 cells of the CI subset among them (`Subset/CellTest.AsPredicted/*`); no warning in the build |
| The CI subset on libstdc++ | every cell's ten seeds as predicted. The schedules are identical to the Mac's by construction (Section 14's draws), the noise is not, and no assertion depends on it, so the gate holds on both standard libraries, as REQ-TEST-006's rationale says |
| pytest (Python 3.13.16, against the instrumented binaries) | 176 of 176 in 140.6 s, `test_lane_fault.py` and `test_fault_campaign.py` among them |
| lcov over `src/` | lines 98.4% (3,159 of 3,211), functions 97.2% (351 of 361), branches 93.7% (1,507 of 1,608) |
| cppcheck (the runner's package, 2.13.0), 43 files | 0 findings |
| `page` job | 84 of 84 in 13 files; no file under `dashboard/` changed |
| Wall time | the C++ job 4 min 52 s; the page job 16 s |

Per-scope branch coverage on GCC, from `scripts/coverage_check.sh`, beside the base's record and this entry's Mac preview:

| Scope | Record (GCC) | Run 39200167816 on `e23fc87` (GCC) | §21.3 preview (clang) | Threshold |
|---|---|---|---|---|
| processing | 99.7% | 100.0% | 100.0% | 90% |
| config | 100.0% | 100.0% | 98.2% | 90% |
| timing | 100.0% | 100.0% | 96.9% | 80% |
| logging | 100.0% | 100.0% | 86.8% | 80% |
| transport | 88.8% | 88.8% | 91.4% | 80% |
| replay | 97.6% | 95.0% | 100.0% | 80% |
| drivers | 89.4% | 89.2% | 91.2% | 70% |
| overall | 93.7% | 93.4% | 94.3% | 80% |

The files the topic added or changed under lcov's scope, on GCC:

| File | Lines | Branches | Functions | At `e23fc87` |
|---|---|---|---|---|
| `processing/lane_fault_injector.cpp` (new) | 109 of 109 | 59 of 60 | 11 of 11 | — |
| `replay/recorded_injections.cpp` (new) | 24 of 24 | 22 of 22 | 1 of 1 | — |
| `drivers/fault_injecting_source.cpp` | 118 of 118 | 32 of 32 | 15 of 15 | 74 of 74, 16 of 16, 8 of 8 |
| `processing/fault_detector.cpp` | 155 of 155 | 70 of 70 | 13 of 13 | 154 of 154, 70 of 70, 13 of 13 |

The branch totals reconcile with the base's record exactly:
- The topic added 98 branches: 60 in the injector, 22 in `recorded_injections.cpp` and 16 in the field modes. 1,510 and 98 make the 1,608.
- 97 of them are taken, and 1,410 and 97 make the 1,507.
- The replay scope rose with `recorded_injections.cpp`'s 22 of 22; `log_replay_source.cpp`'s untaken branch is Entry 19's, on a line the topic left alone.

The one untaken branch is a decision, not an exception edge, on `lane_fault_injector.cpp:155`:
- **The line:** `const bool number_starts = (first >= '0' && first <= '9') || first == '.';`, the leg on which `first == '.'` is true, an onset written with its decimal point first (`1@.5`).
- **No exception edge:** GCC's 60 branches are exactly the two legs of every condition on the file's eighteen conditional lines, counted by hand, so no unwind edge is among them.
- **Why clang showed it taken:** clang counts four branches on that line where GCC counts six, and all four were taken, so the preview could not show the leg (§21.3).
- **Why no test took it:** none of the tested inputs begins its number with a point; `2@ 1` and `2@-1` fail before the test, and `1@0.5` passes as a digit.
- **The fix:** the commit that records this run adds `1@.5`, read as 500 ms, to `LaneFaultInjectorTest.ParsesLaneAtSecondsAndRefusesEverythingElse` and to TC-037 step 1, so the next run should show 60 of 60 and processing at 100.0%.

### 21.8 Open

- §21.7's run of this branch, after the push: the subset's 45 cells on GCC and libstdc++; the coverage of `lane_fault_injector.cpp` and `recorded_injections.cpp` on the record; cppcheck on the 45 files.
- The campaign of record, after the bench session, at the commit that holds the measured constants (TC-036). It changes the uncovered cells' consequences and nothing the thresholds fix.
- TC-037 step 9: the demo recorded with `--lane-fault 2@60` and replayed paced to the page, lane 2's cell amber from 60.06 s (a hand record).
- FAULT-024 at the detector, a non-finite raw value declared OUT_OF_RANGE: the follow-up the plan recommended (FTS-DD-001 §12).
- The README's line on "six GCC exception-unwind edges marked per line" stands against the source's eight markers (`LCOV_EXCL_EXCEPTION_BR_LINE`, Entry 20 §20.7 the eighth). This is pre-existing and left for its author. (Closed by Entry 23: the README points to §1.3, the standing list.)
- Entry 20's TC-009 step 8 stands, as do Entry 19's (Phase B, FAULT-003b) and Entry 18's open items.

## Entry 22 — 2026-10-19, The detector's non-finite check (FAULT-024): the campaign's cells U06 and U07 measured before and after (macOS preview)

The check closes the two blind spots the fault-injection campaign measured in Entry 21: a NaN pressure that left the voter without a majority to the end of the run with every field held, and an infinite rotation rate voted through as a NORMAL roll. What changed:
- **Requirements:** one amendment, REQ-FAULT-003's non-finite clause; SRD 0.15.0 → 0.16.0, still 67 statements.
- **Fault model:** FAULT-024 moved from *Uncovered Failures* to *Out-of-Range Failures*, its number kept; FTS-FM-001 0.8.0 → 0.9.0.
- **Test plan:** one test card, TC-038; TC-035 step 4 amended.
- **Design:** §7's rule written and §12's parked bullet decided.
- **The report:** FTS-FI-001's Section 5.6, predictions committed before the code, and its finding 7.
- **Code:** a check in `fault_detector.cpp`, and three validity measures in the campaign's classifier.
- **Unchanged:** no format, no key, no thread.

The commits are 12 to 15 on `claude/fault-injection-monte-carlo-694c09`, after commit 11 (Entry 21 §21.7): 12 the amendment and the predictions, 13 the measures, 14 the check, 15 the records. §22.7 records the CI run.

### 22.1 What was built

- **The amendment and the predictions (commit 12):**
  - REQ-FAULT-003 gains: "A reading of the barometer or the IMU, or a GPS fix's position, altitude, ground speed or course, that is not a finite number is invalid and shall be detected as out of range." Its rationale says why it is tested first and why the ADS-B extras are not.
  - FAULT-024's new Detection and Response.
  - TC-038.
  - Design §7's paragraph, and §12's bullet decided.
  - FTS-FI-001 §5.6: U06 and U07 since the check. Section 5.3 stays as written.
- **The measures (commit 13):** `held_frames` (every computed value FAILURE_WARNING), `normal_at_end` and `nonfinite_voted_frames` in `RunOutcome` and the record, with `OutcomeTest.TheValidityMeasuresAreCounted`. No behaviour changed: commit 13's U06 and U07 rows equal `307a69c`'s in every column they share (§22.5).
- **The check (commit 14), in `src/processing/fault_detector.cpp`:**
  - `first_non_finite`, a `std::find_if_not` over an initializer list whose decision sits on a line of its own, is the first test of `observe_baro`, `observe_imu` and `observe_gps`, and declares OUT_OF_RANGE with the value.
  - `observe_gps` gains its guard that a frame between fixes, carrying the held fix, is not judged again.
- **The campaign's side (commit 14):**
  - The gate for U06 and U07 moves to Section 5.6: `judge_non_finite`, which is `judge_detected` with a NaN-aware event value plus the downstream measures. `judge_as_written` keeps Section 5.3.
  - The two known answers are rewritten.
  - The generator's U06 and U07 rows tabulate both predictions and the new measures, with a dash for a record older than them.
  - `test_fault_campaign.py`'s planted-departure test counts U06's and U07's by-design departures.
- **The records (commit 15):** this entry; FTS-FI-001's status, finding 7 and Section 8's second blind spot, closed; the matrix, the README, CLAUDE.md, test plan §7.
- **Tests:**
  - unit +5, 480 → 485, the five `NonFinite*` tests in `test_fault_detection.cpp`;
  - fault-injection +1, 89 → 90, `OutcomeTest.TheValidityMeasuresAreCounted` (the two known answers were replaced, not added);
  - pytest 176, one assertion changed.

### 22.2 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 575 of 575 (485 unit, 90 fault-injection), `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings |
| pytest | 176 of 176 in 132.2 s, against the instrumented binaries as against the plain ones. The first run of commit 14 failed one test: `test_a_departure_from_section_5_is_named_in_section_7` assumed that no unplanted run departs from Section 5 as written, and U06's and U07's now do by design; it counts them |
| cppcheck 2.21.0 | 0 findings on `src/`, 45 files. The hand preview on `tests/fault_injection/` found one style finding, a counting loop in a known answer, now `std::count_if`; then 0 |
| Red first | the five `NonFinite*` tests failed against commit 13's detector. The barometer, IMU and GPS tests saw no event; the pipeline test saw NO_MAJORITY (16) and the fields held FAILURE_WARNING from the NaN's frame on |
| The byte checks | §22.4 |
| U06 and U07 before and after, and the full campaign | §22.5 |

### 22.3 Coverage preview

Mac, clang, `build-cov`, every C++ test and the pytest suite against the instrumented binaries; the record is §22.7's on GCC. Every scope is above its threshold:

| Scope | Branches |
|---|---|
| processing | 100.0% |
| config | 98.2% |
| timing | 96.9% |
| logging | 86.8% |
| transport | 91.4% |
| replay | 100.0% |
| drivers | 91.2% |
| overall | 94.3% (1,392 of 1,476); lines 99.1%, functions 419 of 419 |

`processing/fault_detector.cpp` has 205 of 205 lines, 76 of 76 branches and 15 of 15 functions, against 66 and 13 before. The ten new branches are the helper's decision and the four new decisions in the three observers, each taken both ways; the two new functions are the helper and its predicate. Each new decision is a test of a local on a line of its own, so GCC's record should show no exception edge on it.

### 22.4 The byte checks

No default run carries a non-finite value, so no byte may move. Every hash is equal to Entry 21 §21.4's:

| Run | SHA-256 |
|---|---|
| The seed-42 session | `f1dc959aa7583c90b65293470056ce79561b71caac1ef0c7dfed8ba3e0e20ec5` |
| Its replay | `189eb162b045fe2b3abe5f9b133c170f909d0d14dcce01c50418bcfc7172b9e3` |
| The demo | `6f877d1de11e83a7f3c37e91325e06070276b880ec5d7fff9c88c3d73d5bfd1c` |
| The demo with `--lane-fault 2@60` | `a2d1d891b85efbaad0440035eacfc66a1177c5ed1a8534454742bb08331bf4cc` (the voter's status alone differing from the unaltered demo, three events) |
| Its replay | `33b34f6034cfe202460f1c531d74814dece33d9b468ba0c340d886227f02a5e8` (every record identical, the header differing in the run mode alone) |

### 22.5 U06 and U07 before and after the check

The cells were rerun, 300 runs each, with `build/fault_campaign --cells U06,U07`:
- **Before:** on commit 13's tree (`0e0e73d8c1b9d158e83fa30c5359bde03d0cda98`), into `build/campaign/u06-u07-before`. That record equals in every shared column a rerun at `307a69c` (`build/campaign/u06-u07-before-307a69c`), which equals the preview's U06 and U07 rows byte for byte.
- **After:** on commit 14's tree (`777d1704563ec6bde1b96d702fea1cefde96fc2e`), into `build/campaign/u06-u07-after`.

| Measure, runs of 300 | U06 before | U06 after | U07 before | U07 after |
|---|---|---|---|---|
| declared OUT_OF_RANGE on its frame (0 ms) | 0 | 300 | 0 | 300 |
| recovered 80 ms after removal | 0 | 300 | 0 | 300 |
| a voter event | 300 (NO_MAJORITY) | 0 | 0 | 0 |
| no majority from the fault to the end of the run | 300 | 0 | 0 | 0 |
| every field FAILURE_WARNING from the fault to the end | 300 | 0 | 0 | 0 |
| any frame with every field FAILURE_WARNING | 300 | 0 | 0 | 0 |
| every field NORMAL at the end of the run | 0 | 300 | 300 (the roll's +∞ NORMAL) | 300 |
| roll +∞, NORMAL and VOTED on every frame from onset | 0 | 0 | 300 | 0 |
| a value emitted that is not a number | 0 | 0 | 300 | 0 |
| an invariant broken | 0 | 0 | 0 | 0 |
| as the gate predicts | 300 | 300 | 300 | 300 |
| as FTS-FI-001 Section 5.3 was written | 300 | 0 | 300 | 0 |
| error against the twin, median / maximum | none (no NORMAL frame) | 0.086 / 0.274 m (fused altitude) | +∞ (roll) | 0.011 / 0.048° (roll) |

The two claims, measured:
- **A NaN pressure no longer holds every field under FAILURE_WARNING to the end of the run.** Before, all 300 runs held every field for the 500 frames from the NaN to the end. After, none holds every field on any frame, none loses the majority, and all 300 end NORMAL.
- **An infinite rotation rate is no longer voted NORMAL.** Before, all 300 runs voted a roll of +∞ with validity NORMAL on every frame from onset. After, no run emits a value that is not a number.

Every run is as Section 5.6 predicted, and departs from Section 5.3 as written by design.

**The rest of the campaign is unchanged.** The full campaign on commit 14's tree, into `build/campaign/after-full` (97,100 runs in 154.4 s, every one as the gate predicts), equals the preview's record (`build/campaign/tree-140644b3…`) in every column the two share, for every one of the 96,500 runs outside U06 and U07. Its U06 and U07 rows equal the targeted after-run's. Its 694 departures from Section 5 as written are the preview's 94 in V09 and V12 and U06's and U07's 600. No run in it emits a value that is not a number.

### 22.6 Decisions taken, and deviations

- **The request (2026-10-19).** Derek asked to close the loop with evidence: rerun U06 and U07 before and after the fix, and record that a NaN pressure no longer holds every field under FAILURE_WARNING to the end of the run and that an infinite roll is no longer voted NORMAL. The fix is the follow-up the plan's flag 7 recommended and the session had offered as a session of its own. None had started it, so it was built here, test first, the recommended scope taken without a further question.
- **The scope:** every fresh reading of the barometer and the IMU, and a GPS fix's five fields. The ADS-B vertical rate and pressure altitude are left out: nothing consumes them until Phase B, and a field nobody reads should not take a valid fix down. No source writes NaN into a judged field, so no run's bytes move (§22.4):
  - the NMEA parser refuses a non-finite number and reads an empty field as 0;
  - the ADS-B source dereferences only the values a state vector holds;
  - the simulator's values are finite.
- **One amendment, not a new requirement.** REQ-FAULT-003 already asked for invalid readings to be detected; the clause names the non-finite case and the GPS fields.
- **FAULT-024 moved, its number kept,** to sit beside FAULT-008 to FAULT-010; *Uncovered Failures* says where it went.
- **Predictions before the code, measures before the check.** FTS-FI-001 §5.6 was committed (commit 12) before the check (commit 14). The measures got a commit of their own (13), so that the before and after records come from one classifier and each names a commit's tree.
- **The report's generated blocks were not regenerated.** They are the preview's, at commit 8's tree, and FTS-FI-001's status line says that their U06 and U07 rows predate the check. The next campaign of record regenerates them with commit 14's generator, whose rows show both predictions.
- **A test's assumption corrected (§22.2),** not a prediction.
- **Library facts:**
  - `std::find_if_not` over `std::initializer_list<double>`: the list's backing array lives until the end of the full expression that made it, so the helper returns the value, never a pointer into it.
  - `std::isfinite` is false for NaN and both infinities.

### 22.7 Measurement of record

CI run 39298962945 on `aedd45b`, the branch's head, on draft pull request 4, 2026-10-19 18:00:40 to 18:05:10 UTC. It ran on the one push that carried commits 11 to 15 (`307a69c..aedd45b`). Both jobs passed.

| Check | Result |
|---|---|
| Google Test (GCC 13.3.0, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 575 of 575, the five `NonFinite*` tests and the subset's 45 cells among them; no warning in the build |
| The CI subset on libstdc++ | every cell's ten seeds as predicted, `Subset/CellTest.AsPredicted/U06` and `U07` under FTS-FI-001 Section 5.6 |
| pytest (Python 3.13.16, against the instrumented binaries) | 176 of 176 in 138.2 s |
| lcov over `src/` | lines 98.4% (3,185 of 3,237), functions 97.2% (353 of 363), branches 93.8% (1,518 of 1,618) |
| cppcheck (the runner's package, 2.13.0), 43 files | 0 findings |
| `page` job | 84 of 84 in 13 files; no file under `dashboard/` changed |
| Wall time | the C++ job 4 min 27 s; the page job 17 s |

Per-scope branch coverage on GCC, beside Entry 21's record and this entry's preview:

| Scope | Record (GCC) | Run 39292191411 on `307a69c` (GCC) | §22.3 preview (clang) | Threshold |
|---|---|---|---|---|
| processing | 100.0% | 99.7% | 100.0% | 90% |
| config | 100.0% | 100.0% | 98.2% | 90% |
| timing | 100.0% | 100.0% | 96.9% | 80% |
| logging | 100.0% | 100.0% | 86.8% | 80% |
| transport | 88.8% | 88.8% | 91.4% | 80% |
| replay | 97.6% | 97.6% | 100.0% | 80% |
| drivers | 89.4% | 89.4% | 91.2% | 70% |
| overall | 93.8% | 93.7% | 94.3% | 80% |

The files commits 11 to 15 changed under lcov's scope, on GCC:

| File | Lines | Branches | Functions | At `307a69c` |
|---|---|---|---|---|
| `processing/fault_detector.cpp` | 181 of 181 | 80 of 80 | 15 of 15 | 155 of 155, 70 of 70, 13 of 13 |
| `processing/lane_fault_injector.cpp` (only its test changed) | 109 of 109 | 60 of 60 | 11 of 11 | 109 of 109, 59 of 60, 11 of 11 |

The record reconciles with Entry 21's exactly:
- **Branches:** 1,608 and the check's 10 make the 1,618.
- **Taken:** 1,507, the check's 10 and the leading point's leg make the 1,518.
- **Lines:** 3,159 and the detector's 26 make the 3,185.
- **Functions:** 351 and the helper with its predicate make the 353.

The four expectations this entry set before the push:

| Expectation | Measured |
|---|---|
| `lane_fault_injector.cpp` at 60 of 60, the leg Entry 21 §21.7 found untaken now taken by `1@.5` | 60 of 60: met |
| `fault_detector.cpp`'s new branches all taken, with no exception edge | 80 of 80. That is the 70 of the earlier run and exactly the check's 10 decision legs, so GCC added no unwind edge to the lines that test a local: met |
| processing at 100.0% | 100.0%: met |
| the subset's U06 and U07 as predicted on libstdc++ | both passed under Section 5.6, with every other cell: met |

None was missed. Entry 21 §21.7's one open measurement, the leading point's leg, is closed by the same run.

### 22.8 Open

- The campaign of record after the bench session, whose generated tables will show U06 and U07 as detected.
- The ADS-B extras' finiteness, with Phase B.
- Entry 21's open items stand (TC-037 step 9; the README's count of exception markers), and Entry 20's (TC-009 step 8). (The README's count closed by Entry 23.)

## Entry 23 — 2026-10-19, The cleanup pass before the stack merges: a value that is not a finite number on the bridge, the twelve exception-edge lines, Entry 18's aircraft (macOS preview)

Review of the stack before it merges found three things wrong, none of which a test had caught:
- **The bridge could not send a value that is not a finite number.** The frame carrying the detector's verdict on such a reading never reached the page (§23.5).
- **The exception-edge markers.** Four lines carried unmarked unwind edges (Entry 18 §18.8), the README counted six markers where the source had eight, and seven marker comments, §1.3 and CLAUDE.md's rule said that a marked decision stays counted, which Entry 11 §11.7 had shown false on the runner's lcov 2.0 (§23.4).
- **Entry 18 named six real aircraft** by callsign and ICAO address in a public repository, against the data license (§23.6).

What changed:
- **Requirements:** one sentence in REQ-DISP-005; SRD 0.16.0 → 0.17.0, still 67 statements.
- **Display specification:** §2.1's row for a value that arrived as null, and §6's trail; FTS-DS-001 0.4.0 → 0.5.0.
- **Test plan:** TC-022 steps 17 and 18; TC-023 step 8.
- **Design:** §9's bridge and instruments paragraphs.
- **Code:** the bridge's one encoder; the page's types, rule, plot and trail; seven C++ lines restructured, one marked, five marker comments corrected.
- **Records:** §1.3, the standing exclusions table; CLAUDE.md's rule refined; the README's count replaced by a pointer; Entry 18 de-identified; four test fixtures moved into the synthetic set.
- **Unchanged:** no format, no key, no thread; the default run's bytes (§23.2).

The commits are 17 to 20 on `claude/fault-injection-monte-carlo-694c09`, after commit 16 (`40cfd54`, Entry 22 §22.7): 17 the bridge and the page, 18 the exception-edge lines, 19 Entry 18 and the fixtures, 20 the records. §23.7 records the CI run.

### 23.1 What was built

- **The bridge and the page (commit 17; REQ-DISP-005, REQ-DISP-001):**
  - `pipeline/bridge.py`: `wire_text`, the one encoder for every message, turns each float that is not a finite number into null through `finite_or_null` and calls `json.dumps` with `allow_nan=False`, so that a bare token can never be written. `finite_number`, the type of `--track-min-speed`, refuses NaN and both infinities.
  - `dashboard/src/feed.ts`: `WireNumber`, `number | null`, for the frame's 18 floats; `WirePosition`; `finitePosition`.
  - `pfd/instrumentState.ts`: `instrumentState` takes each instrument's values, and a null is the failure flag after the feed's own cause, whatever its verdict; `trackShown` shows no track for a speed that arrived as null.
  - `pfd/Pfd.tsx` hands each instrument its values, GS and TRK on states of their own; `App.tsx` gives the plot its coordinates and a marker only at a finite position; `useTelemetryFeed.ts` adds no trail point for a position that arrived as null.
  - REQ-DISP-005's sentence, FTS-DS-001 0.5.0, design §9, TC-022 steps 17 and 18, TC-023 step 8, the matrix.
- **The twelve lines (commit 18; REQ-TEST-003):** seven restructured and five marked (§23.4); one assertion in `test_opensky_client.cpp`, that an HTTP error's detail is empty, which no test had checked; CLAUDE.md's rule; §1.3; the README's pointer; the workflow's comment; `opensky_json.h`'s comment.
- **Entry 18 and the fixtures (commit 19; REQ-DISP-007):** §23.6.
- **The records (commit 20):** this entry; the closing notes on Entry 11 §11.8, Entry 18 §18.8, Entry 21 §21.8 and Entry 22 §22.8; the README, CLAUDE.md.
- **Tests:**
  - pytest 176 → 179, the three new in `test_bridge.py`, whose thirteen others now parse every message as `JSON.parse` does;
  - page 84 → 89, five new in four files;
  - Google Test 575 (485 unit, 90 fault-injection) unchanged, one assertion added.

### 23.2 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 575 of 575 (485 unit, 90 fault-injection), serially and with `-j 10`, `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings |
| pytest | 179 of 179: in 132.9 s against the plain binaries on commit 17's tree, and in 133.3 s against the instrumented ones on commit 18's |
| Page | 89 of 89 in 13 files; `npm run build` (`tsc --noEmit`, then Vite) clean |
| cppcheck 2.21.0 | 0 findings on `src/`, 45 files |
| Red first | Against `40cfd54`, the two bridge tests failed with `ValueError: NaN is not JSON` at the first frame that carried one, and the flag's test timed out at 10 s, a bridge serving with a NaN threshold; four page tests failed (§23.5). The fifth page test, a frame whose value arrived as null kept, passed before and after: JSON's null always parsed |
| The byte checks | The five hashes of Entry 22 §22.4, each equal on commit 18's build: the seed-42 session `f1dc959a…`, its replay `189eb162…`, the demo `6f877d1d…`, the demo with `--lane-fault 2@60` `a2d1d891…`, its replay `33b34f60…` |

### 23.3 Coverage preview

Mac, clang, `build-cov`, every C++ test and the pytest suite against the instrumented binaries, on commit 18's tree; the record is §23.7's on GCC. Every scope is above its threshold:

| Scope | Branches | Entry 22 §22.3 |
|---|---|---|
| processing | 100.0% | 100.0% |
| config | 98.2% | 98.2% |
| timing | 96.9% | 96.9% |
| logging | 92.2% | 86.8% |
| transport | 91.4% | 91.4% |
| replay | 100.0% | 100.0% |
| drivers | 91.6% | 91.2% |
| overall | 94.8% (1,380 of 1,456); lines 99.1% (3,719 of 3,752), functions 419 of 419 | 94.3% (1,392 of 1,476) |

On the new lines, every restructured decision is taken both ways and no moved call keeps a branch:
- **The decisions:** `log_replay_source.cpp:56` (the reader test) and `:60` (`!opened`), two of two each; `log_reader.cpp:124`, the filter, four of four; `opensky_client.cpp:29`, two of two.
- **The calls, with no branch after the filter:** `fault_detector.cpp:163`, `voter.cpp:172`, `log_replay_source.cpp:59`, `log_reader.cpp:119`, `:120` and `:123`, `aircraft_selector.cpp:104`, `http_client.cpp:7` and `opensky_client.cpp:32`. The named predicates carry none either, but for `fault_detector.cpp`'s `||`, two of two as before.
- **The marked lines** keep clang's edges in this preview (`log_reader.cpp:121` and `opensky_json.cpp:121`, five of eight each). clang's data flags no edge as an exception edge, so lcov 2.5 ignores the marker here (Entry 11 §11.3); the runner's lcov 2.0 removes the lines whole.
- **Logging rose from 86.8%** because the old filter line carried sixteen of clang's edges, five of them unwind edges never taken; its condition now holds the decision's four, all taken.

### 23.4 The twelve lines

The question Entry 11 §11.8 left open (restructure the marked lines, pin lcov 2.5, or keep the markers with a note) is closed by CLAUDE.md's refined rule: where a decision line can be restructured so that the call sits on a line of its own, it is; the marker stays only where the call cannot leave the condition, as in a loop condition or a range-for.

"Its own line" has to mean a complete line. lcov's branch filter (`containsConditional`, read in lcov 2.5's `lcovutil.pm`) drops a line's branches only when the line holds none of `? ! < > == != && ||` and no `if`, `for`, `while`, `switch` or `case`, and its parentheses balance; a line that opens a call it does not close is judged together with its continuation. Two of Entry 18 §18.8's four lines already gave the call a statement of its own, split across two lines with the predicate's `==` on the second, which is why they kept an edge.

The record before is run 39298962945's, read from its `coverage-html` artifact (downloaded with Derek's permission) before any edit; the record after is what §23.7 checks.

| # | The line at `40cfd54` | What it is | Decision | Record before (GCC) | Expected after (GCC) |
|---|---|---|---|---|---|
| 1 | `config/telemetry_config.cpp:159`, `Section::Section` | a range-for over a YAML node | marked, kept: yaml-cpp's iterator calls belong to the loop | none, the marker removing the line | none |
| 2 | `processing/fault_detector.cpp:157`, `observe_imu` | `std::find_if` over the three axes, across five lines | restructured: the predicate named `at_a_rail`, the call on line 163 | none on the call (the marker); the predicate's `\|\|`, 4 of 4 on line 159 | none on the call; the `\|\|` 4 of 4; the file 80 of 80 |
| 3 | `processing/voter.cpp:171`, `Voter::vote` | `std::count_if` over the lanes, across two lines | restructured: `in_vote` named | none (the marker) | none; the file 84 of 84 |
| 4 | `timing/periodic_worker.cpp:54`, `PeriodicWorker::run` | `wait_for` as the loop's condition | marked, kept | none | none; the file 4 of 4 |
| 5 | `replay/log_replay_source.cpp:56`, `LogReplaySource::has_next` | `!reader_.has_value() && !open_next_file()` | restructured: the reader test, the call into a local on a line of its own, then the test of the local | none: the decision went with the edges | lines 56 and 60, two of two each; the file 23 of 24, from 19 of 20 |
| 6 | `logging/log_reader.cpp:104`, `LogReader::next_frame` | `next()` as the loop's condition | marked, kept | none | none |
| 7 | `logging/log_reader.cpp:116`, `list_log_files` | a range-for over `directory_iterator`, whose increment can throw | marked, kept; now line 121 | none | none |
| 8 | `logging/log_reader.cpp:118`, `list_log_files` | a string and a path temporary in the filter | restructured: the prefix and the `.bin` path built before the loop, the extension into a local; the condition calls only `rfind` and path `==`, both `noexcept` | none: the decision went with the edges | line 124, four of four; the file 22 of 22, from 18 of 18 |
| 9 | `drivers/aircraft_selector.cpp:103`, `AircraftSelector::update` | `std::find_if` across two lines | restructured: `is_selected` named | 2, 1 taken: the call returning 41 times, unwinding never | none; the file 55 of 62, from 56 of 64 |
| 10 | `drivers/http_client.cpp:6`, `HttpResponse::header` | as 9 | restructured: `has_name` named | 2, 1 taken | none; 2 of 2, from 3 of 4 |
| 11 | `drivers/opensky_client.cpp:27`, `failure` | a `std::string` built in each arm of a ternary | restructured, with line 26's ternary on the same flag, into one `if`/`else`, the copy on a line of its own; the defaults, 0 and empty, make the two the same | lines 26 and 27: 14, 9 taken; line 27 alone 12, five of them never taken | the `if`'s two, both taken; the file 88 of 96, from 95 of 108 |
| 12 | `drivers/opensky_json.cpp:121`, `parse_states` | a range-for over the states array | marked, new: nlohmann's iterator calls belong to the loop | 8, 5 taken: the loop's two legs and three calls returning, three unwinding never | none; the file 103 of 122, from 108 of 130 |

Seven lines restructured and five marked: `src/` goes from eight markers to five, and §1.3 lists the five with the tests that take each loop's decision both ways. In total the record should move from 1,518 of 1,618 (93.8%) to 1,512 of 1,602 (94.4%):
- **Branches, −16:** −2 and −2 (lines 9 and 10), −12 (line 11's fourteen become two), −8 (line 12), +4 and +4 (lines 5 and 8).
- **Taken, −6:** −1, −1, −7, −5, +4, +4.
- **By scope:** drivers 749 of 829 (90.3%, from 89.4%), logging 52 of 52 (100.0%), replay 45 of 46 (97.8%, from 97.6%); processing, config, timing and transport unchanged.

One expectation rests on an assumption: the predicates of lines 9 and 10 compare `std::string`s and read an `std::optional` through `operator->`, calls that should give their lines no branch as long as libstdc++ declares them `noexcept`. If either line carries an unwind edge, §23.7 says so.

### 23.5 The bridge's finding

Review found it, not the campaign. The fault-injection campaign (FTS-FI-001) runs the C++ pipeline in-process and judges each frame; the transports, the bridge and the page are outside it, and no campaign run encodes a frame as JSON.

- **The fault.** `json.dumps` writes NaN, Infinity and -Infinity as bare tokens unless told otherwise, and they are not JSON. The page's `JSON.parse` refused each such message, `parseMessage` returned null and the hook dropped it, frame and all. Since FAULT-024 (Entry 22) the detector declares a reading that is not a finite number OUT_OF_RANGE and its raw field keeps the reading, so the frame dropped was the one carrying the verdict. A GPS fix is held until the next one, so a fix that was not a number rode about 50 frames, 1,000 ms, which is the page's stale threshold: the page went STALE at the threshold's edge for each such fix, and a receiver that kept sending one would have left it STALE without ever showing the GPS DEGRADED. The relative position, computed from the NaN latitude, went the same way.
- **The fix.** A value that is not a finite number crosses as null, the status or the validity beside it saying why (REQ-DISP-005's sentence), and the page never draws a null.
- **Before and after:**
  - **In-process,** with the frames as the binary sends them (a NaN pressure under BARO DEGRADED, gyro x +∞ and gyro y −∞ under IMU DEGRADED, a NaN fix held across 49 frames under GPS DEGRADED): on `40cfd54`'s bridge the strict parse failed at the first of them; now every message parses, with null exactly where a value was not finite.
  - **Through the binary,** with the session's own frames, three readings replaced, replayed: BARO, IMU and GPS DEGRADED on those frames, the detector's OUT_OF_RANGE event for each, every computed field a number, every frame VOTED and every frame forwarded.
  - **On the page,** the held NaN fix as the bridge now sends it, every 20 ms for 2 s: RUNNING throughout, the GPS DEGRADED in the channel row, the GS, TRK and POS flags. The same frames with bare tokens: every one refused, and STALE past 1,000 ms.
- **A second fault, which the null exposed.** Before the rule the page drew a null as zero: a null coordinate is 0 in arithmetic, so the red run's trail drew each null fix at the origin (`150,150 150,150 150,150` for one real fix and two null ones), and a ground speed that arrived as null under NOMINAL, on a frame between fixes that the detector does not judge, read 0 kt. `instrumentState` now flags a null whatever its verdict, and the trail and the marker take only a position whose coordinates are numbers.
- **The one float a flag puts on the wire,** `--track-min-speed`, accepted `nan`: the red run's bridge served with it until the test's 10 s timeout. It now refuses NaN and both infinities, exiting with status 2.

### 23.6 Decisions taken, and deviations

- **The request (2026-10-19).** Derek asked for the cleanup pass as the last change before the stack merges, plan mode first, on this branch as Entry 23. Four questions were answered before the plan was written:
  - the marker list lives in §1.3, which becomes the standing exclusions table;
  - the four fixture identifiers in real namespaces are replaced;
  - Entry 18's FlightAware clause keeps the landing time only;
  - the CI run's `coverage-html` artifact may be downloaded, this run's and the next.
- **One encoder at the boundary,** not a conversion per field: the coordinate rule's place, so that a float added to the frame later crosses as null with no change.
- **The page's rule goes beyond DEGRADED.** A null under NORMAL or NOMINAL needs an injected value or a crafted log today, and the old page would have drawn it as zero.
- **`--track-min-speed`'s refusal,** the refinement the plan proposed, came with its approval.
- **The marker rule.** The marker's own effect on lcov 2.0 decided it: a marked line loses its whole branch record, so restructuring keeps measured a decision that marking hides. Lines 5 and 8 of §23.4 had been tested but not measured since Entry 5.
- **The plan's model of line 11 was wrong.** It counted six branches; the record has twelve. The artifact, read before any edit, set §23.4's expectations from the record instead of the model.
- **Entry 18 (commit 19).** Four of its lines named six aircraft by callsign and ICAO address, and its first selection's row named the first aircraft's flight by FlightAware's flight number, route, aircraft type and gate. Each aircraft became a label, A to F in order of first appearance, one label for a callsign and its address, and every number stayed; the FlightAware clause keeps that the hand check agreed, and the landing time. A note at the entry's head says that earlier revisions named them, that the history keeps them, and that the message of commit `4dabebd` names the first aircraft too. History is not rewritten: every later tree hash is pinned, the preview's included.
- **The tree, searched (2026-10-19).** Nothing else from the live run is in the 244 tracked files, the PNGs' metadata included, nor in the titles and descriptions of pull requests 2 to 4. Four fixtures used identifiers in real namespaces, against `opensky_examples.h`'s rule of unallocated addresses and callsigns no airline uses: the callsign `UAL1234`, an airline's designator, and the addresses `a1b2c3`, `b2c3d4` and `0a0b0c`. They moved into that file's set (TEST001; `000001`, `000002`, `000003`; `00000a` and `00000A` for the case test) in `test_session_message.cpp`, `test_bridge.py`, `test_session_status.py`, `testMessages.ts`, `Pfd.test.tsx` and TC-021's and TC-031's steps; no test count changed. A search of the tree for the twelve identifiers, with a list kept outside the repository, finds nothing.
- **Flags, pre-existing, found while reading:**
  - the matrix's header had named SRD v0.15.0 since Entry 22's commit 12, and FTS-DS-001's v0.14.0; both now name v0.17.0;
  - `main.cpp`'s marker, added in `bc7bc59` for the replay probe, was removed in `f8cebb5` with no entry saying so (`main.cpp` is outside lcov's scope);
  - Entry 12 §12.7 says "the three marked lines of `list_log_files`", which held two, and Entry 13 §13.3 cites a coverage style rule in CLAUDE.md that then lived in `telemetry_config.cpp`'s coverage note; both are dated and left as they are;
  - Entry 18's row of switches says "eight" and lists seven; the eighth is the first selection, as the next row's count of 8 changes says; kept.
- **Library facts:**
  - Python's `json.dumps` defaults to `allow_nan=True` and writes `NaN`, `Infinity` and `-Infinity`; `json.loads` accepts them, and `parse_constant` is how a test refuses them;
  - `JSON.stringify(NaN)` is `"null"`, so a page test of a bare token writes its text by hand;
  - in JavaScript, `null` in arithmetic is 0, and `null >= 0` is true;
  - `std::basic_string::rfind` and `std::filesystem::path`'s `operator==` are `noexcept`;
  - argparse reads `--track-min-speed -inf` as a second option, so the test writes `--track-min-speed=-inf`.

### 23.7 Measurement of record

CI run 39319857862 on `279e8c6`, the branch's head, on draft pull request 4, 2026-10-19 20:30:22 to 20:34:51 UTC. It ran on the one push that carried commits 17 to 20 (`40cfd54..279e8c6`). Both jobs passed. The run on `40cfd54`, commit 16 alone (39303078311), passed too.

| Check | Result |
|---|---|
| Google Test (GCC 13.3.0, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 575 of 575 in 22.6 s; no warning in the build |
| pytest (Python 3.13.16, against the instrumented binaries) | 179 of 179 in 138.5 s, the three new bridge tests among them |
| lcov over `src/` | lines 98.4% (3,192 of 3,243), functions 97.2% (353 of 363), branches 94.4% (1,514 of 1,604) |
| cppcheck (the runner's package, 2.13.0), 43 files | 0 findings |
| `page` job | 89 of 89 in 13 files, the TypeScript build clean |
| Wall time | the C++ job 4 min 29 s; the page job 22 s |

Per-scope branch coverage on GCC, beside Entry 22's record and this entry's preview:

| Scope | Record (GCC) | Run 39298962945 on `aedd45b` (GCC) | §23.3 preview (clang) | Threshold |
|---|---|---|---|---|
| processing | 100.0% | 100.0% | 100.0% | 90% |
| config | 100.0% | 100.0% | 98.2% | 90% |
| timing | 100.0% | 100.0% | 96.9% | 80% |
| logging | 100.0% | 100.0% | 92.2% | 80% |
| transport | 88.8% | 88.8% | 91.4% | 80% |
| replay | 97.8% | 97.6% | 100.0% | 80% |
| drivers | 90.3% | 89.4% | 91.6% | 70% |
| overall | 94.4% | 93.8% | 94.8% | 80% |

The files commits 17 to 20 changed under lcov's scope, on GCC, read from the run's `coverage-html` artifact as from §23.4's:

| File | Lines | Branches | Functions | At `aedd45b` |
|---|---|---|---|---|
| `drivers/aircraft_selector.cpp` | 79 of 79 | 55 of 62 | 14 of 14 | 79 of 79, 56 of 64, 14 of 14 |
| `drivers/http_client.cpp` | 6 of 6 | 2 of 2 | 2 of 2 | 6 of 6, 3 of 4, 2 of 2 |
| `drivers/opensky_client.cpp` | 152 of 165 | 88 of 96 | 14 of 14 | 151 of 164, 95 of 108, 14 of 14 |
| `drivers/opensky_json.cpp` | 145 of 150 | 103 of 122 | 11 of 11 | 145 of 150, 108 of 130, 11 of 11 |
| `logging/log_reader.cpp` | 78 of 78 | 24 of 24 | 5 of 5 | 74 of 75, 18 of 18, 5 of 5 |
| `replay/log_replay_source.cpp` | 46 of 46 | 23 of 24 | 5 of 5 | 44 of 44, 19 of 20, 5 of 5 |

`fault_detector.cpp` (80 of 80) and `voter.cpp` (84 of 84) are unchanged in every count, and the five marked lines carry no branch data.

The record reconciles with Entry 22's exactly:
- **Branches:** 1,618, less 2 and 2 (the two `find_if` calls' pairs), 12 (`failure()`'s fourteen become the `if`'s two) and 8 (the new marker's line), plus 4 (`has_next`'s two tests) and 6 (the filter's), make the 1,604.
- **Taken:** 1,518, less 1, 1, 7 and 5, plus 4 and 6, make the 1,514.
- **Lines:** 3,237 and the six new statement lines (one in `failure()`, three in `list_log_files`, two in `has_next`) make the 3,243. 3,185 and those six, all executed, make 3,191. The 3,192nd is `list_log_files`'s closing brace, which the earlier record held unexecuted: a brace that only an unwinding cleanup reached, the kind Entry 5 §5.4 names. Its hoisted locals now run their destructors there on every return.
- **Functions:** 353 of 363, unchanged; a named lambda is the function an inline one was.

The five expectations §23.4 set before the push:

| Expectation | Measured |
|---|---|
| No unwind edge on a restructured line: `aircraft_selector.cpp` 55 of 62, `http_client.cpp` 2 of 2, `opensky_client.cpp` 88 of 96 | 55 of 62, 2 of 2 and 88 of 96. The named predicates' lines carry no branch, so §23.4's assumption about libstdc++'s `noexcept` held: met |
| The two decisions the markers had hidden, measured and taken: `log_replay_source.cpp` 23 of 24, `log_reader.cpp` 22 of 22 | `log_replay_source.cpp` 23 of 24. `log_reader.cpp` 24 of 24, every branch taken, but the count is off by two: GCC counts six branches on the filter's two-term `&&` (two for each term and two for the test of their result), where §23.4 counted four, clang's number. The earlier record held the precedent, six on `has_next`'s two-term `while` at line 55, unread: the coverage met, the count missed |
| The new marker's line gone, the rest unchanged: `opensky_json.cpp` 103 of 122; `fault_detector.cpp` 80 of 80, `voter.cpp` 84 of 84 | all three: met |
| The totals: 1,512 of 1,602 (94.4%); drivers 90.3%, logging 100.0%, replay 97.8%; the other scopes unchanged | 1,514 of 1,604, 94.4%; every scope's percentage as predicted. The two extra branches are the filter's, so this is the same miss |
| The suites: Google Test 575, pytest 179, the page 89 | 575, 179 and 89: met |

One expectation was missed, in a count and not in coverage: the filter's decision is measured and fully taken, with six branches where four were predicted. Every restructured line keeps its decision fully covered, and no restructured line carries an unwind edge.

### 23.8 Open

- Entry 22's open items stand (the campaign of record after the bench session; the ADS-B extras' finiteness, with Phase B), with Entry 21's TC-037 step 9 and Entry 20's TC-009 step 8. The README's count of exception markers (Entry 21 §21.8) is closed.

## Entry 24 — 2026-10-19, The ADS-B selection's altitude ceiling: REQ-SENS-009 amended, the eleventh key of the `adsb` block, the overflight of Entry 18 reproduced in the synthetic set (macOS preview)

The live run of 2026-10-15 found that REQ-SENS-009's rule, the nearest descending aircraft to the reference point, is judged on the ground distance alone: aircraft B, descending through 3,498 m HAE on an arrival route 2.4 km from the reference point, displaced aircraft A on short final for 36 s, and the fused altitude followed it from 152 m to 2,934 m and back (Entry 18 §18.5). The fix changes the requirement's words and adds a tunable, so it was parked in design §12 for Phase B; it is now the last change before the stack merges, on a branch of its own. The design call, taken from three options laid out in plan mode (§24.4): a ceiling on candidates, one key, no other term.

What changed:
- **Requirements:** REQ-SENS-009's statement (a candidate's geometric altitude at most a configurable ceiling; the kept aircraft kept while at or below it) and its rationale; REQ-CFG-001's list; SRD 0.17.0 → 0.18.0, still 67 statements.
- **Fault model:** FAULT-015's What, Cause, Detection and Response, with the reason `above`; FTS-FM-001 0.9.0 → 0.10.0.
- **Test plan:** TC-028 steps 11 to 15, its objective, preconditions and pass/fail; TC-029 step 20, its level line and pass/fail, and step 19's expectation; TC-016 steps 4 and 5's key count.
- **Design:** §3's ADS-B paragraph and the key count; §12's finding bullet decided, and the schema bullet's third instance, the first to meet a real recording (§24.5).
- **Code:** `adsb.ceiling_m`, 48 → 49 keys; `AircraftSelector::candidate` a member over the ceiling, `under_ceiling`, the `above` drop; `AdsbSource`'s candidate count over a lambda; five unit tests; the pytest's third aircraft.
- **Records:** the matrix, README, CLAUDE.md; the Planned lists, from which the altitude term leaves Phase B.
- **Unchanged:** no format change, no thread; the default runs' bytes (§24.2).

The commits are 1 to 4 on `claude/adsb-altitude-term-d9561c`, stacked on the fault-injection branch at `94b7c42` (Entry 23 §23.7) and kept off `main` until after the v0.1.0 tag, draft pull request 5 to `main`: 1 the documents, 2 the key, 3 the rule and its tests, 4 the pytest and the records. §24.7 records the CI run.

### 24.1 What was built

- **Documents (commit 1; REQ-SENS-009, REQ-CFG-001):** the statements and rationale above; FAULT-015; TC-028 steps 11 to 15 and their expected rows; TC-029 step 20; the design's two sections; the matrix's header at SRD 0.18.0 and the REQ-SENS-009 row's note.
- **The key (commit 2; REQ-CFG-001, REQ-SENS-009):** `src/drivers/adsb_config.h`: `ceiling_m`, 1,650 m, its derivation as its comment; `src/config/telemetry_config.cpp`: `read_number_at_least("ceiling_m", 0.0, …)`, the eleventh key of the block, required when the block is present like the ten before it; `config/telemetry_config.yaml`: the key between the margin and the miss tolerance, with its one-line comment; `tests/unit/test_config.cpp`: the defaults test's line, the rule table's row (`NonNegative`, `-1` refused by name) and the count, 48 → 49; the counts in the matrix, README and CLAUDE.md.
- **The rule (commit 3; REQ-SENS-009):** `src/drivers/aircraft_selector.h` / `.cpp`: `SwitchReason::ABOVE` ("above") appended, so the earlier values keep their numbers; the constructor's fourth argument; `candidate` a `const` member, usable, under the ceiling and descending, since the ceiling is the selector's; `under_ceiling`, private, for a usable state only (its geometric altitude present, so no branch re-tests the optional); in `update`, after the `usable` test and before the margin, a kept aircraft above the ceiling is dropped with `above` and the nearest candidate, chosen among aircraft under the ceiling, or none takes its place. `src/drivers/adsb_source.cpp`: the selector built with the ceiling; the candidate count over a lambda on its own line. `tests/unit/test_aircraft_selector.cpp`: `place`, which sets an aircraft by distance and bearing from `AdsbReference{}` through the inverse of `distance_m`'s formula and checks the placement against the selector's own distance, so no test writes a coordinate; `make_with_ceiling`; the five tests of TC-028 steps 11 to 15; `EveryReasonHasAName` with `above`; 11 → 16 tests. The matrix's REQ-SENS-009 row, README's selection sentence, CLAUDE.md's ADS-B bullet.
- **The pytest and the records (commit 4; REQ-SENS-009, REQ-TEST-002):** `tests/integration/test_adsb.py`: a third synthetic aircraft, `000003` TEST003, in every states response, descending through 3,498 m 2.4 km from the reference point, placed by `placed(distance_m, bearing_deg)` from the reference the shipped file names (`adsb_value`, two regexes, so the file gains no coordinate or threshold); `test_an_aircraft_above_the_ceiling_is_never_presented`, TC-029 step 20; the exit line's census 2 aircraft and 1 candidate; 13 → 14 tests in the file. This entry; the Planned lists in CLAUDE.md and README; the matrix's pytest rows; two key-count sites the sweep found after commit 1 (design §3's "ten keys", TC-016 step 5's 48).

### 24.2 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 580 of 580 (575 before), `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings; serially, 22 s |
| pytest | 180 of 180 in 133 s (179 before); the ADS-B run over the local stand-in with the third aircraft in every response: 12 polls, 2 aircraft and 1 candidate in the last response, 2 selection changes, no frame, row or status carrying the overflight, the replay byte-identical |
| Page (Vitest) | not run: nothing under `dashboard/` changed (89 of 89 in Entry 23 §23.7) |
| cppcheck 2.21.0 | 0 findings in 45 files with CI's flags |
| Red first | commit 2: the shipped file with the key against the loader without it, `adsb.ceiling_m: unknown key`, every `ConfigTest` that starts from the shipped text failing, then green with the field and the one loader line; commit 3: the four behaviour tests of TC-028 steps 11 to 14 against a selector that stored the ceiling and did not apply it (the overflight taken as `nearer`, the boundary not held, the aircraft above the ceiling 100 m out challenging, no `above`), 4 of 16 failing, then green; step 15 passed at once, the key having landed in commit 2 |
| The byte checks | the five hashes of Entry 22 §22.4, each equal on commit 3's build: the seed-42 session `f1dc959a…`, its replay `189eb162…`, the demo `6f877d1d…`, the demo with `--lane-fault 2@60` `a2d1d891…`, its replay `33b34f60…`; the key reaches no simulated run |

### 24.3 Coverage preview

The Mac recipe of Entry 11 (`build-cov` on clang, lcov 2.5, the unit tests and the pytest suite against the instrumented binary, the workflow's removals and `--filter branch`), read for the lines this topic adds; the record is §24.7.

- `aircraft_selector.cpp` on clang: 47 of 52 branches, 88 of 89 lines in the filtered data; the one unexecuted line is 13, the closing brace of `identity_of` after its `return`, which clang's gcov lists as a line of its own and GCC does not (the record had every line, Entry 18 §18.7); line 26's unreachable return is excluded by its marker as before. The lines this topic adds: `candidate`'s chain (line 49), six branches on clang, every one taken, the ceiling's false leg 32 times (the overflight, the boundary and the go-around tests); `under_ceiling` (line 53), a comparison, no branch; the `above` test in `update` (line 126), both ways, 32 kept under the ceiling and 3 dropped; `switch_reason_name`'s new case under the switch's `LCOV_EXCL_BR_LINE` as before. The source's count over a lambda carries no branch.
- Per scope, from `scripts/coverage_check.sh` over the filtered `lcov.info`: processing 100.0%, config 98.2%, timing 96.9%, logging 92.2%, transport 91.4%, replay 100.0%, drivers 91.6%, overall 94.8%, every scope over its threshold; Entry 23's preview read 94.8% overall and 91.6% for drivers too, so the four taken branches moved neither figure at the first decimal.
- The untaken legs on clang are the five absences in `usable` (lines 43 to 45: no fixture nulls a latitude, longitude, velocity, track or position time). The expectations for the record are in §24.5.

### 24.4 The geometry, and the design call

All distances are the selector's own equirectangular `distance_m` over the shipped box and reference point; the field's height is §18.4's touchdown reading.

| Quantity | Value |
|---|---|
| The box's edges from the reference point | 14.3 km north, 19.0 km south, 24.6 km east, 15.1 km west |
| Its corners | 20.8 (NW), 24.3 (SW), 28.4 (NE), 31.1 km (SE, the far corner) |
| A 3° glide path | 262 m high 5 km out; 1,629 m high at the far corner |
| The field | −23 m HAE at touchdown (§18.4), so the path's HAE at the far corner is 1,606 m |
| The overflight (§18.5) | 3,498 m HAE at 2.4 km; slant distance 4,242 m |
| An aircraft on a 3° final 5 km out | slant distance 5,007 m |
| The fixtures' three arrivals | 5.8°, 4.8° and 4.3° above the field at 5, 9 and 14 km: 480, 730 and 1,030 m, all under the ceiling |

- **Slant distance** (`√(d² + h²)`) reorders and never excludes: an aircraft on a 3° path scores `d × 1.0014`, so it beats the overflight only inside 4.24 km of the reference point; 5 km out it still loses, 5,007 m against 4,242 m. Rejected.
- **A weighted altitude term** (`√(d² + (w·h)²)`): the overflight loses to a 3° final at distance D only when `w² (h_o² − h_D²) > D² − d_o²`, so w > 1.26 against a final 5 km out, 2.8 at 10 km, 5.9 at 20 km and 10.0 at the far corner; one weight for the box must be at least ten, it depends on the box, alone in the sky the overflight is still selected, and nothing bounds the number: every expectation is a comparison between two aircraft, never a boundary a test can sit on either side of. Rejected.
- **A ceiling on candidates**, chosen: inside the box every aircraft on an approach to this field is below `d_far × tan 3°`, and an aircraft above it is on no approach to this field at any ground distance, so height alone separates the two populations, which no function of distance can, and the separation holds with no other candidate present: the selection becomes none, which the channel reports as a timeout (FAULT-003), rather than a vehicle it is not. One number, one boundary, both sides tested; the margin hysteresis untouched among the candidates that remain. A cone, `h ≤ d·tan γ`, was considered and dropped: it collapses at the reference point and needs the field's own HAE as a second number.
- **The shipped value, derived:** 31,091 m × tan 3° = 1,629 m above the field, 1,606 m HAE, rounded up to the next fifty, 1,650 m: 44 m (about six of the broadcast's 25 ft quanta) above the path at the far corner, 1,848 m below the overflight. Why the far corner: the largest ground distance a candidate in the box can have, so nothing on the 3° path inside the box is ever excluded, and an aircraft above the ceiling far out was never the nearest, so the ceiling bites only near the field. Why the geometric altitude: the field the frame presents, present for every usable state, in the vector's own datum; the pressure altitude may be absent and moves with the weather. `AircraftSelectorTest.TheShippedCeilingClearsAThreeDegreePathAtTheBoxesFarCorner` pins the value to the shipped box: a change to the box fails it until the ceiling is re-derived.
- **No hysteresis band at the ceiling:** a band would be a second tunable. The pathological case is an aircraft level within one broadcast quantum of the ceiling, reading a hair of descent, nearer than the kept aircraft by more than the margin; each flap is two recorded changes and two re-births, which a live run's `_adsb.csv` would show (§24.8).

### 24.5 The recorded configurations, and the expectations for CI

- **The schema consequence.** Every configuration recorded since 2026-10-15 carries the `adsb` block, because the record is the shipped file's text and the file holds the block in every mode. Under design §12's schema bullet a replay without `--config` is judged by the record's keys, so every log recorded before this change, ADS-B or simulated, Entry 18's live log among them, is now refused without `--config`: `adsb.ceiling_m: missing`. The remedy the bullet names, `--config` with a current file, runs the replay, whose `Configuration:` line then says the file is not the one recorded and that identity is not claimed; in fact the bytes are unchanged, because the ceiling never enters a replay: the selection ran at the source, the log holds the frames and `<prefix>_adsb.csv` the switches, and a replay of an ADS-B recording reads the block only for `timeout_ms`. Nothing in the repository is affected (no log or record is committed, and the pytest records and replays with one file); an optional key with a fallback is what REQ-CFG-001's rationale forbids; the schema version stays parked, with this change as the bullet's third instance and the first to meet a real recording. The hand check on the live log is in §24.8.
- **Expectations for the record (§24.7):** `aircraft_selector.cpp` from 55 of 62 branches (Entry 23 §23.7) to 59 of 66: `candidate`'s chain gains a term, two branches, and the `above` test is a two-branch `if`, all four taken; GCC counts an `&&` chain's terms at two each plus two for the result (Entry 23 §23.7's lesson), so the chain's count is the record's to confirm. The seven untaken legs Entry 18 §18.7 lists stay (five absences in `usable`, the null vertical rate, one more). `adsb_source.cpp` unchanged in count: the lambda and the call make no decision. Overall 1,518 of 1,608 (94.4%); the suites 580, 180 and 89.

### 24.6 Decisions taken, and deviations

- **The five design calls**, taken as the plan recommended and approved as written: the ceiling; the key `adsb.ceiling_m`; 1,650 m; the reason `above`; the CI artifact for §24.7 to be confirmed before it is downloaded.
- **The red step's shape (commit 3).** A stored ceiling with no reader fails clang's `-Wunused-private-field` under `-Werror`, so the honest red defined `under_ceiling` and called it nowhere; four tests failed on behaviour, and the fifth, the derivation pin, passed at once.
- **Two key-count sites** the verification sweep found after commit 1's patch was taken, design §3's "ten keys" and TC-016 step 5's "48", are corrected in commit 4 rather than by re-cutting the earlier patches.
- **The branch move.** The plan's step 0 named the reset as Derek's command; the request itself had asked for it, so it was run in the session on a clean tree and is recorded here.
- **No coordinate written.** The fixtures of steps 11 to 15 and the pytest's third aircraft are placed by distance and bearing from the configured reference point; the pytest reads that point from the shipped file. The one coordinate pair in the diff is TC-028's pre-existing preconditions line, the airport's published position, on a line this entry amended.
- **Entry 18 §18.6** lists "`std::count_if` over a static member function" among the facts it rests on; since commit 3 the predicate is a lambda over the member. The dated entry stands.
- **The pytest's census.** `test_exit_line_counts_what_the_script_implies` now expects 2 aircraft and 1 candidate in the last response, the overflight counted and never a candidate.

### 24.7 Measurement of record

CI run 39342447232 on `8fbdf03`, the branch's head, on draft pull request 5, 2026-10-19 23:34:50 to 23:38:52 UTC. It ran on the one push that carried commits 1 to 4 (`94b7c42..8fbdf03`), the branch's first. Both jobs passed.

| Check | Result |
|---|---|
| Google Test (GCC 13.3.0, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 580 of 580 in 20.1 s; no warning in the build |
| pytest (Python 3.13.16, against the instrumented binaries) | 180 of 180 in 135.8 s, TC-029 step 20's test among them |
| lcov over `src/` | lines 98.4% (3,202 of 3,253), functions 97.3% (355 of 365), branches 94.3% (1,519 of 1,610) |
| cppcheck (the runner's package, 2.13.0), 43 files | 0 findings |
| `page` job | 89 of 89 in 13 files, the TypeScript build clean |
| Wall time | the C++ job 4 min 0 s; the page job 19 s |

Per-scope branch coverage on GCC, beside Entry 23's record and this entry's preview:

| Scope | Record (GCC) | Run 39319857862 on `279e8c6` (GCC) | §24.3 preview (clang) | Threshold |
|---|---|---|---|---|
| processing | 100.0% | 100.0% | 100.0% | 90% |
| config | 100.0% | 100.0% | 98.2% | 90% |
| timing | 100.0% | 100.0% | 96.9% | 80% |
| logging | 100.0% | 100.0% | 92.2% | 80% |
| transport | 88.8% | 88.8% | 91.4% | 80% |
| replay | 97.8% | 97.8% | 100.0% | 80% |
| drivers | 90.3% | 90.3% | 91.6% | 70% |
| overall | 94.3% | 94.4% | 94.8% | 80% |

The files commits 2 and 3 changed under lcov's scope, on GCC, read from the run's `coverage-html` artifact beside Entry 23's:

| File | Lines | Branches | Functions | At `279e8c6` |
|---|---|---|---|---|
| `drivers/aircraft_selector.cpp` | 87 of 87 | 60 of 68 | 15 of 15 | 79 of 79, 55 of 62, 14 of 14 |
| `drivers/adsb_source.cpp` | 158 of 159 | 54 of 58 | 19 of 19 | 157 of 158, 54 of 58, 18 of 18 |
| `config/telemetry_config.cpp` | 482 of 482 | 242 of 242 | 31 of 31 | 481 of 481, 242 of 242, 31 of 31 |

The other 59 files of the report equal Entry 23's record in lines, functions and branches.

The record reconciles with Entry 23's exactly:
- **Branches:** 1,604, plus 2 (`candidate`'s chain, four terms where it had three, eight branches where it had six, now over lines 49 and 50), 2 (the `above` test, line 126) and 2 (an exception-unwind pair on the loop's `if (!candidate(state))`, line 93, below), make the 1,610.
- **Taken:** 1,514, plus 2, 2 and 1, make the 1,519. The pair's unwind leg is the one not taken: the call returned 423 times and never unwound.
- **Lines:** 3,243, plus 8 in the selector, 1 in the source (the lambda) and 1 in the loader (the key's line), make the 3,253, every new one executed: 3,202.
- **Functions:** 363, plus `under_ceiling` and the source's lambda, make the 365; 355 ran.

The five expectations §24.5 set before the push:

| Expectation | Measured |
|---|---|
| `aircraft_selector.cpp` 59 of 66: `candidate`'s chain two more branches and the `above` test two, all four taken | The four as predicted: the chain's new term taken both ways (32 states above the ceiling), the `above` test 32 kept and 3 dropped. But 60 of 68: line 93 carries an unwind pair it did not carry on Entry 23's record. Missed by that pair |
| The seven untaken legs stay, named as the five absences in `usable`, the null vertical rate and one more | Seven old legs stay, but §24.5 named one wrongly. The null vertical rate was taken already: `candidate`'s six branches were all taken on Entry 23's record, by `AdsbSourceTest.VerticalRateAndPressureAltitudeAreRawFieldsWithPresenceBits` since format version 3. The seven are the five absences in `usable` (lines 43 and 44), the unwind leg of `take`'s call on the landed-or-incomplete line (124), and `nearest == nullptr` under a kept aircraft in the challenge test (138). With line 93's unwind leg, eight are untaken |
| `adsb_source.cpp` unchanged in count | 54 of 58, as before: met |
| Overall 1,518 of 1,608 (94.4%) | 1,519 of 1,610 (94.3%): the same pair |
| The suites: Google Test 580, pytest 180, the page 89 | 580, 180 and 89: met |

**The unwind pair, read from the record.** The statement on line 93 did not change: Entry 23's record held it at two branches, both taken. Its callee changed. GCC's C++ front end marks a function as one that cannot throw when nothing in its body calls a function not already known not to throw, and a call to such a function gets no exception edge. The old `candidate` called `usable`, defined above it, and the optional's `noexcept` members. The new one also calls `under_ceiling`, which commit 3 defined below it. So when `candidate`'s body is read, `under_ceiling` is not yet known not to throw, `candidate` is left as one that may, and its call in `update` gets the pair. Every other line agrees with that reading:
- `update`'s calls to `usable` (line 123) and to `under_ceiling` (line 126), both defined above `update` and calling nothing that throws, carry no pair.
- `take` (line 124), which copies strings, carries one, as on Entry 23's record.
- The Mac preview could not show it: clang decides what may throw by its own rules, and its line 93 carried the decision's two branches only.

The reading fits every line of the record; a run with the fix confirms it or not.

**The fix, not made here.** Three ways, each one line or two:
- define `under_ceiling` above `candidate`, with a comment that says why the order matters, which removes the cause;
- declare `under_ceiling` `noexcept`, which removes it too, with a keyword `src/` does not use yet;
- put the call into a local on a line of its own, CLAUDE.md's rule for a call that can throw, which leaves the edge and removes it from the measure.

Predicted with either of the first two: line 93 back to two branches, `aircraft_selector.cpp` 59 of 66, overall 1,518 of 1,608 (94.4%). The decision is Derek's (§24.8).

Two expectations missed in this entry, both in counts and neither in coverage: every decision the topic added is taken both ways. The lesson is Entry 23's again, from the other side: the preview cannot see what GCC decides about exceptions, and §24.5 named the untaken legs from Entry 18 §18.7 instead of from the latest record.

### 24.8 Open

- **The live run (TC-029 step 19), optional and Derek's:** every `NEW_VEHICLE` frame's `gps_altitude_m` at or below 1,650 m (a kept aircraft is dropped at the poll after it crosses, so a frame may carry one poll's climb above it); no `nearer` row to an aircraft at an arrival altitude and an `above` row only for a climb-out; the exit line's census against §18.4's 46 aircraft and 5 candidates; the vertical speed on final with no excursion like §18.5's; the hand check that every aircraft the strip names is an arrival; and the schema consequence in practice, Entry 18's first live file replayed without `--config` (refused by name) and with the shipped file (byte-identical to §18.4's `2971db60…`). Recorded as §24.9 when run.
- **A watch item:** no band at the ceiling (§24.4); a live run's selection record would show a flap.
- **Phase B keeps** the vertical rate as the filter's third measurement, the pressure altitude, and FAULT-024's finiteness check for the two ADS-B fields (Entry 22 §22.8).
- **Line 93's unwind pair (§24.7):** the fix is `under_ceiling` defined above `candidate` (or declared `noexcept`, or the call put into a local), with its own record; Derek's to decide, in this pull request or after the merge. (Decided 2026-10-19: the order, under a comment that guards it, in commit 6; confirmed on the record, Entry 25 §25.4.)
- `aircraft_selector.cpp`'s other untaken legs on the record (§24.7, correcting §24.5 and the plan, which named them from Entry 18 §18.7): the five absences in `usable` would take one table-driven test, and a kept aircraft with no candidate at all one case; `take`'s unwind leg on line 124 no input reaches. Outside this topic.
- Entry 23's open items stand (the campaign of record after the bench session, TC-037 step 9, TC-009 step 8).

## Entry 25 — 2026-10-19, The selector's candidate test off its exception edge: `under_ceiling` defined above `candidate` (macOS preview)

Entry 24 §24.7 found an exception-unwind pair on `update`'s `if (!candidate(state))` in `src/drivers/aircraft_selector.cpp` that Entry 23's record did not have. The statement had not changed; its callee had. Since commit 3, `candidate` calls `under_ceiling`, which was defined below it, so GCC no longer knew that `candidate` cannot throw. Of the three fixes §24.7 named, Derek chose the order, with a one-line comment that guards it. This entry records the change, with the prediction for the record written before the push.

What changed:
- **Code:** `under_ceiling`'s definition moved above `candidate`'s, under a one-line comment. No statement changed.
- **Records:** this entry; Entry 24 §24.8's item for the pair points here.
- **Unchanged:** every behaviour; no requirement, format, key or test; the default runs' bytes (§25.2).

The commit is 6 on `claude/adsb-altitude-term-d9561c`, after commit 5 (`045af0a`, Entry 24 §24.7), on draft pull request 5. §25.4 records the CI run.

### 25.1 What was built

- **`src/drivers/aircraft_selector.cpp` (commit 6; REQ-SENS-009, REQ-TEST-003):** `under_ceiling` now follows `usable` and precedes `candidate`, the order in which `candidate` calls them, under the comment "Must stay above candidate() so GCC sees candidate() cannot throw; a reorder silently restores update()'s unwind edge."
- **Why the order works.** GCC's C++ front end marks a function as one that cannot throw when nothing in its body calls a function not already known not to throw. With `under_ceiling` read first, everything `candidate` calls is known not to throw by the time `candidate` is read: `usable`, `under_ceiling` and the optional's `noexcept` members. So `candidate` is marked too, and `update`'s call to it, now on line 94, needs no exception edge.
- **Why the comment.** Nothing else guards the order. A later reorder would build, pass every test, and show only in the next CI record, as the pair did in Entry 24 §24.7.

### 25.2 Test results (preview)

| Check | Result |
|---|---|
| Google Test | 580 of 580, `-DWARNINGS_AS_ERRORS=ON` on clang, zero warnings |
| pytest | 180 of 180 in 132.7 s |
| cppcheck 2.21.0 | 0 findings in 45 files with CI's flags |
| The byte checks | the five hashes of Entry 22 §22.4, each equal on commit 6's build: the seed-42 session `f1dc959a…`, its replay `189eb162…`, the demo `6f877d1d…`, the demo with `--lane-fault 2@60` `a2d1d891…`, its replay `33b34f60…` |

The coverage preview is not run. clang decides what may throw by its own rules, and its line 93 carried only the decision's two branches before this change (Entry 24 §24.7), so a preview cannot show the edge this commit removes. The record is the run.

### 25.3 The expectations for the record

Written before the push, from run 39342447232 (Entry 24 §24.7):
- **The pair is gone.** Line 94, line 93 before the move, carries only the decision's two branches, both taken, and `aircraft_selector.cpp` reads 59 of 66.
- **Nothing else in the file moves.** `candidate`'s chain stays at eight branches, now over lines 54 and 55; the `above` test stays at two, now on line 127. The seven untaken legs §24.7 names stay untaken, now on lines 43, 44, 125 and 139.
- **The totals:** branches 1,518 of 1,608 (94.4%); drivers 753 of 833 (90.4%, from 90.3%); every other scope unchanged. Lines 3,202 of 3,253 and functions 355 of 365 are unchanged: the comment is no code, and the move adds no line.
- **The suites:** Google Test 580, pytest 180, the page 89.
- **If line 94 still carries the pair,** §24.7's reading is wrong, and §25.4 says so.

### 25.4 Measurement of record

CI run 39346811798 on `14a30fa`, the branch's head, on draft pull request 5, 2026-10-20 00:18:02 to 00:22:40 UTC. It ran on the push that carried commit 6 alone (`045af0a..14a30fa`). Both jobs passed. The run on `045af0a`, commit 5 alone (39345414050), passed too.

| Check | Result |
|---|---|
| Google Test (GCC 13.3.0, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 580 of 580 in 22.6 s; no warning in the build |
| pytest (Python 3.13.16, against the instrumented binaries) | 180 of 180 in 138.7 s |
| lcov over `src/` | lines 98.4% (3,202 of 3,253), functions 97.3% (355 of 365), branches 94.4% (1,518 of 1,608) |
| cppcheck (the runner's package, 2.13.0), 43 files | 0 findings |
| `page` job | 89 of 89 in 13 files, the TypeScript build clean |
| Wall time | the C++ job 4 min 34 s; the page job 20 s |

Per-scope branch coverage on GCC: drivers 90.4% (753 of 833), where run 39342447232 on `8fbdf03` read 90.3% (754 of 835), and overall 94.4%, where it read 94.3%. Processing, config, timing and logging stay at 100.0%, transport at 88.8% and replay at 97.8%. `drivers/aircraft_selector.cpp` reads 87 of 87 lines, 59 of 66 branches and 15 of 15 functions, against 87 of 87, 60 of 68 and 15 of 15 on `8fbdf03`. The other 61 files of the report equal that record.

The five expectations §25.3 set before the push:

| Expectation | Measured |
|---|---|
| The pair gone: line 94 two branches, both taken; `aircraft_selector.cpp` 59 of 66 | line 94 carries two branches, taken 243 and 180 times, the counts of the decision's two branches on the previous record, and no unwind pair; 59 of 66: met |
| Nothing else in the file moves: the chain at eight branches over lines 54 and 55, the `above` test at two on line 127, the seven untaken legs on lines 43, 44, 125 and 139 | all as stated: met |
| The totals: 1,518 of 1,608 (94.4%); drivers 753 of 833 (90.4%); every other scope unchanged; lines and functions unchanged | all as stated: met |
| The suites: Google Test 580, pytest 180, the page 89 | 580, 180 and 89: met |
| If line 94 still carries the pair, §24.7's reading is wrong | it does not, so the reading stands |

Every expectation was met. This was the experiment Entry 24 §24.7 proposed: the order of two definitions, with no statement changed, decided whether GCC gave a third function's call an exception edge.

### 25.5 Decisions taken

- **The order, not `noexcept` or a local (Derek's choice, 2026-10-19).** The order removes the cause and adds no keyword `src/` does not already use. `noexcept` would remove the cause too, and state the fact in the interface. A local would leave the edge in place and only take it out of the measure.
- **The comment** is Derek's request: it says that the order matters, and that a reorder brings the edge back silently.
- **CLAUDE.md's rule is unchanged.** It names by kind the calls to keep off a decision's line: a `std::string` temporary, a standard-library call, an iterator range. A call to one of the project's own functions counts too when that function calls one defined after it. Whether the rule should say so is Derek's call.

### 25.6 Open

- Entry 24's open items stand: the live run (recorded as §24.9 when run), the watch item at the ceiling, Phase B, the other untaken legs and Entry 23's items.

## Entry 26 — 2026-10-19, The stack merged into `main`: five merge commits, the tree check, the run on `main`; CI pinned to Ubuntu 24.04

Pull requests 1 to 5, the stack built on branches from 2026-10-14 to 2026-10-19 (Entries 16 to 25), were merged into `main` on 2026-10-19, in order, each by a merge commit. `main` had stayed at `363b02b` since the stack was cut from it. This entry records the merge, the check that `main`'s tree is the last head's, and the runs on `main`. It also records three things decided with the merge: the dropped pre-stack tag, two corrections to the record, and a pinned runner image.

What changed:
- **`main`:** the 77 commits of the five topics, under five merge commits (§26.1). Nothing was rewritten or squashed.
- **CI (commit 2):** both jobs run on `ubuntu-24.04` instead of `ubuntu-latest` (§26.6).
- **Records:**
  - this entry;
  - Entry 17 §17.7 (commit 1), reserved since 2026-10-15;
  - README's measurement of record, and the matrix's REQ-TEST-002 and REQ-TEST-003 rows, now on the run on `main`;
  - five CI rows corrected from 45 to 43 files (§26.5);
  - README's and CLAUDE.md's wording that assumed the dropped tag, three stale facts in README and one in FTS-DS-001 (§26.7).
- **Unchanged:** no source file, test, requirement, format or key.

The commits are 1 to 3 on `claude/stack-merge-main-9a0427`, cut from `main` at `80cd4ca`, pull request 6:
1. Entry 17 §17.7.
2. The pinned runner.
3. This entry and the documents.

§26.8 records the pull request's run, the first on the pinned image.

### 26.1 The merge

| Pull request | Topic | Head | Commits | Merge commit | Merged (UTC) |
|---|---|---|---|---|---|
| 1 | PFD parts 1 and 2 (Entries 16 and 17) | `dc989d6` | 18 | `9d2d3dd` | 2026-10-20 01:23:42 |
| 2 | ADS-B Phase A and format version 3 (Entries 18 and 19) | `a7461a3` | 20 | `2d7b994` | 01:25:30 |
| 3 | The redundancy voter (Entry 20) | `e23fc87` | 11 | `69e1901` | 01:26:27 |
| 4 | The fault-injection campaign, FAULT-024's check and the cleanup pass (Entries 21 to 23) | `94b7c42` | 21 | `64a181b` | 01:27:12 |
| 5 | The ADS-B selection's altitude ceiling (Entries 24 and 25) | `6ccfa08` | 7 | `80cd4ca` | 01:27:42 |

How the merge was done:
- **The pull requests were updated first.** Each title and description was rewritten to name its requirement IDs and to drop the hold for the pre-stack tag (§26.6).
  - The titles matter beyond the pull request page. The repository writes each merge commit as "Merge pull request #N from DerekZ-113/<branch>", with the pull request's title as its body.
  - So every merge commit names its requirement IDs, and none carries a trailer.
- **Each merge ran alone.** Each pull request was merged with `gh pr checks`, `gh pr ready` and `gh pr merge --merge --match-head-commit <head>`, one at a time, in order.
  - `--match-head-commit` refuses a head that has moved since it was verified.
  - Each merge began only after the previous one had merged, so no pull request was carried into `main` inside another's merge.
- **Every check passed.** Each pull request's three checks (`build-test-coverage`, `page`, GitGuardian) were green, and its mergeability read `CLEAN` before its merge.
- **The branches are kept.** All five remain on `origin` at their heads; deleting them is a later decision.

### 26.2 The tree check

Before the merge, checked read-only with `git` and the GitHub API:
- **`main` had not moved.** It was at `363b02b`.
  - Its last push, `b2e0a05..363b02b` at 2026-10-15 04:24:39 UTC, came before the stack's first commit (`7c43ed3`, 06:14:04 UTC).
  - The repository's activity log shows no later push or force-push to `main`.
- **The heads formed a chain.** Each head was an ancestor of the next, and `363b02b` an ancestor of the first (`git merge-base --is-ancestor`).
- **Nothing could force a rebase.** No branch protection or ruleset applied to `main`, so nothing could require a head to be brought up to date between merges.

So each merge was a three-way merge whose base, the previous head, had the same tree as `main` at that moment. Its result was therefore the merged head's tree, and `main` had to end at `6ccfa08`'s tree.

After the merge, each merge commit's first parent is the merge before it (`363b02b` for the first), its second parent is the pull request's head, and its tree equals the head's:

| Merge commit | Parents | Tree, equal to the head's |
|---|---|---|
| `9d2d3dd` | `363b02b`, `dc989d6` | `9dc825ed…` |
| `2d7b994` | `9d2d3dd`, `a7461a3` | `cd1325f3…` |
| `69e1901` | `2d7b994`, `e23fc87` | `4ba401b1…` |
| `64a181b` | `69e1901`, `94b7c42` | `19afc8ba…` |
| `80cd4ca` | `64a181b`, `6ccfa08` | `1c29c435abd67d7201d26af2ef05c29688fee266` |

`main` is `80cd4ca`. Its tree is the one Entry 25 §25.4 measured on its branch.

### 26.3 The expectations for the record

Written before the merge, from the five pull requests' runs on their heads, all green:

| Head | Run | Google Test / pytest | Branches | Read before this entry |
|---|---|---|---|---|
| `dc989d6` | 38802982922 | 323 / 115 | 843 of 884 | never: Entry 17 §17.7 had been reserved, and commit 1 fills it |
| `a7461a3` | 39179224047 | 412 / 142 | 1,320 of 1,420 | only in progress (documents only); now read, Entry 19 §19.7's figures exactly |
| `e23fc87` | 39200167816 | 456 / 145 | 1,410 of 1,510 | Entry 21 §21.7 |
| `94b7c42` | 39325798389 | 575 / 179 | 1,514 of 1,604 | never (documents only); now read, Entry 23 §23.7's figures exactly |
| `6ccfa08` | 39347614840 | 580 / 180 | 1,518 of 1,608 | never (documents only); now read, Entry 25 §25.4's figures exactly |

A pull request's run tests its head merged into `main`. With `main` an ancestor of every head, that merge is the head's own tree, so each run on a merge commit tests the tree its pull request's run tested. Expected of the run on `80cd4ca`:
- **Tests:** Google Test 580, pytest 180, the page 89.
- **Coverage:** lines 3,202 of 3,253, functions 355 of 365, branches 1,518 of 1,608 (94.4%).
- **By scope:** processing, config, timing and logging 100.0%, transport 88.8%, replay 97.8%, drivers 90.4%.
- **cppcheck:** 0 findings in 45 files, the count taken from §25.4.
- **The earlier runs:** each of the four runs on the earlier merge commits equal to its pull request's run.

### 26.4 Measurement of record

CI run 39353550801 on `80cd4ca`, the push of the last merge to `main`, 2026-10-20 01:27:44 to 01:32:34 UTC. Both jobs passed.

| Check | Result |
|---|---|
| Google Test (GCC 13.3.0, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 580 of 580; no warning in the build |
| pytest (Python 3.13.16, against the instrumented binaries) | 180 of 180 in 136.4 s |
| lcov over `src/` | lines 98.4% (3,202 of 3,253), functions 97.3% (355 of 365), branches 94.4% (1,518 of 1,608) |
| cppcheck (the runner's package, 2.13.0), 43 files | 0 findings |
| `page` job | 89 of 89 in 13 files, the TypeScript build clean |
| Wall time | the C++ job 4 min 46 s; the page job 24 s |

Per-scope branch coverage on GCC: processing, config, timing and logging 100.0%, transport 88.8%, replay 97.8%, drivers 90.4%, overall 94.4%, each equal to Entry 25 §25.4.

The runs on the five merge commits, each pushed to `main` by its merge, all on Ubuntu 24.04 with GCC 13.3.0:

| Merge commit | Run | Google Test / pytest / page | Branches |
|---|---|---|---|
| `9d2d3dd` | 39353187063 | 323 / 115 / 66 | 843 of 884 (95.4%) |
| `2d7b994` | 39353352304 | 412 / 142 / 73 | 1,320 of 1,420 (93.0%) |
| `69e1901` | 39353438222 | 456 / 145 / 84 | 1,410 of 1,510 (93.4%) |
| `64a181b` | 39353505644 | 575 / 179 / 89 | 1,514 of 1,604 (94.4%) |
| `80cd4ca` | 39353550801 | 580 / 180 / 89 | 1,518 of 1,608 (94.4%) |

Each equals the run on its pull request's head, its lines and functions included. The runner image changed between the second and third runs (20261012.334.1 to 20261018.339.1, both Ubuntu 24.04 with GCC 13.3.0), and no figure moved with it.

The expectations §26.3 set before the merge:

| Expectation | Measured |
|---|---|
| Google Test 580, pytest 180, the page 89 | 580, 180 and 89: met |
| Lines 3,202 of 3,253, functions 355 of 365, branches 1,518 of 1,608 | all as stated: met |
| The scopes of §25.4 | all as stated: met |
| cppcheck 0 findings in 45 files | 0 findings in 43 files: the count missed (§26.5) |
| The four earlier runs equal to their pull requests' runs | all four: met |

Every expectation was met except that one count, and the count was wrong in the record, not in the run.

### 26.5 The cppcheck count

- **What the runner checks.** cppcheck on the runner has checked 43 files on every run since Entry 21: one per `.cpp` under `src/`, of which there are 43 at `80cd4ca`. The runs behind Entries 21 §21.7 to 25 §25.4 (39292191411, 39298962945, 39319857862, 39342447232 and 39346811798) each print `43/43 files checked`.
- **What the record said.** The CI rows of those five entries read "45 files", the count the same entries' Mac previews give for cppcheck 2.21.0. Entries 19 §19.7 and 20 §20.7 carry the runner's own counts, 39 and 41.
- **What changes.**
  - Every one of those runs found zero, so no conclusion changes.
  - The five rows now read 43 (Derek's choice, 2026-10-19), as §18.7's 40 was corrected to 38 in Entry 19's commit 9.
  - §26.3's expectation, taken from §25.4, carried the same error, and §26.4 records the miss.

### 26.6 The release order, and the runner

**The release order:**
- **The hold.** The pull requests had been held for a `v0.1.0` tag on `main` before the stack merged. The titles of 1 and 2 said "hold until after v0.1.0", and the descriptions of 1, 3, 4 and 5 said the branch would merge, or stay off `main`, until after that tag.
- **What changed.** The release order flipped: the stack merged first, so there is no pre-stack tag, and the first tag's name is decided at the release.
- **What still names 0.1.0.** Four places, all left for the release to settle with the tag:
  - `CMakeLists.txt:2`'s project version;
  - the User-Agent the libcurl client sends the OpenSky Network (`src/drivers/curl_http_client.h:25`, asserted by `tests/unit/test_curl_http_client.cpp:153`);
  - `dashboard/package.json:4`;
  - the banner `src/main.cpp:685` prints.
- **What stays as written.** The entries that described the hold are snapshots of their commits: Entry 14, in an item since closed, and the opening paragraphs of Entries 16 to 21 and 24.
- **What this commit changes.** README and CLAUDE.md plan "the first release's demo video" where they said "the v0.2.0 demo video".
- **REQ-TEST-004** stays Partial: no tag has run cppcheck's gate yet.

**The runner:**
- **The notice.** Run 39353550801 carried GitHub's notice that the `ubuntu-latest` label moves to Ubuntu 26 from 2026-10-19.
- **Why it matters.** Every run on record so far ran on Ubuntu 24.04, and the record's GCC 13.3.0, lcov 2.0 and cppcheck 2.13.0 are that image's packages. So is CLAUDE.md's rule for exception-edge markers, written for lcov 2.0's handling of them.
- **The pin.** Commit 2 pins both jobs to `ubuntu-24.04` (Derek's choice, 2026-10-19), with the reason beside it in the workflow. A newer image becomes a decision of its own, with its expectations written first.

### 26.7 Corrections in this pull request

- **Entry 17 §17.7** was reserved since 2026-10-15 and is filled from run 38802982922 (commit 1).
- **The SRD's version.** README's document table and FTS-DS-001's Related line named SRD v0.17.0. Entry 24's first commit (`c6896d0`) made it v0.18.0 and updated the matrix's header, not these two. FTS-DS-001's own version is unchanged, since only its reference was corrected.
- **README's key count.** README's configuration paragraph said "the 48 tunables" and listed the `adsb` block without its altitude ceiling. Both have been out of date since Entry 24's second commit.
- **README's campaign figure.** README's status said "97,100 fault injections", but 10,000 of the campaign's 97,100 runs are the clean cell C00 (FTS-FI-001 §3). It now says 87,100 of them inject a fault.
- **README's measurement of record** moves from run 38674948292 on `c988cf9` to the run of §26.4, and the sentence about branches not yet on `main` goes.
  - The matrix's REQ-TEST-002 and REQ-TEST-003 rows name the same run.
  - REQ-TEST-002's per-file counts are taken from its `-rA` lines (`test_bridge.py` 16, `test_adsb.py` 16), and its Mac figure from Entry 25 §25.2.

### 26.8 This pull request's run

Written before the run. Expected:
- the image `ubuntu-24.04`;
- §26.4's figures exactly, since nothing under `src/` or `tests/` differs from `80cd4ca`.

CI run 39358634665 on `7f4e744`, this pull request's head, 2026-10-20 02:23:23 to 02:27:40 UTC. It is the first run on the pinned image, from the one push that carried commits 1 to 3 (`80cd4ca..7f4e744`). Both jobs passed, and the repository's GitGuardian check with them.

| Check | Result |
|---|---|
| Runner | `ubuntu-24.04` (Ubuntu 24.04.5, image 20261012.334.1) for both jobs. GitHub's notice that `ubuntu-latest` moves to Ubuntu 26, carried by §26.4's run, is gone |
| Google Test (GCC 13.3.0, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 580 of 580 in 19.6 s; no warning in the build |
| pytest (Python 3.13.16, against the instrumented binaries) | 180 of 180 in 135.6 s |
| lcov over `src/` | lines 98.4% (3,202 of 3,253), functions 97.3% (355 of 365), branches 94.4% (1,518 of 1,608) |
| cppcheck (the runner's package, 2.13.0), 43 files | 0 findings |
| `page` job | 89 of 89 in 13 files; statements 99.17%, branches 99.04%, functions 97.84%, lines 99.71%; the build clean in 155 ms |
| Wall time | the C++ job 4 min 14 s; the page job 28 s |

Per-scope branch coverage on GCC: processing, config, timing and logging 100.0%, transport 88.8%, replay 97.8%, drivers 90.4%, overall 94.4%.

| Expectation | Measured |
|---|---|
| The image `ubuntu-24.04` | `ubuntu-24.04` on both jobs: met |
| §26.4's figures exactly | the same tests, lines, functions, branches and scopes, and cppcheck's 43 files: met |

- **The image's version.** This run's image, 20261012.334.1, is the one the first two runs on `main` had, not the 20261018.339.1 of the last three. Both builds of Ubuntu 24.04 gave the same tests and the same coverage.
- **The measurement of record** stays §26.4's run on `main`. This run is a pull request's, and it confirms that the pin reproduces that record.
- **A second notice.** This run and §26.4's both carry it: `actions/checkout@v4`, `actions/setup-python@v5`, `actions/setup-node@v4` and `actions/upload-artifact@v4` target Node.js 20, which GitHub now runs on Node.js 24. Nothing failed, and §26.9 lists it.

### 26.9 Open

- **§26.8**, from this pull request's run. (Closed 2026-10-19: recorded from run 39358634665, both expectations met.)
- **The actions' runtime notice.** The four actions §26.8 names target Node.js 20 and run on Node.js 24. Nothing fails today, and moving them to versions built for Node.js 24 is a change of its own.
- **`simulated_data.cpp:118`.** GCC counts four edges on the line, one never taken (§17.7). No entry has classified that edge: it may be an exception-unwind edge under CLAUDE.md's rule, or another of GCC's edges.
- **The branches.** The five pull requests' branches stay on `origin` until Derek deletes them.
- **The topics' open items.** Each topic's open items stand (Entries 16 to 25). On the Pi that means TC-009 steps 7 and 8, TC-021 step 9 and TC-014 step 13. (Run 2026-10-20: Entry 8 §8.6, Entry 16 §16.9, Entry 20 §20.9.)
- **The first release:** its tag's name, and the four places that name 0.1.0 (§26.6).

## Entry 27 — 2026-10-20, Pre-release hardening: the sanitized build in CI, the actions on Node.js 24, a replay of any log under `--config`, two coverage items, the ISO 26262 crosswalk, the README restructured

Six items before the first release, on one branch from `main` at `7d01520` (Entry 26's merge), one draft pull request, this entry. Entry 26 §26.9 left two of them open (the actions' runtime notice and `simulated_data.cpp:118`), Entry 24 §24.8 one (the selector's other untaken legs), and Entry 26's review flagged the replay's naming refusal (`src/main.cpp:705` to `709`) for running ahead of `--config` while its message asked for it.

What changes:
- **A requirement:** REQ-TEST-007, the sanitized build (SRD 0.19.0), with TC-039; TC-016 steps 22 and 23 and TC-028 step 16 for the two code changes that need a test of their own.
- **Code:** `src/main.cpp` (the replay's `--config`), `src/drivers/aircraft_selector.cpp` and `src/drivers/simulated_data.cpp` (one call each off a decision line), `CMakeLists.txt` (`ENABLE_SANITIZERS`).
- **Tests:** `test_aircraft_selector.cpp` (one table-driven test), `test_config.py` (two tests), `test_udp_publisher.cpp` (one test skipping itself under the sanitized build, §27.8).
- **CI:** a third job, `sanitize`; the four actions on their first Node.js 24 majors; `ubuntu-24.04` kept on every job.
- **Documents:** FTS-DD-001 §2 (the crosswalk), §9 (one sentence) and, with commit 10, §14 (the campaign's measured duration); README to under 1,200 words, its command reference moved to `docs/command_reference.md`; one rule in CLAUDE.md and, with commits 10 and 11, its Project Structure, its pytest count and its CI paragraph; with commit 11, the command reference's pytest line.
- **Unchanged:** the frame, the log format, every key, the five default hashes (§27.6).

The commits are 1 to 11 on `claude/inspiring-galileo-632i0k`:
1. The documents and this entry's expectations, before the code (§27.1 to §27.3).
2. The replay's `--config` (item 3 of the topic).
3. The selector's table-driven test and `take`'s call off its decision line (item 4).
4. The simulator's scripted-readings call off its decision line (item 4).
5. The `sanitize` job (item 1).
6. The actions (item 2).
7. The crosswalk (item 5).
8. The README, the command reference, the CLAUDE.md rule (item 6).
9. §27.4 to §27.8: what was built, the preview, the byte checks, the README's mapping, the decisions; then the push and the pull request.
10. §27.9 from the pull request's run, 39370957221; README's numbers and the matrix's run references on it; FTS-DD-001 §14's duration; CLAUDE.md's two lines.
11. CLAUDE.md's CI paragraph with the `sanitize` job, the one skip named; the command reference's pytest line at 182; this list and §27.10. Documents only, after the run on commit 10's tree (39373751291) passed.

The preview this time is not a Mac's. The commits were built and measured in an Ubuntu 24.04 container with the runner's own packages (GCC 13.3.0, lcov 2.0, cppcheck 2.13.0, Python 3.13.16), so the local figures are expected to equal the record, with one known exception (§27.2).

### 27.1 The expectations for the record

Written before any code was changed, from Entry 25 §25.4 and Entry 26 §26.4 (the record on `main`'s tree) and from the baseline measurement of §27.2:

- **The files changed under lcov's scope:**
  - `drivers/aircraft_selector.cpp`: **64 of 64 branches** (59 of 66 on the record), 88 of 88 lines (87 of 87), 15 of 15 functions. The table-driven test takes the five absences in `usable` (lines 43 and 44) and `nearest == nullptr` under a kept aircraft (line 139); the unwind pair of `take`'s call on the landed-or-incomplete line (125) leaves the measure when the reason goes into a local and the call onto a line with no decision (CLAUDE.md's rule), −2 branches and −1 taken, so 59 − 1 + 6 = 64; the local is the one new line.
  - `drivers/simulated_data.cpp`: **13 of 14 branches** (14 of 16 on the record), 84 of 84 lines (82 of 82), 3 of 3 functions. Line 118's unwind pair leaves the measure the same way, −2 and −1; line 61's non-advancing timestamp stays the one untaken leg, as on every record since Entry 17 §17.7; the `if` and its body are the two new lines.
  - `src/main.cpp` is changed and outside the measure (§1.3).
- **The totals:** branches **1,522 of 1,604 (94.9%)**, from 1,518 of 1,608; lines 3,205 of 3,256, from 3,202 of 3,253; functions 355 of 365, unchanged.
- **By scope:** drivers **757 of 829 (91.3%)**, from 753 of 833 (90.4%); processing, config, timing and logging 100.0%, transport 88.8% (71 of 80), replay 97.8% (45 of 46), unchanged.
- **The suites:** Google Test 581 (580 and the table-driven test), pytest 182 (180 and TC-016's steps 22 and 23), the page 89.
- **cppcheck:** 0 findings in 43 files.
- **The `sanitize` job:** green, all 581 tests passing under both sanitizers, no `AddressSanitizer`, `LeakSanitizer` or `runtime error:` line in its log.
- **The `page` job:** 89 of 89 and the build clean, on `checkout@v5` and `setup-node@v5`.
- **The byte checks:** the five hashes of Entry 22 §22.4 unchanged on the Mac (the seed-42 session `f1dc959a…`, its replay `189eb162…`, the demo `6f877d1d…`, the demo with `--lane-fault 2@60` `a2d1d891…`, its replay `33b34f60…`), Derek's check, since CI computes none and a Linux build's bytes are its own (FTS-DD-001 §8); and on this container the five hashes at the head equal to the five measured at `7d01520` before any edit (§27.6).
- **If line 125 or line 118 still carries a pair,** §27.2's reading is wrong and §27.9 says so; if the table-driven test leaves a leg on lines 43, 44 or 139, the test missed it.

### 27.2 The baseline, the classification of `simulated_data.cpp:118`, and the selector's seven legs

Measured on this container at `7d01520` before any edit, with the workflow's exact commands (the coverage configure, `ctest`, `pytest`, the two `lcov` calls, `coverage_check.sh`):

| Check | This container | The record (Entry 26 §26.4) |
|---|---|---|
| Google Test | 580 of 580 in 31.9 s, one of them skipped: `ConfigTest.UnreadableFileIsReported` skips itself under root (`geteuid() == 0`), and this container runs as root | 580 of 580 |
| pytest | 180 of 180 in 139.9 s | 180 of 180 in 136.4 s |
| lcov over `src/` | lines 3,198 of 3,253, functions 355 of 365, branches 1,517 of 1,608 (94.3%) | 3,202 of 3,253; 355 of 365; 1,518 of 1,608 (94.4%) |
| By scope | config 241 of 242 (99.6%); processing 300 of 300, timing 26 of 26, logging 54 of 54, transport 71 of 80, replay 45 of 46, drivers 753 of 833 | config 100.0%; the rest the same figures |

The one difference is the skipped test: the unreadable-file branch of `telemetry_config.cpp` and that branch's four lines are the record's missing 1 branch and 4 lines, so the container reproduces the record once that test runs as an ordinary user, which the head's measurement does (§27.5). Both files under study equal the record exactly: `aircraft_selector.cpp` 87 of 87 lines, 59 of 66 branches, 15 of 15 functions (Entry 25 §25.4); `simulated_data.cpp` 82 of 82, 14 of 16, 3 of 3.

**Line 118.** lcov's raw record before the branch filter, which tags an exception branch `e`:

| Edge | Count | What it is |
|---|---|---|
| `BRDA:118,0,0` | 15,502 | the `?:`'s first outcome, `profile_ != nullptr`: the scripted frames |
| `BRDA:118,0,1` | 560,162 | its second, the stationary frames |
| `BRDA:118,0,2` | 15,502 | `scripted_readings(timestamp_ms)` returning, once per scripted frame |
| `BRDA:118,e0,3` | 0 | the same call unwinding: never |

So the never-taken edge Entry 17 §17.7 counted and Entry 26 §26.9 left open is the exception-unwind edge of `scripted_readings`, kept by `--filter branch` because the `?:` makes the line a decision: CLAUDE.md's case exactly, and the remedy is the rule's, the call on a complete line of its own inside an `if` whose decision keeps its two branches. Line 61's four edges read 15,496, 6, 15,496 and 0, two decisions of the `&&` with `timestamp_ms > last_timestamp_ms_` false the untaken one, the leg §17.3 named, and no exception edge.

**The selector's seven.** The untaken edges of `aircraft_selector.cpp` after the filter, by line and GCC's branch index: 43:1 and 43:3 (latitude and longitude absent, `has_value()` false 0 times against 565), 44:3, 44:5 and 44:7 (ground speed, track and position time absent, 0 against 458), 125:`e0`:3 (`take`'s call unwinding, 0 against 4 returns, beside the `?:`'s 3 landed and 1 incomplete) and 139:1 (`nearest != nullptr` false under a kept aircraft, 0 against 32). Entry 24 §24.7's list, confirmed edge by edge; the geometric altitude's leg (44:1) is taken 112 times by TEST006 and the ground's (43:5) 107 times by TEST005.

### 27.3 The actions

Entry 26 §26.8's notice: `checkout@v4`, `setup-python@v5`, `setup-node@v4` and `upload-artifact@v4` target Node.js 20. Each moves to the first major that runs on Node.js 24, read from the action's own release notes on 2026-10-20 (the release pages print day and month without a year; the years are the release feeds' timestamps where the feed still lists the release, and otherwise follow from the sequence):

| Action | From | To | The release note | Released | Later majors, not taken |
|---|---|---|---|---|---|
| `actions/checkout` | v4 | **v5** | v5.0.0: "Update actions checkout to use node 24"; minimum compatible runner v2.327.1 | 11 Aug 2025 (the year by sequence: `setup-python` v6.0.0 and `setup-node` v5.0.0, both 2025-09-04, moved their own checkout from v4 to v5) | v6.0.0 (20 Nov 2025), v7.0.1 (2026-07-20) |
| `actions/setup-python` | v5 | **v6** | v6.0.0, under breaking changes: "Upgrade to node 24"; runner v2.327.1 or later | 2025-09-04 | v7.0.0 (2026-07-20) |
| `actions/setup-node` | v4 | **v5** | v5.0.0, under breaking changes: "Upgrade action to use node24"; runner v2.327.1 or later. Its second breaking change, automatic caching when `package.json` carries a `packageManager` field, does not apply: `dashboard/package.json` has none and the step keeps `cache: npm` | 04 Sep 2025 (the year by sequence: v6.0.0 is 2025-10-14) | v6.0.0 (2025-10-14), v7.1.0 (2026-10-08) |
| `actions/upload-artifact` | v4 | **v6** | v6.0.0: "actions/upload-artifact@v6 now runs on Node.js 24 (`runs.using: node24`)"; minimum runner 2.327.1. v5.0.0 (2025-10-24) "had preliminary support for Node.js 24, however this action was by default still running on Node.js 20", in v6's own words, so v5 is not the first | 2025-12-12 | v7.0.2 (2026-10-07) |

GitHub-hosted runners are past v2.327.1. The pin to `ubuntu-24.04` stays (§26.6).

### 27.4 What was built

- **The replay's `--config` (commit 2; REQ-CFG-003).** `src/main.cpp` derives the record's path for every replay but refuses a log not named `<prefix>_NNN.bin` only when no `--config` was given; with one, the named file is the configuration and the `Configuration:` line reads `(no configuration recorded beside <log>, which is not named <prefix>_NNN.bin; replay identity is not claimed)`, while a prefix-named log with no record beside it keeps `(no configuration recorded with <log>; replay identity is not claimed)`, a branch no test had run. The usage line for `--config` says "or this one when given". FTS-DD-001 §9's recorded-configuration paragraph gains the sentence. Tests: `test_config.py`'s `test_replay_of_a_log_not_named_by_prefix_needs_config` (the refusal, naming the log, the rule and `--config`, with no `logs/` created; then the replay under `--config`, its records byte-identical to the session's and its record equal to the shipped file) and `test_replay_with_config_and_no_record_says_so` (TC-016 steps 22 and 23).
- **The selector (commit 3; REQ-SENS-009, REQ-TEST-003).** `AircraftSelectorTest.EveryAbsentFieldReplacesTheKeptAircraftAndNoCandidateKeepsIt` (TC-028 step 16): one table of seven rows, the kept aircraft with latitude, longitude, geometric altitude, ground speed, track and position time absent in turn and once on the ground, each row through `update()` so that the absence is judged where candidates are gathered and where the kept aircraft is checked (`incomplete` six times, `landed` once, the nearest candidate taking over); then the kept aircraft alone and levelled off, kept with no candidate anywhere, the leg on line 139. In `aircraft_selector.cpp` the landed-or-incomplete reason goes into a local on its own line and `take` is called on a line with no decision, under a comment that says why.
- **The simulator (commit 4; REQ-SENS-007, REQ-TEST-003).** `simulated_data.cpp`: `BaseReadings base = kStationary;` and an `if (profile_ != nullptr)` whose body calls `scripted_readings` on a line of its own, the same arithmetic in the same order, so no byte of a default run moves (§27.6).
- **The `sanitize` job (commit 5; REQ-TEST-007).** `CMakeLists.txt`'s `ENABLE_SANITIZERS`: `-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g` on `telemetry_core`, `telemetry`, `unit_tests`, `fault_campaign_core`, `fault_campaign` and `fault_injection_tests`, the link flag public on the two libraries, the fetched libraries not instrumented, and `TELEMETRY_SANITIZED` defined for `unit_tests`. The job: `ubuntu-24.04`, the two system libraries, the configure with the option and `-DWARNINGS_AS_ERRORS=ON`, the build, `ctest --output-on-failure` under `UBSAN_OPTIONS=print_stacktrace=1`; no coverage, no pytest, no cppcheck. The first sanitized run gave one report, §27.8's finding, and `UdpPublisherTest.SocketFailureIsReportedNotThrown` now skips itself under the definition with its reason printed (TC-039's preconditions, TC-010 step 8, REQ-TEST-007's rationale).
- **The actions (commit 6).** §27.3's four versions, and the workflow's header says why.
- **The crosswalk (commit 7).** FTS-DD-001 §2's last paragraph, "The same level in ISO 26262's terms": the derivations, the practices that carry over in each standard's own terms, no table numbers and no per-ASIL ratings (the secondary sources disagree on them and the standard's text was not available), no mapping and no ASIL claimed.
- **The README (commit 8).** 1,195 words by `wc -w` (the diagram and the tables counted), from 5,533: the pitch (78 words), Status (75), one line per component to its design section, the numbers and the gate, the practices and the limits, five findings, the quick start, top-level folders, three roadmap lines, the OpenSky paragraph and citation byte for byte, the license. The command section moved verbatim to `docs/command_reference.md`, headed as a reference and not a controlled document, with the three CI configurations added. CLAUDE.md: one bullet under Scope Discipline, nothing else.

### 27.5 Test results (preview, this container)

Measured at commit 8's tree (`src/` and `tests/` as at commit 5), with the workflow's exact commands, `ctest` and `pytest` run as an ordinary user named `runner` so that `ConfigTest.UnreadableFileIsReported` runs as it does on the runner, the coverage counters zeroed first:

| Check | Result |
|---|---|
| Google Test (GCC 13.3.0, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 581 of 581 in 31.9 s, none skipped; no warning in the build |
| pytest (Python 3.13.16, against the instrumented binary) | 182 of 182 in 139.2 s, TC-016's steps 22 and 23 among them (`test_config.py` 14, every other file's count as on the record) |
| lcov 2.0 over `src/`, 62 files | lines 98.4% (3,205 of 3,256), functions 97.3% (355 of 365), branches 94.9% (1,522 of 1,604) |
| cppcheck 2.13.0 (the runner's package), 43 files | 0 findings under CI's gating flags (`--error-exitcode=1`) |
| The sanitized build (`-DENABLE_SANITIZERS=ON -DWARNINGS_AS_ERRORS=ON`, as root) | built clean; ctest 581 of 581 in 59.3 s, two skipped (the documented one and this container's root skip), no `AddressSanitizer`, `LeakSanitizer` or `runtime error:` line |
| TC-039 step 4 | a one-past-the-end read of a `std::vector`, compiled with the option's flags: `ERROR: AddressSanitizer: heap-buffer-overflow`, exit 1; `INT_MAX + 1`: `runtime error: signed integer overflow`, exit 1 |

Per-scope branch coverage, from `scripts/coverage_check.sh`: processing 300 of 300, config 242 of 242, timing 26 of 26, logging 54 of 54 (100.0%), transport 71 of 80 (88.8%), replay 45 of 46 (97.8%), drivers 757 of 829 (91.3%), overall 94.9%.

The two files under study:

| File | Lines | Branches | Functions | §27.1 predicted |
|---|---|---|---|---|
| `drivers/aircraft_selector.cpp` | 88 of 88 | 64 of 64 | 15 of 15 | 88 of 88, 64 of 64, 15 of 15 |
| `drivers/simulated_data.cpp` | 84 of 84 | 13 of 14 | 3 of 3 | 84 of 84, 13 of 14, 3 of 3 |

The only untaken edge in either file is line 61's, `timestamp_ms > last_timestamp_ms_` false. The selector's reason line (127 after the comment) carries the `?:`'s two branches, taken 4 and 7 times (the old tests' three landed and one incomplete, and the table's one and six), and no pair; the simulator's `if` (122) carries its two, 15,502 and 560,162, and no pair. Every figure §27.1 set is met on this container, down to the two scopes the changes move; the record is the run (§27.9).

### 27.6 The byte checks

No default run may change: the two code changes under the simulator and the selector keep every arithmetic step in its order, and the selector never runs in a default run. Entry 21 §21.4's five runs were recorded on this container at `7d01520`, before any edit, and again at the head (commit 4's tree, the last to touch `src/`), with the configuration of record, and hashed with `sha256sum`:

| Run | At `7d01520` | At the head |
|---|---|---|
| The seed-42 session, `--cycles 500` | `e5e6523bd48b6f1f10f98592c513ad8bc3bcd438c4f6e5fff4a8667cd9920ac4` | equal |
| Its replay | `500ac1a7fa5ffc6a674fdfd27e234c3b26b19d0ea998a903a8e7d18c6be0a285` | equal |
| The demo, `--motion demo --cycles 7500` | `8ec4bd4af2c4e2d00fe54129dfada5d08e0de9f9bfd0d89a0d6151542cceeca4` | equal |
| The demo with `--lane-fault 2@60` | `72162bd7b780c52f84bcd1df7fa2b0a76b5396de843f01175481fc8771c654ad` | equal |
| Its replay | `2b3ae4ee73eb4773d805c0438cf3847572c3163c110c756ce67ba11b4b67b290` | equal |

These are a Linux build's bytes (GCC 13.3.0, libstdc++), not the Mac's: FTS-DD-001 §8 claims no cross-platform identity, and `std::normal_distribution` differs between the two standard libraries, so Entry 22 §22.4's five Mac hashes (`f1dc959a…`, `189eb162…`, `6f877d1d…`, `a2d1d891…`, `33b34f60…`) are the expectation for Derek's check on the Mac (§27.1), which this entry cannot make. The exit lines on both builds read as Entry 21 §21.4 describes them: the lane fault's demo `lanes in the vote 1 3`, two voter events, the replay `re-applied: the replay's input records it`, every replay `end of log reached`.

### 27.7 The README's mapping

Nothing was cut whose home is not a document. Paragraph by paragraph of the README at `7d01520`:

| README paragraph (before) | After | Where the cut facts live |
|---|---|---|
| The pitch | rewritten, 78 words | the timing evidence: FTS-DD-001 §10 |
| **Status** (534 words) | 75 words | the deterministic core and the transports: FTS-DD-001 §8 and §9; the drivers and the GPS path awaiting the Pi: §3 and Entries 8 §8.6 and 13 §13.8; the Python reader, the replay-identity pytest and the noise analysis: §3 ("The Python side"), FTS-NP-001; pytest in CI: FTS-TP-001 §2.2; the configuration: §3; validity, NO_DATA and format version 3: §7 and §9; the replay's end and pace: §9 and §10; the session status, the bridge and the page: §9; the instruments: §9 and FTS-DS-001; the lanes and the voter: §13; the campaign's 97,100 and 87,100 runs: FTS-FI-001 §3 and Entry 26 §26.7; the ADS-B source and the live run of 2026-10-15: §3 and Entry 18; the data license: §12 |
| The diagram | shortened | FTS-DD-001 §4, §9, §10 |
| The seventeen Architecture bullets | fourteen one-line entries, each linking to its section | One seam, Drivers, Configuration, ADS-B source: §3; Detector before processor: §4 and §6; Processing: §5 and §6; Fault handling and Validity: §7 and FTS-FM-001; Redundancy and voting: §13; Fault-injection campaign: §14, its numbers (707.5 channel-hours, the latencies) FTS-FI-001 §5 to §7, the 300-run comparison Entry 22 §22.5; Logging and replay, Replay's end and pace, Session status, Bridge and page, Transport: §9 and §10; Instruments: §9 and FTS-DS-001; the overflight: Entry 18 §18.5 and §12 |
| Verification: the record, the table, the merge paragraph, the gate and the exclusions | the record and the table kept, the gate in one sentence | the five merges: Entry 26 §26.1; the exclusions by site: FTS-VR-001 §1.3; the Mac-preview rule: this document's introduction |
| DO-178C-aligned practices: the document table, the loop, the limits | four bullets and the limits sentence | each document's description: its own header and purpose; the limits in full: FTS-DD-001 §2 |
| Findings (three) | five one-liners | the write-ups: Entries 5 §5.2, 7 §7.1, 5 §5.5 to §5.8, 18 §18.5, 22 §22.5 |
| Build, test, run, replay: the prerequisites, five command blocks, the exit lines | the quick start | verbatim in `docs/command_reference.md` |
| Layout: every folder with its files | top-level folders | the files by module: FTS-TM-001 §11's reverse-trace tables; CLAUDE.md's Project Structure |
| Roadmap | three lines | FTS-TP-001 §7, FTS-DD-001 §12 |
| Data sources, License | unchanged, byte for byte | |

### 27.8 Decisions taken, and deviations from the plan

- **REQ-TEST-007 is new, not an amendment of REQ-TEST-004 (the plan, approved).** cppcheck reports on ordinary pushes and gates on tags; the sanitizers gate every push and watch the code run. Two policies, two statements.
- **The one sanitizer finding, and the skip (Derek's choice, 2026-10-20).** The first sanitized `ctest` failed one test of 581: `udp_publisher.cpp:15:21: runtime error: member access within address … which does not point to an object of type 'UdpPublisher'`, `object has invalid vptr`, `<memory cannot be printed>`, in `UdpPublisherTest.SocketFailureIsReportedNotThrown`. The test sets the `RLIMIT_NOFILE` soft limit to 0 around the construction so that `socket()` fails (TC-010 step 8); `UdpPublisher` is polymorphic, so the vptr check runs at the constructor's first member access, and GCC 13's libubsan validates a vtable by probing its memory through a `pipe()`, which the exhausted table refuses, so the failed probe is reported as an invalid vptr and the diagnostic cannot read the memory either. Proof: a scratch program constructing a plainly valid polymorphic object reports the same with the limit lowered first and runs clean without; with one object of the type constructed before the limit drops, libubsan's per-type cache answers and nothing is reported. No descriptor count separates the cases (`socket()` needs one, the probe two). Three ways were put to Derek: a skip under the sanitized build by name with its reason printed, the cache warmed by a construction before the limit drops (which leans on an implementation detail), or the job left red. The skip was chosen: `TELEMETRY_SANITIZED` is defined for `unit_tests` by the option, the test's `GTEST_SKIP()` names the cause, ctest reports the skip, and the branch stays measured on the coverage build. It is recorded in TC-039's preconditions, TC-010 step 8, REQ-TEST-007's rationale and the matrix's row, and a second such skip needs the same evidence. §27.1's expectation of 581 passing stands as written; ctest counts a skipped test as not failed.
- **Two wordings for "no record".** A prefix-named log with no record beside it keeps the old note; a log of another name gets one that says why there can be no record. One wording would have hidden the rule.
- **The flag reference's home.** `docs/command_reference.md`, a reference and not a controlled document, since the alternatives were a `--help` flag (code without a requirement) or the README's word budget. CLAUDE.md's Project Structure lists `docs/` by document and was not touched beyond the one rule, so it is one file short (§27.10).
- **The Linux hashes beside the Mac's.** CI computes no hash and the Mac's bytes are not reproducible here, so the check this entry can make is a Linux build against itself, before and after; the Mac check is Derek's.
- **The baseline corrected one prediction before the code.** The plan predicted `simulated_data.cpp` at 81 of 81 lines from Entry 17 §17.7's 79; the baseline measurement read 82 of 82, so §27.1 says 84 of 84. Written before commit 4, not after.
- **The tests as an ordinary user.** The container runs as root, under which `ConfigTest.UnreadableFileIsReported` skips itself; the head's measurement runs ctest and pytest as a user named `runner` so that the local figures can equal the record exactly.
- **A disagreement flagged, then resolved with commit 10.** The README at `7d01520` said the full campaign takes "about 2.5 minutes on one core"; FTS-DD-001 §14 said "about two minutes". The new README states no duration, and §14 now states the preview's own record, 143 s on one core on the platform FTS-FI-001 Section 3 names (Derek's choice, 2026-10-20).
- **The actions' years.** The release pages print day and month only; the years are the release feeds' timestamps where the feed still lists the release (`setup-python` v6.0.0, `upload-artifact` v6.0.0 and v5.0.0) and follow from the sequence for the two the feeds have aged out (`checkout` v5.0.0, `setup-node` v5.0.0), as §27.3 says.

### 27.9 Measurement of record

CI run 39370957221 on `93034c9`, the branch's head (commit 9), on draft pull request 7, 2026-10-20 04:42:06 to 04:46:17 UTC, from the one push that carried commits 1 to 9 (`7d01520..93034c9`): the branch's first run, and the first with three jobs. All three passed, and the repository's GitGuardian check with them.

| Check | Result |
|---|---|
| Runner | `ubuntu-24.04` on all three jobs (Ubuntu 24.04, image 20261012.334.1 on the C++ job, §26.8's); GitHub's Node.js 20 notice of §26.8 is gone |
| Google Test (GCC 13.3.0, `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`) | 581 of 581 in 21.6 s, none skipped; no warning in the build |
| pytest (Python 3.13.16, against the instrumented binaries) | 182 of 182 in 134.0 s, TC-016's steps 22 and 23 among them |
| lcov over `src/`, 62 files | lines 98.4% (3,205 of 3,256), functions 97.3% (355 of 365), branches 94.9% (1,522 of 1,604) |
| cppcheck (the runner's package, 2.13.0), 43 files | 0 findings |
| `sanitize` job (GCC 13.3.0, `-DWARNINGS_AS_ERRORS=ON -DENABLE_SANITIZERS=ON`) | built clean; ctest 581 of 581 in 48.8 s, one skipped by name, `UdpPublisherTest.SocketFailureIsReportedNotThrown` (§27.8); no `AddressSanitizer`, `LeakSanitizer` or `runtime error:` line |
| `page` job (`checkout@v5`, `setup-node@v5`, Node 22) | 89 of 89 in 13 files in 9.25 s; statements 99.17%, branches 99.04%, functions 97.84%, lines 99.71%; `tsc --noEmit && vite build` clean in 131 ms. The action's post step prints Node's `punycode` deprecation (DEP0040), the action's own under Node 24, not the page's |
| Wall time | the C++ job 4 min 8 s; `sanitize` 2 min 21 s; `page` 25 s |

Per-scope branch coverage on GCC, from `scripts/coverage_check.sh`: processing, config, timing and logging 100.0%, transport 88.8%, replay 97.8%, drivers 91.3%, overall 94.9%.

The two files, from the job's coverage step, which prints every file's filtered lines, functions and branches (this container's proxy refuses the `coverage-html` artifact's host, so the per-line counts below are this container's run of the same tests, §27.5):

| File | Lines | Branches | Functions | §27.1 predicted |
|---|---|---|---|---|
| `drivers/aircraft_selector.cpp` | 88 of 88 | 64 of 64 | 15 of 15 | 88 of 88, 64 of 64, 15 of 15: met |
| `drivers/simulated_data.cpp` | 84 of 84 | 13 of 14 | 3 of 3 | 84 of 84, 13 of 14, 3 of 3: met |

The expectations §27.1 set before the code:

| Expectation | Measured |
|---|---|
| `aircraft_selector.cpp` 64 of 64 and 88 lines; `simulated_data.cpp` 13 of 14 and 84 lines | as stated: met |
| Branches 1,522 of 1,604 (94.9%); lines 3,205 of 3,256; functions 355 of 365 | as stated: met |
| Drivers 757 of 829 (91.3%); the other scopes unchanged | 91.3%; processing, config, timing and logging 100.0%, transport 88.8%, replay 97.8%: met |
| Google Test 581, pytest 182, the page 89 | 581, 182 and 89: met |
| cppcheck 0 findings in 43 files | 0 in 43: met |
| The `sanitize` job green, 581 passing, no report | green, 581 of 581, no report; the one skip is §27.8's, documented after the expectation was written and reported by ctest as not failed: met |
| The `page` job on `checkout@v5` and `setup-node@v5` | 89 of 89, the build clean: met |
| The five Linux hashes at the head equal to the baseline's | equal, all five (§27.6): met |
| The five Mac hashes unchanged | Derek's check, not a CI measurement: open (§27.10) |
| If line 125 or line 118 still carried a pair, §27.2's reading was wrong | neither does: 64 of 64 and 13 of 14 are the counts without a pair, which would have moved each by two |

Every expectation the run can measure was met. The run tests the tree `main` will carry after the merge: `main` has not moved from `7d01520`, the pull request's base and §27.2's baseline.

**The leg left at `simulated_data.cpp:61`.** The line is `if (positioned_ && timestamp_ms > last_timestamp_ms_)`, the guard that integrates the scripted velocity over the interval since the previous frame. Its four edges, from the container's run of the same tests (§27.2's raw record, which the topic did not change): `positioned_` true 15,496 times and false 6 (the first scripted frame of each of the six generators the two suites build with a profile), and `timestamp_ms > last_timestamp_ms_` true 15,496 times and false 0. The untaken leg is a scripted frame whose timestamp is not later than its predecessor's, under `positioned_` true. No source produces one: `SimulatedSource` stamps n × period and never repeats or retreats, so the leg is the generator's own rule for a caller the sources are not. A direct test of `generate()` could take it, since `generate()` is public and a generator built with a `MotionProfile` can be asked for one timestamp twice: the second frame must leave the integrated position where the first left it (a fix at the same fix index is the held one, so the frame's latitude and longitude equal the first frame's bit for bit) while the per-frame noise is drawn as usual. Legitimate, since the rule is the generator's and not an artefact of GCC's counting; not done here, because §27.1's expectations are frozen; a candidate step for the next topic that touches the simulator (TC-026), with a prediction of 14 of 14.

### 27.10 Open

- **Derek's: the five Mac hashes of Entry 22 §22.4 on this branch's head** (§27.6). From the repository's root on the Mac, with the previews' configure, in a scratch directory so that `logs/` and its ADS-B recordings are never touched:
  ```bash
  git fetch origin claude/inspiring-galileo-632i0k && git checkout claude/inspiring-galileo-632i0k
  cmake -S . -B build -DWARNINGS_AS_ERRORS=ON && cmake --build build --parallel
  R=$PWD; C=$R/config/telemetry_config.yaml; H=$(mktemp -d /tmp/fts-hash.XXXXXX); mkdir -p $H/session $H/demo $H/demofault
  (cd $H/session && $R/build/telemetry --config $C --cycles 500 >/dev/null && $R/build/telemetry --config $C --replay logs/telemetry_000.bin >/dev/null)
  (cd $H/demo && $R/build/telemetry --config $C --motion demo --cycles 7500 >/dev/null)
  (cd $H/demofault && $R/build/telemetry --config $C --motion demo --cycles 7500 --lane-fault 2@60 >/dev/null && $R/build/telemetry --config $C --replay logs/telemetry_000.bin >/dev/null)
  shasum -a 256 $H/session/logs/telemetry_000.bin $H/session/logs/replay_000.bin $H/demo/logs/telemetry_000.bin $H/demofault/logs/telemetry_000.bin $H/demofault/logs/replay_000.bin
  ```
  Expected, in that order: `f1dc959a…`, `189eb162…`, `6f877d1d…`, `a2d1d891…`, `33b34f60…`; about six minutes, the two demos paced. A difference is first compared with the build recipe Entry 22 §22.4's hashes were taken with, and then a finding.
  (Closed 2026-10-19: all five equal to Entry 22 §22.4 on `c510034`, the merge into `main`, whose tree is this branch's head's; Apple clang 21.0.0 (clang-2100.3.34.2), a fresh unoptimized build.)
- **The leg at `simulated_data.cpp:61`:** the direct test of `generate()` above, with the next simulator topic.
- **A follow-up, if wanted:** pytest against the sanitized binary, which the `sanitize` job does not run.
- **CLAUDE.md:** nothing. Commit 11 describes the `sanitize` job in its CI paragraph (what it builds and runs, that it gates every push, the one skip by name), which commit 10 had left at two jobs. Its Build and Test block keeps "about 2.5 min" for the campaign by Derek's decision (2026-10-20): the preview's 143 s, which FTS-DD-001 §14 states, rounds to it.
- **Entry 26's open items stand:** the topics' Pi steps, the first release's tag and the four places that name 0.1.0, the five branches on `origin`. Closed by this entry: the actions' runtime notice, `simulated_data.cpp:118`, and Entry 24 §24.8's other untaken legs.
