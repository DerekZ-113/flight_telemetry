# Flight Telemetry System

[![CI](https://github.com/DerekZ-113/flight_telemetry/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/DerekZ-113/flight_telemetry/actions/workflows/ci.yml)

Embedded flight telemetry for a Raspberry Pi, in C++17: barometer, IMU, and GPS readings fused into altitude and attitude at a fixed 50 Hz, with per-channel fault detection, a binary log that replays bit-for-bit through the same pipeline, and per-cycle timing evidence. Built with DO-178C-aligned practices: every line of code traces to a written requirement and to a test, and coverage, static analysis, and a formal fault model are part of every build.

**Status:** the deterministic core is complete and verified on simulated and replayed data; processed frames leave the process over ZeroMQ and over UDP to a Python receiver that reads either; the BMP280 and MPU6050 drivers and the live sensor source are built and unit-tested against a fake bus, awaiting their first run on the Raspberry Pi; the GPS receiver's path (NMEA over a serial device, USB or the header UART) parses sentences, pairs them into fixes and delivers each through the same live source at the receiver's own 1 Hz, verified against the u-blox manual's sentences and on a pseudo-terminal, awaiting the board; the Python log reader, the replay-identity pytest, and the noise analysis are built and verified against simulated logs, ready for the first stationary capture; the pytest suite runs in CI on every push; every tunable comes from one YAML configuration file, validated at startup and recorded beside each session's log; every computed field carries its validity in the frame, a channel is NO_DATA until its first reading, and the log format's version 3 keeps version 2's bytes as its prefix, records the run mode and the mode the frames were produced in in its header, and adds to every frame its source's sequence number, a flags byte, the ADS-B vertical rate and pressure altitude and the GPS fix quality, while readers still replay version-1 and version-2 logs; a replay ends at its log and can be paced to its recorded timestamps, its records byte-identical either way; a nineteen-byte session status on the wire names the run mode, the mode the frames were produced in, the end of the stream and the aircraft an ADS-B run presents; and a WebSocket bridge and a browser page show frames arriving at the pace they happened, from live, simulated or replayed data, stale when they stop and ended when the publisher says so; and a Primary Flight Display draws them, an attitude indicator, an altitude tape, a vertical speed indicator, a ground speed readout, a track indicator and a ground track plot, each showing a value only while the frame says it can be trusted, with a scripted flight for the simulator and a replay demo; and three identical processing lanes behind the fault detector run every frame while a majority voter chooses the one the system emits, outvoting a wrong lane, excluding one that stays wrong and flagging the frame when no two lanes agree, its decisions in every frame and on the PFD's voter panel; a seeded Monte Carlo campaign of 97,100 fault injections measures the detector and the voter against predictions committed before its code, and names, with numbers, the faults no check catches (FTS-FI-001, a preview against the simulator's noise model). The measured constants and the receiver's first fixes on the Pi are next; an ADS-B source presents one real aircraft on approach to SFO from the OpenSky Network's REST API through the same seam, selected with hysteresis, held between polls under the freshness rule and labelled ADS-B with its callsign on the PFD (built and run live on 2026-10-15; the logs of those sessions stay out of the repository under the network's data license).

## Architecture

```
DataSource ──raw + read_ok──▶ FaultDetector ──raw + status──▶ 3 × TelemetryProcessor ─▶ Voter ──raw + computed──▶ BinaryLogger ──▶ logs/*.bin
   │                                                                                                   │
   ├─ SimulatedSource        (built)          paced by FixedRateScheduler(Clock)                        ├──▶ ZmqPublisher ──PUB socket──▶ pipeline/receiver.py --endpoint
   ├─ LogReplaySource        (built)          jitter → logs/telemetry_jitter.csv                        └──▶ UdpPublisher ──datagrams──▶ pipeline/receiver.py --udp
   ├─ FaultInjectingSource   (built, wraps any source)                                                      (--zmq, --udp, or both; one Publisher seam; the same bytes as the log)
   ├─ LiveSensorSource       (built: BMP280 and MPU6050 over I2C, a NEO-6M-class GPS over a serial device)
   └─ AdsbSource             (built: one aircraft from the OpenSky Network REST API, polled on a thread, held between polls; barometer and IMU NOT_AVAILABLE)

   a replay ends at its log (has_next) and, with --pace, is released on the log's own timestamps by TimestampPacer(Clock); lateness → logs/<prefix>_jitter.csv
   the wire also carries a 19-byte session status (mode and source mode; running or ended; the ADS-B aircraft) ──▶ pipeline/bridge.py ──WebSocket on the loopback──▶ dashboard/ (the React page)
   --motion demo scripts the simulator (a 150 s flight the pipeline's own models invert, the default byte-identical); dashboard/src/pfd/ draws the instruments,
   ATT, ALT, VS, GS, TRK and the ground track plot, each showing a value only while its validity or status allows it and a flag naming it otherwise
```

- **One seam.** `DataSource` is an abstract base with one virtual `read_frame()`. A source fills raw sensor fields, `read_ok` flags, its own count of its frames and the facts it states about a frame (a new vehicle, which optional fields are present) only; computed fields, health and validity belong to the pipeline. Live, simulated, and replayed frames run byte-for-byte the same code.
- **Drivers.** The barometer and IMU drivers talk to an `I2cBus` interface, never to the OS, so their register protocol and arithmetic are tested on a machine with no bus, against a fake that serves recorded bytes; the Linux implementation is one descriptor on `/dev/i2c-1`, addressed per transaction, held under RAII. The barometer compensation reproduces Bosch's worked example to the count, and the MPU6050 driver refuses a sample rate its divider cannot produce exactly. A failed read is an empty reading, never an exception: the channel's fields hold, `read_ok` clears, and the detector decides when that has lasted long enough to matter. The GPS receiver sits behind a `SerialPort` seam the same way (POSIX termios in raw mode, non-blocking, tested on a pseudo-terminal, so it needs no platform split); the driver frames NMEA sentences out of the byte stream, checks each checksum, and pairs the GGA and RMC of one epoch into a fix that is fresh on exactly the cycle it completed, once a second, and held between. A receiver without a fix delivers nothing, so the channel times out like a silent one, and the receiver's counters at exit tell the two apart.
- **Detector before processor.** The Kalman and complementary filters carry state across frames and consume a reading only when the detector marked its channel NOMINAL and the driver obtained it this cycle, so a 1 Hz GPS fix is applied once, not fifty times; an IMU gap longer than the timeout re-seeds attitude from the accelerometer instead of integrating one rate sample across it. The detector judges timeouts on the frame's own timestamp, never a wall clock, so a replay reproduces the same faults at the same frames.
- **Processing.** ISA pressure-to-altitude; a 2-state Kalman filter (altitude, vertical velocity) fusing barometer and GPS; a complementary filter for pitch and roll with alpha treated as a time constant (about 1 s at 50 Hz).
- **Fault handling.** Communication timeout, stuck value, out-of-range (a reading that is not a finite number among them, tested first on every channel), and recovery after five consecutive valid reads, per channel. A channel is NO_DATA until its first reading; a failed sensor marks its channel DEGRADED and logs an event; the rest of the pipeline keeps running. A channel the active source does not provide (the GPS without a receiver, the barometer and IMU of the ADS-B source) is NOT_AVAILABLE from the first frame, a fact the source states and the detector leaves alone: no timeout, no event, a no-source flag on the display instead of a failure flag, and a replay reproduces it.
- **Redundancy and voting.** Three identical `TelemetryProcessor` lanes, each with state of its own, process every frame the one detector checked, in the loop's own thread, and a voter compares their outputs bit for bit in the thirteen fields a lane writes or carries (the five computed values, their validities, the three channel statuses; a NaN agrees with nothing) and emits the checked frame with the majority's computed fields. A lane outside the majority on three consecutive cycles is excluded until restart; with no majority the frame holds the last vote under FAILURE_WARNING and says NO_MAJORITY, and the loop goes on. A lane whose output names another frame has overrun. The voter's decisions are the frame's `voter_status`, recomputed on replay, never copied; its events (a disagreement, an exclusion, no majority, an overrun) are new values of the fault event's bytes, so the format is unchanged. Three lanes on one processor prove the mechanism and catch nondeterminism; they do not survive the Pi failing, and they cannot see a sensor fault, since every lane gets the same frame (FTS-DD-001 Section 13). A lane-fault interface lets the tests and the fault-injection campaign alter a lane; the binary uses it only under `--lane-fault <lane>@<seconds>`, which records the injection in the log, and a replay of that log re-applies it.
- **Fault-injection campaign.** `tests/fault_injection/` runs the real simulator, detector, lanes and voter in one process, unpaced, over seeded faults: the detector's faults, faults just below each threshold, faults no check covers (a bias, a drift, a frozen fix; and a NaN and an infinity, until the campaign's evidence brought the detector a non-finite check on 2026-10-19), clean runs, and lane faults at persistence counts 1, 2, 3, 4, 5 and 8. A run's seed is its id, and the id alone rebuilds it; the schedule's draws use only what the C++ standard fixes, so a schedule is the same on every platform. Every prediction was committed before the engine existed, and a fixed subset of 450 runs gates every build against them. The full campaign, about 2.5 minutes on one core, writes a record from which `analysis/fault_campaign.py` regenerates the report's tables and figures (Wilson intervals, rule-of-three bounds, nearest-rank latencies). The preview held every per-run prediction, measured the timeout at exactly 500 ms and the GPS's at 1,020 to 2,000 ms, found zero false alarms in 707 channel-hours (zero by construction on this noise model, as the report says), and found what Section 5 had not predicted: an identical bit flip that is NaN below 2 m delays a wrong majority instead of preventing it, and a pressure bias's fused altitude overshoots by a fifth (FTS-FI-001 Section 7). Measured before and after the non-finite check, in 300 runs each, a NaN pressure no longer leaves the voter without a majority with every field held under FAILURE_WARNING to the end of the run, and an infinite rotation rate no longer reaches the frame as a roll voted NORMAL: both are declared OUT_OF_RANGE on their frame and recover 80 ms after removal.
- **Validity.** Every computed field (barometric altitude, pitch, roll, fused altitude, vertical speed) carries one validity byte in the same 120-byte frame, written last by the processor in order of precedence: NO_COMPUTED_DATA while no estimate has ever existed, FAILURE_WARNING while the channel it depends on is not trusted or no altitude source contributes, NORMAL otherwise. The number is always defined (0, or the held or coasting estimate), never NaN; the flag is what a display acts on, the way an ARINC 429 word carries its sign/status matrix beside its data. Zero means "not judged" and the pipeline never writes it, so a frame nobody judged cannot pass as valid.
- **Logging and replay.** Records are raw struct bytes behind a 16-byte self-describing header (magic, version, record sizes, the run mode and the mode the frames were produced in, endianness marker), with fault events as tagged records in the same stream and rotation at record boundaries. Format version 3 names every byte of both records, so a `static_assert` proves the layout has no padding, and keeps every version-2 byte where it was: readers accept versions 1 to 3, read an older frame as a prefix with a zero tail, and refuse anything else by name before a frame flows. Every frame carries its source's sequence number, which a replay reproduces. `LogReplaySource` strips a log back to raw readings and feeds the same pipeline; every record of the output log is byte-identical to the original's (same build, same platform, same format version), its header naming the replay, and a version-1 or version-2 log replays into a version-3 one.
- **Timing.** Absolute deadlines (`clock_nanosleep` with `TIMER_ABSTIME` on Linux), re-phase and count missed cycles on overrun, never burst to catch up. Jitter goes to a CSV per run so the telemetry log stays deterministic.
- **Configuration.** `config/telemetry_config.yaml` holds the 48 tunables (the period, the sensors' bus, addresses and IMU rate, the GPS receiver's switch, device and baud rate, the fault thresholds, the filter tuning, the log directory and size, the transport addresses, the simulator's seed and noise, and in an optional `adsb` block the ADS-B source's bounding box, reference point, poll interval, selection margin, miss tolerance and channel timeout; a file without the block is refused only by `--adsb`, so every configuration recorded before the block replays as recorded). It is loaded once at startup, every value checked and every problem named in one report, and handed to each module by constructor; nothing reads it after that, and nothing in the binary falls back to a built-in value. A copy is written beside the log, and a replay reads that copy, so a changed constant can never silently break replay identity.
- **Replay's end, and its pace.** A source says whether it has a next frame (`has_next()`, true forever for a sensor or the simulator), so a replay runs to the end of its log and never past it, and `--cycles` caps it. `--pace` releases each replayed frame at the wall time its recorded timestamp maps to, through the same `Clock` seam the scheduler uses, late frames released at once and never skipped: the paced log is byte-identical to the unpaced one, and the lateness goes to `logs/replay_jitter.csv`.
- **Session status.** Nineteen bytes under their own magic on each transport, never a log record: the run mode (LIVE, SIMULATED, REPLAY, ADSB), the mode the frames were produced in (a replay's from its log's header, so the page labels a replayed ADS-B recording's datum, identity and attribution as a live one's) and, in ADS-B mode, the selected aircraft's identity, before the first frame, once a second and at once when the aircraft changes, and ENDED three times after the last, so a consumer that joins late learns what it is looking at and a finished replay never looks like a lost link. The receiver reads it like any record and ends on ENDED.
- **Bridge and page.** `pipeline/bridge.py` forwards every frame and status to browser clients over a WebSocket as JSON by name, listening on the loopback unless told otherwise and never forwarding latitude or longitude. `dashboard/` is the display's first screen: the mode label, the feed's state, the rate, the last frame, each computed field with its validity by name beside the number, and each channel's status; staleness is judged from the page's own clock at arrival, and an ended stream is shown as ended, never stale.
- **Instruments.** `dashboard/src/pfd/` is the Primary Flight Display: an attitude indicator (the symbol fixed, the horizon moving: nose up moves it down, a right roll turns it counterclockwise), an altitude tape in feet, a vertical speed indicator in feet per minute, a ground speed readout in knots and a track indicator from the GPS (GS and TRK, never airspeed or heading: there is no pitot and no magnetometer, and the track is withheld below 1 m/s because a stationary receiver's course is noise), and a ground track plot relative to the first fix on a plain grid, never a map. One rule decides what every instrument shows: the value while its validity or channel status allows it, dashes for no data, and a flag naming the instrument in place of a value that cannot be trusted (FAILURE_WARNING, DEGRADED, a stale or lagging feed, a lost bridge; grey for an ended stream); nothing on an instrument is ever struck through, which the diagnostic view below keeps for itself. The lag, the page's elapsed time minus the frame's, catches what arrival time cannot: frames queued in the bridge, the socket or a throttled tab. Specified in `docs/display_spec.md` (FTS-DS-001); `--motion demo` scripts the simulator's 150 s flight for the replay demo.
- **ADS-B source.** `--adsb` presents one real aircraft from the OpenSky Network's REST API as the frame's GPS channel: latitude, longitude, the geometric altitude (a GNSS height above the WGS-84 ellipsoid, which the altitude tape labels `ft HAE`), the ground speed and the track, in the service's SI units, through the same seam as the sensors; the pressure altitude and vertical rate the broadcast carries are raw fields of their own, each under a presence bit, and no pressure is back-computed from an altitude. The request runs on the project's one thread (`PeriodicWorker`) and hands the loop one whole report under a mutex, latest wins, so the loop never waits on the network. Of the aircraft in the configured box (SFO and its 28L/28R final, public places), the source selects the nearest descending to the airport's reference point and keeps it with hysteresis (a 2 km margin, two tolerated misses), records every switch beside the log, announces it in the session status before the first frame of the new aircraft and flags that frame, on which the filters begin again, so a switch never reads as a climb; one state vector is one measurement, fresh on the cycle it first arrives and held with `gps_read_ok` false until the next poll, so the Kalman filter predicts between polls and never re-applies a reading. The credentials live in a file outside the repository (`--adsb-credentials`, mode 600, never a key of the configuration), travel only in the token request's body, and are never printed, logged or written; CI never calls the service, and the end-to-end test runs against a local stand-in.
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

On the display's branch, the ADS-B branch stacked on it, the voter's branch stacked on that and the fault-injection branch on the voter's (not yet on `main`): 485 unit tests, 90 fault-injection tests (the CI subset's 45 cells among them), 176 pytest and 84 page tests pass on the Mac; the CI runs on their pull requests are the measurement of record (FTS-VR-001 Entries 16 to 22).

Branch coverage is the gating figure. `scripts/coverage_check.sh` fails the build below 90% in `src/processing/` and `src/config/`, 80% in timing, logging, transport, and replay, 70% in drivers, and 80% overall. Every exclusion is listed with its justification in the verification results: the composition root `main.cpp`, Google Test's own sources and the test files themselves, the no-match edge of exhaustive `switch` statements, one `EINTR` retry loop, and six GCC exception-unwind edges marked per line. Numbers from a developer Mac (clang) are recorded as previews only, because the Pi's compiler family is GCC.

## DO-178C-aligned practices

| Document | ID | What it holds |
|---|---|---|
| [Requirements](docs/requirements.md) | FTS-SRD-001 v0.16.0 | 67 "shall" statements with rationale; the scope of record |
| [Design](docs/design.md) | FTS-DD-001 | software level, every architecture decision with its reason, log format, timing, open decisions |
| [Traceability matrix](docs/traceability_matrix.md) | FTS-TM-001 | requirement → code → test, forward and reverse; status per requirement (57 Verified, 10 Partial, 0 Not started) |
| [Fault model](docs/fault_model.md) | FTS-FM-001 | 24 failure modes (sensor, external source, processing lane, and one no check covers), each with cause, detection, response, and the test that proves it; what each one does to the validity of every computed field |
| [Test plan](docs/test_plan.md) | FTS-TP-001 | test levels including on-target hardware steps and the page's, pass/fail criteria, test cards TC-001 to TC-038, coverage thresholds and exclusion policy |
| [Verification results](docs/verification_results.md) | FTS-VR-001 | per-commit test and coverage records, justified exclusions, findings, the CI run of record |
| [Noise profile](docs/noise_profile.md) | FTS-NP-001 | the stationary-capture method, the estimators and their tolerances, which Kalman constants are measured and which are tuning choices, the capture's provenance |
| [Fault-injection report](docs/fault_injection_report.md) | FTS-FI-001 | the campaign's method, the predictions committed before its code, the measured latencies, rates and bounds against them, every gap a finding, the blind spots named |
| [Display specification](docs/display_spec.md) | FTS-DS-001 | each instrument's source field, rule, units, range, scale, update and flags; the feed's states; the ground track plot; the demo flight |

Every change follows the same loop: requirement, matrix row, code, test, matrix update, coverage and static analysis, one commit naming the requirement IDs.

**Limits, stated in the same breath.** Software level DAL C is assumed, not assigned by a certification authority, and nothing here is certified. Nothing here was reviewed or tested by a party independent of the development. No tool qualification under DO-330 was performed on the compiler, Google Test, gcov/lcov, or cppcheck. MC/DC coverage is out of scope; DAL C requires statement and decision coverage, and branch coverage is what the gate measures.

## Findings worth reading

Three findings, none of them caught by a unit test on a single machine, each written up in [verification_results.md](docs/verification_results.md):

1. **Unspecified padding in brace-built fault events (Entry 5 §5.2).** `FaultEvent` is 16 bytes with two padding bytes. The detector built events as braced temporaries; clang zero-fills them, GCC does not. Every local run passed. The first CI run on GCC failed the replay identity test because the live and replay detectors saw different stack contents in those two bytes. Fix: one construction site that value-initializes the struct, plus a test that reads the padding bytes through `offsetof` and asserts zero on any toolchain.
2. **Uninitialized padding in the simulator's frame (Entry 4 §4.3).** Same defect class one struct over, found by a `memcmp` test added when the requirement was reworded to say "raw". Two identical runs could have produced logs that differed in bytes carrying no data.
3. **Coverage tooling, not code (Entry 5 §5.5 to §5.8).** GCC keeps exception-unwind branches on conditional lines, which left `src/logging/` gated at 80.6% on noise. lcov's documented setting for removing them wiped all branch data on the runner's lcov 2.0 and turned CI red. The change was reverted, the runner reproduced in a container with the same toolchain, and the noise resolved with six per-line markers, each justified. Lesson recorded: test coverage tooling on the toolchain that produces the number of record.

## Build, test, run, replay

Requires CMake 3.16 or newer, a C++17 compiler (GCC or clang), the ZeroMQ C library (`brew install zeromq` on macOS, `sudo apt-get install libzmq3-dev` on Debian and Ubuntu) and libcurl (shipped with macOS; `sudo apt-get install libcurl4-openssl-dev` on Debian and Ubuntu). Google Test (v1.15.2), the cppzmq header wrapper (v4.11.0), yaml-cpp (0.9.0) and nlohmann/json (3.12.0, by the release archive's SHA-256) are fetched at configure time, pinned.

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

```bash
./build/telemetry                                   # 5 paced cycles on the simulated source, config/telemetry_config.yaml
./build/telemetry --config other.yaml               # the same with another file (every key required, every problem named)
./build/telemetry --replay logs/telemetry_000.bin   # the same pipeline fed from the log, unpaced, to its end, with the configuration recorded beside it
cmp logs/telemetry_000.bin logs/replay_000.bin 16 16   # prints nothing: every record byte-identical past the 16-byte header
cmp -l logs/telemetry_000.bin logs/replay_000.bin      # one line, 11 2 3: the header's run mode, SIMULATED against REPLAY
./build/telemetry --replay logs/telemetry_000.bin --pace   # the same at the log's recorded cadence; the log still byte-identical, lateness in logs/replay_jitter.csv
./build/telemetry --motion demo --cycles 7500              # the simulator's 150 s scripted flight (takeoff, climb, turn, descent): the demo's recording, 7,500 frames
./build/telemetry --live --cycles 250               # on the Pi: BMP280 and MPU6050 over the configured I2C bus, the GPS when sensors.gps.enabled, 5 s paced
./build/telemetry --adsb --cycles 15000 --zmq tcp://127.0.0.1:5555   # one real aircraft on approach to SFO from the OpenSky Network, five minutes, to the bridge and the page below
                                                    # (reads ~/.config/flight_telemetry/opensky_credentials.json, mode 600, or --adsb-credentials <file>; 60 credits)
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
cd dashboard && npm test -- --run && npm run build                      # 84 page tests (Vitest, jsdom), the TypeScript check and the bundle
```

```bash
.venv/bin/python -m pytest                                              # 176 tests in about 132 s: one simulated session, replay identity, the log reader, the noise oracles, both transports, the configuration file, validity and the format version, the replay's end and pace, the session status, the bridge, the scripted flight, the ADS-B run over a local stand-in, the voter, the lane fault and its replay, the campaign's statistics and report
.venv/bin/python -m pipeline.log_reader logs/                           # a run's rotated files as one sequence: counts, steps, freshness, events
.venv/bin/python -m analysis.noise_profile logs/ --figures docs/img --json noise.json   # per-channel noise, Allan deviation, the derived Kalman constants
```

```bash
./build/telemetry --motion demo --cycles 7500 --lane-fault 2@60   # the demo with lane 2 upset from 60 s: outvoted at once, excluded 40 ms later, the injection in the log
./build/telemetry --replay logs/telemetry_000.bin                 # its replay re-applies the recorded fault: every record equal to the recording's
./build/fault_campaign --out build/campaign/mine --ci             # the CI subset's 450 runs in about a second (--replicates <n> for more, --cells to choose)
./build/fault_campaign --seed 6900004                             # any run from its seed: its schedule, its outcome, both verdicts (--log <dir> for its binary log)
scripts/fault_campaign.sh                                         # the full campaign on a clean tree, about 2.5 minutes, FTS-FI-001's tables and figures regenerated
```

The live run prints each frame with the validity beside each computed value, writes `logs/telemetry_000.bin`, `logs/telemetry_jitter.csv` and `logs/telemetry_config.yaml` (the configuration it ran with, verbatim), and prints a timing summary, a validity summary (on how many frames every computed field was NORMAL, and the first) and the voter's (the frames, the lanes left in the vote, the frames with no majority, the voter's events). The replay names its input's format version, writes `logs/replay_000.bin` and `logs/replay_config.yaml`, runs to the end of its log unless `--cycles` caps it and says which ended it, and with `--pace` writes `logs/replay_jitter.csv` and prints its lateness. A live run adds a `GPS:` line (sentences, fixes, epochs without a fix, bad checksums and the rest of the receiver's counters) or says the receiver is disabled; a run with `--zmq`, `--udp`, or both adds one transport summary line per publisher (frames published, send failures, status messages); `--replay` with either publishes the log as a burst, or at its recorded cadence with `--pace`. The pytest suite, coverage, and static analysis run in CI on every push; the exact lcov and cppcheck invocations are in [.github/workflows/ci.yml](.github/workflows/ci.yml).

## Layout

```
src/            telemetry_frame, data_source, main.cpp
  drivers/      simulated_data, simulated_source, fault_injecting_source,
                i2c_bus, bmp280, mpu6050, serial_port, nmea, nmea_gps, live_sensor_source,
                adsb_config, http_client, curl_http_client, opensky_json, opensky_client, aircraft_selector, adsb_source,
                selection_log (the ADS-B source: the HTTP seam, the token client, selection with hysteresis, sample-and-hold)
  processing/   altitude, kalman_filter, complementary_filter, fault_detector, processor,
                voter, redundant_processor, lane_fault_hook (three lanes behind the detector, the majority, the lane-fault interface),
                lane_fault_injector (the lane faults the campaign and --lane-fault inject)
  logging/      log_format, binary_logger, log_reader
  replay/       log_replay_source, recorded_injections (the lane faults a replayed log records)
  timing/       clock, fixed_rate_scheduler, timestamp_pacer (a replay on its log's timestamps), jitter_log, periodic_worker (the project's one thread)
  transport/    publisher (the seam), zmq_publisher, udp_publisher, session_message (mode, end and the ADS-B aircraft's identity on the wire)
  config/       telemetry_config (the typed structs, the loader and its rules, the record's naming)
config/         telemetry_config.yaml, the configuration of record (48 keys, ten of them in the optional adsb block, one comment per section citing its requirement)
pipeline/       frame_format (the byte layout, shared by log and wire, and the session status), receiver (ZeroMQ SUB or UDP),
                log_reader (files, rotation, the numpy layout), bridge (the receiver to browser clients over a WebSocket)
dashboard/      the page: Vite, React and TypeScript; the feed hook, the diagnostic view, the instruments (src/pfd/), their Vitest tests
analysis/       noise_profile (per-channel noise, Allan deviation, the derived Kalman constants),
                campaign_stats and fault_campaign (the fault-injection campaign's statistics and report generator)
tests/unit/     one Google Test file per module
tests/fault_injection/  the campaign's engine (catalogue, seeds, runner, classifier, predictions), fault_campaign, its known answers and the CI subset
tests/integration/  pytest: replay identity, the log reader, the noise oracles, both transports, the configuration file, validity and the format version, the replay's end and pace, the session status, the bridge, the scripted flight, the ADS-B run over a local stand-in for the service, the voter, the lane fault, the campaign's statistics and report
docs/           the nine controlled documents above, and img/ for the report's figures
scripts/        coverage_check.sh, the per-scope branch threshold gate; fault_campaign.sh, the campaign on demand
```

## Roadmap

Planned, no dates: the GPS receiver's first fixes on the Pi and the board's own capture as a test fixture; the first stationary capture, with the measured barometric variance replacing its placeholder (the analysis is built and verified); the v0.2.0 demo video from the replay demo; the fault-injection campaign of record, rerun after the stationary capture's constants land; ADS-B Phase B, the broadcast's vertical rate as the filter's third measurement and the selection's altitude term.

## Data sources

The ADS-B mode reads live traffic from [The OpenSky Network](https://opensky-network.org) under its terms of use, which license the data for non-profit research and education. Nothing the service returns is committed: every fixture is synthetic in the service's schema, the logs of ADS-B sessions stay under `logs/` and are expunged when the project ends, and a recording made for publication runs the bridge with `--hide-identity`, since the license requires aircraft identifiers to be de-identified. The attended sessions this project runs, each bounded by `--cycles`, are its educational use; an unattended or operational use would need the network's written agreement. Work using the data cites:

Matthias Schäfer, Martin Strohmeier, Vincent Lenders, Ivan Martinovic and Matthias Wilhelm. *Bringing up OpenSky: A large-scale ADS-B sensor network for research.* ACM/IEEE International Conference on Information Processing in Sensor Networks (IPSN), April 2014.

## License

MIT, see [LICENSE](LICENSE).
