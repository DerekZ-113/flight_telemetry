"""TC-021 steps 5 through 8 at the integration level (REQ-TRANS-004; also REQ-TRANS-001,
REQ-TRANS-002, REQ-TEST-002).

The binary announces its mode before the first frame, repeats it about once a second and
reports the end after the last frame, on both transports; the receiver reads a status like any
record, keeps the latest, and ends on ENDED without waiting out its quiet timeout; the statuses
are not frames and change no byte of the log. The nineteen bytes carry the selected aircraft's
identity in ADS-B mode since version 2 and the source mode of REQ-LOG-007 since version 3, in the
byte version 2 reserved (TC-021 step 6, TC-031 step 10); the ADS-B run itself is test_adsb.py.
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


# --- step 6: the nineteen bytes ---------------------------------------------------------------


def status_bytes(mode: int, state: int, identity: bytes = bytes(11), version: int = 3,
                 source: int | None = None) -> bytes:
    """A status as the binary writes one: the magic, the version, the mode, the state, the source
    mode (the mode itself unless given; a replay names its recording's) and the eleven identity bytes
    (zero: nothing selected, or another mode)."""
    return b"FTSS" + bytes([version, mode, state, mode if source is None else source]) + identity


IDENTITY = bytes.fromhex("000001") + b"TEST001 "   # the address as three bytes, the callsign space-padded


def test_parse_message_reads_a_status():
    assert frame_format.parse_message(status_bytes(2, 1)) == SessionStatus("SIMULATED", "RUNNING", None, "SIMULATED")
    assert frame_format.parse_message(status_bytes(3, 2, source=2)) == \
        SessionStatus("REPLAY", "ENDED", None, "SIMULATED")
    assert frame_format.parse_message(status_bytes(1, 1)) == SessionStatus("LIVE", "RUNNING", None, "LIVE")
    assert frame_format.parse_message(status_bytes(4, 1, IDENTITY)) == \
        SessionStatus("ADSB", "RUNNING", ("000001", "TEST001"), "ADSB")
    # An aircraft without a callsign is eight spaces on the wire: an identity, not none.
    assert frame_format.parse_message(status_bytes(4, 1, bytes.fromhex("000003") + b" " * 8)) == \
        SessionStatus("ADSB", "RUNNING", ("000003", ""), "ADSB")
    # The source mode (REQ-LOG-007, TC-031 step 10): a replay of an ADS-B recording, and of a log older
    # than format version 3, whose source nobody recorded.
    assert frame_format.parse_message(status_bytes(3, 1, source=4)) == SessionStatus("REPLAY", "RUNNING", None, "ADSB")
    assert frame_format.parse_message(status_bytes(3, 1, source=0)).source_mode == "UNKNOWN"


def test_short_status_is_a_format_error():
    with pytest.raises(frame_format.FormatError, match="8 bytes"):
        frame_format.parse_message(b"FTSS" + bytes([1, 2, 1, 0]))   # a version-1 status: eight bytes


def test_unknown_mode_or_state_is_a_format_error():
    with pytest.raises(frame_format.FormatError, match="mode 9"):
        frame_format.parse_message(status_bytes(9, 1))
    with pytest.raises(frame_format.FormatError, match="state 0"):
        frame_format.parse_message(status_bytes(2, 0))
    with pytest.raises(frame_format.FormatError, match="source mode 9"):
        frame_format.parse_message(status_bytes(3, 1, source=9))
    with pytest.raises(frame_format.FormatError, match="mode 0"):   # a run never names itself UNKNOWN
        frame_format.parse_message(status_bytes(0, 1, source=2))


def test_unknown_status_version_is_a_format_error():
    # Version 3 only: nothing persists a status, so a mismatched pair fails by name (version 2 had a
    # reserved zero where version 3 has the source mode).
    for version in (1, 2, 4):
        with pytest.raises(frame_format.FormatError, match=f"version {version}"):
            frame_format.parse_message(status_bytes(2, 1, version=version))


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

    running = status_bytes(3, 1, source=2)    # the replay of a simulated recording
    ended = status_bytes(3, 2, source=2)
    messages = [running, log_bytes([make_frame(0)]), log_bytes([make_frame(1)]), ended, ended,
                log_bytes([make_frame(2)])]   # the frame after ENDED is never read
    receiver = FrameReceiver(ListSource(messages), PERIOD_MS)
    assert receiver.session is None
    start = time.perf_counter()
    records = list(receiver)
    elapsed = time.perf_counter() - start
    assert [type(r).__name__ for r in records] == ["SessionStatus", "Frame", "Frame", "SessionStatus"]
    assert receiver.session == SessionStatus("REPLAY", "ENDED", None, "SIMULATED")
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
    assert all(status.mode == status.source_mode == "SIMULATED" for _, status in run.statuses)

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
    assert receiver.session == SessionStatus("SIMULATED", "ENDED", None, "SIMULATED")
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
    assert receiver.session == SessionStatus("SIMULATED", "ENDED", None, "SIMULATED")
    assert last_frame_at is not None
    assert ended_at - last_frame_at < 0.5, "the iteration waited out the quiet timeout instead of ending on ENDED"


# --- step 7: a replay says REPLAY ----------------------------------------------------------------


@pytest.mark.parametrize("version, recorded", [(3, "SIMULATED"), (2, "UNKNOWN")])
def test_replay_announces_replay(telemetry_bin, tmp_path, version, recorded):
    # An unpaced replay is a burst: on the CI runner all 60 frames and every status went out
    # before a ZeroMQ subscriber's join could land (the slow-joiner effect, FTS-DD-001 Section 9),
    # and the receiver saw nothing. A bound datagram socket has no join, so the burst is read whole.
    log = write_replayable(tmp_path / "log", raw_frames(60), version=version)
    cwd = tmp_path / "run"
    cwd.mkdir()
    receiver, flag = make_receiver("udp")
    run = run_published(telemetry_bin, ["--replay", str(log), *flag], cwd, receiver)
    assert run.returncode == 0, run.stdout[-400:]
    assert len(run.frames) == 60, "the burst must arrive whole on a bound datagram socket"
    assert run.statuses and all(status.mode == "REPLAY" for _, status in run.statuses)
    # The recording's source mode from its header (REQ-LOG-007): SIMULATED, or UNKNOWN for a log
    # older than format version 3, never a guess from the frames (TC-031 step 13).
    assert all(status.source_mode == recorded for _, status in run.statuses)
    assert [n for n, status in run.statuses if status.state == "RUNNING"] == [0, 50]
    assert [n for n, status in run.statuses if status.state == "ENDED"] == [60] * ENDED_REPEATS


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
