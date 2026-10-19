"""The voter as the binary, the receiver and the Python reader present it (REQ-RED-001 to REQ-RED-004,
REQ-RED-006; TC-032 step 11, TC-034; also REQ-LOG-003, REQ-TEST-002).

The voter's events ride new values of the event's existing channel and type bytes (FTS-DD-001
Section 9), so the twin must name them as the C++ side does and print them as the console does.
"""
from __future__ import annotations

import re

from pipeline import frame_format
from pipeline.frame_format import FaultEvent
from pipeline.log_reader import read_log
from pipeline.receiver import format_event
from test_config import run
from test_validity import write_replayable

VOTER_LINE = re.compile(r"^Voter:\s+(\d+) frames, lanes in the vote ([\d ]+), no majority on (\d+) frames, "
                        r"voter events (\d+)$", re.M)


# The twin's names for the voter's channel and the four lane types, pinned with their numbers, which
# are what a log carries (FTS-FM-001 FAULT-019 to FAULT-022), and the receiver's line for one, the
# console's [FAULT] line (TC-032 step 11).
def test_voter_event_names():
    assert frame_format.CHANNEL_NAMES == {0: "BARO", 1: "IMU", 2: "GPS", 3: "VOTER"}
    assert frame_format.FAULT_TYPE_NAMES == {0: "COMM_TIMEOUT", 1: "STUCK", 2: "OUT_OF_RANGE", 3: "RECOVERY",
                                             4: "LANE_DISAGREEMENT", 5: "LANE_FAULTED", 6: "NO_MAJORITY",
                                             7: "LANE_OVERRUN", 8: "LANE_FAULT_INJECTED"}
    event = FaultEvent(timestamp_ms=60, channel=3, type=5, reserved=0, value=3.0)
    assert format_event(event) == "[FAULT] t=60 ms channel=VOTER type=LANE_FAULTED value=3.0"


# TC-034 step 6: the session fixture's log and its replay, three lanes voting every frame: VOTED on all
# 500, no event of any kind, and the exit line saying so (REQ-RED-001, REQ-RED-002).
def test_session_votes_every_frame(sim_session):
    for path in (sim_session.log, sim_session.replay):
        log = read_log(path)
        assert len(log.frames) == 500
        assert all(frame.voter_status == 0x0001 for frame in log.frames), path
        assert log.events == [], path
    for stdout in (sim_session.stdout, sim_session.replay_stdout):
        match = VOTER_LINE.search(stdout)
        assert match is not None, stdout[-600:]
        assert match.groups() == ("500", "1 2 3", "0", "0")


# TC-034 step 7: a version-3 log recorded before the voter (the session's own frames with the voter's
# status 0, as the single lane wrote them) replays with the status recomputed, record for record the
# voted session's: a verdict is never copied from the log (REQ-LOG-003, REQ-RED-002).
def test_pre_voter_version_3_log_replays_voted(telemetry_bin, sim_session, tmp_path):
    voted = read_log(sim_session.log)
    pre_voter = [frame._replace(voter_status=0) for frame in voted.frames]
    source = write_replayable(tmp_path / "pre", pre_voter)
    assert all(frame.voter_status == 0 for frame in read_log(source).frames)
    completed = run(telemetry_bin, tmp_path, "--replay", str(source.relative_to(tmp_path)), "--cycles", "500")
    assert completed.returncode == 0, completed.stderr
    replay = read_log(tmp_path / "logs" / "replay_000.bin")
    assert replay.frame_bytes == voted.frame_bytes
    assert replay.events == []
