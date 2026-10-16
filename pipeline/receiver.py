"""Receiver for the telemetry stream over either transport (REQ-TRANS-001, REQ-TRANS-002;
the consumer side of REQ-TEST-002).

Run it before the publisher:

    .venv/bin/python -m pipeline.receiver --endpoint tcp://127.0.0.1:5555
    ./build/telemetry --zmq tcp://127.0.0.1:5555 --cycles 500

    python3 -m pipeline.receiver --udp 127.0.0.1:5556
    ./build/telemetry --udp 127.0.0.1:5556 --cycles 500

FrameReceiver is the reusable part; the dashboard server will drive it the
way main() does, and the pytest drives it in-process. Every message is parsed
with pipeline.frame_format, the module that also describes the log on disk,
so the wire and the file cannot drift apart silently: the header on each
message is checked against this module's own layout before a single field
is read. The two transports differ only in how the bytes arrive, so each is
a source with one recv(); the parsing and the accounting are shared.

Loss is counted here, not at the publisher. A PUB socket drops silently when
this side falls behind its high-water mark, a late subscriber never sees
what was sent before it subscribed (the slow-joiner effect), and a datagram
socket acknowledges nothing: a full receive buffer or a packet lost on the
wire is invisible to the sender. Consecutive frame timestamps should differ
by one period; a larger step means frames were missed, a step of zero or
less means a frame arrived out of order (a datagram can), and both counts
are reported at exit. The UDP mode is standard library only, so it runs on
the Pi without pyzmq.

The session status (REQ-TRANS-004) rides the same socket: the binary says which
mode its frames are in (LIVE, SIMULATED, REPLAY, ADSB) before the first frame,
about once a second after it and at once when the aircraft an ADS-B run
presents changes, and says ENDED after the last. The receiver reads it like
any record, keeps the latest in `session`, prints it with the aircraft's
identity when there is one, and ends on ENDED at once instead of waiting a
quiet timeout, so a finished publisher no longer looks like a lost link.
"""
from __future__ import annotations

import argparse
import socket
import sys
from typing import Iterator, Protocol

from pipeline import frame_format
from pipeline.frame_format import FaultEvent, Frame, SessionStatus

DEFAULT_ENDPOINT = "tcp://127.0.0.1:5555"


class ByteSource(Protocol):
    """Where the bytes come from: one message per recv(), or None after the timeout."""

    endpoint: str

    def recv(self) -> bytes | None: ...

    def close(self) -> None: ...


class ZmqSource:
    """A SUB socket connected to the publisher's endpoint (REQ-TRANS-001)."""

    def __init__(self, endpoint: str, timeout_ms: int = 1000, reconnect_ivl_ms: int = 10) -> None:
        import zmq   # here, not at module level: the UDP mode must not need it

        self.endpoint = endpoint
        self._zmq = zmq
        self._context = zmq.Context()
        self._socket = self._context.socket(zmq.SUB)
        self._socket.setsockopt(zmq.RCVTIMEO, timeout_ms)   # recv() gives up instead of hanging
        # A subscriber started before the publisher retries its connect on
        # this interval; the library default of 100 ms would miss the first
        # frames of a run that starts right after it (TC-005 step 1). The
        # pytest passes 1 ms so the subscription precedes frame 0 by a margin.
        self._socket.setsockopt(zmq.RECONNECT_IVL, reconnect_ivl_ms)
        self._socket.setsockopt(zmq.SUBSCRIBE, b"")         # empty prefix: every message
        self._socket.connect(endpoint)

    def recv(self) -> bytes | None:
        try:
            return self._socket.recv()
        except self._zmq.Again:
            return None

    def close(self) -> None:
        self._socket.close(linger=0)
        self._context.term()


class UdpSource:
    """A datagram socket bound where the publisher sends (REQ-TRANS-002).

    A datagram socket has no join: everything sent after the bind arrives,
    so the receiver is started first and nothing else needs to settle."""

    def __init__(self, host: str, port: int, timeout_ms: int = 1000,
                 receive_buffer_bytes: int | None = None) -> None:
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        if receive_buffer_bytes is not None:
            # The kernel's queue is the UDP analog of the high-water mark; the
            # stall test (TC-010 step 6) makes it small so a stall drops.
            self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer_bytes)
        self._socket.settimeout(timeout_ms / 1000)
        self._socket.bind((host, port))
        self.port = self._socket.getsockname()[1]   # the kernel's choice when port was 0
        self.endpoint = f"{host}:{self.port}"

    def recv(self) -> bytes | None:
        try:
            data, _ = self._socket.recvfrom(65535)
        except socket.timeout:
            return None
        return data

    def close(self) -> None:
        self._socket.close()


class FrameAccounting:
    """What arrived and what did not, from the timestamps alone."""

    def __init__(self, period_ms: int = 20) -> None:
        self.period_ms = period_ms
        self.received = 0          # frames parsed
        self.gaps = 0              # frames missing between consecutive timestamps
        self.out_of_order = 0      # frames whose timestamp did not advance
        self._last_timestamp: int | None = None

    def account(self, timestamp_ms: int) -> None:
        self.received += 1
        if self._last_timestamp is None:
            self._last_timestamp = timestamp_ms
            return
        step = timestamp_ms - self._last_timestamp
        if step <= 0:
            # A late frame: counted as a gap when it was missing, counted here
            # when it arrives; the baseline stays at the newest timestamp, so
            # the frames after it are judged against the right neighbour.
            self.out_of_order += 1
            return
        if step > self.period_ms:
            self.gaps += step // self.period_ms - 1
        self._last_timestamp = timestamp_ms


class FrameReceiver:
    """A byte source that yields parsed records and counts what it missed."""

    def __init__(self, source: ByteSource, period_ms: int = 20) -> None:
        self.source = source
        self.accounting = FrameAccounting(period_ms)
        self.session: SessionStatus | None = None   # the latest status, None until one arrives

    @property
    def received(self) -> int:
        return self.accounting.received

    @property
    def gaps(self) -> int:
        return self.accounting.gaps

    @property
    def out_of_order(self) -> int:
        return self.accounting.out_of_order

    def close(self) -> None:
        self.source.close()

    def __enter__(self) -> FrameReceiver:
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def recv(self) -> Frame | FaultEvent | SessionStatus | None:
        """The next record, or None when nothing arrived within the timeout.

        Raises frame_format.FormatError if a message does not match this
        module's layout; that is a version mismatch, or a stray datagram on
        the port, not a transient."""
        raw = self.source.recv()
        if raw is None:
            return None
        record = frame_format.parse_message(raw)
        if isinstance(record, Frame):
            self.accounting.account(record.timestamp_ms)
        elif isinstance(record, SessionStatus):
            self.session = record
        return record

    @property
    def ended(self) -> bool:
        """Whether the publisher has said ENDED (REQ-TRANS-004)."""
        return self.session is not None and self.session.state == "ENDED"

    def __iter__(self) -> Iterator[Frame | FaultEvent | SessionStatus]:
        """Records until the publisher says ENDED or the stream goes quiet for one timeout."""
        while True:
            record = self.recv()
            if record is None:
                return
            yield record
            if self.ended:
                return


def format_frame(frame: Frame) -> str:
    """One line per frame: each computed value with its validity beside it (REQ-PROC-007), then
    the channel statuses. A consumer reads the name next to the number, never the number alone."""
    status = " ".join(
        f"{name}={frame_format.STATUS_NAMES.get(value, value)}"
        for name, value in (("baro", frame.baro_status), ("imu", frame.imu_status), ("gps", frame.gps_status))
    )
    validity = frame_format.VALIDITY_NAMES
    return (f"t={frame.timestamp_ms:7d} ms  "
            f"alt {frame.fused_altitude_m:8.2f} m [{validity.get(frame.fused_altitude_validity, frame.fused_altitude_validity)}]  "
            f"vs {frame.vertical_speed_mps:6.2f} m/s [{validity.get(frame.vertical_speed_validity, frame.vertical_speed_validity)}]  "
            f"pitch {frame.pitch_deg:6.2f}  roll {frame.roll_deg:6.2f} deg "
            f"[{validity.get(frame.pitch_validity, frame.pitch_validity)}/{validity.get(frame.roll_validity, frame.roll_validity)}]  "
            f"{status}")


def format_identity(identity: tuple[str, str] | None) -> str:
    """The selected aircraft by callsign and address, or none."""
    if identity is None:
        return "none"
    icao24, callsign = identity
    return f"{callsign or '-'} ({icao24})"


def format_session(status: SessionStatus) -> str:
    line = f"session: mode={status.mode} state={status.state}"
    if status.mode == "ADSB":
        line += f" aircraft={format_identity(status.identity)}"
    return line


def format_event(event: FaultEvent) -> str:
    return (f"[FAULT] t={event.timestamp_ms} ms channel={frame_format.CHANNEL_NAMES.get(event.channel, event.channel)} "
            f"type={frame_format.FAULT_TYPE_NAMES.get(event.type, event.type)} value={event.value}")


def parse_udp_destination(text: str) -> tuple[str, int]:
    """HOST:PORT, split at the last colon."""
    host, _, port = text.rpartition(":")
    if not host or not port.isdigit():
        raise argparse.ArgumentTypeError(f"expected HOST:PORT, got {text!r}")
    return host, int(port)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Print telemetry frames published by `telemetry --zmq` or `telemetry --udp`.")
    where = parser.add_mutually_exclusive_group()
    where.add_argument("--endpoint", help=f"ZeroMQ publisher endpoint to connect to (default {DEFAULT_ENDPOINT})")
    where.add_argument("--udp", type=parse_udp_destination, metavar="HOST:PORT",
                       help="bind a UDP socket here and read the publisher's datagrams")
    parser.add_argument("--period-ms", type=int, default=20, help="frame period, for the gap count")
    parser.add_argument("--timeout-ms", type=int, default=1000, help="quiet time after the last frame that ends the run")
    args = parser.parse_args(argv)

    source: ByteSource
    if args.udp is not None:
        host, port = args.udp
        source = UdpSource(host, port, args.timeout_ms)
        waiting = f"listening for datagrams on {source.endpoint}"
    else:
        source = ZmqSource(args.endpoint or DEFAULT_ENDPOINT, args.timeout_ms)
        waiting = f"waiting for a publisher on {source.endpoint}"

    with FrameReceiver(source, args.period_ms) as receiver:
        print(f"{waiting} (Ctrl-C to stop)", flush=True)
        try:
            while True:
                try:
                    record = receiver.recv()
                except frame_format.FormatError as e:
                    print(f"format error: {e}", file=sys.stderr)
                    return 2
                if record is None:
                    if receiver.received > 0:
                        break          # the stream went quiet: the publisher is gone
                    continue           # nothing yet: keep waiting for the publisher to start
                if isinstance(record, Frame):
                    print(format_frame(record), flush=True)
                elif isinstance(record, SessionStatus):
                    print(format_session(record), flush=True)
                    if receiver.ended:
                        break          # the publisher said so: no quiet timeout to wait out
                else:
                    print(format_event(record), flush=True)
        except KeyboardInterrupt:
            pass
        session = (f"session {receiver.session.mode} {receiver.session.state.lower()}"
                   + (f" aircraft {format_identity(receiver.session.identity)}" if receiver.session.mode == "ADSB" else "")
                   if receiver.session is not None else "no session status received")
        print(f"received {receiver.received} frames, gaps {receiver.gaps} frames, "
              f"out of order {receiver.out_of_order}; {session}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
