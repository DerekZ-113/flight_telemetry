# Flight Telemetry System

[![CI](https://github.com/DerekZ-113/flight_telemetry/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/DerekZ-113/flight_telemetry/actions/workflows/ci.yml)

Embedded flight telemetry for a Raspberry Pi, in C++17: barometer, IMU, and GPS readings fused into altitude and attitude at a fixed 50 Hz, with per-channel fault detection, a binary log that replays bit-for-bit through the same pipeline, and per-cycle timing evidence. Built with DO-178C-aligned practices: every line of code traces to a written requirement and to a test, and coverage, static analysis, and a formal fault model are part of every build.

**Status:** the deterministic core is complete and verified on simulated and replayed data; processed frames leave the process over ZeroMQ and over UDP to a Python receiver that reads either; the BMP280 and MPU6050 drivers and the live sensor source are built and unit-tested against a fake bus, awaiting their first run on the Raspberry Pi; the Python log reader, the replay-identity pytest, and the noise analysis are built and verified against simulated logs, ready for the first stationary capture; the pytest suite runs in CI on every push; every tunable comes from one YAML configuration file, validated at startup and recorded beside each session's log; every computed field carries its validity in the frame, a channel is NO_DATA until its first reading, and the log format's version 2 names every byte while readers still replay version-1 logs. The measured constants, the GPS driver and the Primary Flight Display are next.

## Architecture

```
DataSource ──raw + read_ok──▶ FaultDetector ──raw + status──▶ TelemetryProcessor ──raw + computed──▶ BinaryLogger ──▶ logs/*.bin
   │                                                                                                   │
   ├─ SimulatedSource        (built)          paced by FixedRateScheduler(Clock)                        ├──▶ ZmqPublisher ──PUB socket──▶ pipeline/receiver.py --endpoint
   ├─ LogReplaySource        (built)          jitter → logs/telemetry_jitter.csv                        └──▶ UdpPublisher ──datagrams──▶ pipeline/receiver.py --udp
   ├─ FaultInjectingSource   (built, wraps any source)                                                      (--zmq, --udp, or both; one Publisher seam; the same bytes as the log)
   └─ LiveSensorSource       (built: BMP280 and MPU6050 over I2C; NEO-6M over UART next)
```

- **One seam.** `DataSource` is an abstract base with one virtual `read_frame()`. A source fills raw sensor fields and `read_ok` flags only; computed fields, health and validity belong to the pipeline. Live, simulated, and replayed frames run byte-for-byte the same code.
- **Drivers.** The barometer and IMU drivers talk to an `I2cBus` interface, never to the OS, so their register protocol and arithmetic are tested on a machine with no bus, against a fake that serves recorded bytes; the Linux implementation is one descriptor on `/dev/i2c-1`, addressed per transaction, held under RAII. The barometer compensation reproduces Bosch's worked example to the count, and the MPU6050 driver refuses a sample rate its divider cannot produce exactly. A failed read is an empty reading, never an exception: the channel's fields hold, `read_ok` clears, and the detector decides when that has lasted long enough to matter.
- **Detector before processor.** The Kalman and complementary filters carry state across frames and consume a reading only when the detector marked its channel NOMINAL and the driver obtained it this cycle, so a 1 Hz GPS fix is applied once, not fifty times; an IMU gap longer than the timeout re-seeds attitude from the accelerometer instead of integrating one rate sample across it. The detector judges timeouts on the frame's own timestamp, never a wall clock, so a replay reproduces the same faults at the same frames.
- **Processing.** ISA pressure-to-altitude; a 2-state Kalman filter (altitude, vertical velocity) fusing barometer and GPS; a complementary filter for pitch and roll with alpha treated as a time constant (about 1 s at 50 Hz).
- **Fault handling.** Communication timeout, stuck value, out-of-range, and recovery after five consecutive valid reads, per channel. A channel is NO_DATA until its first reading; a failed sensor marks its channel DEGRADED and logs an event; the rest of the pipeline keeps running.
- **Validity.** Every computed field (barometric altitude, pitch, roll, fused altitude, vertical speed) carries one validity byte in the same 104-byte frame, written last by the processor in order of precedence: NO_COMPUTED_DATA while no estimate has ever existed, FAILURE_WARNING while the channel it depends on is not trusted or no altitude source contributes, NORMAL otherwise. The number is always defined (0, or the held or coasting estimate), never NaN; the flag is what a display acts on, the way an ARINC 429 word carries its sign/status matrix beside its data. Zero means "not judged" and the pipeline never writes it, so a frame nobody judged cannot pass as valid.
- **Logging and replay.** Records are raw struct bytes behind a 16-byte self-describing header (magic, version, record sizes, endianness marker), with fault events as tagged records in the same stream and rotation at record boundaries. Format version 2 names every byte of both records, so a `static_assert` proves the layout has no padding; readers accept versions 1 and 2 and refuse anything else by name before a frame flows. `LogReplaySource` strips a log back to raw readings and feeds the same pipeline; the output log is byte-identical to the original (same build, same platform, same format version), and a version-1 log replays into a version-2 one.
- **Timing.** Absolute deadlines (`clock_nanosleep` with `TIMER_ABSTIME` on Linux), re-phase and count missed cycles on overrun, never burst to catch up. Jitter goes to a CSV per run so the telemetry log stays deterministic.
- **Configuration.** `config/telemetry_config.yaml` holds the 36 tunables (the period, the sensors' bus, addresses and IMU rate, the fault thresholds, the filter tuning, the log directory and size, the transport addresses, the simulator's seed and noise). It is loaded once at startup, every value checked and every problem named in one report, and handed to each module by constructor; nothing reads it after that, and nothing in the binary falls back to a built-in value. A copy is written beside the log, and a replay reads that copy, so a changed constant can never silently break replay identity.
- **Transport.** Two publishers behind one `Publisher` seam, so the loop knows neither: with `--zmq <endpoint>` every processed frame goes out on a ZeroMQ PUB socket, with `--udp <ipv4>:<port>` as one datagram to one unicast destination, both as the same one-record log message (the 16-byte header, the tag, the frame bytes), assembled by one function, so `pipeline/receiver.py` reads either transport with the layout module it uses for the log. Neither blocks the loop: the PUB socket drops to a slow or absent subscriber at a 100-message high-water mark; the datagram socket is connected at startup and sends with `MSG_DONTWAIT`, so a destination that cannot be routed is refused before the loop starts and every refusal the kernel reports later (a full buffer, the port-unreachable it learns from an earlier datagram) is counted and never retried. Loss and reordering are measured at the receiver from timestamp gaps.

## Verification

Measurement of record: GitHub Actions run [38490312035](https://github.com/DerekZ-113/flight_telemetry/actions/runs/38490312035) on commit `797f06b`, Ubuntu 24.04, GCC 13.3, the first run with the configuration scope gated ([verification_results.md](docs/verification_results.md), Entry 11 §11.7).

| Item | Value |
|---|---|
| Unit tests (Google Test) | 212 of 212 passed |
| Integration tests (pytest) | 78 of 78 passed in 64.8 s |
| Branch coverage (gcov/lcov) | 94.2% (488 of 518), every per-scope threshold met: config 100%, transport 96.2%, drivers 81.3% (hardware-bound error paths), the other scopes 100% |
| Line coverage | 98.6% (1313 of 1332) |
| Compiler warnings under `-Werror` | 0 |
| cppcheck findings | 0 |

Branch coverage is the gating figure. `scripts/coverage_check.sh` fails the build below 90% in `src/processing/` and `src/config/`, 80% in timing, logging, transport, and replay, 70% in drivers, and 80% overall. Every exclusion is listed with its justification in the verification results: the composition root `main.cpp`, Google Test's own sources and the test files themselves, the no-match edge of exhaustive `switch` statements, one `EINTR` retry loop, and seven GCC exception-unwind edges marked per line. Numbers from a developer Mac (clang) are recorded as previews only, because the Pi's compiler family is GCC.

## DO-178C-aligned practices

| Document | ID | What it holds |
|---|---|---|
| [Requirements](docs/requirements.md) | FTS-SRD-001 v0.7.0 | 37 "shall" statements with rationale; the scope of record |
| [Design](docs/design.md) | FTS-DD-001 | software level, every architecture decision with its reason, log format, timing, open decisions |
| [Traceability matrix](docs/traceability_matrix.md) | FTS-TM-001 | requirement → code → test, forward and reverse; status per requirement (28 Verified, 8 Partial, 1 Not started) |
| [Fault model](docs/fault_model.md) | FTS-FM-001 | 11 failure modes, each with cause, detection, response, and the test that proves it; what each one does to the validity of every computed field |
| [Test plan](docs/test_plan.md) | FTS-TP-001 | test levels including on-target hardware steps, pass/fail criteria, test cards TC-001 to TC-017, coverage thresholds and exclusion policy |
| [Verification results](docs/verification_results.md) | FTS-VR-001 | per-commit test and coverage records, justified exclusions, findings, the CI run of record |
| [Noise profile](docs/noise_profile.md) | FTS-NP-001 | the stationary-capture method, the estimators and their tolerances, which Kalman constants are measured and which are tuning choices, the capture's provenance |

Every change follows the same loop: requirement, matrix row, code, test, matrix update, coverage and static analysis, one commit naming the requirement IDs.

**Limits, stated in the same breath.** Software level DAL C is assumed, not assigned by a certification authority, and nothing here is certified. Nothing here was reviewed or tested by a party independent of the development. No tool qualification under DO-330 was performed on the compiler, Google Test, gcov/lcov, or cppcheck. MC/DC coverage is out of scope; DAL C requires statement and decision coverage, and branch coverage is what the gate measures.

## Findings worth reading

Three findings, none of them caught by a unit test on a single machine, each written up in [verification_results.md](docs/verification_results.md):

1. **Unspecified padding in brace-built fault events (Entry 5 §5.2).** `FaultEvent` is 16 bytes with two padding bytes. The detector built events as braced temporaries; clang zero-fills them, GCC does not. Every local run passed. The first CI run on GCC failed the replay identity test because the live and replay detectors saw different stack contents in those two bytes. Fix: one construction site that value-initializes the struct, plus a test that reads the padding bytes through `offsetof` and asserts zero on any toolchain.
2. **Uninitialized padding in the simulator's frame (Entry 4 §4.3).** Same defect class one struct over, found by a `memcmp` test added when the requirement was reworded to say "raw". Two identical runs could have produced logs that differed in bytes carrying no data.
3. **Coverage tooling, not code (Entry 5 §5.5 to §5.8).** GCC keeps exception-unwind branches on conditional lines, which left `src/logging/` gated at 80.6% on noise. lcov's documented setting for removing them wiped all branch data on the runner's lcov 2.0 and turned CI red. The change was reverted, the runner reproduced in a container with the same toolchain, and the noise resolved with six per-line markers, each justified. Lesson recorded: test coverage tooling on the toolchain that produces the number of record.

## Build, test, run, replay

Requires CMake 3.16 or newer, a C++17 compiler (GCC or clang), and the ZeroMQ C library (`brew install zeromq` on macOS, `sudo apt-get install libzmq3-dev` on Debian and Ubuntu). Google Test (v1.15.2), the cppzmq header wrapper (v4.11.0) and yaml-cpp (0.9.0) are fetched at configure time, pinned.

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

```bash
./build/telemetry                                   # 5 paced cycles on the simulated source, config/telemetry_config.yaml
./build/telemetry --config other.yaml               # the same with another file (every key required, every problem named)
./build/telemetry --replay logs/telemetry_000.bin   # the same pipeline fed from the log, unpaced, with the configuration recorded beside it
cmp logs/telemetry_000.bin logs/replay_000.bin      # prints nothing: the two logs are byte-identical
./build/telemetry --live --cycles 250               # on the Pi: BMP280 and MPU6050 over the configured I2C bus, 5 s paced
```

```bash
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt       # once: pyzmq, numpy, matplotlib, pytest, pinned
.venv/bin/python -m pipeline.receiver --endpoint tcp://127.0.0.1:5555   # start first; one line per frame, gap count at exit
./build/telemetry --zmq tcp://127.0.0.1:5555 --cycles 500             # 10 s at 50 Hz, logged and published
python3 -m pipeline.receiver --udp 127.0.0.1:5556                      # the same receiver over UDP, standard library only
./build/telemetry --udp 127.0.0.1:5556 --cycles 500                   # one datagram per frame; --zmq and --udp may be combined
```

```bash
.venv/bin/python -m pytest                                              # 85 tests in about 65 s: one simulated session, replay identity, the log reader, the noise oracles, both transports, the configuration file, validity and the format version
.venv/bin/python -m pipeline.log_reader logs/                           # a run's rotated files as one sequence: counts, steps, freshness, events
.venv/bin/python -m analysis.noise_profile logs/ --figures docs/img --json noise.json   # per-channel noise, Allan deviation, the derived Kalman constants
```

The live run prints each frame with the validity beside each computed value, writes `logs/telemetry_000.bin`, `logs/telemetry_jitter.csv` and `logs/telemetry_config.yaml` (the configuration it ran with, verbatim), and prints a timing summary and a validity summary (on how many frames every computed field was NORMAL, and the first). The replay names its input's format version, writes `logs/replay_000.bin` and `logs/replay_config.yaml`, and says so if it ran past the end of its log. A run with `--zmq`, `--udp`, or both adds one transport summary line per publisher (frames published, send failures); `--replay` with either publishes the log as a burst. The pytest suite, coverage, and static analysis run in CI on every push; the exact lcov and cppcheck invocations are in [.github/workflows/ci.yml](.github/workflows/ci.yml).

## Layout

```
src/            telemetry_frame, data_source, main.cpp
  drivers/      simulated_data, simulated_source, fault_injecting_source,
                i2c_bus, bmp280, mpu6050, live_sensor_source
  processing/   altitude, kalman_filter, complementary_filter, fault_detector, processor
  logging/      log_format, binary_logger, log_reader
  replay/       log_replay_source
  timing/       clock, fixed_rate_scheduler, jitter_log
  transport/    publisher (the seam), zmq_publisher, udp_publisher
  config/       telemetry_config (the typed structs, the loader and its rules, the record's naming)
config/         telemetry_config.yaml, the configuration of record (36 keys, one comment per section citing its requirement)
pipeline/       frame_format (the byte layout, shared by log and wire), receiver (ZeroMQ SUB or UDP),
                log_reader (files, rotation, the numpy layout)
analysis/       noise_profile (per-channel noise, Allan deviation, the derived Kalman constants)
tests/unit/     one Google Test file per module
tests/integration/  pytest: replay identity, the log reader, the noise oracles, both transports, the configuration file, validity and the format version
docs/           the seven controlled documents above
scripts/        coverage_check.sh, the per-scope branch threshold gate
```

## Roadmap

Planned, no dates: the NEO-6M GPS driver over UART behind the same `DataSource` seam; the first stationary capture, with the measured barometric variance replacing its placeholder (the analysis is built and verified); the Primary Flight Display, which reads the validity bytes the frame now carries; redundancy voting across sources, whose output frame will use the same validity vocabulary; an ADS-B data source.

## License

MIT, see [LICENSE](LICENSE).
