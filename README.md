# Flight Telemetry System

[![CI](https://github.com/DerekZ-113/flight_telemetry/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/DerekZ-113/flight_telemetry/actions/workflows/ci.yml)

Embedded flight telemetry for a Raspberry Pi, in C++17: barometer, IMU, and GPS readings fused into altitude and attitude at a fixed 50 Hz, with per-channel fault detection, a binary log that replays bit-for-bit through the same pipeline, and per-cycle timing evidence. Built with DO-178C-aligned practices: every line of code traces to a written requirement and to a test, and coverage, static analysis, and a formal fault model are part of every build.

**Status:** the deterministic core is complete and verified on simulated and replayed data; processed frames leave the process over ZeroMQ and over UDP to a Python receiver that reads either; the BMP280 and MPU6050 drivers and the live sensor source are built and unit-tested against a fake bus, awaiting their first run on the Raspberry Pi; the GPS receiver's path (NMEA over a serial device, USB or the header UART) parses sentences, pairs them into fixes and delivers each through the same live source at the receiver's own 1 Hz, verified against the u-blox manual's sentences and on a pseudo-terminal, awaiting the board; the Python log reader, the replay-identity pytest, and the noise analysis are built and verified against simulated logs, ready for the first stationary capture; the pytest suite runs in CI on every push; every tunable comes from one YAML configuration file, validated at startup and recorded beside each session's log; every computed field carries its validity in the frame, a channel is NO_DATA until its first reading, and the log format's version 2 names every byte while readers still replay version-1 logs; a replay ends at its log and can be paced to its recorded timestamps, byte-identical either way; an eight-byte session status on the wire names the run mode and the end of the stream; and a WebSocket bridge and a browser page show frames arriving at the pace they happened, from live, simulated or replayed data, stale when they stop and ended when the publisher says so; and a Primary Flight Display draws them, an attitude indicator, an altitude tape, a vertical speed indicator, a ground speed readout, a track indicator and a ground track plot, each showing a value only while the frame says it can be trusted, with a scripted flight for the simulator and a replay demo. The measured constants and the receiver's first fixes on the Pi are next.

## Architecture

```
DataSource ──raw + read_ok──▶ FaultDetector ──raw + status──▶ TelemetryProcessor ──raw + computed──▶ BinaryLogger ──▶ logs/*.bin
   │                                                                                                   │
   ├─ SimulatedSource        (built)          paced by FixedRateScheduler(Clock)                        ├──▶ ZmqPublisher ──PUB socket──▶ pipeline/receiver.py --endpoint
   ├─ LogReplaySource        (built)          jitter → logs/telemetry_jitter.csv                        └──▶ UdpPublisher ──datagrams──▶ pipeline/receiver.py --udp
   ├─ FaultInjectingSource   (built, wraps any source)                                                      (--zmq, --udp, or both; one Publisher seam; the same bytes as the log)
   └─ LiveSensorSource       (built: BMP280 and MPU6050 over I2C, a NEO-6M-class GPS over a serial device)

   a replay ends at its log (has_next) and, with --pace, is released on the log's own timestamps by TimestampPacer(Clock); lateness → logs/<prefix>_jitter.csv
   the wire also carries an 8-byte session status (mode; running or ended) ──▶ pipeline/bridge.py ──WebSocket on the loopback──▶ dashboard/ (the React page)
   --motion demo scripts the simulator (a 150 s flight the pipeline's own models invert, the default byte-identical); dashboard/src/pfd/ draws the instruments,
   ATT, ALT, VS, GS, TRK and the ground track plot, each showing a value only while its validity or status allows it and a flag naming it otherwise
```

- **One seam.** `DataSource` is an abstract base with one virtual `read_frame()`. A source fills raw sensor fields and `read_ok` flags only; computed fields, health and validity belong to the pipeline. Live, simulated, and replayed frames run byte-for-byte the same code.
- **Drivers.** The barometer and IMU drivers talk to an `I2cBus` interface, never to the OS, so their register protocol and arithmetic are tested on a machine with no bus, against a fake that serves recorded bytes; the Linux implementation is one descriptor on `/dev/i2c-1`, addressed per transaction, held under RAII. The barometer compensation reproduces Bosch's worked example to the count, and the MPU6050 driver refuses a sample rate its divider cannot produce exactly. A failed read is an empty reading, never an exception: the channel's fields hold, `read_ok` clears, and the detector decides when that has lasted long enough to matter. The GPS receiver sits behind a `SerialPort` seam the same way (POSIX termios in raw mode, non-blocking, tested on a pseudo-terminal, so it needs no platform split); the driver frames NMEA sentences out of the byte stream, checks each checksum, and pairs the GGA and RMC of one epoch into a fix that is fresh on exactly the cycle it completed, once a second, and held between. A receiver without a fix delivers nothing, so the channel times out like a silent one, and the receiver's counters at exit tell the two apart.
- **Detector before processor.** The Kalman and complementary filters carry state across frames and consume a reading only when the detector marked its channel NOMINAL and the driver obtained it this cycle, so a 1 Hz GPS fix is applied once, not fifty times; an IMU gap longer than the timeout re-seeds attitude from the accelerometer instead of integrating one rate sample across it. The detector judges timeouts on the frame's own timestamp, never a wall clock, so a replay reproduces the same faults at the same frames.
- **Processing.** ISA pressure-to-altitude; a 2-state Kalman filter (altitude, vertical velocity) fusing barometer and GPS; a complementary filter for pitch and roll with alpha treated as a time constant (about 1 s at 50 Hz).
- **Fault handling.** Communication timeout, stuck value, out-of-range, and recovery after five consecutive valid reads, per channel. A channel is NO_DATA until its first reading; a failed sensor marks its channel DEGRADED and logs an event; the rest of the pipeline keeps running.
- **Validity.** Every computed field (barometric altitude, pitch, roll, fused altitude, vertical speed) carries one validity byte in the same 104-byte frame, written last by the processor in order of precedence: NO_COMPUTED_DATA while no estimate has ever existed, FAILURE_WARNING while the channel it depends on is not trusted or no altitude source contributes, NORMAL otherwise. The number is always defined (0, or the held or coasting estimate), never NaN; the flag is what a display acts on, the way an ARINC 429 word carries its sign/status matrix beside its data. Zero means "not judged" and the pipeline never writes it, so a frame nobody judged cannot pass as valid.
- **Logging and replay.** Records are raw struct bytes behind a 16-byte self-describing header (magic, version, record sizes, endianness marker), with fault events as tagged records in the same stream and rotation at record boundaries. Format version 2 names every byte of both records, so a `static_assert` proves the layout has no padding; readers accept versions 1 and 2 and refuse anything else by name before a frame flows. `LogReplaySource` strips a log back to raw readings and feeds the same pipeline; the output log is byte-identical to the original (same build, same platform, same format version), and a version-1 log replays into a version-2 one.
- **Timing.** Absolute deadlines (`clock_nanosleep` with `TIMER_ABSTIME` on Linux), re-phase and count missed cycles on overrun, never burst to catch up. Jitter goes to a CSV per run so the telemetry log stays deterministic.
- **Configuration.** `config/telemetry_config.yaml` holds the 38 tunables (the period, the sensors' bus, addresses and IMU rate, the GPS receiver's switch, device and baud rate, the fault thresholds, the filter tuning, the log directory and size, the transport addresses, the simulator's seed and noise). It is loaded once at startup, every value checked and every problem named in one report, and handed to each module by constructor; nothing reads it after that, and nothing in the binary falls back to a built-in value. A copy is written beside the log, and a replay reads that copy, so a changed constant can never silently break replay identity.
- **Replay's end, and its pace.** A source says whether it has a next frame (`has_next()`, true forever for a sensor or the simulator), so a replay runs to the end of its log and never past it, and `--cycles` caps it. `--pace` releases each replayed frame at the wall time its recorded timestamp maps to, through the same `Clock` seam the scheduler uses, late frames released at once and never skipped: the paced log is byte-identical to the unpaced one, and the lateness goes to `logs/replay_jitter.csv`.
- **Session status.** Eight bytes under their own magic on each transport, never a log record: the run mode (LIVE, SIMULATED, REPLAY) before the first frame and once a second, and ENDED three times after the last, so a consumer that joins late learns what it is looking at and a finished replay never looks like a lost link. The receiver reads it like any record and ends on ENDED.
- **Bridge and page.** `pipeline/bridge.py` forwards every frame and status to browser clients over a WebSocket as JSON by name, listening on the loopback unless told otherwise and never forwarding latitude or longitude. `dashboard/` is the display's first screen: the mode label, the feed's state, the rate, the last frame, each computed field with its validity by name beside the number, and each channel's status; staleness is judged from the page's own clock at arrival, and an ended stream is shown as ended, never stale.
- **Instruments.** `dashboard/src/pfd/` is the Primary Flight Display: an attitude indicator (the symbol fixed, the horizon moving: nose up moves it down, a right roll turns it counterclockwise), an altitude tape in feet, a vertical speed indicator in feet per minute, a ground speed readout in knots and a track indicator from the GPS (GS and TRK, never airspeed or heading: there is no pitot and no magnetometer, and the track is withheld below 1 m/s because a stationary receiver's course is noise), and a ground track plot relative to the first fix on a plain grid, never a map. One rule decides what every instrument shows: the value while its validity or channel status allows it, dashes for no data, and a flag naming the instrument in place of a value that cannot be trusted (FAILURE_WARNING, DEGRADED, a stale or lagging feed, a lost bridge; grey for an ended stream); nothing on an instrument is ever struck through, which the diagnostic view below keeps for itself. The lag, the page's elapsed time minus the frame's, catches what arrival time cannot: frames queued in the bridge, the socket or a throttled tab. Specified in `docs/display_spec.md` (FTS-DS-001); `--motion demo` scripts the simulator's 150 s flight for the replay demo.
- **Transport.** Two publishers behind one `Publisher` seam, so the loop knows neither: with `--zmq <endpoint>` every processed frame goes out on a ZeroMQ PUB socket, with `--udp <ipv4>:<port>` as one datagram to one unicast destination, both as the same one-record log message (the 16-byte header, the tag, the frame bytes), assembled by one function, so `pipeline/receiver.py` reads either transport with the layout module it uses for the log. Neither blocks the loop: the PUB socket drops to a slow or absent subscriber at a 100-message high-water mark; the datagram socket is connected at startup and sends with `MSG_DONTWAIT`, so a destination that cannot be routed is refused before the loop starts and every refusal the kernel reports later (a full buffer, the port-unreachable it learns from an earlier datagram) is counted and never retried. Loss and reordering are measured at the receiver from timestamp gaps.

## Verification

Measurement of record: GitHub Actions run [38674948292](https://github.com/DerekZ-113/flight_telemetry/actions/runs/38674948292) on commit `c988cf9`, Ubuntu 24.04, GCC 13.3, the first run with FAULT-010 tripping at both accelerometer rails, after the GPS receiver's run on `4783b9d` ([verification_results.md](docs/verification_results.md), Entry 14 §14.6; Entry 13 §13.7 and Entry 12 §12.7 record the two runs before it).

| Item | Value |
|---|---|
| Unit tests (Google Test) | 289 of 289 passed |
| Integration tests (pytest) | 85 of 85 passed in 67.5 s |
| Branch coverage (gcov/lcov) | 95.4% (796 of 834), every per-scope threshold met: config 100%, transport 96.2%, drivers 91.0% (hardware-bound error paths, and five parser and driver outcomes no test takes yet, Entry 13 §13.7), the other scopes 100% |
| Line coverage | 98.6% (1805 of 1831) |
| Compiler warnings under `-Werror` | 0 |
| cppcheck findings | 0 |

On the display's branch (not yet on `main`): 323 unit tests, 115 pytest and 66 page tests pass on the Mac; the CI runs on its pull request are the measurement of record (FTS-VR-001 Entries 16 and 17).

Branch coverage is the gating figure. `scripts/coverage_check.sh` fails the build below 90% in `src/processing/` and `src/config/`, 80% in timing, logging, transport, and replay, 70% in drivers, and 80% overall. Every exclusion is listed with its justification in the verification results: the composition root `main.cpp`, Google Test's own sources and the test files themselves, the no-match edge of exhaustive `switch` statements, one `EINTR` retry loop, and six GCC exception-unwind edges marked per line. Numbers from a developer Mac (clang) are recorded as previews only, because the Pi's compiler family is GCC.

## DO-178C-aligned practices

| Document | ID | What it holds |
|---|---|---|
| [Requirements](docs/requirements.md) | FTS-SRD-001 v0.11.0 | 47 "shall" statements with rationale; the scope of record |
| [Design](docs/design.md) | FTS-DD-001 | software level, every architecture decision with its reason, log format, timing, open decisions |
| [Traceability matrix](docs/traceability_matrix.md) | FTS-TM-001 | requirement → code → test, forward and reverse; status per requirement (38 Verified, 9 Partial, 0 Not started) |
| [Fault model](docs/fault_model.md) | FTS-FM-001 | 11 failure modes, each with cause, detection, response, and the test that proves it; what each one does to the validity of every computed field |
| [Test plan](docs/test_plan.md) | FTS-TP-001 | test levels including on-target hardware steps and the page's, pass/fail criteria, test cards TC-001 to TC-026, coverage thresholds and exclusion policy |
| [Verification results](docs/verification_results.md) | FTS-VR-001 | per-commit test and coverage records, justified exclusions, findings, the CI run of record |
| [Noise profile](docs/noise_profile.md) | FTS-NP-001 | the stationary-capture method, the estimators and their tolerances, which Kalman constants are measured and which are tuning choices, the capture's provenance |
| [Display specification](docs/display_spec.md) | FTS-DS-001 | each instrument's source field, rule, units, range, scale, update and flags; the feed's states; the ground track plot; the demo flight |

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
./build/telemetry --replay logs/telemetry_000.bin   # the same pipeline fed from the log, unpaced, to its end, with the configuration recorded beside it
cmp logs/telemetry_000.bin logs/replay_000.bin      # prints nothing: the two logs are byte-identical
./build/telemetry --replay logs/telemetry_000.bin --pace   # the same at the log's recorded cadence; the log still byte-identical, lateness in logs/replay_jitter.csv
./build/telemetry --motion demo --cycles 7500              # the simulator's 150 s scripted flight (takeoff, climb, turn, descent): the demo's recording, 7,500 frames
./build/telemetry --live --cycles 250               # on the Pi: BMP280 and MPU6050 over the configured I2C bus, the GPS when sensors.gps.enabled, 5 s paced
```

```bash
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt       # once: pyzmq, numpy, matplotlib, pytest, pinned
.venv/bin/python -m pipeline.receiver --endpoint tcp://127.0.0.1:5555   # start first; one line per frame, gap count at exit
./build/telemetry --zmq tcp://127.0.0.1:5555 --cycles 500             # 10 s at 50 Hz, logged and published
python3 -m pipeline.receiver --udp 127.0.0.1:5556                      # the same receiver over UDP, standard library only
./build/telemetry --udp 127.0.0.1:5556 --cycles 500                   # one datagram per frame; --zmq and --udp may be combined
```

```bash
.venv/bin/python -m pipeline.bridge --endpoint tcp://127.0.0.1:5555   # frames and statuses to browser clients over ws://127.0.0.1:8765 (the loopback; --listen to widen)
cd dashboard && npm ci && npm run dev                                   # the page at http://127.0.0.1:5173; then a live, simulated or paced replay run with --zmq
./build/telemetry --replay logs/telemetry_000.bin --pace --zmq tcp://127.0.0.1:5555   # the demo: the recorded flight to the PFD at its own pace, labelled REPLAY
cd dashboard && npm test -- --run && npm run build                      # 66 page tests (Vitest, jsdom), the TypeScript check and the bundle
```

```bash
.venv/bin/python -m pytest                                              # 108 tests in about 100 s: one simulated session, replay identity, the log reader, the noise oracles, both transports, the configuration file, validity and the format version, the replay's end and pace, the session status, the bridge
.venv/bin/python -m pipeline.log_reader logs/                           # a run's rotated files as one sequence: counts, steps, freshness, events
.venv/bin/python -m analysis.noise_profile logs/ --figures docs/img --json noise.json   # per-channel noise, Allan deviation, the derived Kalman constants
```

The live run prints each frame with the validity beside each computed value, writes `logs/telemetry_000.bin`, `logs/telemetry_jitter.csv` and `logs/telemetry_config.yaml` (the configuration it ran with, verbatim), and prints a timing summary and a validity summary (on how many frames every computed field was NORMAL, and the first). The replay names its input's format version, writes `logs/replay_000.bin` and `logs/replay_config.yaml`, runs to the end of its log unless `--cycles` caps it and says which ended it, and with `--pace` writes `logs/replay_jitter.csv` and prints its lateness. A live run adds a `GPS:` line (sentences, fixes, epochs without a fix, bad checksums and the rest of the receiver's counters) or says the receiver is disabled; a run with `--zmq`, `--udp`, or both adds one transport summary line per publisher (frames published, send failures, status messages); `--replay` with either publishes the log as a burst, or at its recorded cadence with `--pace`. The pytest suite, coverage, and static analysis run in CI on every push; the exact lcov and cppcheck invocations are in [.github/workflows/ci.yml](.github/workflows/ci.yml).

## Layout

```
src/            telemetry_frame, data_source, main.cpp
  drivers/      simulated_data, simulated_source, fault_injecting_source,
                i2c_bus, bmp280, mpu6050, serial_port, nmea, nmea_gps, live_sensor_source
  processing/   altitude, kalman_filter, complementary_filter, fault_detector, processor
  logging/      log_format, binary_logger, log_reader
  replay/       log_replay_source
  timing/       clock, fixed_rate_scheduler, timestamp_pacer (a replay on its log's timestamps), jitter_log
  transport/    publisher (the seam), zmq_publisher, udp_publisher, session_message (mode and end on the wire)
  config/       telemetry_config (the typed structs, the loader and its rules, the record's naming)
config/         telemetry_config.yaml, the configuration of record (48 keys, ten of them in the optional adsb block, one comment per section citing its requirement)
pipeline/       frame_format (the byte layout, shared by log and wire, and the session status), receiver (ZeroMQ SUB or UDP),
                log_reader (files, rotation, the numpy layout), bridge (the receiver to browser clients over a WebSocket)
dashboard/      the page: Vite, React and TypeScript; the feed hook, the diagnostic view, the instruments (src/pfd/), their Vitest tests
analysis/       noise_profile (per-channel noise, Allan deviation, the derived Kalman constants)
tests/unit/     one Google Test file per module
tests/integration/  pytest: replay identity, the log reader, the noise oracles, both transports, the configuration file, validity and the format version, the replay's end and pace, the session status, the bridge, the scripted flight
docs/           the eight controlled documents above
scripts/        coverage_check.sh, the per-scope branch threshold gate
```

## Roadmap

Planned, no dates: the GPS receiver's first fixes on the Pi and the board's own capture as a test fixture; the first stationary capture, with the measured barometric variance replacing its placeholder (the analysis is built and verified); the v0.2.0 demo video from the replay demo; redundancy voting across sources, whose output frame will use the same validity vocabulary; an ADS-B data source.

## License

MIT, see [LICENSE](LICENSE).
