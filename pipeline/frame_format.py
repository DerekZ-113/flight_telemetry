"""Byte layout of the telemetry log and of every ZeroMQ message (REQ-LOG-001, REQ-TRANS-001).

This module is the Python side of three C++ definitions and must change with them:

    src/telemetry_frame.h            TelemetryFrame  ->  FRAME, FRAME_FIELDS
    src/processing/fault_detector.h  FaultEvent      ->  EVENT, EVENT_FIELDS
    src/logging/log_format.h         LogFileHeader   ->  HEADER, LOG_FORMAT_VERSION

A log file is HEADER followed by {tag byte, record} pairs; a ZeroMQ message is
HEADER followed by exactly one such pair (FTS-DD-001 section 9). The C++ side
writes the structs' in-memory bytes verbatim, so the formats here use '@':
native byte order and native alignment. Format version 2 names every byte, so
there is no padding to reproduce: reserved_a sits where version 1 had four
padding bytes before latitude, and the five validity bytes and reserved_b
where it had six after gps_read_ok; the event's reserved sits where it had
two after type. A version-1 file therefore unpacks with the same Structs.
This module reads versions 1 to 2 (REQ-LOG-005) and keeps a file's bytes as
they are: the C++ reader zeroes a version-1 frame's validity and reserved
bytes, but here the byte-identity tests compare files, so nothing is rewritten.

The header carries the sizes the sender actually used, and check_header()
compares them with these formats on every file and every message, so a layout
change on either side is refused with a message instead of producing
plausible garbage.
"""
from __future__ import annotations

import struct
from collections import namedtuple
from typing import Iterator, NamedTuple

LOG_FORMAT_VERSION = 2                  # kLogFormatVersion: what the binary writes
OLDEST_READABLE_LOG_FORMAT_VERSION = 1  # kOldestReadableLogFormatVersion
MAGIC = b"FTLG"
ENDIAN_MARKER = 0x01020304      # kEndianMarker as this host stores it

HEADER = struct.Struct("@4sHHHHI")        # 16 bytes: magic, version, frame_size, event_size, reserved, endian_marker
FRAME = struct.Struct("@Q11fI2d5f3B3?5BB")  # 104 bytes, no implicit padding
EVENT = struct.Struct("@QBBHf")             # 16 bytes

TAG_FRAME = 1                   # RecordType::FRAME
TAG_EVENT = 2                   # RecordType::FAULT_EVENT

# Declaration order of src/telemetry_frame.h, which is the byte order.
FRAME_FIELDS = (
    "timestamp_ms",
    "pressure_hpa", "temperature_c", "baro_altitude_m",
    "accel_x", "accel_y", "accel_z",
    "gyro_x", "gyro_y", "gyro_z",
    "pitch_deg", "roll_deg",
    "reserved_a",
    "latitude", "longitude",
    "gps_altitude_m", "ground_speed_mps", "heading_deg",
    "fused_altitude_m", "vertical_speed_mps",
    "baro_status", "imu_status", "gps_status",
    "baro_read_ok", "imu_read_ok", "gps_read_ok",
    "baro_altitude_validity", "pitch_validity", "roll_validity",
    "fused_altitude_validity", "vertical_speed_validity",
    "reserved_b",
)
EVENT_FIELDS = ("timestamp_ms", "channel", "type", "reserved", "value")

STATUS_NAMES = {0: "NOMINAL", 1: "DEGRADED", 2: "NO_DATA"}                      # ChannelStatus
VALIDITY_NAMES = {0: "NOT_JUDGED", 1: "NO_COMPUTED_DATA", 2: "FAILURE_WARNING", 3: "NORMAL"}   # Validity
VALIDITY_NORMAL = 3
# Each computed field and the validity byte that judges it (REQ-PROC-007).
COMPUTED_VALIDITY = {
    "baro_altitude_m": "baro_altitude_validity",
    "pitch_deg": "pitch_validity",
    "roll_deg": "roll_validity",
    "fused_altitude_m": "fused_altitude_validity",
    "vertical_speed_mps": "vertical_speed_validity",
}
VALIDITY_FIELDS = tuple(COMPUTED_VALIDITY.values())
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
    if not OLDEST_READABLE_LOG_FORMAT_VERSION <= header.version <= LOG_FORMAT_VERSION:
        raise FormatError(f"format version {header.version}; this module reads "
                          f"{OLDEST_READABLE_LOG_FORMAT_VERSION} to {LOG_FORMAT_VERSION}")
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
