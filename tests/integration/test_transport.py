"""TC-005 and TC-010 steps 6 and 7 at the integration level, over both transports
(REQ-TRANS-001, REQ-TRANS-002, REQ-TRANS-003, REQ-TEST-002).

The receiver is pipeline.receiver's FrameReceiver, driven in-process with the matching
source; the binary runs as a subprocess with its stdout in a file (a pipe nobody drains
fills at 64 KB and blocks the loop, which would fail the timing assertions for the wrong
reason). The receiver is always started first: a datagram socket has no join, and a SUB
socket retries its connect until the publisher binds (every 1 ms here, 10 ms as shipped),
so the subscription precedes frame 0 by a margin. Pub/sub still has no join handshake, so
the ZeroMQ assertions run from the first frame received and bound the join to the first
five frames; UDP asserts every frame.
"""
from __future__ import annotations

import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest

from conftest import CONFIG_ARGS, TIMING_LINE, TRANSPORT_LINE, free_tcp_port, run_published
from pipeline.frame_format import FRAME, Frame
from pipeline.log_reader import read_log
from pipeline.receiver import FrameAccounting, FrameReceiver, UdpSource, ZmqSource
from synthetic_log import log_bytes, make_frame

TRANSPORTS = ("zmq", "udp")
PERIOD_MS = 20


def make_receiver(transport: str, receive_buffer_bytes: int | None = None):
    """A receiver started before the binary, and the flag the binary needs to reach it."""
    if transport == "udp":
        source = UdpSource("127.0.0.1", 0, timeout_ms=1000, receive_buffer_bytes=receive_buffer_bytes)
        return FrameReceiver(source), ["--udp", f"127.0.0.1:{source.port}"]
    port = free_tcp_port()
    source = ZmqSource(f"tcp://127.0.0.1:{port}", timeout_ms=1000, reconnect_ivl_ms=1)
    return FrameReceiver(source), ["--zmq", f"tcp://127.0.0.1:{port}"]


def frame_bytes_by_timestamp(log_path: Path) -> dict[int, bytes]:
    log = read_log(log_path)
    return {frame.timestamp_ms: raw for frame, raw in zip(log.frames, log.frame_bytes)}


def assert_byte_identical_to_log(frames: list[Frame], log_path: Path) -> None:
    """TC-005 step 7: every received frame repacks to the logged frame with its timestamp."""
    expected = frame_bytes_by_timestamp(log_path)
    for frame in frames:
        assert frame.timestamp_ms in expected, f"t={frame.timestamp_ms} was received but never logged"
        assert FRAME.pack(*frame) == expected[frame.timestamp_ms], f"t={frame.timestamp_ms} differs from the log"


def transport_summary(stdout: str, name: str) -> tuple[int, int]:
    """(published, send failures) from the binary's exit line for one transport."""
    for match in TRANSPORT_LINE.finditer(stdout):
        if match.group("name") == name:
            return int(match.group("published")), int(match.group("failures"))
    raise AssertionError(f"no Transport line for {name} in:\n{stdout[-600:]}")


# --- the accounting, without a binary (TC-005 step 6, TC-010 step 6 semantics) --------------


def test_accounting_counts_nothing_on_the_grid():
    accounting = FrameAccounting(PERIOD_MS)
    for timestamp in range(0, 200, PERIOD_MS):
        accounting.account(timestamp)
    assert (accounting.received, accounting.gaps, accounting.out_of_order) == (10, 0, 0)


def test_accounting_counts_missing_frames_from_the_step():
    # Floor division: a step under two periods is jitter, a step of k periods is k - 1 missing.
    accounting = FrameAccounting(PERIOD_MS)
    for timestamp in (0, 39, 80, 140):
        accounting.account(timestamp)
    assert accounting.gaps == 0 + 1 + 2
    assert accounting.out_of_order == 0


def test_accounting_counts_a_late_frame_once_each_way():
    # 40 arrives after 60: counted as a gap when missing, as out of order when it arrives;
    # the baseline stays at 60, so 80 is judged against its real neighbour.
    accounting = FrameAccounting(PERIOD_MS)
    for timestamp in (0, 20, 60, 40, 80):
        accounting.account(timestamp)
    assert (accounting.received, accounting.gaps, accounting.out_of_order) == (5, 1, 1)


def test_receiver_parses_and_ends_when_the_source_goes_quiet():
    class ListSource:
        endpoint = "list"

        def __init__(self, messages: list[bytes]) -> None:
            self.messages = messages

        def recv(self) -> bytes | None:
            return self.messages.pop(0) if self.messages else None

        def close(self) -> None:
            pass

    messages = [log_bytes([make_frame(i)]) for i in (0, 1, 3)]   # one message per frame, frame 2 lost
    receiver = FrameReceiver(ListSource(messages), PERIOD_MS)
    frames = list(receiver)
    assert [f.timestamp_ms for f in frames] == [0, 20, 60]
    assert (receiver.received, receiver.gaps, receiver.out_of_order) == (3, 1, 0)


# --- TC-005: every frame, in order, byte-identical to the log -------------------------------


@pytest.mark.parametrize("transport", TRANSPORTS)
def test_every_frame_arrives_in_order_and_matches_the_log(telemetry_bin, tmp_path, transport):
    receiver, flag = make_receiver(transport)
    run = run_published(telemetry_bin, [*flag, "--cycles", "200"], tmp_path, receiver)
    assert run.returncode == 0, run.stdout[-400:]
    assert run.frames, "nothing received"

    join = run.frames[0].timestamp_ms // PERIOD_MS
    if transport == "udp":
        assert join == 0, "a bound datagram socket has no join; frame 0 must arrive"
    else:
        assert join <= 5, f"the subscription reached the publisher only at frame {join}"
    assert [f.timestamp_ms for f in run.frames] == list(range(join * PERIOD_MS, 4000, PERIOD_MS))
    assert (receiver.gaps, receiver.out_of_order) == (0, 0)
    assert len(run.frames) == 200 - join
    assert_byte_identical_to_log(run.frames, run.log)
    assert transport_summary(run.stdout, "ZeroMQ" if transport == "zmq" else "UDP") == (200, 0)
    print(f"[{transport}] join index {join}, received {len(run.frames)}, {run.elapsed_s:.2f} s")


# --- TC-010 step 6: a receiver that stalls for 3 s -------------------------------------------


@pytest.mark.parametrize("transport", TRANSPORTS)
def test_stalled_receiver_never_blocks_the_loop(telemetry_bin, tmp_path, transport):
    receiver, flag = make_receiver(transport, receive_buffer_bytes=4096 if transport == "udp" else None)

    def stall_after_fifty(received: int) -> None:
        if received == 50:
            time.sleep(3.0)   # 150 frames go by while nothing is read

    run = run_published(telemetry_bin, [*flag, "--cycles", "500"], tmp_path, receiver, on_frame=stall_after_fifty)
    assert run.returncode == 0, run.stdout[-400:]
    assert transport_summary(run.stdout, "ZeroMQ" if transport == "zmq" else "UDP") == (500, 0)

    timing = TIMING_LINE.search(run.stdout)
    assert timing is not None, run.stdout[-400:]
    missed = int(timing.group("missed"))
    assert missed < 75, f"{missed} missed cycles: a blocked 3 s send would cost 150"

    timestamps = [f.timestamp_ms for f in run.frames]
    assert timestamps == sorted(set(timestamps)), "a frame arrived out of order or twice"
    assert receiver.out_of_order == 0
    jumps = [i for i in range(1, len(timestamps)) if timestamps[i] - timestamps[i - 1] != PERIOD_MS]
    assert len(jumps) <= 1, f"more than one hole in the stream: {jumps[:5]}"
    assert timestamps[-1] == 9980, "the frames after the stall did not arrive"
    assert_byte_identical_to_log(run.frames, run.log)

    if transport == "udp" and sys.platform == "linux":
        # The kernel's buffer limit is strict on Linux: a 4 KB buffer holds a handful of the
        # 150 stalled frames. macOS sometimes holds hundreds, so there the count is recorded.
        assert receiver.gaps >= 50, f"only {receiver.gaps} frames dropped during the stall"
    print(f"[{transport}] stall: received {len(run.frames)}, gaps {receiver.gaps}, "
          f"missed cycles {missed}, {run.elapsed_s:.2f} s")


# --- TC-010 step 7: no receiver at all ---------------------------------------------------------


@pytest.mark.parametrize("transport", TRANSPORTS)
def test_no_receiver_counts_and_continues(telemetry_bin, tmp_path, transport):
    if transport == "udp":
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as gone:
            gone.bind(("127.0.0.1", 0))
            flag = ["--udp", f"127.0.0.1:{gone.getsockname()[1]}"]   # bound once, now nobody's
    else:
        flag = ["--zmq", f"tcp://127.0.0.1:{free_tcp_port()}"]
    completed = subprocess.run([str(telemetry_bin), *CONFIG_ARGS, *flag, "--cycles", "50"], cwd=tmp_path,
                               capture_output=True, text=True, timeout=60)
    assert completed.returncode == 0, completed.stderr
    assert len(read_log(tmp_path / "logs" / "telemetry_000.bin").frames) == 50
    published, failures = transport_summary(completed.stdout, "ZeroMQ" if transport == "zmq" else "UDP")
    assert published + failures == 50
    if transport == "zmq":
        assert failures == 0   # a PUB socket with nobody to send to refuses nothing
    print(f"[{transport}] no receiver: published {published}, refused by the kernel {failures}")


# --- startup refusals (REQ-TRANS-003: refused at startup, never inside the loop) -------------


@pytest.mark.parametrize("flag, value, names", [
    ("--udp", "not-an-address", "<ipv4>:<port>"),
    ("--udp", "127.0.0.1:99999", "port"),
    ("--udp", "0.0.0.0:5556", "unicast"),
    ("--udp", "localhost:5556", "dotted-quad"),
    ("--zmq", "not-an-endpoint", "cannot bind"),
])
def test_bad_destination_is_refused_at_startup(telemetry_bin, tmp_path, flag, value, names):
    completed = subprocess.run([str(telemetry_bin), *CONFIG_ARGS, flag, value, "--cycles", "5"], cwd=tmp_path,
                               capture_output=True, text=True, timeout=60)
    assert completed.returncode == 1
    assert names in completed.stderr, completed.stderr
    log = tmp_path / "logs" / "telemetry_000.bin"
    assert not log.exists() or len(read_log(log).frames) == 0, "the loop ran after a refused destination"


# --- both transports at once -------------------------------------------------------------------


def test_both_transports_at_once(telemetry_bin, tmp_path):
    zmq_receiver, zmq_flag = make_receiver("zmq")
    udp_source = UdpSource("127.0.0.1", 0, timeout_ms=1000)   # 100 datagrams wait in the kernel's buffer
    run = run_published(telemetry_bin, [*zmq_flag, "--udp", f"127.0.0.1:{udp_source.port}", "--cycles", "100"],
                        tmp_path, zmq_receiver)
    udp_receiver = FrameReceiver(udp_source)
    udp_frames = [record for record in udp_receiver if isinstance(record, Frame)]   # the statuses of TC-021 ride the same socket
    udp_receiver.close()

    assert run.returncode == 0, run.stdout[-400:]
    join = run.frames[0].timestamp_ms // PERIOD_MS
    assert join <= 5
    assert [f.timestamp_ms for f in run.frames] == list(range(join * PERIOD_MS, 2000, PERIOD_MS))
    assert [f.timestamp_ms for f in udp_frames] == list(range(0, 2000, PERIOD_MS))
    assert_byte_identical_to_log(run.frames, run.log)
    assert_byte_identical_to_log(udp_frames, run.log)
    assert transport_summary(run.stdout, "ZeroMQ") == (100, 0)
    assert transport_summary(run.stdout, "UDP") == (100, 0)
