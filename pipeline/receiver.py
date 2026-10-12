"""ZeroMQ subscriber for the telemetry stream (REQ-TRANS-001; the consumer side of REQ-TEST-002).

Run it before the publisher:

    .venv/bin/python -m pipeline.receiver --endpoint tcp://127.0.0.1:5555
    ./build/telemetry --zmq tcp://127.0.0.1:5555 --cycles 500

FrameReceiver is the reusable part; the dashboard server will drive it the
way main() does. Every message is parsed with pipeline.frame_format, the
module that also describes the log on disk, so the wire and the file cannot
drift apart silently: the header on each message is checked against this
module's own layout before a single field is read.

Loss is counted here, not at the publisher. A PUB socket drops silently when
this side falls behind its high-water mark, and a late subscriber never sees
what was sent before it subscribed (the slow-joiner effect). Consecutive
frame timestamps should differ by one period; a larger step means frames
were missed, and the count is reported at exit.
"""
from __future__ import annotations

import argparse
import sys
from typing import Iterator

import zmq

from pipeline import frame_format
from pipeline.frame_format import FaultEvent, Frame


class FrameReceiver:
    """A SUB socket that yields parsed records and counts what it missed."""

    def __init__(self, endpoint: str, period_ms: int = 20, timeout_ms: int = 1000) -> None:
        self.endpoint = endpoint
        self.period_ms = period_ms
        self.received = 0          # frames parsed
        self.gaps = 0              # frames missing between consecutive timestamps
        self._last_timestamp: int | None = None
        self._context = zmq.Context()
        self._socket = self._context.socket(zmq.SUB)
        self._socket.setsockopt(zmq.RCVTIMEO, timeout_ms)   # recv() gives up instead of hanging
        # A subscriber started before the publisher retries its connect on
        # this interval; the library default of 100 ms would miss the first
        # frames of a run that starts right after it (TC-005 step 1).
        self._socket.setsockopt(zmq.RECONNECT_IVL, 10)
        self._socket.setsockopt(zmq.SUBSCRIBE, b"")         # empty prefix: every message
        self._socket.connect(endpoint)

    def close(self) -> None:
        self._socket.close(linger=0)
        self._context.term()

    def __enter__(self) -> FrameReceiver:
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def recv(self) -> Frame | FaultEvent | None:
        """The next record, or None when nothing arrived within the timeout.

        Raises frame_format.FormatError if a message does not match this
        module's layout; that is a version mismatch, not a transient."""
        try:
            raw = self._socket.recv()
        except zmq.Again:
            return None
        record = frame_format.parse_message(raw)
        if isinstance(record, Frame):
            self._account(record.timestamp_ms)
        return record

    def _account(self, timestamp_ms: int) -> None:
        self.received += 1
        if self._last_timestamp is not None:
            step = timestamp_ms - self._last_timestamp
            if step > self.period_ms:
                self.gaps += step // self.period_ms - 1
        self._last_timestamp = timestamp_ms

    def __iter__(self) -> Iterator[Frame | FaultEvent]:
        """Records until the stream goes quiet for one timeout."""
        while True:
            record = self.recv()
            if record is None:
                return
            yield record


def format_frame(frame: Frame) -> str:
    status = " ".join(
        f"{name}={frame_format.STATUS_NAMES.get(value, value)}"
        for name, value in (("baro", frame.baro_status), ("imu", frame.imu_status), ("gps", frame.gps_status))
    )
    return (f"t={frame.timestamp_ms:7d} ms  alt {frame.fused_altitude_m:8.2f} m  "
            f"vs {frame.vertical_speed_mps:6.2f} m/s  pitch {frame.pitch_deg:6.2f}  roll {frame.roll_deg:6.2f} deg  {status}")


def format_event(event: FaultEvent) -> str:
    return (f"[FAULT] t={event.timestamp_ms} ms channel={frame_format.CHANNEL_NAMES.get(event.channel, event.channel)} "
            f"type={frame_format.FAULT_TYPE_NAMES.get(event.type, event.type)} value={event.value}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Print telemetry frames published by `telemetry --zmq`.")
    parser.add_argument("--endpoint", default="tcp://127.0.0.1:5555", help="publisher endpoint to connect to")
    parser.add_argument("--period-ms", type=int, default=20, help="frame period, for the gap count")
    parser.add_argument("--timeout-ms", type=int, default=1000, help="quiet time after the last frame that ends the run")
    args = parser.parse_args(argv)

    with FrameReceiver(args.endpoint, args.period_ms, args.timeout_ms) as receiver:
        print(f"waiting for a publisher on {args.endpoint} (Ctrl-C to stop)", flush=True)
        try:
            while True:
                try:
                    record = receiver.recv()
                except frame_format.FormatError as e:
                    print(f"format error: {e}", file=sys.stderr)
                    return 2
                if record is None:
                    if receiver.received > 0:
                        break          # the stream went quiet: the publisher is done
                    continue           # nothing yet: keep waiting for the publisher to start
                print(format_frame(record) if isinstance(record, Frame) else format_event(record), flush=True)
        except KeyboardInterrupt:
            pass
        print(f"received {receiver.received} frames, gaps {receiver.gaps} frames", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
