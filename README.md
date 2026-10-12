# Flight Telemetry System

[![CI](https://github.com/DerekZ-113/flight_telemetry/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/DerekZ-113/flight_telemetry/actions/workflows/ci.yml)

Embedded flight telemetry for a Raspberry Pi, in C++17: barometer, IMU, and GPS readings fused into altitude and attitude at a fixed 50 Hz, with per-channel fault detection, a binary log that replays bit-for-bit through the same pipeline, and per-cycle timing evidence. Built with DO-178C-aligned practices: every line of code traces to a written requirement and to a test, and coverage, static analysis, and a formal fault model are part of every build.

**Status:** the deterministic core is complete and verified on simulated and replayed data, and processed frames leave the process over ZeroMQ to a Python receiver. Raspberry Pi hardware integration (sensor drivers, live source, noise characterization) is next.

## Architecture

```
DataSource ──raw + read_ok──▶ FaultDetector ──raw + status──▶ TelemetryProcessor ──raw + computed──▶ BinaryLogger ──▶ logs/*.bin
   │                                                                                                   │
   ├─ SimulatedSource        (built)          paced by FixedRateScheduler(Clock)                        └──▶ ZmqPublisher ──PUB──▶ pipeline/receiver.py
   ├─ LogReplaySource        (built)          jitter → logs/telemetry_jitter.csv                              (--zmq; the same bytes as the log)
   ├─ FaultInjectingSource   (built, wraps any source)
   └─ live sensor source     (next: BMP280 and MPU6050 over I2C, NEO-6M over UART)
```

- **One seam.** `DataSource` is an abstract base with one virtual `read_frame()`. A source fills raw sensor fields and `read_ok` flags only; computed fields and health belong to the pipeline. Live, simulated, and replayed frames run byte-for-byte the same code.
- **Detector before processor.** The Kalman and complementary filters carry state across frames and consume a reading only when the detector marked its channel NOMINAL and the driver obtained it this cycle, so a 1 Hz GPS fix is applied once, not fifty times. The detector judges timeouts on the frame's own timestamp, never a wall clock, so a replay reproduces the same faults at the same frames.
- **Processing.** ISA pressure-to-altitude; a 2-state Kalman filter (altitude, vertical velocity) fusing barometer and GPS; a complementary filter for pitch and roll with alpha treated as a time constant (about 1 s at 50 Hz).
- **Fault handling.** Communication timeout, stuck value, out-of-range, and recovery after five consecutive valid reads, per channel. A failed sensor marks its channel DEGRADED and logs an event; the rest of the pipeline keeps running.
- **Logging and replay.** Records are raw struct bytes behind a 16-byte self-describing header (magic, version, record sizes, endianness marker), with fault events as tagged records in the same stream and rotation at record boundaries. `LogReplaySource` strips a log back to raw readings and feeds the same pipeline; the output log is byte-identical to the original (same build, same platform).
- **Timing.** Absolute deadlines (`clock_nanosleep` with `TIMER_ABSTIME` on Linux), re-phase and count missed cycles on overrun, never burst to catch up. Jitter goes to a CSV per run so the telemetry log stays deterministic.
- **Transport.** With `--zmq <endpoint>` every processed frame also goes out on a ZeroMQ PUB socket as a one-record log message (the 16-byte header, the tag, the frame bytes), so the Python receiver parses the wire with the same layout module as the log. The publisher never blocks the loop: a PUB socket drops to a slow or absent subscriber at a 100-message high-water mark, failures are counted, and loss is measured at the receiver from timestamp gaps.

## Verification

Measurement of record: GitHub Actions run [34445017604](https://github.com/DerekZ-113/flight_telemetry/actions/runs/34445017604) on commit `c69d13d`, Ubuntu, GCC 13 ([verification_results.md](docs/verification_results.md), Entry 5 §5.8).

| Item | Value |
|---|---|
| Unit tests (Google Test) | 106 of 106 passed |
| Branch coverage (gcov/lcov) | 100.0% (142 of 142), every per-scope threshold met |
| Line coverage | 99.7% (575 of 577) |
| Compiler warnings under `-Werror` | 0 |
| cppcheck findings | 0 |

Branch coverage is the gating figure. `scripts/coverage_check.sh` fails the build below 90% in `src/processing/`, 80% in timing, logging, and replay, 70% in drivers, and 80% overall. Every exclusion is listed with its justification in the verification results: the composition root `main.cpp`, Google Test's own sources and the test files themselves, the no-match edge of exhaustive `switch` statements, one `EINTR` retry loop, and six GCC exception-unwind edges marked per line. Numbers from a developer Mac (clang) are recorded as previews only, because the Pi's compiler family is GCC.

## DO-178C-aligned practices

| Document | ID | What it holds |
|---|---|---|
| [Requirements](docs/requirements.md) | FTS-SRD-001 v0.2.0 | 32 "shall" statements with rationale; the scope of record |
| [Design](docs/design.md) | FTS-DD-001 | software level, every architecture decision with its reason, log format, timing, open decisions |
| [Traceability matrix](docs/traceability_matrix.md) | FTS-TM-001 | requirement → code → test, forward and reverse; status per requirement (12 Verified, 8 Partial, 12 Not started) |
| [Fault model](docs/fault_model.md) | FTS-FM-001 | 11 failure modes, each with cause, detection, response, and the test that proves it |
| [Test plan](docs/test_plan.md) | FTS-TP-001 | test levels, pass/fail criteria, test cards TC-001 to TC-009, coverage thresholds and exclusion policy |
| [Verification results](docs/verification_results.md) | FTS-VR-001 | per-commit test and coverage records, justified exclusions, findings, the CI run of record |

Every change follows the same loop: requirement, matrix row, code, test, matrix update, coverage and static analysis, one commit naming the requirement IDs.

**Limits, stated in the same breath.** Software level DAL C is assumed, not assigned by a certification authority, and nothing here is certified. Nothing here was reviewed or tested by a party independent of the development. No tool qualification under DO-330 was performed on the compiler, Google Test, gcov/lcov, or cppcheck. MC/DC coverage is out of scope; DAL C requires statement and decision coverage, and branch coverage is what the gate measures.

## Findings worth reading

Three findings, none of them caught by a unit test on a single machine, each written up in [verification_results.md](docs/verification_results.md):

1. **Unspecified padding in brace-built fault events (Entry 5 §5.2).** `FaultEvent` is 16 bytes with two padding bytes. The detector built events as braced temporaries; clang zero-fills them, GCC does not. Every local run passed. The first CI run on GCC failed the replay identity test because the live and replay detectors saw different stack contents in those two bytes. Fix: one construction site that value-initializes the struct, plus a test that reads the padding bytes through `offsetof` and asserts zero on any toolchain.
2. **Uninitialized padding in the simulator's frame (Entry 4 §4.3).** Same defect class one struct over, found by a `memcmp` test added when the requirement was reworded to say "raw". Two identical runs could have produced logs that differed in bytes carrying no data.
3. **Coverage tooling, not code (Entry 5 §5.5 to §5.8).** GCC keeps exception-unwind branches on conditional lines, which left `src/logging/` gated at 80.6% on noise. lcov's documented setting for removing them wiped all branch data on the runner's lcov 2.0 and turned CI red. The change was reverted, the runner reproduced in a container with the same toolchain, and the noise resolved with six per-line markers, each justified. Lesson recorded: test coverage tooling on the toolchain that produces the number of record.

## Build, test, run, replay

Requires CMake 3.16 or newer, a C++17 compiler (GCC or clang), and the ZeroMQ C library (`brew install zeromq` on macOS, `sudo apt-get install libzmq3-dev` on Debian and Ubuntu). Google Test (v1.15.2) and the cppzmq header wrapper (v4.11.0) are fetched at configure time, pinned.

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

```bash
./build/telemetry                                   # 5 paced cycles at 50 Hz on the simulated source
./build/telemetry --replay logs/telemetry_000.bin   # the same pipeline fed from the log, unpaced
cmp logs/telemetry_000.bin logs/replay_000.bin      # prints nothing: the two logs are byte-identical
```

```bash
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt       # once: pyzmq, pinned
.venv/bin/python -m pipeline.receiver --endpoint tcp://127.0.0.1:5555   # start first; one line per frame, gap count at exit
./build/telemetry --zmq tcp://127.0.0.1:5555 --cycles 500             # 10 s at 50 Hz, logged and published
```

The live run prints each frame, writes `logs/telemetry_000.bin` and `logs/telemetry_jitter.csv`, and prints a timing summary. The replay writes `logs/replay_000.bin`. A run with `--zmq` adds a transport summary (frames published, send failures); `--replay` and `--zmq` together publish the log as a burst. Coverage and static analysis run in CI; the exact lcov and cppcheck invocations are in [.github/workflows/ci.yml](.github/workflows/ci.yml).

## Layout

```
src/            telemetry_frame, data_source, main.cpp
  drivers/      simulated_data, simulated_source, fault_injecting_source
  processing/   altitude, kalman_filter, complementary_filter, fault_detector, processor
  logging/      log_format, binary_logger, log_reader
  replay/       log_replay_source
  timing/       clock, fixed_rate_scheduler, jitter_log
  transport/    zmq_publisher
pipeline/       frame_format (the byte layout, shared by log and wire), receiver (ZeroMQ SUB)
tests/unit/     one Google Test file per module
docs/           the six controlled documents above
scripts/        coverage_check.sh, the per-scope branch threshold gate
```

## Roadmap

Planned, no dates: sensor drivers (BMP280 and MPU6050 over I2C, NEO-6M over UART) behind the existing `DataSource` seam; noise characterization on stationary sensors, with the measured variances replacing the Kalman placeholders; a UDP publisher; YAML configuration; redundancy voting across sources; the Primary Flight Display; an ADS-B data source.

## License

MIT, see [LICENSE](LICENSE).
