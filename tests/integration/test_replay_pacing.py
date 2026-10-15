"""TC-019 steps 6 and 7 and TC-020 steps 6 through 9 at the integration level (REQ-LOG-006,
REQ-TIME-004; also REQ-LOG-003, REQ-LOG-004, REQ-TEST-002).

A replay ends at the end of its log whatever `--cycles` says, `--cycles` still caps it, and
the run says which happened (TC-019). A paced replay (`--pace`) releases each frame at its
recorded time and writes a log byte-identical to the unpaced one (TC-020).
"""
from __future__ import annotations

from pathlib import Path

from pipeline.log_reader import read_log
from test_config import run
from test_validity import raw_frames, write_replayable

REPLAY_LINE_END = "Replay:    {n} frames, end of log reached"
REPLAY_LINE_CAPPED = "Replay:    {n} frames, stopped by --cycles before the end of the log"


def replay(telemetry_bin, cwd: Path, source: Path, *args: str):
    """Replay `source` from `cwd` (its configuration record sits beside it); the stdout and
    the replay log the run wrote under cwd/logs."""
    cwd.mkdir(parents=True, exist_ok=True)
    completed = run(telemetry_bin, cwd, "--replay", str(source), *args)
    assert completed.returncode == 0, completed.stderr
    return completed.stdout, cwd / "logs" / "replay_000.bin"


# --- TC-019 step 6: the end of the log, the cap, and the default -----------------------------


def test_replay_stops_at_the_end_of_the_log(telemetry_bin, tmp_path):
    source = write_replayable(tmp_path / "short", raw_frames(5))
    stdout, log = replay(telemetry_bin, tmp_path / "run", source, "--cycles", "12")
    assert REPLAY_LINE_END.format(n=5) in stdout, stdout[-400:]
    assert len(read_log(log).frames) == 5, "a cycle ran past the last logged frame"


def test_cycles_caps_a_replay_and_says_so(telemetry_bin, tmp_path):
    source = write_replayable(tmp_path / "short", raw_frames(5))
    stdout, log = replay(telemetry_bin, tmp_path / "run", source, "--cycles", "3")
    assert REPLAY_LINE_CAPPED.format(n=3) in stdout, stdout[-400:]
    assert len(read_log(log).frames) == 3


def test_replay_without_cycles_runs_to_the_end(telemetry_bin, tmp_path):
    source = write_replayable(tmp_path / "short", raw_frames(5))
    stdout, log = replay(telemetry_bin, tmp_path / "run", source)
    assert REPLAY_LINE_END.format(n=5) in stdout, stdout[-400:]
    assert len(read_log(log).frames) == 5


# --- TC-019 step 7: stopping at the end and being capped there are the same bytes ------------


def test_cap_at_the_end_and_past_it_are_the_same_bytes(telemetry_bin, tmp_path):
    source = write_replayable(tmp_path / "short", raw_frames(5))
    logs = []
    for name, cycles in (("past", "12"), ("exact", "5")):
        stdout, log = replay(telemetry_bin, tmp_path / name, source, "--cycles", cycles)
        assert REPLAY_LINE_END.format(n=5) in stdout, stdout[-400:]
        logs.append(log)
    assert logs[0].read_bytes() == logs[1].read_bytes()
    timestamps = [frame.timestamp_ms for frame in read_log(logs[0]).frames]
    assert timestamps == sorted(set(timestamps)) == list(range(0, 100, 20)), "a timestamp repeats"
