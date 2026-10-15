"""TC-019 steps 6 and 7 and TC-020 steps 6 through 9 at the integration level (REQ-LOG-006,
REQ-TIME-004; also REQ-LOG-003, REQ-LOG-004, REQ-TEST-002).

A replay ends at the end of its log whatever `--cycles` says, `--cycles` still caps it, and
the run says which happened (TC-019). A paced replay (`--pace`) releases each frame at its
recorded time and writes a log byte-identical to the unpaced one (TC-020).
"""
from __future__ import annotations

import re
import time
from pathlib import Path

import numpy as np

from conftest import CONFIG_ARGS, run_published
from pipeline.log_reader import read_log
from test_config import run
from test_transport import assert_byte_identical_to_log, make_receiver
from test_validity import raw_frames, write_replayable

PERIOD_MS = 20
PACED_CYCLES = 150   # 3 s at 20 ms: long enough to measure, short enough for the suite
PACED_TIMING_LINE = re.compile(r"^Timing:\s+(?P<frames>\d+) frames paced to the log's timestamps, "
                               r"lateness min/mean/max = (?P<min>-?[\d.]+) / (?P<mean>-?[\d.]+) / (?P<max>-?[\d.]+) us, "
                               r"late by a period or more = (?P<late>\d+)", re.M)

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


# --- TC-020 step 6: paced and unpaced are the same bytes, at different speeds ------------------


def test_paced_replay_is_byte_identical_to_unpaced(telemetry_bin, tmp_path, sim_session):
    elapsed = {}
    logs = {}
    for name, extra in (("paced", ["--pace"]), ("unpaced", [])):
        start = time.perf_counter()
        _, log = replay(telemetry_bin, tmp_path / name, sim_session.log, "--cycles", str(PACED_CYCLES), *extra)
        elapsed[name] = time.perf_counter() - start
        logs[name] = log
    assert logs["paced"].read_bytes() == logs["unpaced"].read_bytes(), "pacing changed the output"
    original = read_log(sim_session.log)
    paced = read_log(logs["paced"])
    assert len(paced.frames) == PACED_CYCLES
    assert paced.frame_bytes == original.frame_bytes[:PACED_CYCLES]
    expected_s = PACED_CYCLES * PERIOD_MS / 1000
    assert elapsed["paced"] > 0.9 * expected_s, f"the paced replay took {elapsed['paced']:.2f} s, not about {expected_s:.1f}"
    assert elapsed["unpaced"] < 1.0, f"the unpaced replay took {elapsed['unpaced']:.2f} s"
    print(f"paced {elapsed['paced']:.2f} s, unpaced {elapsed['unpaced']:.2f} s for {PACED_CYCLES} frames")


# --- TC-020 step 7: lateness recorded under the replay's prefix, never early ------------------


def test_paced_replay_records_lateness_by_prefix(telemetry_bin, tmp_path, sim_session):
    stdout, _ = replay(telemetry_bin, tmp_path / "run", sim_session.log, "--pace", "--cycles", str(PACED_CYCLES))
    logs = tmp_path / "run" / "logs"
    assert (logs / "replay_jitter.csv").exists()
    assert not (logs / "telemetry_jitter.csv").exists(), "a replay must not write a live run's timing file"
    rows = np.loadtxt(logs / "replay_jitter.csv", delimiter=",", skiprows=1)
    assert rows.shape == (PACED_CYCLES, 5)
    lateness_ns = rows[:, 3]
    assert (lateness_ns >= 0).all(), "a frame was released before its deadline"
    assert lateness_ns.max() < PERIOD_MS * 1e6, f"a frame was {lateness_ns.max() / 1e6:.1f} ms late, a period or more"
    match = PACED_TIMING_LINE.search(stdout)
    assert match is not None, stdout[-400:]
    assert int(match.group("frames")) == PACED_CYCLES
    assert int(match.group("late")) == 0
    print(f"lateness min/mean/max = {match.group('min')} / {match.group('mean')} / {match.group('max')} us")


# --- TC-020 step 8: --pace is a replay's flag ---------------------------------------------------


def test_pace_without_replay_is_refused(telemetry_bin, tmp_path):
    completed = run(telemetry_bin, tmp_path, *CONFIG_ARGS, "--pace", "--cycles", "5")
    assert completed.returncode == 1
    assert "--pace" in completed.stderr and "--replay" in completed.stderr, completed.stderr
    assert not (tmp_path / "logs").exists(), "nothing may be built after a refused flag"


# --- TC-020 step 9: a paced replay publishes at the recorded cadence --------------------------


def test_paced_replay_publishes_at_the_recorded_cadence(telemetry_bin, tmp_path, sim_session):
    cwd = tmp_path / "run"
    cwd.mkdir()
    receiver, flag = make_receiver("zmq")
    arrivals: list[float] = []
    run_ = run_published(telemetry_bin, ["--replay", str(sim_session.log), "--pace", "--cycles", "100", *flag], cwd,
                         receiver, on_frame=lambda n: arrivals.append(time.perf_counter()))
    assert run_.returncode == 0, run_.stdout[-400:]
    assert run_.frames, "nothing received"
    join = run_.frames[0].timestamp_ms // PERIOD_MS
    assert join <= 5
    assert [f.timestamp_ms for f in run_.frames] == list(range(join * PERIOD_MS, 100 * PERIOD_MS, PERIOD_MS))
    assert receiver.gaps == 0 and receiver.out_of_order == 0
    assert_byte_identical_to_log(run_.frames, sim_session.log)
    span = arrivals[-1] - arrivals[0]
    expected = (len(run_.frames) - 1) * PERIOD_MS / 1000
    assert 0.8 * expected < span < 1.3 * expected, f"{len(run_.frames)} frames arrived over {span:.2f} s, not about {expected:.2f}"
    assert run_.statuses and run_.statuses[-1][1].state == "ENDED"
    print(f"{len(run_.frames)} paced frames over {span:.2f} s (recorded span {expected:.2f} s)")
