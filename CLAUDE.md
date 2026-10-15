# CLAUDE.md — Flight Telemetry System

## What This Project Is

Embedded flight telemetry for a Raspberry Pi in C++17, built with DO-178C-aligned practices: every line of code traces to a written requirement and to a test, and structural coverage, static analysis, and a formal fault model are part of every build.

**Built:** the 104-byte `TelemetryFrame` record; the `DataSource` abstraction with a simulated source, a fault-injecting decorator, and a log-replay source; the processing pipeline (ISA pressure-to-altitude, 2-state Kalman altitude/velocity fusion, complementary-filter pitch and roll); the fault detector (communication timeout, stuck value, range, recovery) with per-channel DEGRADED state; a binary logger with rotation, a reader, and bit-identical deterministic replay; a fixed-rate scheduler on absolute deadlines with per-cycle jitter logging; two transports behind one `Publisher` seam: a ZeroMQ publisher that sends every processed frame as a one-record log message on a PUB socket, and a UDP publisher that sends the same message as one datagram to a unicast destination over a connected non-blocking socket, neither ever blocking the loop, with a Python receiver that reads either transport through the log's byte layout; an `I2cBus` seam with a Linux device-file implementation and a test fake, the BMP280 (BME280-compatible) and MPU6050 drivers verified against the manufacturers' arithmetic, and a live sensor source selected by `--live`; a `SerialPort` seam (POSIX termios in raw mode, non-blocking, no platform split, unit-tested on a pseudo-terminal), the NMEA 0183 parser verified against the u-blox manual's printed examples, and the `NmeaGps` driver that frames sentences out of the byte stream, pairs the GGA and RMC of one epoch and hands the live source one fresh reading per valid fix at the receiver's own 1 Hz, the receiver named by three `sensors.gps` keys and off in the shipped file; a Python log reader that is the C++ reader's twin, the replay-identity pytest, the noise analysis (per-channel sigma, Allan deviation, gyro bias, the derived Kalman constants) verified against simulated logs, and the transport pytest over both transports; one YAML configuration file (`config/telemetry_config.yaml`, 38 keys) loaded and validated at startup, handed to every module by constructor, and recorded beside each session's log so a replay runs with the configuration its log was recorded with; a validity byte per computed field in the frame, written by the processor in order of precedence (NO_COMPUTED_DATA, FAILURE_WARNING, NORMAL) so a display can tell a value from its absence, NO_DATA on a channel before its first reading, and log format version 2, which names every byte of both records while readers still replay version-1 logs; a replay that ends at its log (`DataSource::has_next()`, a one-frame look-ahead in the replay source, `--cycles` as a cap) and, with `--pace`, is released on the log's own timestamps through the `Clock` seam by a `TimestampPacer`, the paced log byte-identical to the unpaced one and its lateness in `logs/replay_jitter.csv`; an eight-byte session status on both transports (the run mode LIVE, SIMULATED or REPLAY before the first frame and once a second, ENDED three times after the last; never a log record, so the format version stays at 2), which the receiver reads and ends on; a WebSocket bridge (`pipeline/bridge.py`, Python `websockets`, loopback by default, latitude and longitude never forwarded) and a React page (`dashboard/`, Vite and TypeScript, Vitest) showing frames arriving: the mode label, the feed's state (stale from the page's own arrival clock, ended from the status), the rate, the last frame, each computed field with its validity by name, each channel's status; Google Test unit tests, a pytest suite and the page's Vitest tests, all run in CI on every push; CI with a branch-coverage gate and cppcheck; seven controlled documents under `docs/`.

**Planned:** the GPS receiver's first fixes on the Pi (USB or the header UART) and the board's capture as a test fixture; the stationary capture that feeds the measured constants into the configuration file; the display's instruments (part 2: attitude, altimeter tape, vertical speed, heading, ground track, each flagged from its validity, with `display_spec.md` and the deterministic replay demo). The scope of record is `docs/requirements.md`; the architecture of record is `docs/design.md`.

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
./build/telemetry                                   # 5 paced cycles on the simulator with config/telemetry_config.yaml -> logs/telemetry_000.bin and a copy of the configuration beside it
./build/telemetry --config other.yaml               # another file; every key required, every problem named, exit 1 before anything is built
./build/telemetry --replay logs/telemetry_000.bin   # same pipeline fed from the log, unpaced, to its end, with the configuration recorded beside it -> logs/replay_000.bin
./build/telemetry --replay logs/telemetry_000.bin --pace   # the same at the log's recorded cadence; byte-identical; lateness -> logs/replay_jitter.csv
./build/telemetry --live --cycles 250               # on the Pi: BMP280 and MPU6050 over /dev/i2c-1 and, when sensors.gps.enabled, the GPS on sensors.gps.device, paced
cmp logs/telemetry_000.bin logs/replay_000.bin      # silent: the two logs are byte-identical
.venv/bin/python -m pipeline.receiver --endpoint tcp://127.0.0.1:5555   # start first; one line per frame, gap count at exit
./build/telemetry --zmq tcp://127.0.0.1:5555 --cycles 500             # 10 s at 50 Hz, logged and published
python3 -m pipeline.receiver --udp 127.0.0.1:5556                      # the same receiver over UDP, standard library only
./build/telemetry --udp 127.0.0.1:5556 --cycles 500                   # one datagram per frame; --zmq and --udp may be combined
.venv/bin/python -m pipeline.bridge --endpoint tcp://127.0.0.1:5555   # frames and session statuses to browser clients over ws://127.0.0.1:8765 (loopback; --listen to widen)
cd dashboard && npm ci && npm run dev                                   # the page at http://127.0.0.1:5173 (Vite); npm test -- --run and npm run build for its 17 tests and the bundle
.venv/bin/python -m pytest                                              # 108 tests in about 100 s: one simulated session, the transport runs, the configuration file, validity and the format version, the replay's end and pace, the session status, the bridge
.venv/bin/python -m pipeline.log_reader logs/                           # a run's rotated files as one sequence
.venv/bin/python -m analysis.noise_profile logs/ --figures docs/img --json noise.json   # the noise profile
```

Prerequisites beyond CMake and a C++17 compiler: the ZeroMQ C library (`brew install zeromq` / `apt-get install libzmq3-dev`; cppzmq is fetched and pinned by CMake); for the Python side, `python3 -m venv .venv && .venv/bin/pip install -r requirements.txt` (pyzmq, numpy, matplotlib, pytest, websockets, pinned); for the page, Node 22 and `cd dashboard && npm ci` (every package pinned by the lockfile; regenerate it with `npm install --legacy-peer-deps`, since npm 10.9's peer resolver trips over jsdom's optional `canvas` peer).

CI (`.github/workflows/ci.yml`) configures with `-DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON`, builds, runs ctest, runs the pytest suite against the instrumented binary (its runs count toward coverage, test plan §6.1), captures lcov branch coverage, enforces the per-scope thresholds in `scripts/coverage_check.sh` (from `docs/test_plan.md` §6.3), and runs cppcheck (reports on every push, gates on `v*` tags); a second job, `page`, runs the dashboard's Vitest tests with coverage (reported, not gated) and its TypeScript build in parallel. The workflow runs on pushes to `main`, tags and pull requests to `main`, so a branch gets CI through a (draft) pull request. The coverage measurement of record is the CI run on GCC; developer-Mac (clang) numbers are previews. Results and justified exclusions are recorded per commit in `docs/verification_results.md`.

## Tech Stack

- **Built:** C++17 (CMake 3.16+, g++ or clang++), Google Test via FetchContent, ZeroMQ (system libzmq 4.3.5, cppzmq v4.11.0 via FetchContent), POSIX UDP sockets, yaml-cpp 0.9.0 via FetchContent, Python 3.12+ with pyzmq, numpy, matplotlib, pytest and websockets 17.2 (pinned in `requirements.txt`; CI runs 3.13), gcov/lcov, cppcheck, GitHub Actions; the page on Node 22: React 19.3, Vite 8.3, TypeScript 5.9, Vitest 4.1 with jsdom and React Testing Library (pinned in `dashboard/package.json` and its lockfile)
- **Planned:** the display's instruments on the same stack (part 2)

## Project Structure

```
src/
  telemetry_frame.h/.cpp  the record: raw fields, computed fields, status, read_ok   built
  data_source.h           abstract DataSource, raw-only contract                      built
  drivers/                simulated_data, simulated_source, fault_injecting_source,   built
                          i2c_bus, bmp280, mpu6050, serial_port, nmea, nmea_gps,
                          live_sensor_source
  processing/             altitude, kalman_filter, complementary_filter,
                          fault_detector, processor                                   built
  logging/                log_format, binary_logger, log_reader                       built
  replay/                 log_replay_source (has_next: a one-frame look-ahead)    built
  timing/                 clock, fixed_rate_scheduler, timestamp_pacer (--pace),
                          jitter_log                                            built
  transport/              publisher (the seam), zmq_publisher (PUB socket),
                          udp_publisher (connected datagram socket); drop-not-block;
                          session_message (the 8-byte mode and end on the wire)   built
  config/                 telemetry_config: the typed structs, the loader and its
                          rules, the record's naming (REQ-CFG-001 to -003)            built
  main.cpp                composition root; --config, --replay, --pace, --zmq, --udp,
                          --live; the loop asks the source; the status cadence   built

tests/
  unit/                   Google Test, one file per module                            built
  integration/            pytest: conftest (one simulated session, the published-run
                          harness), synthetic_log, test_log_reader, test_replay_identity,
                          test_noise_profile, test_transport (TC-005, TC-010, both),
                          test_config (TC-016), test_validity (TC-017),
                          test_replay_pacing (TC-019, TC-020), test_session_status
                          (TC-021), test_bridge (TC-022)                          built
  fault_injection/        parameterized fault scenarios                               planned

docs/                     requirements, design, traceability_matrix, fault_model,
                          test_plan, verification_results, noise_profile              built
                          display_spec                                                planned
scripts/                  coverage_check.sh                                           built
pipeline/                 frame_format (byte layout shared by log and wire, the session status),
                          receiver (ZeroMQ SUB or UDP; keeps the latest status, ends on ENDED),
                          log_reader (files, rotation, the numpy layout),
                          bridge (the receiver to browser clients over a WebSocket) built
analysis/                 noise_profile (per-channel noise, Allan deviation,
                          the derived Kalman constants)                               built
config/                 telemetry_config.yaml, the configuration of record (38 keys)   built
dashboard/                the page: feed.ts (the JSON contract), useTelemetryFeed (the
                          hook), badge, App; Vitest tests, a fake socket, fake timers  built
coverage/ static_analysis/   generated reports (committed selectively, see .gitignore)
```

## Coding Conventions

- C++17, `-Wall -Wextra -Wpedantic`; CI adds `-Werror` (`WARNINGS_AS_ERRORS=ON`)
- `const&` for passing structs/objects to functions (no unnecessary copies)
- RAII for resource management (I2C bus handles, file handles)
- Header (.h) and implementation (.cpp) split for all classes; every class gets its own pair
- snake_case for files and variables, PascalCase for classes and structs
- Comments explain WHY, not WHAT — the code should be readable on its own
- **Value-initialize every struct that reaches the logger, then assign its fields:** `TelemetryFrame frame{};`, `FaultEvent event{};`. Records are written as raw bytes; since format version 2 neither struct has padding (every former padding byte is a named `reserved` or validity field, and a `static_assert` that the members sum to the size proves it), so the rule now keeps the reserved fields zero rather than invisible bytes. Found twice in Week 1 (`docs/verification_results.md` Entries 4 and 5); guarded by `SimulatedDataTest.SameSeedSameSequence`, `FaultDetectionTest.EventReservedIsZero` and `FrameLayoutTest.EveryByteIsANamedField`. A reserved field means zero until a format version assigns it; any change to either struct bumps `kLogFormatVersion` in the same commit.
- **Mark GCC exception edges on conditional lines.** A line that combines a conditional with a `std::string` temporary, a standard-library call, or an iterator range shows untaken exception-unwind branches on GCC that `lcov --filter branch` cannot remove. Put `// LCOV_EXCL_EXCEPTION_BR_LINE` on that line with a one-line reason and list it in `docs/verification_results.md`. Never add `--rc no_exception_branch=1` to the workflow: on the runner's lcov 2.0 it discards all branch data (Entry 5 §5.5 and §5.6).
- Python code follows standard PEP 8. Every module under `pipeline/` and `analysis/` has a row in the matrix's Python reverse-trace table, and a pytest file is named in the Test column of the requirement it verifies, exactly as the C++ files are.

## Key Architecture Decisions

- **Data source abstraction:** abstract base class so the processing engine doesn't know if it's reading live sensors or replaying from a binary log. Sources fill raw fields and `read_ok` only, leaving status NOMINAL (unjudged), every validity NOT_JUDGED and the reserved fields zero; computed fields, health and validity belong to the pipeline. This is what lets replay use the same code path as live mode (REQ-LOG-004).
- **Drivers behind the I2C seam:** `I2cBus` is addressed per transaction (one descriptor serves the bus); drivers return `std::optional` readings and never throw, and a failed read becomes `read_ok` false with the fields held; the live source stamps frames with the actual read time from the monotonic clock; the Linux implementation sits behind the second `#if` and compiles to a stub elsewhere; the barometer and IMU addresses in `config/telemetry_config.yaml` (0x76, 0x68) and the driver's name (`Bmp280`) are provisional until the chip IDs are read on the Pi (design §12). The GPS receiver sits behind a `SerialPort` seam (`PosixSerialPort`: termios raw mode, `O_NONBLOCK`, a flush at open; POSIX, so no `#if`, and unit-tested on a pseudo-terminal); `NmeaGps` frames sentences, checks checksums, pairs the GGA and RMC of one UTC time into a fix, and the live source sets `gps_read_ok` only on the cycle a valid fix completed, holding it between; a no-fix epoch is not a reading, so the channel times out like a silent one and the `GPS:` exit line's counters tell the two apart; the receiver is `sensors.gps.enabled`, `.device` and `.baud_rate`, off in the shipped file. The first second after start is the device's backlog (a USB receiver hands over what it buffered while the port was closed, after the flush), discarded and counted; a hung-up device is read errors, which `poll(2)` tells from silence.
- **Detector before processor:** the fault detector runs first and owns `ChannelStatus` (NO_DATA until a channel's first reading, then NOMINAL or DEGRADED); the filters consume a reading only when its channel is NOMINAL and the reading is fresh (`read_ok`): predict every cycle, update at each sensor's own rate (REQ-PROC-006). The detector judges timeouts on the frame's own timestamp, never a wall clock, so replay reproduces the same faults at the same frames. The processor is the only writer of `Validity`, judging all five computed fields last, in order of precedence, so the value and its verdict leave in the same frame (REQ-PROC-007); a field that is not NORMAL holds a defined number, never NaN.
- **Fault detection → DEGRADED state:** failed sensors don't crash the system. The channel is marked DEGRADED, the fault is logged as a tagged record in the same stream, remaining sensors continue processing, and the channel returns to NOMINAL after M consecutive valid reads.
- **Log format:** raw struct bytes behind a 16-byte self-describing header (magic, version, record sizes, endianness marker); rotation at record boundaries; format version 2 since 2026-10-13, with readers accepting 1 and 2 and refusing others by name before anything is built (`main` probes the replay input's header), a version-1 frame read with validity NOT_JUDGED and reserved zero; bit-identity is claimed for the same build on the same platform and within one format version (`docs/design.md` §8 and §9).
- **Fixed-rate timing with clock_nanosleep:** absolute-time scheduling (`TIMER_ABSTIME` on Linux; the `nanosleep` fallback for other platforms is one of the two `#if`s in the codebase, the other being the I2C device file in `src/drivers/i2c_bus.cpp`; both sit on the OS boundary), re-phase and count missed cycles on overrun, never burst. Jitter is logged per cycle to a CSV named by the run's prefix (`telemetry_jitter.csv`, `replay_jitter.csv`), not into the telemetry log, so the log stays deterministic.
- **A replay ends at its log and paces on request (REQ-LOG-006, REQ-TIME-004):** `DataSource::has_next()` is true forever for a sensor or the simulator and answered by the replay source from a one-frame look-ahead; the loop asks before every read, so a replay runs to its end by default, `--cycles` caps and never extends, and the `Replay:` line says which ended the run (the padded tail it replaces re-applied the last reading as fresh with `dt` = 0). `--pace` hands each frame's recorded timestamp to a `TimestampPacer` on the same `Clock` seam the scheduler uses, `origin + (t − t0)`, late frames released at once and never skipped; pacing decides when a frame goes out, never what is in it, and the paced log is byte-identical to the unpaced one by test.
- **Kalman filter constants: two measured, two retained, one chosen by replay (REQ-PROC-004, `docs/noise_profile.md`):** the barometric measurement variance is the white part of the barometric altitude noise on a stationary capture (the one-sample Allan deviation squared, immune to the weather's trend) and the initial altitude variance equals it; the process noise describes the vehicle, not a sensor, and stays a documented tuning choice (a Q taken from the accelerometer's own noise would lag a real climb by ten seconds); the GPS variance stays the datasheet figure until a capture with the receiver fixing is analysed; the initial velocity variance, which owns the startup transient, is chosen by replaying the capture through candidates under a rule written before the data. The analysis script and its oracles are built; the capture supplies the values, which land in the `processing` keys of the configuration file and the equal defaults of `ProcessorConfig`.
- **Configuration: one file, validated at startup, injected by value, never global, recorded beside the log (REQ-CFG-001 to REQ-CFG-003):** `config/telemetry_config.yaml` (or `--config`) is the standing configuration and the only source of the values the binary runs with; there is no built-in fallback, and a missing file is refused. `src/config/telemetry_config.cpp` is the one translation unit that knows YAML: it turns the document into typed structs in one pass, checking type and range as it goes (parse, don't validate), with the project's own scalar rules (decimal or `0x` hex, finite numbers, `true`/`false`, non-empty text) and every problem reported together under its dotted key. Values reach modules by constructor, by value (`FaultDetectorConfig`, `ProcessorConfig`, `SimulationNoise`, plain arguments for the rest); no module includes the config header, so a test can hand a module any value. The struct defaults equal the shipped file and are the unit tests' baseline; `ConfigTest.ShippedFileEqualsDefaults` keeps them equal. The file's text is copied beside the log as `<prefix>_config.yaml`; a replay reads it by default, and with `--config` says whether the named file equals the record by content. Flags choose the run and win only on the transports.
- **Dual transport behind one seam:** ZeroMQ for local IPC and UDP for network streaming, mirroring an aircraft-to-ground-station architecture; both built. `Publisher` is `DataSource`'s twin on the output side: the loop holds base pointers and never learns which transport it has, `main` owns the concrete objects, and `--zmq` and `--udp` may be combined. The wire format is the log format: every message and every datagram is a one-record log file (header, tag, frame) assembled by one function in `log_format`, so one Python module describes both and the receiver reads either. Neither publisher blocks (REQ-TRANS-003): the PUB socket has a 100-message high-water mark per subscriber and a bounded linger; the UDP socket is connected at startup (an unroutable or non-unicast destination is refused before the loop), sends with `MSG_DONTWAIT`, and counts every kernel refusal, including the port-unreachable the kernel reports on a later send. The addresses come from the file's `transport` section; `--zmq` and `--udp` enable a transport for one run and override its address. No sequence number travels on the wire (design §12; its trigger is now the voter, and it rides the same format bump as the GPS fix-quality byte); loss and reordering are measured at the receiver from timestamp gaps. One message on each transport is not a log record: the eight-byte session status (`FTSS`, version, mode, state), RUNNING with the run's mode before frame 0 and every second of cycles, ENDED three times after the last frame, so a late joiner learns the mode and a finished replay never looks like a lost link; readers dispatch on the first four bytes, and the status is never logged, so the format version stays at 2.
- **The display's data path: a bridge and a page, no format change (REQ-DISP-003 to REQ-DISP-005):** `pipeline/bridge.py` reads either transport through the receiver on one thread, hands records to an asyncio loop through `call_soon_threadsafe`, and broadcasts each as JSON by name (every field but latitude and longitude, which never cross; validity and status names beside the numbers) to WebSocket clients, listening on the loopback unless `--listen` says otherwise and greeting each client with `hello` (the stale threshold, the period, the last session status, the counts). The page judges staleness from its own clock at arrival, never from the frame, shows no mode until a status names one, never labels simulated or replayed data LIVE, and shows an ended stream as ended, not stale; its socket and clock are injected, the `Clock` seam in a browser, so its tests use a fake socket and fake timers. REQ-DISP-001 and -002 (the instruments) stay drafts in design §11.2 for part 2.

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
- Don't hardcode a tunable — it is a key of `config/telemetry_config.yaml` and a field of its module's config struct, equal by test (REQ-CFG-001); a tunable no requirement names needs the requirement first. The named-constant exception ended with the configuration (2026-10-13)
- Don't use raw `new`/`delete` — use RAII, std::vector, std::unique_ptr
- Don't skip Google Test for any new C++ module
- Don't mix build artifacts with source (out-of-source build in `build/`)
- Don't go beyond the scope of what's being worked on in the current session
- Don't rewrite files that aren't part of the current task
- Don't add requirements, features, or architecture changes without discussion
