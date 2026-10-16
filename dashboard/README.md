# Dashboard: the Primary Flight Display

A browser page that shows frames arriving from `pipeline/bridge.py`: the run mode, the feed's state, the rate, the last frame, each computed field with its validity by name, and each channel's status. It judges staleness from its own arrival clock (REQ-DISP-003), shows no mode until the session status names one and never labels the simulator LIVE (REQ-DISP-004), names the aircraft after the label in ADS-B mode and says `ID UNAVAILABLE` in a replay (REQ-DISP-007), and has no slot for a coordinate (REQ-DISP-005). On that feed, `src/pfd/` draws the instruments (REQ-DISP-001, REQ-DISP-002): an attitude indicator, an altitude tape, a vertical speed indicator, a ground speed readout, a track indicator and a ground track plot relative to the first fix, each showing a value only while its validity or channel status allows it and a flag naming the instrument otherwise, with the lag check (REQ-DISP-006) beside the stale one. The specification is `docs/display_spec.md` (FTS-DS-001); the diagnostic view of part 1 stays below the panel, the one place a flagged or stale value is shown struck through.

## Run

```bash
# the binary, anywhere it can reach: the Mac or the Pi
./build/telemetry --zmq tcp://127.0.0.1:5555 --cycles 1500
./build/telemetry --replay logs/telemetry_000.bin --pace --zmq tcp://127.0.0.1:5555
./build/telemetry --motion demo --cycles 7500          # the simulator's scripted flight: record it, then replay it with the line above (the demo)
./build/telemetry --adsb --cycles 15000 --zmq tcp://127.0.0.1:5555   # one real aircraft on approach to SFO (the OpenSky Network): ADS-B with its callsign, the tape in ft HAE

# the bridge, on the machine the browser runs on (loopback by default)
.venv/bin/python -m pipeline.bridge --endpoint tcp://127.0.0.1:5555

# the page
cd dashboard && npm ci && npm run dev        # http://127.0.0.1:5173, ?bridge=ws://host:port for a bridge elsewhere
```

## Test and build

```bash
cd dashboard && npm test -- --run            # Vitest with jsdom: the hook, the page and the instruments (73 tests)
npm test -- --run --coverage                 # the same with line and branch coverage
npm run build                                # the TypeScript check, then the production bundle in dist/
```

## The bridge's messages

Every message is one JSON text frame. `src/feed.ts` types them and mirrors `pipeline/frame_format.py` and `pipeline/bridge.py`; a renamed field fails the build here.

| `type` | Carries |
|---|---|
| `hello` | first on every connection: `stale_ms`, `period_ms`, the last `session` or `null`, the receiver's `frames_received`, `gaps`, `out_of_order`; `track_min_speed_mps` and `lag_ms`, the two display thresholds (REQ-DISP-002, REQ-DISP-006) |
| `session` | `mode` (`LIVE`, `SIMULATED`, `REPLAY`, `ADSB`), `state` (`RUNNING`, `ENDED`), `identity` (`{icao24, callsign}` in ADS-B mode while an aircraft is selected, otherwise `null`; always `null` when the bridge runs with `--hide-identity`), `received_at_ms` |
| `frame` | `frame` (every field of the 104-byte frame but `latitude` and `longitude`), `validity` (each computed field's name: `NOT_JUDGED`, `NO_COMPUTED_DATA`, `FAILURE_WARNING`, `NORMAL`), `status` (each channel's: `NOMINAL`, `DEGRADED`, `NO_DATA`), `received_at_ms`, `position` (`east_m` and `north_m` relative to the first fix, or `null` before one) |

`received_at_ms` is the bridge's monotonic clock; the page judges staleness from its own clock at arrival, since the two machines share no clock and no frame carries an age.

## Layout

```
src/feed.ts              the message types and the socket seam
src/useTelemetryFeed.ts  the hook: socket, arrival time, rate, staleness, the lag and its origins, the trail, reconnect
src/badge.ts             the feed's one-word state
src/App.tsx, App.css     the page: the panel, the plot, the diagnostic view
src/main.tsx             the entry; ?bridge= for a bridge elsewhere
src/pfd/units.ts         feet, feet per minute and knots by exact constants; the readouts' rounding
src/pfd/instrumentState.ts  what an instrument shows; the track's speed rule; the strip's words
src/pfd/geometry.ts      the panel's geometry as pure functions: the horizon, the tapes, the rose, the plot
src/pfd/Flag.tsx         the flag naming an instrument
src/pfd/AttitudeIndicator.tsx, AltitudeTape.tsx, VerticalSpeed.tsx, GroundSpeed.tsx, TrackIndicator.tsx, GroundTrackPlot.tsx  the instruments
src/pfd/Pfd.tsx          the panel: the strip and the instruments
src/fakeSocket.ts        the tests' socket
src/testMessages.ts      the tests' messages, built as the bridge builds them
src/**/*.test.ts(x)      Vitest, jsdom, React Testing Library, fake timers
```
