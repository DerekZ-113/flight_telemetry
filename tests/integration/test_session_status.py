"""TC-021 steps 5 through 8 at the integration level (REQ-TRANS-004; also REQ-TRANS-001,
REQ-TRANS-002, REQ-TEST-002).

The binary announces its mode before the first frame, repeats it about once a second and
reports the end after the last frame, on both transports; the receiver reads a status like any
record, keeps the latest, and ends on ENDED without waiting out its quiet timeout; the statuses
are not frames and change no byte of the log.
"""
from __future__ import annotations

import subprocess
import time
from pathlib import Path

import pytest

from conftest import CONFIG_ARGS, TRANSPORT_LINE, free_tcp_port, run_published
from pipeline import frame_format
from pipeline.frame_format import Frame, SessionStatus
from pipeline.log_reader import read_log
from pipeline.receiver import FrameReceiver, UdpSource
from test_transport import TRANSPORTS, make_receiver
from test_validity import raw_frames, write_replayable

PERIOD_MS = 20
STATUS_EVERY_MS = 1000                      # kStatusIntervalMs: a RUNNING status at least this often
CYCLES = 120                                # statuses before frames 0, 50 and 100, then ENDED
ENDED_REPEATS = 3                           # kEndedRepeats


def status_count(stdout: str, name: str) -> int:
    """The `status messages = N` figure from the binary's transport line for one transport."""
    for match in TRANSPORT_LINE.finditer(stdout):
        if match.group("name") == name:
            return int(match.group("status"))
    raise AssertionError(f"no Transport line for {name} in:\n{stdout[-600:]}")


# --- step 6: the eight bytes ------------------------------------------------------------------


def test_parse_message_reads_a_status():
    assert frame_format.parse_message(b"FTSS" + bytes([1, 2, 1, 0])) == SessionStatus("SIMULATED", "RUNNING")
    assert frame_format.parse_message(b"FTSS" + bytes([1, 3, 2, 0])) == SessionStatus("REPLAY", "ENDED")
    assert frame_format.parse_message(b"FTSS" + bytes([1, 1, 1, 0])) == SessionStatus("LIVE", "RUNNING")


def test_short_status_is_a_format_error():
    with pytest.raises(frame_format.FormatError, match="7"):
        frame_format.parse_message(b"FTSS" + bytes([1, 2, 1]))


def test_unknown_mode_or_state_is_a_format_error():
    with pytest.raises(frame_format.FormatError, match="mode 9"):
        frame_format.parse_message(b"FTSS" + bytes([1, 9, 1, 0]))
    with pytest.raises(frame_format.FormatError, match="state 0"):
        frame_format.parse_message(b"FTSS" + bytes([1, 2, 0, 0]))


def test_unknown_status_version_is_a_format_error():
    with pytest.raises(frame_format.FormatError, match="version 2"):
        frame_format.parse_message(b"FTSS" + bytes([2, 2, 1, 0]))


# --- the receiver ends on ENDED, not on quiet (in-process) ------------------------------------


class ListSource:
    """Scripted messages; after them, every recv() waits a quiet timeout and returns None."""

    endpoint = "list"

    def __init__(self, messages: list[bytes], quiet_s: float = 1.0) -> None:
        self.messages = list(messages)
        self.quiet_s = quiet_s

    def recv(self) -> bytes | None:
        if self.messages:
            return self.messages.pop(0)
        time.sleep(self.quiet_s)
        return None

    def close(self) -> None:
        pass


def test_receiver_keeps_the_latest_status_and_ends_on_ended():
    from synthetic_log import log_bytes, make_frame

    running = b"FTSS" + bytes([1, 3, 1, 0])
    ended = b"FTSS" + bytes([1, 3, 2, 0])
    messages = [running, log_bytes([make_frame(0)]), log_bytes([make_frame(1)]), ended, ended,
                log_bytes([make_frame(2)])]   # the frame after ENDED is never read
    receiver = FrameReceiver(ListSource(messages), PERIOD_MS)
    assert receiver.session is None
    start = time.perf_counter()
    records = list(receiver)
    elapsed = time.perf_counter() - start
    assert [type(r).__name__ for r in records] == ["SessionStatus", "Frame", "Frame", "SessionStatus"]
    assert receiver.session == SessionStatus("REPLAY", "ENDED")
    assert receiver.received == 2
    assert elapsed < 0.5, f"the receiver waited {elapsed:.2f} s instead of ending on ENDED"


# --- step 5: before the first frame, every second, and at the end, on each transport ----------


@pytest.mark.parametrize("transport", TRANSPORTS)
def test_status_before_first_frame_every_second_and_at_the_end(telemetry_bin, tmp_path, transport):
    receiver, flag = make_receiver(transport)
    run = run_published(telemetry_bin, [*flag, "--cycles", str(CYCLES)], tmp_path, receiver)
    assert run.returncode == 0, run.stdout[-400:]
    assert run.frames, "nothing received"
    assert run.statuses, "no status received"
    assert all(status.mode == "SIMULATED" for _, status in run.statuses)

    running = [n for n, status in run.statuses if status.state == "RUNNING"]
    ended = [n for n, status in run.statuses if status.state == "ENDED"]
    for n in running:
        assert n < len(run.frames), "a RUNNING status after the last frame"
        assert run.frames[n].timestamp_ms % STATUS_EVERY_MS == 0, f"RUNNING before frame t={run.frames[n].timestamp_ms}"
    assert ended and all(n == len(run.frames) for n in ended), "ENDED must follow the last frame"
    assert len(ended) <= ENDED_REPEATS
    if transport == "udp":
        assert running == [0, 50, 100], running          # a bound datagram socket has no join
        assert len(ended) == ENDED_REPEATS
    else:
        assert len(running) >= 2, running                 # the ones at 1 s and 2 s survive any join
    assert receiver.session == SessionStatus("SIMULATED", "ENDED")
    name = "ZeroMQ" if transport == "zmq" else "UDP"
    assert status_count(run.stdout, name) == 3 + ENDED_REPEATS
    print(f"[{transport}] RUNNING before frames {running}, ENDED x{len(ended)} after frame {len(run.frames) - 1}")


def test_receiver_iteration_ends_at_ended_not_at_quiet(telemetry_bin, tmp_path):
    source = UdpSource("127.0.0.1", 0, timeout_ms=1000)
    receiver = FrameReceiver(source, PERIOD_MS)
    with open(tmp_path / "stdout.txt", "w") as out:
        process = subprocess.Popen([str(telemetry_bin), *CONFIG_ARGS, "--udp", f"127.0.0.1:{source.port}",
                                    "--cycles", "60"], cwd=tmp_path, stdout=out, stderr=subprocess.STDOUT)
        try:
            last_frame_at = None
            for record in receiver:
                if isinstance(record, Frame):
                    last_frame_at = time.perf_counter()
            ended_at = time.perf_counter()
        finally:
            process.wait(timeout=60)
            receiver.close()
    assert process.returncode == 0
    assert receiver.received == 60
    assert receiver.session == SessionStatus("SIMULATED", "ENDED")
    assert last_frame_at is not None
    assert ended_at - last_frame_at < 0.5, "the iteration waited out the quiet timeout instead of ending on ENDED"


# --- step 7: a replay says REPLAY ----------------------------------------------------------------


def test_replay_announces_replay(telemetry_bin, tmp_path):
    source = write_replayable(tmp_path / "log", raw_frames(60))
    cwd = tmp_path / "run"
    cwd.mkdir()
    receiver, flag = make_receiver("zmq")
    run = run_published(telemetry_bin, ["--replay", str(source), *flag], cwd, receiver)
    assert run.returncode == 0, run.stdout[-400:]
    assert run.statuses and all(status.mode == "REPLAY" for _, status in run.statuses)
    assert run.statuses[-1][1].state == "ENDED"


# --- step 8: a status is not a frame: the log is unchanged by publishing ----------------------


def test_statuses_change_no_byte_of_the_log(telemetry_bin, tmp_path):
    logs = []
    for name, flag in (("plain", []), ("published", ["--zmq", f"tcp://127.0.0.1:{free_tcp_port()}"])):
        cwd = tmp_path / name
        cwd.mkdir()
        completed = subprocess.run([str(telemetry_bin), *CONFIG_ARGS, *flag, "--cycles", str(CYCLES)], cwd=cwd,
                                   capture_output=True, text=True, timeout=60)
        assert completed.returncode == 0, completed.stderr
        logs.append((cwd / "logs" / "telemetry_000.bin").read_bytes())
    assert logs[0] == logs[1]
    assert len(read_log(tmp_path / "plain" / "logs" / "telemetry_000.bin").frames) == CYCLES
