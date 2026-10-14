"""Write telemetry logs from Python for the reader and noise tests (test helper).

Packs with the same HEADER, FRAME and EVENT Structs the C++ bytes are read with, so a
synthetic file is byte for byte what BinaryLogger would write for the same records.
"""
from __future__ import annotations

from pathlib import Path
from typing import Iterable, Sequence

from pipeline.frame_format import (
    ENDIAN_MARKER, EVENT, FRAME, FRAME_FIELDS, HEADER, LOG_FORMAT_VERSION, MAGIC, TAG_EVENT, TAG_FRAME,
    VALIDITY_FIELDS, VALIDITY_NORMAL, FaultEvent, Frame, Header,
)

PERIOD_MS = 20

# The integer fields need integer zeros: struct.pack refuses 0.0 for B, H and I.
_ZERO_FRAME: dict[str, object] = dict.fromkeys(FRAME_FIELDS, 0.0)
_ZERO_FRAME.update(timestamp_ms=0, reserved_a=0, baro_status=0, imu_status=0, gps_status=0,
                   baro_read_ok=True, imu_read_ok=True, gps_read_ok=True, reserved_b=0,
                   **{name: VALIDITY_NORMAL for name in VALIDITY_FIELDS})


def make_header(**overrides) -> Header:
    return Header(MAGIC, LOG_FORMAT_VERSION, FRAME.size, EVENT.size, 0, ENDIAN_MARKER)._replace(**overrides)


def make_frame(index: int = 0, **overrides) -> Frame:
    """A frame on the 20 ms grid: zeros, NOMINAL statuses, every channel fresh, every computed
    field NORMAL (a processed frame, as the binary writes one), then overrides."""
    fields = dict(_ZERO_FRAME, timestamp_ms=index * PERIOD_MS)
    fields.update(overrides)
    return Frame(**fields)


def make_event(timestamp_ms: int, channel: int, kind: int, value: float) -> FaultEvent:
    return FaultEvent(timestamp_ms, channel, kind, 0, value)


def pack_record(record: Frame | FaultEvent) -> bytes:
    if isinstance(record, Frame):
        return bytes([TAG_FRAME]) + FRAME.pack(*record)
    return bytes([TAG_EVENT]) + EVENT.pack(*record)


def log_bytes(records: Iterable[Frame | FaultEvent], header: Header | None = None, torn_bytes: int = 0,
              version: int = LOG_FORMAT_VERSION) -> bytes:
    """A whole file: header, records, and optionally the first torn_bytes of one more frame record.
    `version` writes the header of another format version over the same bytes (a version-1 file
    has this layout with zeros where version 2 carries validity and reserved fields)."""
    out = bytearray(HEADER.pack(*(header or make_header(version=version))))
    count = 0
    for record in records:
        out += pack_record(record)
        count += 1
    if torn_bytes:
        out += pack_record(make_frame(count))[:torn_bytes]
    return bytes(out)


def write_log(path: str | Path, records: Iterable[Frame | FaultEvent], header: Header | None = None,
              torn_bytes: int = 0, version: int = LOG_FORMAT_VERSION) -> Path:
    path = Path(path)
    path.write_bytes(log_bytes(records, header, torn_bytes, version))
    return path


def write_rotated(directory: str | Path, prefix: str, frames: Sequence[Frame], per_file: int) -> list[Path]:
    """The frames split across prefix_000.bin, prefix_001.bin, ... every per_file records."""
    paths: list[Path] = []
    for start in range(0, len(frames), per_file):
        path = Path(directory) / f"{prefix}_{len(paths):03d}.bin"
        write_log(path, frames[start:start + per_file])
        paths.append(path)
    return paths


def frames_from_columns(n: int, period_ms: int = PERIOD_MS, **columns: Sequence) -> list[Frame]:
    """n frames on the period grid with the named fields taken from the given sequences."""
    frames = []
    for i in range(n):
        values = {name: column[i] for name, column in columns.items()}
        values.setdefault("timestamp_ms", i * period_ms)
        frames.append(make_frame(i, **values))
    return frames
