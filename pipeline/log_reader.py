"""Read telemetry log files: the Python twin of src/logging/log_reader.cpp (REQ-LOG-001, REQ-LOG-003).

frame_format.py describes the bytes. This module adds what the C++ LogReader and
list_log_files add on their side: files, rotated sequences, and the rules that make
a set of files one run. It is standard-library only at import, so the summary runs
on the Raspberry Pi without the venv; numpy is imported inside the array functions.

Torn tail, the C++ rule: a tag byte with fewer bytes behind it than its record needs
ends the file. Every complete record before it is kept and `torn_bytes` counts what
was left, where LogReader reports "torn record at end of file" and keeps ok() true.
An unknown tag is an error on both sides. frame_format.iter_records raises after
yielding instead, which would make list() lose the whole file, so this module walks
parse_record itself.
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path
from typing import NamedTuple, Sequence

from pipeline import frame_format
from pipeline.frame_format import (
    CHANNEL_NAMES, EVENT, EVENT_FIELDS, FAULT_TYPE_NAMES, FRAME, FRAME_FIELDS, HEADER,
    TAG_EVENT, TAG_FRAME, FaultEvent, FormatError, Frame, Header,
)

DEFAULT_PREFIX = "telemetry"    # BinaryLogger's prefix for live and simulated runs; replays use "replay"
DEFAULT_MAX_STEP_MS = 1000      # largest timestamp step allowed across a rotation boundary
CHANNELS = ("baro", "imu", "gps")


class LogFile(NamedTuple):
    paths: tuple[Path, ...]
    header: Header
    frames: list[Frame]
    events: list[FaultEvent]
    frame_bytes: list[bytes]    # each frame's 104 raw bytes, in order: exact comparisons, numpy without conversion
    torn_bytes: int             # bytes of an incomplete trailing record; 0 when the file ends cleanly


class SequenceError(FormatError):
    """Rotated files that do not form one run."""


_FILE_NAME = re.compile(r"^(?P<prefix>.+)_(?P<index>\d{3,})\.bin$")


def read_log(path: str | Path) -> LogFile:
    """One file: header validated, every complete record kept, a torn tail counted."""
    path = Path(path)
    buf = path.read_bytes()
    try:
        header = frame_format.parse_header(buf)
        frame_format.check_header(header)
    except FormatError as error:
        raise FormatError(f"{path.name}: {error}") from None
    frames: list[Frame] = []
    events: list[FaultEvent] = []
    frame_bytes: list[bytes] = []
    offset = HEADER.size
    torn = 0
    while offset < len(buf):
        tag = buf[offset]
        if tag == TAG_FRAME:
            layout = FRAME
        elif tag == TAG_EVENT:
            layout = EVENT
        else:
            raise FormatError(f"{path.name}: unknown record tag {tag} at offset {offset}")
        start = offset + 1
        if len(buf) - start < layout.size:
            torn = len(buf) - offset
            break
        record, offset = frame_format.parse_record(buf, offset)
        if tag == TAG_FRAME:
            frames.append(record)
            frame_bytes.append(buf[start:start + FRAME.size])
        else:
            events.append(record)
    return LogFile((path,), header, frames, events, frame_bytes, torn)


def file_index(path: str | Path) -> int | None:
    """The rotation index in a `prefix_NNN.bin` name, or None for any other name."""
    match = _FILE_NAME.match(Path(path).name)
    return int(match.group("index")) if match else None


def find_log_files(directory: str | Path, prefix: str = DEFAULT_PREFIX) -> list[Path]:
    """`prefix_NNN.bin` files in index order. list_log_files sorts by name; the index is
    zero-padded to three digits, so the two orders agree below 1000 files (55 hours)."""
    found = []
    for path in Path(directory).iterdir():
        match = _FILE_NAME.match(path.name)
        if match and match.group("prefix") == prefix:
            found.append((int(match.group("index")), path))
    return [path for _, path in sorted(found)]


def merge_logs(logs: Sequence[LogFile], max_step_ms: int = DEFAULT_MAX_STEP_MS) -> LogFile:
    """Rotated files as one run, or a SequenceError naming the file that breaks it.

    Three rules: indices consecutive; timestamps strictly increasing within and across
    files; the step across a boundary at most max_step_ms. A file that restarts at 0
    fails the second rule. A leftover from an aborted earlier capture of the same length
    passes all three (same start, same rotation boundary, same rate), which is why the
    summary also prints the frame total and warns when file times are out of order.
    """
    if not logs:
        raise SequenceError("no log files given")
    previous_index = file_index(logs[0].paths[0])
    for log in logs[1:]:
        index = file_index(log.paths[0])
        if previous_index is not None and index is not None and index != previous_index + 1:
            raise SequenceError(f"{log.paths[0].name}: index {index} follows {previous_index}; "
                                "a file is missing or does not belong")
        previous_index = index
    last_ms: int | None = None
    last_name = ""
    for log in logs:
        name = log.paths[0].name
        stamps = [frame.timestamp_ms for frame in log.frames]
        for earlier, later in zip(stamps, stamps[1:]):
            if later <= earlier:
                raise SequenceError(f"{name}: timestamp {later} ms follows {earlier} ms; timestamps must increase")
        if not stamps:
            continue
        if last_ms is not None:
            if stamps[0] <= last_ms:
                raise SequenceError(f"{name}: restarts at {stamps[0]} ms after {last_name} ends at {last_ms} ms: "
                                    "a different run")
            if stamps[0] - last_ms > max_step_ms:
                raise SequenceError(f"{name}: starts {stamps[0] - last_ms} ms after {last_name} ends; "
                                    f"the limit is {max_step_ms} ms")
        last_ms = stamps[-1]
        last_name = name
    return LogFile(
        tuple(path for log in logs for path in log.paths),
        logs[0].header,
        [frame for log in logs for frame in log.frames],
        [event for log in logs for event in log.events],
        [raw for log in logs for raw in log.frame_bytes],
        sum(log.torn_bytes for log in logs),
    )


def read_logs(paths: Sequence[str | Path], max_step_ms: int = DEFAULT_MAX_STEP_MS) -> LogFile:
    """A rotated sequence, in the given order, read as one run (see merge_logs)."""
    return merge_logs([read_log(path) for path in paths], max_step_ms)


# numpy views. The compiler's padding is reproduced twice, by struct's '@' and by
# numpy's align=True; the import-time assertions below check that both agree on the
# size and on every field offset, or the array would misread every frame after the first.
_STRUCT_TO_NUMPY = {"Q": "u8", "f": "f4", "d": "f8", "B": "u1", "?": "?"}


def _field_codes(layout: struct.Struct) -> list[str]:
    """One struct code per field: '@Q11f2d5f3B3?0Q' -> ['Q', 'f', 'f', ...]. A zero count
    ('0Q') is an alignment directive, not a field, and contributes nothing."""
    codes: list[str] = []
    for count, code in re.findall(r"(\d*)([A-Za-z?])", layout.format.lstrip("@=<>!")):
        codes.extend([code] * (int(count) if count else 1))
    return codes


def _dtype(layout: struct.Struct, names: tuple[str, ...]):
    import numpy as np

    codes = _field_codes(layout)
    dtype = np.dtype([(name, _STRUCT_TO_NUMPY[code]) for name, code in zip(names, codes, strict=True)],
                     align=True)
    assert dtype.itemsize == layout.size, (dtype.itemsize, layout.size)
    for i, (name, code) in enumerate(zip(names, codes)):
        expected = struct.calcsize("@" + "".join(codes[:i]) + "0" + code)
        assert dtype.fields[name][1] == expected, (name, dtype.fields[name][1], expected)
    return dtype


def frame_dtype():
    """The TelemetryFrame layout as a numpy dtype, 104 bytes, derived from FRAME."""
    return _dtype(FRAME, FRAME_FIELDS)


def event_dtype():
    """The FaultEvent layout as a numpy dtype, 16 bytes, derived from EVENT."""
    return _dtype(EVENT, EVENT_FIELDS)


def frames_array(log: LogFile):
    """The frames as a structured array over the file's own bytes (read-only)."""
    import numpy as np

    return np.frombuffer(b"".join(log.frame_bytes), dtype=frame_dtype())


def events_array(log: LogFile):
    import numpy as np

    return np.frombuffer(b"".join(EVENT.pack(*event) for event in log.events), dtype=event_dtype())


def summary(log: LogFile, period_ms: int = 20) -> str:
    """What a run looks like before any analysis: counts, span, steps, freshness, events."""
    lines = [f"{len(log.paths)} file(s): {len(log.frames)} frames, {len(log.events)} events, "
             f"torn bytes {log.torn_bytes}"]
    stamps = [frame.timestamp_ms for frame in log.frames]
    if stamps:
        lines.append(f"timestamps: {stamps[0]} .. {stamps[-1]} ms, {(stamps[-1] - stamps[0]) / 1000:.2f} s")
    steps = [later - earlier for earlier, later in zip(stamps, stamps[1:])]
    if steps:
        limit = 1.5 * period_ms
        over = sum(1 for step in steps if step > limit)
        lines.append(f"step: mean {sum(steps) / len(steps):.3f} ms, min {min(steps)}, max {max(steps)}, "
                     f"over {limit:g} ms: {over}")
    fresh = {ch: sum(1 for f in log.frames if getattr(f, f"{ch}_read_ok")) for ch in CHANNELS}
    degraded = {ch: sum(1 for f in log.frames if getattr(f, f"{ch}_status") != 0) for ch in CHANNELS}
    lines.append("fresh: " + ", ".join(f"{ch} {n}" for ch, n in fresh.items()))
    lines.append("degraded frames: " + ", ".join(f"{ch} {n}" for ch, n in degraded.items()))
    for event in log.events:
        channel = CHANNEL_NAMES.get(event.channel, event.channel)
        kind = FAULT_TYPE_NAMES.get(event.type, event.type)
        lines.append(f"  event t={event.timestamp_ms} ms channel={channel} type={kind} value={event.value:g}")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="python -m pipeline.log_reader",
                                     description="Summarize telemetry log files as one run.")
    parser.add_argument("paths", nargs="+", help="log files in rotation order, or one directory of them")
    parser.add_argument("--prefix", default=DEFAULT_PREFIX, help="file prefix when a directory is given")
    parser.add_argument("--period-ms", type=int, default=20, help="loop period, for the step report")
    args = parser.parse_args(argv)
    paths = [Path(p) for p in args.paths]
    if len(paths) == 1 and paths[0].is_dir():
        directory = paths[0]
        paths = find_log_files(directory, args.prefix)
        if not paths:
            print(f"error: no {args.prefix}_NNN.bin files in {directory}", file=sys.stderr)
            return 2
    try:
        logs = []
        previous_mtime: float | None = None
        previous_name = ""
        for path in paths:
            log = read_log(path)
            logs.append(log)
            stamps = [frame.timestamp_ms for frame in log.frames]
            span = f"t = {stamps[0]} .. {stamps[-1]} ms" if stamps else "no frames"
            print(f"{path.name}: {path.stat().st_size} bytes, {len(log.frames)} frames, {len(log.events)} events, "
                  f"torn bytes {log.torn_bytes}, {span}")
            mtime = path.stat().st_mtime
            if previous_mtime is not None and mtime < previous_mtime:
                print(f"warning: {path.name} is older than {previous_name}: a stale file from an earlier run?",
                      file=sys.stderr)
            previous_mtime, previous_name = mtime, path.name
        merged = merge_logs(logs)
    except FormatError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    print(summary(merged, args.period_ms))
    return 0


if __name__ == "__main__":
    sys.exit(main())
