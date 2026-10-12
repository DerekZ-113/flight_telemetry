"""Byte layout of the telemetry log and of every ZeroMQ message (REQ-LOG-001, REQ-TRANS-001).

This module is the Python side of three C++ definitions and must change with them:

    src/telemetry_frame.h            TelemetryFrame  ->  FRAME, FRAME_FIELDS
    src/processing/fault_detector.h  FaultEvent      ->  EVENT, EVENT_FIELDS
    src/logging/log_format.h         LogFileHeader   ->  HEADER, LOG_FORMAT_VERSION

A log file is HEADER followed by {tag byte, record} pairs; a ZeroMQ message is
HEADER followed by exactly one such pair (FTS-DD-001 section 9). The C++ side
writes the structs' in-memory bytes verbatim, so the formats here use '@':
native byte order and native alignment, including the padding the compiler
inserts (4 bytes before latitude, 6 after gps_read_ok). The trailing '0Q'
aligns the end of the frame to 8 bytes the way the compiler does; without it
struct reports 98 bytes, not 104, and every frame after the first is misread.

The header carries the sizes the sender actually used, and check_header()
compares them with these formats on every file and every message, so a layout
change on either side is refused with a message instead of producing
plausible garbage.
"""
from __future__ import annotations

import struct
from collections import namedtuple
from typing import Iterator, NamedTuple

LOG_FORMAT_VERSION = 1          # kLogFormatVersion
MAGIC = b"FTLG"
ENDIAN_MARKER = 0x01020304      # kEndianMarker as this host stores it

HEADER = struct.Struct("@4sHHHHI")        # 16 bytes: magic, version, frame_size, event_size, reserved, endian_marker
FRAME = struct.Struct("@Q11f2d5f3B3?0Q")  # 104 bytes
EVENT = struct.Struct("@QBBf0Q")          # 16 bytes

TAG_FRAME = 1                   # RecordType::FRAME
TAG_EVENT = 2                   # RecordType::FAULT_EVENT

# Declaration order of src/telemetry_frame.h, which is the byte order.
FRAME_FIELDS = (
    "timestamp_ms",
    "pressure_hpa", "temperature_c", "baro_altitude_m",
    "accel_x", "accel_y", "accel_z",
    "gyro_x", "gyro_y", "gyro_z",
    "pitch_deg", "roll_deg",
    "latitude", "longitude",
    "gps_altitude_m", "ground_speed_mps", "heading_deg",
    "fused_altitude_m", "vertical_speed_mps",
    "baro_status", "imu_status", "gps_status",
    "baro_read_ok", "imu_read_ok", "gps_read_ok",
)
EVENT_FIELDS = ("timestamp_ms", "channel", "type", "value")

STATUS_NAMES = {0: "NOMINAL", 1: "DEGRADED"}                                    # ChannelStatus
CHANNEL_NAMES = {0: "BARO", 1: "IMU", 2: "GPS"}                                 # Channel
FAULT_TYPE_NAMES = {0: "COMM_TIMEOUT", 1: "STUCK", 2: "OUT_OF_RANGE", 3: "RECOVERY"}   # FaultType

assert len(FRAME_FIELDS) == len(FRAME.unpack(bytes(FRAME.size)))
assert len(EVENT_FIELDS) == len(EVENT.unpack(bytes(EVENT.size)))

Frame = namedtuple("Frame", FRAME_FIELDS)
FaultEvent = namedtuple("FaultEvent", EVENT_FIELDS)


class Header(NamedTuple):
    magic: bytes
    version: int
    frame_size: int
    event_size: int
    reserved: int
    endian_marker: int


class FormatError(ValueError):
    """The bytes do not match the layout this module describes."""


def parse_header(buf: bytes, offset: int = 0) -> Header:
    if len(buf) - offset < HEADER.size:
        raise FormatError(f"need {HEADER.size} header bytes, have {len(buf) - offset}")
    return Header._make(HEADER.unpack_from(buf, offset))


def check_header(header: Header) -> None:
    """Refuse anything this module cannot read correctly, naming the mismatch."""
    if header.magic != MAGIC:
        raise FormatError(f"bad magic {header.magic!r}, expected {MAGIC!r}")
    if header.version != LOG_FORMAT_VERSION:
        raise FormatError(f"format version {header.version}; this module reads version {LOG_FORMAT_VERSION}")
    if header.frame_size != FRAME.size:
        raise FormatError(f"frame size {header.frame_size} in the header, {FRAME.size} in this module")
    if header.event_size != EVENT.size:
        raise FormatError(f"event size {header.event_size} in the header, {EVENT.size} in this module")
    if header.endian_marker != ENDIAN_MARKER:
        raise FormatError(f"endianness marker {header.endian_marker:#010x}: written on a host with the other byte order")


def parse_record(buf: bytes, offset: int) -> tuple[Frame | FaultEvent, int]:
    """The tagged record at `offset`; returns it and the offset just after it."""
    if offset >= len(buf):
        raise FormatError("no record at the end of the data")
    tag = buf[offset]
    if tag == TAG_FRAME:
        layout, make = FRAME, Frame._make
    elif tag == TAG_EVENT:
        layout, make = EVENT, FaultEvent._make
    else:
        raise FormatError(f"unknown record tag {tag} at offset {offset}")
    start = offset + 1
    if len(buf) - start < layout.size:
        raise FormatError(f"torn record: tag {tag} at offset {offset} needs {layout.size} bytes, {len(buf) - start} remain")
    return make(layout.unpack_from(buf, start)), start + layout.size


def iter_records(buf: bytes) -> Iterator[Frame | FaultEvent]:
    """Every record in a log file's bytes, after validating its header."""
    check_header(parse_header(buf))
    offset = HEADER.size
    while offset < len(buf):
        record, offset = parse_record(buf, offset)
        yield record


def parse_message(buf: bytes) -> Frame | FaultEvent:
    """A ZeroMQ message: a header and exactly one record, nothing after it."""
    check_header(parse_header(buf))
    record, end = parse_record(buf, HEADER.size)
    if end != len(buf):
        raise FormatError(f"message has {len(buf) - end} bytes after its record")
    return record


if __name__ == "__main__":
    print(f"header {HEADER.size} bytes, frame {FRAME.size} bytes, event {EVENT.size} bytes")
