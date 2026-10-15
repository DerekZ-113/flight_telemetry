# Dashboard: the Primary Flight Display's data path (part 1)

A browser page that shows frames arriving from `pipeline/bridge.py`: the run mode, the feed's state, the rate, the last frame, each computed field with its validity by name, and each channel's status. It judges staleness from its own arrival clock (REQ-DISP-003), shows no mode until the session status names one and never labels the simulator LIVE (REQ-DISP-004), and has no slot for a coordinate (REQ-DISP-005). Part 2 builds the instruments on this feed.

## Run

```bash
# the binary, anywhere it can reach: the Mac or the Pi
./build/telemetry --zmq tcp://127.0.0.1:5555 --cycles 1500
./build/telemetry --replay logs/telemetry_000.bin --pace --zmq tcp://127.0.0.1:5555

# the bridge, on the machine the browser runs on (loopback by default)
.venv/bin/python -m pipeline.bridge --endpoint tcp://127.0.0.1:5555

# the page
cd dashboard && npm ci && npm run dev        # http://127.0.0.1:5173, ?bridge=ws://host:port for a bridge elsewhere
```

## Test and build

```bash
cd dashboard && npm test -- --run            # Vitest with jsdom: the hook and the page
npm test -- --run --coverage                 # the same with line and branch coverage
npm run build                                # the TypeScript check, then the production bundle in dist/
```

## The bridge's messages

Every message is one JSON text frame. `src/feed.ts` types them and mirrors `pipeline/frame_format.py` and `pipeline/bridge.py`; a renamed field fails the build here.

| `type` | Carries |
|---|---|
| `hello` | first on every connection: `stale_ms`, `period_ms`, the last `session` or `null`, the receiver's `frames_received`, `gaps`, `out_of_order` |
| `session` | `mode` (`LIVE`, `SIMULATED`, `REPLAY`), `state` (`RUNNING`, `ENDED`), `received_at_ms` |
| `frame` | `frame` (every field of the 104-byte frame but `latitude` and `longitude`), `validity` (each computed field's name: `NOT_JUDGED`, `NO_COMPUTED_DATA`, `FAILURE_WARNING`, `NORMAL`), `status` (each channel's: `NOMINAL`, `DEGRADED`, `NO_DATA`), `received_at_ms` |

`received_at_ms` is the bridge's monotonic clock; the page judges staleness from its own clock at arrival, since the two machines share no clock and no frame carries an age.

## Layout

```
src/feed.ts              the message types and the socket seam
src/useTelemetryFeed.ts  the hook: socket, arrival time, rate, staleness, reconnect
src/App.tsx, App.css     the view
src/fakeSocket.ts        the tests' socket
src/testMessages.ts      the tests' messages, built as the bridge builds them
src/*.test.ts(x)         Vitest, jsdom, React Testing Library, fake timers
```
