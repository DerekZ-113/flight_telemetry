"""TC-004 steps 3 to 6 at the integration level (REQ-LOG-003, REQ-LOG-004, REQ-TEST-002).

The session fixture ran the simulator for 500 frames and replayed its log through the same
binary. Both logs are parsed here and compared field by field and byte by byte, which is the
Python parse step the card has waited on. Exact equality throughout: the requirement says
identical, and a tolerance would hide the defect deterministic replay exists to catch.
"""
from __future__ import annotations

import pytest

from pipeline.frame_format import FRAME, FRAME_FIELDS
from pipeline.log_reader import frame_dtype, read_log

# strip_to_raw in src/replay/log_replay_source.cpp clears these before the pipeline recomputes them.
COMPUTED_FIELDS = ("baro_altitude_m", "pitch_deg", "roll_deg", "fused_altitude_m", "vertical_speed_mps",
                   "baro_status", "imu_status", "gps_status")
RAW_FIELDS = tuple(name for name in FRAME_FIELDS if name not in COMPUTED_FIELDS)


@pytest.fixture(scope="module")
def logs(sim_session):
    return read_log(sim_session.log), read_log(sim_session.replay)


def test_frame_and_event_counts_match(logs):
    original, replay = logs
    assert len(original.frames) == len(replay.frames) == 500
    assert original.events == replay.events == []


def test_raw_fields_identical(logs):
    original, replay = logs
    for i, (a, b) in enumerate(zip(original.frames, replay.frames)):
        for name in RAW_FIELDS:
            assert getattr(a, name) == getattr(b, name), f"frame {i}, field {name}"


def test_computed_fields_identical_as_bytes(logs):
    original, replay = logs
    fields = frame_dtype().fields
    for i, (a, b) in enumerate(zip(original.frame_bytes, replay.frame_bytes)):
        if a != b:
            differing = [name for name, (dtype, offset) in fields.items()
                         if a[offset:offset + dtype.itemsize] != b[offset:offset + dtype.itemsize]]
            pytest.fail(f"frame {i} differs in {differing}")


def test_logs_byte_identical(sim_session):
    assert sim_session.log.read_bytes() == sim_session.replay.read_bytes()


def test_frames_pack_round_trip(logs):
    # Repacking the fields reproduces the file's bytes only if every padding byte is zero:
    # the value-initialization rule (FTS-VR-001 Entries 4 and 5) checked at this level.
    for log in logs:
        for frame, raw in zip(log.frames, log.frame_bytes):
            assert FRAME.pack(*frame) == raw


def test_timestamps_are_the_simulator_grid(logs):
    for log in logs:
        assert [frame.timestamp_ms for frame in log.frames] == list(range(0, 10000, 20))
