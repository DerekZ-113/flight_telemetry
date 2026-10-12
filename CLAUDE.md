# CLAUDE.md — Flight Telemetry System

## What This Project Is

Embedded flight telemetry for a Raspberry Pi in C++17, built with DO-178C-aligned practices: every line of code traces to a written requirement and to a test, and structural coverage, static analysis, and a formal fault model are part of every build.

**Built:** the 104-byte `TelemetryFrame` record; the `DataSource` abstraction with a simulated source, a fault-injecting decorator, and a log-replay source; the processing pipeline (ISA pressure-to-altitude, 2-state Kalman altitude/velocity fusion, complementary-filter pitch and roll); the fault detector (communication timeout, stuck value, range, recovery) with per-channel DEGRADED state; a binary logger with rotation, a reader, and bit-identical deterministic replay; a fixed-rate scheduler on absolute deadlines with per-cycle jitter logging; a ZeroMQ publisher that sends every processed frame as a one-record log message on a PUB socket and never blocks the loop, with a Python receiver that shares the log's byte layout; Google Test unit tests; CI with a branch-coverage gate and cppcheck; six controlled documents under `docs/`.

**Planned:** I2C drivers (BMP280 barometer, MPU6050 IMU) and a UART GPS driver (NEO-6M) behind a live `DataSource`; noise characterization feeding the Kalman constants; a UDP publisher; YAML configuration; pytest integration tests; a Python/React Primary Flight Display. The scope of record is `docs/requirements.md`; the architecture of record is `docs/design.md`.

## The One Rule That Matters

**Never write code without a requirement.** Every feature starts with a requirement ID in `docs/requirements.md`, then code, then test, then traceability update. If a feature isn't in requirements.md, it either needs a requirement added first or it doesn't belong. This is the DO-178C discipline and it's non-negotiable for this project.

## DO-178C Workflow — The Loop

Every feature follows this exact sequence:

1. Add requirement to `docs/requirements.md` (REQ-XXXX-NNN format)
2. Add row to `docs/traceability_matrix.md` (empty implementation/test columns)
3. Write the code
4. Update traceability with implementation file/function
5. Write the test (Google Test for C++, pytest for integration)
6. Update traceability with test file/function
7. Run coverage (gcov/lcov), run static analysis (cppcheck)
8. Commit everything together, naming the requirement IDs in the message

## Build and Test

```bash
cmake -S . -B build                                 # configure; fetches Google Test (pinned v1.15.2)
cmake --build build --parallel                      # telemetry_core, telemetry, unit_tests
ctest --test-dir build --output-on-failure          # every TEST() is its own CTest case
./build/telemetry                                   # 5 paced cycles on the simulator -> logs/telemetry_000.bin
./build/telemetry --replay logs/telemetry_000.bin   # same pipeline fed from the log, unpaced -> logs/replay_000.bin
cmp logs/telemetry_000.bin logs/replay_000.bin      # silent: the two logs are byte-identical
.venv/bin/python -m pipeline.receiver --endpoint tcp://127.0.0.1:5555   # start first; one line per frame, gap count at exit
./build/telemetry --zmq tcp://127.0.0.1:5555 --cycles 500             # 10 s at 50 Hz, logged and published
```

Prerequisites beyond CMake and a C++17 compiler: the ZeroMQ C library (`brew install zeromq` / `apt-get install libzmq3-dev`; cppzmq is fetched and pinned by CMake) and, for the receiver, `python3 -m venv .venv && .venv/bin/pip install -r requirements.txt`.

CI (`.github/workflows/ci.yml`) configures with `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`, builds, runs ctest, captures lcov branch coverage, enforces the per-scope thresholds in `scripts/coverage_check.sh` (from `docs/test_plan.md` §6.3), and runs cppcheck (reports on every push, gates on `v*` tags). The coverage measurement of record is the CI run on GCC; developer-Mac (clang) numbers are previews. Results and justified exclusions are recorded per commit in `docs/verification_results.md`.

## Tech Stack

- **Built:** C++17 (CMake 3.16+, g++ or clang++), Google Test via FetchContent, ZeroMQ (system libzmq 4.3.5, cppzmq v4.11.0 via FetchContent), Python 3.12+ with pyzmq (pinned in `requirements.txt`), gcov/lcov, cppcheck, GitHub Actions
- **Planned:** Python (log parser, analysis scripts, pytest integration tests), UDP (network streaming), YAML (configuration), React (PFD dashboard)

## Project Structure

```
src/
  telemetry_frame.h/.cpp  the record: raw fields, computed fields, status, read_ok   built
  data_source.h           abstract DataSource, raw-only contract                      built
  drivers/                simulated_data, simulated_source, fault_injecting_source    built
                          I2C bus, BMP280, MPU6050, NEO-6M, live sensor source        planned
  processing/             altitude, kalman_filter, complementary_filter,
                          fault_detector, processor                                   built
  logging/                log_format, binary_logger, log_reader                       built
  replay/                 log_replay_source                                           built
  timing/                 clock, fixed_rate_scheduler, jitter_log                     built
  transport/              zmq_publisher (PUB socket, drop-not-block)                  built
                          UDP publisher                                               planned
  main.cpp                composition root; --replay <file>                           built

tests/
  unit/                   Google Test, one file per module                            built
  integration/            pytest, end-to-end pipeline                                 planned
  fault_injection/        parameterized fault scenarios                               planned

docs/                     requirements, design, traceability_matrix, fault_model,
                          test_plan, verification_results                             built
                          display_spec, noise_profile                                 planned
scripts/                  coverage_check.sh                                           built
pipeline/                 frame_format (byte layout shared by log and wire),
                          receiver (ZeroMQ SUB, reusable by the dashboard)            built
analysis/ dashboard/ config/                                                          planned
coverage/ static_analysis/   generated reports (committed selectively, see .gitignore)
```

## Coding Conventions

- C++17, `-Wall -Wextra -Wpedantic`; CI adds `-Werror` (`WARNINGS_AS_ERRORS=ON`)
- `const&` for passing structs/objects to functions (no unnecessary copies)
- RAII for resource management (I2C bus handles, file handles)
- Header (.h) and implementation (.cpp) split for all classes; every class gets its own pair
- snake_case for files and variables, PascalCase for classes and structs
- Comments explain WHY, not WHAT — the code should be readable on its own
- **Value-initialize every struct that reaches the logger, then assign its fields:** `TelemetryFrame frame{};`, `FaultEvent event{};`. Records are written as raw bytes, padding included; a default-initialized struct writes stack contents into the log and breaks replay identity. Found twice in Week 1 (`docs/verification_results.md` Entries 4 and 5); guarded by `SimulatedDataTest.SameSeedSameSequence` and `FaultDetectionTest.EventPaddingBytesAreZero`.
- **Mark GCC exception edges on conditional lines.** A line that combines a conditional with a `std::string` temporary, a standard-library call, or an iterator range shows untaken exception-unwind branches on GCC that `lcov --filter branch` cannot remove. Put `// LCOV_EXCL_EXCEPTION_BR_LINE` on that line with a one-line reason and list it in `docs/verification_results.md`. Never add `--rc no_exception_branch=1` to the workflow: on the runner's lcov 2.0 it discards all branch data (Entry 5 §5.5 and §5.6).
- Python code follows standard PEP 8

## Key Architecture Decisions

- **Data source abstraction:** abstract base class so the processing engine doesn't know if it's reading live sensors or replaying from a binary log. Sources fill raw fields and `read_ok` only; computed fields and health belong to the pipeline. This is what lets replay use the same code path as live mode (REQ-LOG-004).
- **Detector before processor:** the fault detector runs first and owns `ChannelStatus`; the filters consume only readings marked NOMINAL. The detector judges timeouts on the frame's own timestamp, never a wall clock, so replay reproduces the same faults at the same frames.
- **Fault detection → DEGRADED state:** failed sensors don't crash the system. The channel is marked DEGRADED, the fault is logged as a tagged record in the same stream, remaining sensors continue processing, and the channel returns to NOMINAL after M consecutive valid reads.
- **Log format:** raw struct bytes behind a 16-byte self-describing header (magic, version, record sizes, endianness marker); rotation at record boundaries; bit-identity is claimed for the same build on the same platform only (`docs/design.md` §8 and §9).
- **Fixed-rate timing with clock_nanosleep:** absolute-time scheduling (`TIMER_ABSTIME` on Linux; the `nanosleep` fallback for other platforms is the only `#if` in the codebase), re-phase and count missed cycles on overrun, never burst. Jitter is logged per cycle to a CSV, not into the telemetry log, so the log stays deterministic.
- **Kalman filter covariances from measured data:** a noise characterization run on stationary sensors will supply the variances. Until then the five constants in `processor.cpp` are named placeholders and REQ-PROC-004 stays Partial.
- **Dual transport:** ZeroMQ (built) for local IPC, UDP (planned) for network streaming, mirroring an aircraft-to-ground-station architecture. The wire format is the log format: every message is a one-record log file (header, tag, frame), so one Python module describes both. The publisher never blocks: PUB socket, 100-message high-water mark per subscriber, bounded linger, failures counted; loss is measured at the receiver from timestamp gaps (REQ-TRANS-003).

## Scope Discipline

This project is built incrementally, topic by topic. When editing files:

- **Only touch what you're asked to touch.** If asked to review `fault_model.md`, don't also rewrite `requirements.md`. Stay in scope.
- **Don't add features, requirements, or code that haven't been discussed.** `docs/requirements.md` defines the scope and `docs/design.md` the architecture. If something isn't in them, it needs a requirement first or it doesn't belong. Open questions live in `docs/design.md` §12; resolve them there before coding against them.
- **Don't restructure the project.** Folder structure and architecture are decided. Improvements to existing files are welcome. Reorganizing everything is not.
- **Review and refine, don't rewrite from scratch.** If a file is 90% good, fix the 10%. Don't delete and start over.
- **Flag issues, don't silently fix them.** If you spot a discrepancy between documents, say so and let the author decide. Don't quietly align them.
- **Cross-document consistency matters.** Requirement IDs, fault IDs, test card numbers, terminology, and technical details should match across all docs. When reviewing, check that references between documents are consistent.
- **Treat existing files as intentional.** They were discussed and decided on; a change needs a reason, not a preference.

## What NOT to Do

- Don't write code without a requirement in `docs/requirements.md`
- Don't skip the traceability matrix update when adding code or tests
- Don't hardcode sensor parameters — they go in `config/telemetry_config.yaml` (REQ-CFG-001); until that lands, a named constant with a comment citing the requirement
- Don't use raw `new`/`delete` — use RAII, std::vector, std::unique_ptr
- Don't skip Google Test for any new C++ module
- Don't mix build artifacts with source (out-of-source build in `build/`)
- Don't go beyond the scope of what's being worked on in the current session
- Don't rewrite files that aren't part of the current task
- Don't add requirements, features, or architecture changes without discussion
