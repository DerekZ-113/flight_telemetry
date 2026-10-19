"""Bridge from the telemetry stream to a browser page (REQ-DISP-005; the consumer side of
REQ-TRANS-004, and the feed REQ-DISP-003, REQ-DISP-004 and REQ-DISP-006 judge).

    .venv/bin/python -m pipeline.bridge --endpoint tcp://127.0.0.1:5555
    .venv/bin/python -m pipeline.bridge --udp 127.0.0.1:5556 --listen 127.0.0.1:8765

One thread reads the receiver (FrameReceiver.recv() blocks with its timeout, exactly as
receiver.py's main does) and hands each record to the asyncio loop with call_soon_threadsafe,
the one place the two meet; the loop serves the WebSocket and broadcasts each record as one
JSON text message to every open connection. Push, not poll: the server writes the moment a
frame arrives, so the page's own arrival time means something, which is what it judges
staleness from (no frame carries an age). Every new client is greeted with `hello`: the stale
threshold and the period in milliseconds, the last session status if one has arrived, and the
receiver's counts, so a page opened mid-run knows the mode at once instead of at the next
status.

The bridge listens on the loopback unless --listen names another address: anything bound to
every interface is reachable by every host on the network, the browser is on the same machine,
and widening the bind is a conscious flag.

It never forwards latitude or longitude: screenshots and the demo video go public, and the one
place every frame passes is the right place for the rule. What the ground track plot gets instead
is a position relative to the first NOMINAL fresh fix the bridge saw, metres north and east
(RelativeOrigin); the origin itself never leaves this process, and a displacement reveals a
shape, not a place. The origin is re-taken after an ENDED and when a timestamp goes backwards (a
restarted binary that never said ENDED), the same events that re-take the page's lag origin
(REQ-DISP-006). Every other field goes out by name, with each validity and each channel status by
name beside the numbers, from the same tables the receiver prints from, so the page and the
receiver cannot disagree.

A value that is not a finite number goes out as null (REQ-DISP-005). JSON has no NaN and no
infinity: Python's encoder would write them as bare tokens, which the page's JSON.parse refuses
along with the whole message. Since FAULT-024 the detector declares such a reading OUT_OF_RANGE and
the raw field keeps the value, so the frame refused would be the very frame that carries the
verdict. A GPS fix is held until the next one, so a fix that is not a number would leave the page
STALE instead of showing the GPS DEGRADED. Null with the status or the validity beside it says what
happened. `wire_text` is the one encoder for every message, and its `allow_nan=False` turns a slip
into an exception here rather than a dropped frame there.

Two display thresholds ride `hello` beside the stale threshold, so the page has one source of
truth for what it judges: the ground speed under which the track is withheld (`--track-min-speed`,
REQ-DISP-002: a stationary receiver's course is noise) and the lag past which the instruments are
removed (`--lag-ms`, REQ-DISP-006).

In ADS-B mode the status names the aircraft the frames present (REQ-SENS-009), and the bridge
forwards it in every `session` message and in `hello` as `identity`, the address and the callsign
by name, or null when nothing is selected (REQ-DISP-007). A new aircraft is a new track: the origin
is re-taken at the status that announces it, which the binary sends before the first frame carrying
it. `--hide-identity` forwards null instead, for a recording made for publication: the data license
requires aircraft identifiers to be de-identified in anything published, and withholding them at the
source is honest where blurring a video is not; the origin rule still follows the hidden identity.

The bridge never ends on its own: a quiet source is a quiet source (the SUB socket reconnects
by itself when the binary restarts; a bound datagram socket just waits), and the page shows the
quiet as STALE. Ctrl-C prints a summary. Fault events never travel on the wire; one that did
would be counted and dropped here, not forwarded, since no requirement asks for it on the page.
"""
from __future__ import annotations

import argparse
import asyncio
import json
import math
import signal
import sys
import threading
import time
from typing import Any

from websockets.asyncio.server import ServerConnection, broadcast, serve

from pipeline import frame_format
from pipeline.frame_format import COMPUTED_VALIDITY, STATUS_NAMES, VALIDITY_NAMES, Frame, SessionStatus
from pipeline.receiver import DEFAULT_ENDPOINT, ByteSource, FrameReceiver, UdpSource, ZmqSource, parse_udp_destination

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8765
DEFAULT_STALE_MS = 1000     # fifty frames at 50 Hz, and the receiver's own quiet timeout
DEFAULT_PERIOD_MS = 20
DEFAULT_TRACK_MIN_SPEED_MPS = 1.0   # above a stationary receiver's Doppler noise, below a walking pace (REQ-DISP-002)
DEFAULT_LAG_MS = 500                # half the stale threshold, ten times any delay FTS-VR-001 Entry 16 measured (REQ-DISP-006)

# Metres per degree of latitude on a sphere of the mean Earth radius, and per degree of
# longitude at a latitude, that times its cosine: the twin of kMetersPerDegreeLatitude in
# src/drivers/motion_profile.h, so the ground track plot's scale is the simulator's script's.
# Within a few kilometres the approximation is under half a percent.
METERS_PER_DEGREE = 6_371_000 * math.pi / 180

# The frame fields that never cross the WebSocket (REQ-DISP-005).
WITHHELD_FIELDS = ("latitude", "longitude")


class RelativeOrigin:
    """The first NOMINAL fresh fix since the bridge started, since an ENDED or since a timestamp
    went backwards, and every later position relative to it in metres (REQ-DISP-005). The origin's
    coordinates never leave this object."""

    def __init__(self) -> None:
        self._latitude: float | None = None
        self._longitude: float | None = None
        self._metres_per_degree_east = METERS_PER_DEGREE

    @property
    def taken(self) -> bool:
        return self._latitude is not None

    def reset(self) -> None:
        self._latitude = None
        self._longitude = None

    def take_if_first_fix(self, frame: Frame) -> None:
        """The origin is a reading the detector judged good and the source marked fresh: a held
        value is the previous fix again, and NO_DATA has no fix at all."""
        if self.taken or STATUS_NAMES.get(frame.gps_status) != "NOMINAL" or not frame.gps_read_ok:
            return
        self._latitude = frame.latitude
        self._longitude = frame.longitude
        self._metres_per_degree_east = METERS_PER_DEGREE * math.cos(math.radians(frame.latitude))

    def relative(self, frame: Frame) -> dict[str, float] | None:
        """Metres east and north of the origin, or None before one exists."""
        if self._latitude is None or self._longitude is None:
            return None
        return {"east_m": (frame.longitude - self._longitude) * self._metres_per_degree_east,
                "north_m": (frame.latitude - self._latitude) * METERS_PER_DEGREE}


def finite_or_null(value: Any) -> Any:
    """The value with every float that is not a finite number replaced by None, through every dict
    and list in it (REQ-DISP-005)."""
    if isinstance(value, float):
        return value if math.isfinite(value) else None
    if isinstance(value, dict):
        return {key: finite_or_null(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [finite_or_null(item) for item in value]
    return value


def wire_text(message: dict[str, Any]) -> str:
    """A message as the WebSocket carries it: JSON with null for every value that is not a finite
    number, and allow_nan=False behind that, so a bare NaN or Infinity is never written."""
    return json.dumps(finite_or_null(message), allow_nan=False)


def frame_message(frame: Frame, received_at_ms: float, position: dict[str, float] | None = None) -> dict[str, Any]:
    """One frame as the page sees it: every field but the coordinates, with the validity of
    each computed field and the status of each channel by name beside the numbers, and the
    position relative to the first fix (None before one exists)."""
    fields = {name: value for name, value in frame._asdict().items() if name not in WITHHELD_FIELDS}
    validity = {field: VALIDITY_NAMES.get(getattr(frame, byte), str(getattr(frame, byte)))
                for field, byte in COMPUTED_VALIDITY.items()}
    status = {channel: STATUS_NAMES.get(getattr(frame, f"{channel}_status"), str(getattr(frame, f"{channel}_status")))
              for channel in ("baro", "imu", "gps")}
    return {"type": "frame", "received_at_ms": received_at_ms, "frame": fields, "validity": validity, "status": status,
            "position": position}


def identity_message(identity: tuple[str, str] | None) -> dict[str, str] | None:
    """The selected aircraft by name, or None: nothing selected, another mode, or hidden."""
    if identity is None:
        return None
    icao24, callsign = identity
    return {"icao24": icao24, "callsign": callsign}


def session_message(status: SessionStatus, received_at_ms: float, hide_identity: bool = False) -> dict[str, Any]:
    """A status as the page sees it: the run mode, the state, the identity unless hidden, and the
    source mode of the frames (REQ-LOG-007), by which the page labels a replay's identity, the
    tape's datum and the attribution (REQ-DISP-007)."""
    return {"type": "session", "mode": status.mode, "state": status.state,
            "identity": None if hide_identity else identity_message(status.identity),
            "source_mode": status.source_mode, "received_at_ms": received_at_ms}


class Bridge:
    """The receiver on one thread, the WebSocket server on the asyncio loop that starts it.

        async with Bridge(source, port=0) as bridge:
            host, port = bridge.address

    The reader thread is the only thread that touches the byte source; the loop is the only
    thread that touches the connections. The hand-off is call_soon_threadsafe, nothing else."""

    def __init__(self, source: ByteSource, *, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT,
                 period_ms: int = DEFAULT_PERIOD_MS, stale_ms: int = DEFAULT_STALE_MS,
                 track_min_speed_mps: float = DEFAULT_TRACK_MIN_SPEED_MPS, lag_ms: int = DEFAULT_LAG_MS,
                 hide_identity: bool = False) -> None:
        self.receiver = FrameReceiver(source, period_ms)
        self.host = host
        self.port = port
        self.period_ms = period_ms
        self.stale_ms = stale_ms
        self.track_min_speed_mps = track_min_speed_mps
        self.lag_ms = lag_ms
        self.hide_identity = hide_identity
        self._origin = RelativeOrigin()
        self._last_timestamp_ms: int | None = None
        self._last_identity: tuple[str, str] | None = None
        self._ended_seen = False
        self.forwarded = 0          # frames broadcast
        self.statuses = 0           # session statuses broadcast
        self.dropped = 0            # records of a kind the page has no use for (a fault event)
        self.format_errors = 0      # messages the receiver refused (a stray datagram, a version mismatch)
        self._connections: set[ServerConnection] = set()
        self._server = None
        self._loop: asyncio.AbstractEventLoop | None = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()

    @property
    def address(self) -> tuple[str, int]:
        """The address the server is bound to: what the kernel chose when the port was 0."""
        assert self._server is not None, "the bridge is not started"
        host, port = self._server.sockets[0].getsockname()[:2]
        return host, port

    @property
    def clients(self) -> int:
        return len(self._connections)

    async def start(self) -> None:
        self._loop = asyncio.get_running_loop()
        self._server = await serve(self._handle, self.host, self.port)
        self._thread = threading.Thread(target=self._read, name="bridge-reader", daemon=True)
        self._thread.start()

    async def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            # The thread is inside recv() for at most the source's timeout.
            await asyncio.get_running_loop().run_in_executor(None, self._thread.join)
        if self._server is not None:
            self._server.close()
            await self._server.wait_closed()
        self.receiver.close()

    async def __aenter__(self) -> Bridge:
        await self.start()
        return self

    async def __aexit__(self, *exc: object) -> None:
        await self.stop()

    def hello(self) -> dict[str, Any]:
        """What a client needs before its first frame (REQ-DISP-003, REQ-DISP-004)."""
        session = self.receiver.session
        return {"type": "hello", "stale_ms": self.stale_ms, "period_ms": self.period_ms,
                "session": ({"mode": session.mode, "state": session.state,
                             "identity": None if self.hide_identity else identity_message(session.identity),
                             "source_mode": session.source_mode}
                            if session is not None else None),
                "frames_received": self.receiver.received, "gaps": self.receiver.gaps,
                "out_of_order": self.receiver.out_of_order,
                "track_min_speed_mps": self.track_min_speed_mps, "lag_ms": self.lag_ms}

    # --- the reader thread --------------------------------------------------------------------

    def _read(self) -> None:
        assert self._loop is not None
        while not self._stop.is_set():
            try:
                record = self.receiver.recv()
            except frame_format.FormatError:
                # A stray datagram on the port, or a build this module cannot read: counted,
                # never fatal here, because the next message may be fine.
                self._loop.call_soon_threadsafe(self._count_format_error)
                continue
            if record is None:
                continue   # the source's timeout: nothing arrived, keep reading
            # The arrival time is taken here, on receipt, before the hand-off.
            self._loop.call_soon_threadsafe(self._deliver, record, time.monotonic() * 1000.0)

    # --- the loop thread -----------------------------------------------------------------------

    def _count_format_error(self) -> None:
        self.format_errors += 1

    def _deliver(self, record: Any, received_at_ms: float) -> None:
        if isinstance(record, Frame):
            self.forwarded += 1
            # A new session re-takes the origin (REQ-DISP-005) at the events that re-take the
            # page's lag origin (REQ-DISP-006): a frame after an ENDED, or a timestamp that goes
            # backwards, which is a restarted binary that never said ENDED.
            went_backwards = self._last_timestamp_ms is not None and record.timestamp_ms < self._last_timestamp_ms
            if self._ended_seen or went_backwards:
                self._origin.reset()
                self._ended_seen = False
            self._last_timestamp_ms = record.timestamp_ms
            self._origin.take_if_first_fix(record)
            message = frame_message(record, received_at_ms, self._origin.relative(record))
        elif isinstance(record, SessionStatus):
            self.statuses += 1
            self._ended_seen = self._ended_seen or record.state == "ENDED"
            # A new aircraft is a new track (REQ-DISP-007): the origin is re-taken at the status
            # that announces it, which precedes the first frame carrying it, hidden or not.
            if record.identity != self._last_identity:
                self._origin.reset()
                self._last_identity = record.identity
            message = session_message(record, received_at_ms, self.hide_identity)
        else:
            self.dropped += 1
            return
        # broadcast() writes to every open connection without waiting; a connection that has
        # closed is skipped. A slow page accumulates buffered data, which is its own problem:
        # the page judges staleness itself, and the bridge never slows the receiver for it.
        broadcast(self._connections, wire_text(message))

    async def _handle(self, connection: ServerConnection) -> None:
        self._connections.add(connection)
        try:
            await connection.send(wire_text(self.hello()))
            await connection.wait_closed()
        finally:
            self._connections.discard(connection)


def finite_number(text: str) -> float:
    """A number given on the command line that the page will compare against: a finite one, since a
    threshold that is not a number would cross as null (REQ-DISP-005) and compare as zero."""
    try:
        value = float(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"expected a number, got {text!r}") from None
    if not math.isfinite(value):
        raise argparse.ArgumentTypeError(f"expected a finite number, got {text!r}")
    return value


def parse_listen_address(text: str) -> tuple[str, int]:
    """HOST:PORT, split at the last colon; the port 0 lets the kernel choose."""
    host, _, port = text.rpartition(":")
    if not host or not port.isdigit():
        raise argparse.ArgumentTypeError(f"expected HOST:PORT, got {text!r}")
    return host, int(port)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Forward telemetry frames and session statuses to browser clients over a WebSocket.")
    where = parser.add_mutually_exclusive_group()
    where.add_argument("--endpoint", help=f"ZeroMQ publisher endpoint to connect to (default {DEFAULT_ENDPOINT})")
    where.add_argument("--udp", type=parse_udp_destination, metavar="HOST:PORT",
                       help="bind a UDP socket here and read the publisher's datagrams")
    parser.add_argument("--listen", type=parse_listen_address, metavar="HOST:PORT",
                        default=(DEFAULT_HOST, DEFAULT_PORT),
                        help=f"the WebSocket address (default {DEFAULT_HOST}:{DEFAULT_PORT}, the loopback: "
                             "any other host is reachable by every machine on the network)")
    parser.add_argument("--period-ms", type=int, default=DEFAULT_PERIOD_MS, help="frame period, for the gap count")
    parser.add_argument("--stale-ms", type=int, default=DEFAULT_STALE_MS,
                        help="how long without a frame before the page marks the feed stale")
    parser.add_argument("--track-min-speed", type=finite_number, default=DEFAULT_TRACK_MIN_SPEED_MPS, metavar="M/S",
                        help="the ground speed under which the page withholds the track (a stationary receiver's "
                             f"course is noise; default {DEFAULT_TRACK_MIN_SPEED_MPS})")
    parser.add_argument("--lag-ms", type=int, default=DEFAULT_LAG_MS,
                        help=f"the lag past which the page removes its instruments (default {DEFAULT_LAG_MS})")
    parser.add_argument("--hide-identity", action="store_true",
                        help="forward no aircraft identity, for a recording made for publication (the data "
                             "license requires de-identified aircraft)")
    args = parser.parse_args(argv)

    source: ByteSource
    if args.udp is not None:
        host, port = args.udp
        source = UdpSource(host, port, timeout_ms=1000)
    else:
        source = ZmqSource(args.endpoint or DEFAULT_ENDPOINT, timeout_ms=1000)
    listen_host, listen_port = args.listen

    async def run() -> None:
        async with Bridge(source, host=listen_host, port=listen_port, period_ms=args.period_ms,
                          stale_ms=args.stale_ms, track_min_speed_mps=args.track_min_speed,
                          lag_ms=args.lag_ms, hide_identity=args.hide_identity) as bridge:
            host, port = bridge.address
            print(f"bridge: reading {source.endpoint}, serving ws://{host}:{port} "
                  f"(stale {bridge.stale_ms} ms, lag {bridge.lag_ms} ms, track from {bridge.track_min_speed_mps} m/s; "
                  f"{'identity hidden; ' if bridge.hide_identity else ''}Ctrl-C to stop)", flush=True)
            stop = asyncio.Event()
            for signum in (signal.SIGINT, signal.SIGTERM):   # Ctrl-C, or a plain kill: both end with the summary
                asyncio.get_running_loop().add_signal_handler(signum, stop.set)
            await stop.wait()
            receiver = bridge.receiver
            session = (f"session {receiver.session.mode} {receiver.session.state.lower()}"
                       + (f" source {receiver.session.source_mode}"
                          if receiver.session.source_mode != receiver.session.mode else "")
                       if receiver.session is not None else "no session status received")
            print(f"\nforwarded {bridge.forwarded} frames and {bridge.statuses} statuses to {bridge.clients} client(s); "
                  f"received {receiver.received} frames, gaps {receiver.gaps} frames, "
                  f"out of order {receiver.out_of_order}, refused {bridge.format_errors}; {session}", flush=True)

    asyncio.run(run())
    return 0


if __name__ == "__main__":
    sys.exit(main())
