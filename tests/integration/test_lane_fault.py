"""The demonstration lane fault as the binary, its log and its replay present it (REQ-RED-007; TC-037 steps 5 to 8;
also REQ-RED-005, REQ-RED-003, REQ-LOG-003).

`--lane-fault <lane>@<seconds>` flips bit 22 of one lane's fused-altitude output on every cycle from the first frame at
or after that time. The voter outvotes the lane on its first altered frame and excludes it at the persistence count of
three; the log records the injection as an event; a replay re-applies it, so the replay reproduces the run record for
record. Without the option nothing changes.
"""
from __future__ import annotations

import re

import pytest

from conftest import CONFIG_ARGS, assert_replay_identical
from pipeline.frame_format import FaultEvent
from pipeline.log_reader import read_log
from test_config import run

CYCLES = 200
VOTED = 0x0001
LANE_2_FAULTED = 0x0004
VOTER = 3
LANE_DISAGREEMENT, LANE_FAULTED, LANE_FAULT_INJECTED = 4, 5, 8

VOTER_LINE = re.compile(r"^Voter:\s+(\d+) frames, lanes in the vote ([\d ]+), no majority on (\d+) frames, "
                        r"voter events (\d+)$", re.M)


@pytest.fixture(scope="module")
def faulted_run(telemetry_bin, tmp_path_factory):
    """Lane 2 altered from 1 s of a 200-frame seed-42 run, and that run's replay."""
    directory = tmp_path_factory.mktemp("lane_fault")
    recorded = run(telemetry_bin, directory, *CONFIG_ARGS, "--lane-fault", "2@1", "--cycles", str(CYCLES))
    assert recorded.returncode == 0, recorded.stderr
    log = directory / "logs" / "telemetry_000.bin"
    replayed = run(telemetry_bin, directory, "--replay", str(log.relative_to(directory)))
    assert replayed.returncode == 0, replayed.stderr
    return log, recorded.stdout, directory / "logs" / "replay_000.bin", replayed.stdout


# TC-037 step 5: the injection is logged on the frame it began, then the voter's answer to it: the onset on the same
# frame, the exclusion two cycles later. The injection is the lane fault's own record, not a voter event.
def test_the_log_records_the_injection_and_the_voters_answer(faulted_run):
    log, _, _, _ = faulted_run
    assert read_log(log).events == [
        FaultEvent(timestamp_ms=1000, channel=VOTER, type=LANE_FAULT_INJECTED, reserved=0, value=2.0),
        FaultEvent(timestamp_ms=1000, channel=VOTER, type=LANE_DISAGREEMENT, reserved=0, value=2.0),
        FaultEvent(timestamp_ms=1040, channel=VOTER, type=LANE_FAULTED, reserved=0, value=2.0),
    ]


# TC-037 step 5: lane 2 is outvoted, so every frame equals the unaltered session's (the fixture's first 200 frames,
# the same seed) in every field but the voter's status, which names lane 2 from the frame after its exclusion.
def test_lane_2_is_outvoted_and_excluded_and_nothing_else_moves(faulted_run, sim_session):
    log, _, _, _ = faulted_run
    frames = read_log(log).frames
    unaltered = read_log(sim_session.log).frames[:CYCLES]
    assert len(frames) == CYCLES
    for frame, reference in zip(frames, unaltered):
        expected = VOTED | (LANE_2_FAULTED if frame.timestamp_ms >= 1060 else 0)
        assert frame.voter_status == expected, frame.timestamp_ms
        assert frame._replace(voter_status=0) == reference._replace(voter_status=0), frame.timestamp_ms


# TC-037 step 5: the exit lines. The voter counts its own two events; the injection is reported on its own line.
def test_the_exit_lines_name_the_fault(faulted_run):
    _, stdout, _, _ = faulted_run
    assert ("Lane fault:    lane 2 from 1000 ms, bit 22 of its fused altitude flipped on every cycle "
            "(--lane-fault, REQ-RED-007)") in stdout
    match = VOTER_LINE.search(stdout)
    assert match is not None, stdout[-600:]
    assert match.groups() == (str(CYCLES), "1 3", "0", "2")


# TC-037 step 6: the replay finds the recorded injection, re-applies it from the same frame, and reproduces the run:
# every record identical, the header differing in the run mode alone (REQ-LOG-003).
def test_the_replay_reapplies_the_fault_and_reproduces_the_run(faulted_run):
    log, _, replay, replay_stdout = faulted_run
    assert "re-applied: the replay's input records it" in replay_stdout
    assert_replay_identical(replay, log)


# TC-037 step 7: a malformed value, and the option beside a replay, are refused before anything is built.
@pytest.mark.parametrize("args", [
    ["--lane-fault", "4@1"],
    ["--lane-fault", "0@1"],
    ["--lane-fault", "2@"],
    ["--lane-fault", "x"],
    ["--lane-fault", "2@-1"],
    ["--lane-fault", "2@nan"],
    ["--replay", "logs/telemetry_000.bin", "--lane-fault", "2@1"],
])
def test_lane_fault_is_refused_where_it_has_no_meaning(telemetry_bin, tmp_path, args):
    completed = run(telemetry_bin, tmp_path, *CONFIG_ARGS, *args)
    assert completed.returncode == 1
    assert "--lane-fault" in completed.stderr, completed.stderr
    assert not (tmp_path / "logs").exists(), "a refused run must build nothing"


# TC-037 step 8: a run without the option records no injection and prints no lane-fault line.
def test_a_run_without_the_option_has_no_lane_fault(sim_session):
    for path in (sim_session.log, sim_session.replay):
        assert all(event.type != LANE_FAULT_INJECTED for event in read_log(path).events), path
    for stdout in (sim_session.stdout, sim_session.replay_stdout):
        assert "Lane fault:" not in stdout
