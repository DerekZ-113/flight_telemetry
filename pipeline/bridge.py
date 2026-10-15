"""Bridge from the telemetry stream to a browser page (REQ-DISP-005; the consumer side of
REQ-TRANS-004, and the feed REQ-DISP-003 and REQ-DISP-004 judge).

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
place every frame passes is the right place for the rule. Until an instrument needs a position
none is forwarded; a position relative to the first fix comes with that instrument. Every other
field goes out by name, with each validity and each channel status by name beside the numbers,
from the same tables the receiver prints from, so the page and the receiver cannot disagree.

The bridge never ends on its own: a quiet source is a quiet source (the SUB socket reconnects
by itself when the binary restarts; a bound datagram socket just waits), and the page shows the
quiet as STALE. Ctrl-C prints a summary. Fault events never travel on the wire; one that did
would be counted and dropped here, not forwarded, since no requirement asks for it on the page.
"""
from __future__ import annotations

import argparse
import asyncio
import json
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

# The frame fields that never cross the WebSocket (REQ-DISP-005).
WITHHELD_FIELDS = ("latitude", "longitude")


def frame_message(frame: Frame, received_at_ms: float) -> dict[str, Any]:
    """One frame as the page sees it: every field but the coordinates, with the validity of
    each computed field and the status of each channel by name beside the numbers."""
    fields = {name: value for name, value in frame._asdict().items() if name not in WITHHELD_FIELDS}
    validity = {field: VALIDITY_NAMES.get(getattr(frame, byte), str(getattr(frame, byte)))
                for field, byte in COMPUTED_VALIDITY.items()}
    status = {channel: STATUS_NAMES.get(getattr(frame, f"{channel}_status"), str(getattr(frame, f"{channel}_status")))
              for channel in ("baro", "imu", "gps")}
    return {"type": "frame", "received_at_ms": received_at_ms, "frame": fields, "validity": validity, "status": status}


def session_message(status: SessionStatus, received_at_ms: float) -> dict[str, Any]:
    return {"type": "session", "mode": status.mode, "state": status.state, "received_at_ms": received_at_ms}


class Bridge:
    """The receiver on one thread, the WebSocket server on the asyncio loop that starts it.

        async with Bridge(source, port=0) as bridge:
            host, port = bridge.address

    The reader thread is the only thread that touches the byte source; the loop is the only
    thread that touches the connections. The hand-off is call_soon_threadsafe, nothing else."""

    def __init__(self, source: ByteSource, *, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT,
                 period_ms: int = DEFAULT_PERIOD_MS, stale_ms: int = DEFAULT_STALE_MS) -> None:
        self.receiver = FrameReceiver(source, period_ms)
        self.host = host
        self.port = port
        self.period_ms = period_ms
        self.stale_ms = stale_ms
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
                "session": {"mode": session.mode, "state": session.state} if session is not None else None,
                "frames_received": self.receiver.received, "gaps": self.receiver.gaps,
                "out_of_order": self.receiver.out_of_order}

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
            message = frame_message(record, received_at_ms)
        elif isinstance(record, SessionStatus):
            self.statuses += 1
            message = session_message(record, received_at_ms)
        else:
            self.dropped += 1
            return
        # broadcast() writes to every open connection without waiting; a connection that has
        # closed is skipped. A slow page accumulates buffered data, which is its own problem:
        # the page judges staleness itself, and the bridge never slows the receiver for it.
        broadcast(self._connections, json.dumps(message))

    async def _handle(self, connection: ServerConnection) -> None:
        self._connections.add(connection)
        try:
            await connection.send(json.dumps(self.hello()))
            await connection.wait_closed()
        finally:
            self._connections.discard(connection)


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
                          stale_ms=args.stale_ms) as bridge:
            host, port = bridge.address
            print(f"bridge: reading {source.endpoint}, serving ws://{host}:{port} (Ctrl-C to stop)", flush=True)
            stop = asyncio.Event()
            for signum in (signal.SIGINT, signal.SIGTERM):   # Ctrl-C, or a plain kill: both end with the summary
                asyncio.get_running_loop().add_signal_handler(signum, stop.set)
            await stop.wait()
            receiver = bridge.receiver
            session = (f"session {receiver.session.mode} {receiver.session.state.lower()}"
                       if receiver.session is not None else "no session status received")
            print(f"\nforwarded {bridge.forwarded} frames and {bridge.statuses} statuses to {bridge.clients} client(s); "
                  f"received {receiver.received} frames, gaps {receiver.gaps} frames, "
                  f"out of order {receiver.out_of_order}, refused {bridge.format_errors}; {session}", flush=True)

    asyncio.run(run())
    return 0


if __name__ == "__main__":
    sys.exit(main())
