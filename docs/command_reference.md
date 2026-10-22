# Flight Telemetry System: command reference

This is a reference, not a controlled document: it carries no FTS identifier, and the usage text `print_usage()` prints in `src/main.cpp` is the source of truth for every flag. It holds what the README held until 2026-10-20 (the `Build, test, run, replay` section, moved here verbatim so that the README stays short) and the three configurations CI builds.

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
cd dashboard && npm test -- --run && npm run build                      # 89 page tests (Vitest, jsdom), the TypeScript check and the bundle
```

```bash
.venv/bin/python -m pytest                                              # 186 tests in about 134 s: one simulated session, replay identity, a replay of any log under --config, the log reader, the noise oracles, both transports, the configuration file, validity and the format version, the replay's end and pace, the session status, the bridge, the scripted flight, the ADS-B run over a local stand-in, the voter, the lane fault and its replay, the campaign's statistics and report
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

The live run prints each frame with the validity beside each computed value, writes `logs/telemetry_000.bin`, `logs/telemetry_jitter.csv` and `logs/telemetry_config.yaml` (the configuration it ran with, verbatim), and prints a timing summary, a validity summary (on how many frames every computed field was NORMAL, and the first) and the voter's (the frames, the lanes left in the vote, the frames with no majority, the voter's events). The replay names its input's format version, writes `logs/replay_000.bin` and `logs/replay_config.yaml`, runs to the end of its log unless `--cycles` caps it and says which ended it, and with `--pace` writes `logs/replay_jitter.csv` and prints its lateness. A live run adds a `GPS:` line (sentences, fixes, epochs without a fix, bad checksums and the rest of the receiver's counters) or says the receiver is disabled; a run with `--zmq`, `--udp`, or both adds one transport summary line per publisher (frames published, send failures, status messages); `--replay` with either publishes the log as a burst, or at its recorded cadence with `--pace`. The pytest suite, coverage, and static analysis run in CI on every push to `main`, every pull request into `main` and every `v*` tag, never on a push to another branch alone; the exact lcov and cppcheck invocations are in [.github/workflows/ci.yml](.github/workflows/ci.yml).

## The three configurations CI builds

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DWARNINGS_AS_ERRORS=ON -DENABLE_COVERAGE=ON     # the coverage build: ctest, pytest, lcov, the gate, cppcheck (REQ-TEST-003, REQ-TEST-004)
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DWARNINGS_AS_ERRORS=ON -DENABLE_SANITIZERS=ON   # the sanitized build: ctest under AddressSanitizer and UndefinedBehaviorSanitizer (REQ-TEST-007)
cd dashboard && npm ci && npm test -- --run --coverage && npm run build                      # the page job (REQ-DISP-003, REQ-DISP-004)
```
