"""pipeline/log_reader.py: TC-008's Integration-level steps and the reader TC-004 step 3 uses
(REQ-LOG-001, REQ-LOG-003, REQ-TEST-002). Synthetic files are written by synthetic_log.py with
the same Structs the C++ bytes are read with; one test reads what the binary itself wrote."""
from __future__ import annotations

import struct

import pytest

import synthetic_log as syn
from pipeline import log_reader
from pipeline.frame_format import (
    EVENT, FRAME, FRAME_FIELDS, FRAME_V2, HEADER, LOG_FORMAT_VERSION, FormatError, frame_size_for,
)
from pipeline.log_reader import (
    SequenceError, event_dtype, file_index, find_log_files, frame_dtype, frames_array, read_log, read_logs, summary,
)


def test_reads_what_the_binary_wrote(sim_session):
    log = read_log(sim_session.log)
    assert len(log.frames) == 500
    assert log.events == []
    assert log.torn_bytes == 0
    assert [frame.timestamp_ms for frame in log.frames[:3]] == [0, 20, 40]
    assert log.frames[-1].timestamp_ms == 9980
    assert (log.header.frame_size, log.header.event_size) == (FRAME.size, EVENT.size)
    # The header says how the file was recorded and how its frames were produced (REQ-LOG-007).
    assert (log.header.version, log.header.run_mode, log.header.source_mode) == (LOG_FORMAT_VERSION, 2, 2)


def test_reads_frames_and_events_in_order(tmp_path):
    frames = [syn.make_frame(0), syn.make_frame(1)]
    event = syn.make_event(20, 2, 0, 2020.0)
    path = syn.write_log(tmp_path / "t_000.bin", [frames[0], event, frames[1]])
    log = read_log(path)
    assert log.frames == frames
    assert log.events == [event]
    assert log.frame_bytes == [FRAME.pack(*frame) for frame in frames]
    assert log.torn_bytes == 0
    assert log.paths == (path,)


@pytest.mark.parametrize("field, value, word", [
    ("magic", b"XXXX", "magic"),
    ("version", LOG_FORMAT_VERSION + 1, "version"),
    ("frame_size", 100, "frame size"),
    ("event_size", 12, "event size"),
    ("endian_marker", 0x04030201, "endian"),
])
def test_rejects_bad_header(tmp_path, field, value, word):
    header = syn.make_header(**{field: value})
    path = syn.write_log(tmp_path / "t_000.bin", [syn.make_frame(0)], header=header)
    with pytest.raises(FormatError, match=word):
        read_log(path)


# The frame size is judged against the header's own version, since the frame grew at version 3
# (REQ-LOG-005, TC-031 step 2; the twin of LoggingTest.FrameSizeIsJudgedPerVersion).
@pytest.mark.parametrize("version, frame_size, expected", [
    (3, 104, "frame size 104 in the header, 120 for version 3 in this module"),
    (2, 120, "frame size 120 in the header, 104 for version 2 in this module"),
    (1, 120, "frame size 120 in the header, 104 for version 1 in this module"),
])
def test_frame_size_is_judged_per_version(tmp_path, version, frame_size, expected):
    header = syn.make_header(version=version, frame_size=frame_size)
    path = syn.write_log(tmp_path / "t_000.bin", [syn.make_frame(0)], header=header)
    with pytest.raises(FormatError, match=expected):
        read_log(path)


def test_torn_tail_keeps_complete_records(tmp_path):
    # Cut 41 bytes into the second record: the tag and 40 of its 120 bytes. The C++ reader
    # returns the first frame, then stops with "torn record"; so does this one.
    path = syn.write_log(tmp_path / "t_000.bin", [syn.make_frame(0)], torn_bytes=41)
    log = read_log(path)
    assert len(log.frames) == 1
    assert log.torn_bytes == 41


def test_unknown_tag_raises(tmp_path):
    path = tmp_path / "t_000.bin"
    path.write_bytes(syn.log_bytes([syn.make_frame(0)]) + bytes([0x7F]) + bytes(FRAME.size))
    with pytest.raises(FormatError, match="unknown record tag 127 at offset 137"):
        read_log(path)


def test_find_log_files_sorted_and_filtered(tmp_path):
    for name in ("t_002.bin", "t_000.bin", "t_001.bin", "other_000.bin", "t_003.txt", "t_jitter.csv"):
        (tmp_path / name).write_bytes(b"")
    assert [path.name for path in find_log_files(tmp_path, "t")] == ["t_000.bin", "t_001.bin", "t_002.bin"]
    assert file_index("telemetry_004.bin") == 4
    assert file_index("telemetry_jitter.csv") is None


def test_read_logs_spans_rotation(tmp_path):
    frames = [syn.make_frame(i) for i in range(10)]
    paths = syn.write_rotated(tmp_path, "t", frames, per_file=3)
    assert len(paths) == 4
    log = read_logs(paths)
    assert [frame.timestamp_ms for frame in log.frames] == list(range(0, 200, 20))
    assert log.paths == tuple(paths)
    assert log.frames == frames


def test_read_logs_rejects_restart(tmp_path):
    first = syn.write_log(tmp_path / "t_000.bin", [syn.make_frame(i) for i in range(5)])
    second = syn.write_log(tmp_path / "t_001.bin", [syn.make_frame(i) for i in range(5)])
    with pytest.raises(SequenceError, match="t_001.bin: restarts at 0 ms after t_000.bin ends at 80 ms"):
        read_logs([first, second])


def test_read_logs_rejects_index_gap(tmp_path):
    first = syn.write_log(tmp_path / "t_000.bin", [syn.make_frame(i) for i in range(5)])
    third = syn.write_log(tmp_path / "t_002.bin", [syn.make_frame(i) for i in range(5, 10)])
    with pytest.raises(SequenceError, match="t_002.bin: index 2 follows 0"):
        read_logs([first, third])


def test_read_logs_rejects_large_step(tmp_path):
    first = syn.write_log(tmp_path / "t_000.bin", [syn.make_frame(i) for i in range(5)])
    second = syn.write_log(tmp_path / "t_001.bin", [syn.make_frame(i) for i in range(100, 105)])
    with pytest.raises(SequenceError, match="t_001.bin: starts 1920 ms after t_000.bin ends; the limit is 1000 ms"):
        read_logs([first, second])


def test_read_logs_rejects_non_increasing_timestamps(tmp_path):
    # The shape a replay leaves when --cycles runs past its file: the last frame repeated.
    frames = [syn.make_frame(0), syn.make_frame(1), syn.make_frame(1)]
    path = syn.write_log(tmp_path / "t_000.bin", frames)
    with pytest.raises(SequenceError, match="t_000.bin: timestamp 20 ms follows 20 ms"):
        read_logs([path])


def test_frame_dtype_matches_struct():
    dtype = frame_dtype()
    assert dtype.itemsize == FRAME.size == 120
    assert FRAME_V2.size == 104
    # The size each version implies, the twin of log_format.h's frame_size_for: 0 for a version neither side defines.
    assert [frame_size_for(v) for v in (0, 1, 2, 3, 4)] == [0, 104, 104, 120, 0]
    assert event_dtype().itemsize == EVENT.size == 16
    assert dtype.fields["sequence"][1] == 52           # where version 1 had the pad before the doubles, version 2 reserved_a
    assert dtype.fields["latitude"][1] == 56
    assert dtype.fields["course_deg"][1] == 80
    assert dtype.fields["gps_read_ok"][1] == 97
    assert dtype.fields["baro_altitude_validity"][1] == 98   # the five validity bytes, then the flags
    assert dtype.fields["flags"][1] == 103
    assert dtype.fields["gps_vertical_rate_mps"][1] == 104   # the version-3 tail (TC-031 step 11)
    assert dtype.fields["gps_pressure_altitude_m"][1] == 108
    assert dtype.fields["gps_fix_quality"][1] == 112
    assert dtype.fields["gps_satellites"][1] == 113
    assert dtype.fields["voter_status"][1] == 114
    assert dtype.fields["reserved_d"][1] == 116
    assert event_dtype().fields["reserved"][1] == 10
    codes = log_reader._field_codes(FRAME)
    for i, name in enumerate(FRAME_FIELDS):
        assert dtype.fields[name][1] == struct.calcsize("@" + "".join(codes[:i]) + "0" + codes[i]), name


def test_frames_array_equals_namedtuples(tmp_path):
    frames = [syn.make_frame(i, pressure_hpa=1013.0 + i, latitude=37.5 + i, gps_read_ok=bool(i % 2))
              for i in range(4)]
    log = read_log(syn.write_log(tmp_path / "t_000.bin", frames))
    array = frames_array(log)
    assert array.shape == (4,)
    assert array.tobytes() == b"".join(log.frame_bytes)
    for i, frame in enumerate(log.frames):
        for name in FRAME_FIELDS:
            assert array[name][i] == getattr(frame, name), name


# --- TC-031 steps 3 and 11: a file older than format version 3 reads as version-3 frames --------


@pytest.mark.parametrize("version", (1, 2))
def test_older_file_reads_as_a_prefix_of_version_3(tmp_path, version):
    # A version-2 source left the reserved fields zero, so sequence and flags read 0.
    frames = [syn.make_frame(i, pressure_hpa=1013.0 + i, course_deg=270.0, gps_read_ok=bool(i % 2), sequence=0)
              for i in range(3)]
    path = syn.write_log(tmp_path / "old_000.bin", frames, version=version)
    assert path.stat().st_size == HEADER.size + 3 * (1 + FRAME_V2.size), "an older file holds 104-byte frames"
    log = read_log(path)
    assert log.header.version == version
    assert (log.header.frame_size, log.header.run_mode, log.header.source_mode) == (FRAME_V2.size, 0, 0)
    assert log.frames == frames, "every old field as written, the tail zero"
    assert all(frame.sequence == 0 and frame.flags == 0 for frame in log.frames)
    assert all(frame.gps_fix_quality == 0 and frame.gps_satellites == 0 for frame in log.frames)
    assert all(frame.gps_vertical_rate_mps == 0.0 and frame.gps_pressure_altitude_m == 0.0 for frame in log.frames)
    assert all(frame.voter_status == 0 and frame.reserved_d == 0 for frame in log.frames)
    assert [len(raw) for raw in log.frame_bytes] == [FRAME_V2.size] * 3, "the file's own bytes are kept"
    array = frames_array(log)
    assert array.dtype.itemsize == FRAME.size and array.shape == (3,)
    for i, frame in enumerate(log.frames):
        for name in FRAME_FIELDS:
            assert array[name][i] == getattr(frame, name), name
    text = summary(log)
    assert "3 frames, 0 events" in text
    assert f"format version {version}, run mode UNKNOWN, source mode UNKNOWN" in text
    assert "sequence: 0 .. 0, 2 breaks (a file older than format version 3 reads 0)" in text


def test_cli_summary(tmp_path, capsys):
    frames = [syn.make_frame(i, gps_read_ok=False) for i in range(3)]
    event = syn.make_event(40, 2, 0, 2020.0)
    syn.write_log(tmp_path / "telemetry_000.bin", [frames[0], frames[1], event, frames[2]])
    assert log_reader.main([str(tmp_path)]) == 0
    out = capsys.readouterr().out
    assert "3 frames, 1 events, torn bytes 0" in out
    assert "format version 3, run mode SIMULATED, source mode SIMULATED" in out
    assert "sequence: 0 .. 2, consecutive" in out
    assert "fresh: baro 3, imu 3, gps 0" in out
    assert "validity: every computed field NORMAL on 3 of 3 frames" in out
    assert "event t=40 ms channel=GPS type=COMM_TIMEOUT value=2020" in out
    bad = tmp_path / "bad.bin"
    bad.write_bytes(b"not a log")
    assert log_reader.main([str(bad)]) == 2
    assert "error: bad.bin:" in capsys.readouterr().err
