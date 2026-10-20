"""TC-017 steps 11 through 15 (REQ-PROC-007, REQ-LOG-005, REQ-FAULT-006; also REQ-LOG-003,
REQ-LOG-004, REQ-TEST-002).

What the binary, the receiver and the Python reader say about validity and the format version:
the session fixture's log and replay are version 3 and NORMAL from frame 0; a version-1 and a
version-2 log replay as a prefix and a version-4 log is refused before anything is built (TC-031
steps 12 and 13 as well); a replay ends at its log
(TC-019, `test_replay_pacing.py`); the receiver's lines carry the validity names; the reader's layout, names, NaN view and
summary agree with the C++ side.
"""
from __future__ import annotations

import re
import shutil
from pathlib import Path

import numpy as np
import pytest

import synthetic_log as syn
from conftest import SHIPPED_CONFIG, run_published
from pipeline.frame_format import (
    COMPUTED_VALIDITY, EVENT, FLAG_NAMES, FRAME, LOG_FORMAT_VERSION, RUN_MODE_NAMES, STATUS_NAMES, VALIDITY_FIELDS,
    VALIDITY_NAMES, VALIDITY_NORMAL, VOTER_STATUS_NAMES,
)
from pipeline.log_reader import event_dtype, frame_dtype, frames_array, invalid_as_nan, read_log, summary
from pipeline.receiver import format_frame
from test_config import RAW_FIELDS, run
from test_transport import assert_byte_identical_to_log, make_receiver

VALIDITY_LINE = re.compile(r"^Validity:\s+every computed field NORMAL on (\d+) of (\d+) frames \(first on frame (\d+)\)", re.M)
NOT_JUDGED = 0


def raw_frames(n: int) -> list:
    """n frames as a source would leave them: raw fields, every validity NOT_JUDGED, GPS fresh on
    frame 0 only, pressure wobbling so the altitude is not constant."""
    return [syn.make_frame(i, pressure_hpa=1013.0 + 0.01 * (i % 5), temperature_c=21.0, accel_z=9.81,
                           gps_altitude_m=3.0, gps_read_ok=(i == 0), **{v: NOT_JUDGED for v in VALIDITY_FIELDS})
            for i in range(n)]


def write_replayable(directory: Path, frames, version: int = LOG_FORMAT_VERSION) -> Path:
    """A log the binary can replay: the file and the configuration record beside it."""
    directory.mkdir(parents=True, exist_ok=True)
    path = syn.write_log(directory / "telemetry_000.bin", frames, version=version)
    shutil.copy(SHIPPED_CONFIG, directory / "telemetry_config.yaml")
    return path


# --- step 11, and TC-031 step 12: the session is version 3 and NORMAL from frame 0 -------------


def test_session_is_version_3_and_normal_from_frame_0(sim_session):
    for path in (sim_session.log, sim_session.replay):
        log = read_log(path)
        assert log.header.version == LOG_FORMAT_VERSION == 3
        # The header says how the file was recorded and how its frames were produced
        # (REQ-LOG-007): the session SIMULATED twice, its replay REPLAY of SIMULATED frames.
        expected_modes = (2, 2) if path == sim_session.log else (3, 2)
        assert (log.header.run_mode, log.header.source_mode) == expected_modes, path
        frames = frames_array(log)
        assert len(frames) == 500
        for field in VALIDITY_FIELDS:
            assert (frames[field] == VALIDITY_NORMAL).all(), field
        for channel in ("baro", "imu", "gps"):
            assert (frames[f"{channel}_status"] == 0).all(), channel   # the simulator is fresh at t = 0
        assert (frames["sequence"] == np.arange(500)).all(), "the source numbers its frames from 0 (REQ-LOG-008)"
        # The simulator's receiver states its fix: FIX_QUALITY alone, quality 1, eight satellites.
        assert (frames["flags"] == 0x08).all()
        assert (frames["gps_fix_quality"] == 1).all() and (frames["gps_satellites"] == 8).all()
        for name in ("gps_vertical_rate_mps", "gps_pressure_altitude_m", "voter_status", "reserved_d"):
            assert (frames[name] == 0).all(), name
    for stdout in (sim_session.stdout, sim_session.replay_stdout):
        match = VALIDITY_LINE.search(stdout)
        assert match is not None, stdout[-600:]
        assert match.groups() == ("500", "500", "0")
        assert "NO_COMPUTED_DATA on 0 frames, FAILURE_WARNING on 0" in stdout
    assert "Replay input: " in sim_session.replay_stdout
    assert "(format version 3, run mode SIMULATED, source mode SIMULATED)" in sim_session.replay_stdout
    assert "Replay:    500 frames, end of log reached" in sim_session.replay_stdout


# --- steps 12 and 13: the format version at the command line -----------------------------------


@pytest.mark.parametrize("version", (1, 2))
def test_older_log_replays_through_this_build(telemetry_bin, tmp_path, version):
    # A source older than format version 3 left the reserved fields zero, and the C++ reader
    # sanitizes a version-1 frame's to zero besides, so the input carries 0 for the comparison.
    frames = [frame._replace(sequence=0) for frame in raw_frames(50)]
    source = write_replayable(tmp_path / "old", frames, version=version)
    completed = run(telemetry_bin, tmp_path, "--replay", str(source.relative_to(tmp_path)), "--cycles", "50")
    assert completed.returncode == 0, completed.stderr
    assert (f"Replay input:  old/telemetry_000.bin (format version {version}, run mode UNKNOWN, source mode UNKNOWN; "
            "this build writes version 3") in completed.stdout
    replay = read_log(tmp_path / "logs" / "replay_000.bin")
    assert replay.header.version == 3
    assert (replay.header.run_mode, replay.header.source_mode) == (3, 0), "REPLAY of frames of an unknown source"
    assert len(replay.frames) == 50
    written = read_log(source).frames      # the file's float32 values, not the Python floats they came from
    for i, (a, b) in enumerate(zip(written, replay.frames)):
        for name in RAW_FIELDS:
            assert getattr(a, name) == getattr(b, name), f"frame {i}, raw field {name}"
    judged = frames_array(replay)
    for field in VALIDITY_FIELDS:
        assert (judged[field] != NOT_JUDGED).all(), field
    assert (judged["fused_altitude_validity"] == VALIDITY_NORMAL).all()
    assert (judged["sequence"] == 0).all() and (judged["flags"] == 0).all()
    for name in ("gps_vertical_rate_mps", "gps_pressure_altitude_m", "gps_fix_quality", "gps_satellites",
                 "voter_status", "reserved_d"):
        assert (judged[name] == 0).all(), name
    assert "Replay:    50 frames, end of log reached" in completed.stdout


def test_version_4_log_is_refused_before_anything_is_built(telemetry_bin, tmp_path):
    source = write_replayable(tmp_path / "v4", raw_frames(5), version=LOG_FORMAT_VERSION + 1)
    completed = run(telemetry_bin, tmp_path, "--replay", str(source.relative_to(tmp_path)), "--cycles", "5")
    assert completed.returncode == 1
    assert "format version 4" in completed.stderr, completed.stderr
    assert "reads 1 to 3" in completed.stderr, completed.stderr
    assert not (tmp_path / "logs").exists(), "nothing may be built before the input is accepted"


# --- step 14: the receiver's lines ---------------------------------------------------------------


@pytest.mark.parametrize("transport", ("zmq", "udp"))
def test_receiver_lines_carry_the_validity_names(telemetry_bin, tmp_path, transport):
    receiver, flag = make_receiver(transport)
    result = run_published(telemetry_bin, [*flag, "--cycles", "50"], tmp_path, receiver)
    assert result.returncode == 0, result.stdout[-400:]
    assert len(result.frames) >= 45
    assert_byte_identical_to_log(result.frames, result.log)
    for frame in result.frames:
        line = format_frame(frame)
        assert line.count("[NORMAL]") == 2, line          # fused altitude, vertical speed
        assert "[NORMAL/NORMAL]" in line, line              # pitch and roll
        assert re.search(rf"seq=\s*{frame.sequence} ", line), line   # the source's count beside the time (REQ-LOG-008)
        assert "baro=NOMINAL imu=NOMINAL gps=NOMINAL" in line, line


# --- step 15: the Python reader ---------------------------------------------------------------


def test_reader_layout_names_nan_view_and_summary(tmp_path, capsys):
    fields = frame_dtype().fields
    assert fields["sequence"][1] == 52
    assert [fields[name][1] for name in VALIDITY_FIELDS] == [98, 99, 100, 101, 102]
    assert fields["flags"][1] == 103
    assert fields["reserved_d"][1] == 116
    assert event_dtype().fields["reserved"][1] == 10
    assert FRAME.size == 120 and EVENT.size == 16
    assert VALIDITY_NAMES == {0: "NOT_JUDGED", 1: "NO_COMPUTED_DATA", 2: "FAILURE_WARNING", 3: "NORMAL"}
    assert STATUS_NAMES == {0: "NOMINAL", 1: "DEGRADED", 2: "NO_DATA", 3: "NOT_AVAILABLE"}
    # The version-3 tables, the twins of FrameFlag, VoterStatus and RunMode (TC-031 step 11).
    assert FLAG_NAMES == {0x01: "NEW_VEHICLE", 0x02: "VERTICAL_RATE", 0x04: "PRESSURE_ALTITUDE", 0x08: "FIX_QUALITY"}
    assert VOTER_STATUS_NAMES == {0x0001: "VOTED", 0x0002: "LANE_1_FAULTED", 0x0004: "LANE_2_FAULTED",
                                  0x0008: "LANE_3_FAULTED", 0x0010: "NO_MAJORITY"}
    assert RUN_MODE_NAMES == {0: "UNKNOWN", 1: "LIVE", 2: "SIMULATED", 3: "REPLAY", 4: "ADSB"}

    # The NaN view: one FAILURE_WARNING field becomes NaN there and nowhere else; the frames
    # themselves keep their numbers.
    frames = [syn.make_frame(0, fused_altitude_m=10.0, pitch_deg=1.0),
              syn.make_frame(1, fused_altitude_m=11.0, pitch_deg=2.0, pitch_validity=2),
              syn.make_frame(2, fused_altitude_m=12.0, pitch_deg=3.0, fused_altitude_validity=1, gps_status=2)]
    log = read_log(syn.write_log(tmp_path / "telemetry_000.bin", frames))
    array = frames_array(log)
    view = invalid_as_nan(array)
    assert np.isnan(view["pitch_deg"][1]) and not np.isnan(view["roll_deg"][1])
    assert np.isnan(view["fused_altitude_m"][2]) and not np.isnan(view["vertical_speed_mps"][2])
    assert not np.isnan(view["pitch_deg"][[0, 2]]).any() and not np.isnan(view["fused_altitude_m"][[0, 1]]).any()
    assert array["pitch_deg"][1] == 2.0, "the view is a copy"
    for field in COMPUTED_VALIDITY:
        assert np.isnan(view[field]).sum() == (1 if field in ("pitch_deg", "fused_altitude_m") else 0), field

    text = summary(log)
    assert "no-data frames: baro 0, imu 0, gps 1" in text
    assert "validity: every computed field NORMAL on 1 of 3 frames; NOT_JUDGED 0, NO_COMPUTED_DATA 1, FAILURE_WARNING 1 field-frames" in text

    # A version-1 file reads, and the Python reader keeps the bytes the file holds (the C++
    # reader zeroes them); the version is the file's.
    v1 = syn.write_log(tmp_path / "v1_000.bin", [syn.make_frame(0, sequence=7, pitch_validity=3)], version=1)
    old = read_log(v1)
    assert old.header.version == 1
    assert old.frames[0].sequence == 7 and old.frames[0].pitch_validity == 3
    assert old.frames[0].gps_fix_quality == 0 and len(old.frame_bytes[0]) == 104   # the tail is not in the file
