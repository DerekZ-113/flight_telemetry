"""Byte layout of the telemetry log and of every message on the wire (REQ-LOG-001, REQ-LOG-005,
REQ-LOG-007, REQ-TRANS-001, REQ-TRANS-004).

This module is the Python side of five C++ definitions and must change with them:

    src/telemetry_frame.h            TelemetryFrame  ->  FRAME, FRAME_FIELDS, FLAG_NAMES, VOTER_STATUS_NAMES
    src/processing/fault_detector.h  FaultEvent      ->  EVENT, EVENT_FIELDS
    src/logging/log_format.h         LogFileHeader   ->  HEADER, LOG_FORMAT_VERSION, frame_size_for
    src/run_mode.h                   RunMode         ->  RUN_MODE_NAMES
    src/transport/session_message.h  session_message ->  SESSION, MODE_NAMES, STATE_NAMES

A log file is HEADER followed by {tag byte, record} pairs; a frame message on either
transport is HEADER followed by exactly one such pair (FTS-DD-001 section 9). The one
message that is not a log record is the session status, nineteen bytes under its own magic
(SESSION below): the run mode, whether the stream has ended and, in ADS-B mode, the identity
of the aircraft the frames present, which the frames cannot say.
A reader tells the two apart by the first four bytes and never finds a status in a file. The C++ side
writes the structs' in-memory bytes verbatim, so the formats here use '@':
native byte order and native alignment. Format version 2 named every byte the compiler used to
pad, so there is no padding to reproduce. Version 3 (2026-10-15) kept every version-2 field where
it was, spent the two reserved fields (the sequence number at 52, the flags byte at 103), renamed
heading_deg course_deg, and appended a 16-byte tail (the ADS-B vertical rate and pressure altitude,
the GPS fix quality and satellite count, the voter's status, one reserved field): 120 bytes. A
file of version 1 or 2 holds 104-byte frames (FRAME_V2), the first 104 bytes of FRAME; this module
reads them and extends each with a zero tail, so one Frame shape serves every version (the prefix
rule, REQ-LOG-005). It keeps a file's bytes as they are: the C++ reader zeroes a version-1 frame's
validity and former reserved bytes, but here the byte-identity tests compare files, so nothing is
rewritten. The header's two bytes after the record sizes were one reserved field until version 3
and are the run mode and the source mode since (REQ-LOG-007), 0 (UNKNOWN) in an older file.

The header carries the sizes the sender actually used, and check_header()
compares them with the layout of the header's own version on every file and every message, so a layout
change on either side is refused with a message instead of producing
plausible garbage.
"""
from __future__ import annotations

import struct
from collections import namedtuple
from typing import Iterator, NamedTuple

LOG_FORMAT_VERSION = 3                  # kLogFormatVersion: what the binary writes
OLDEST_READABLE_LOG_FORMAT_VERSION = 1  # kOldestReadableLogFormatVersion
MAGIC = b"FTLG"
ENDIAN_MARKER = 0x01020304      # kEndianMarker as this host stores it

HEADER = struct.Struct("@4sHHHBBI")        # 16 bytes: magic, version, frame_size, event_size, run_mode, source_mode, endian_marker
FRAME = struct.Struct("@Q11fI2d5f3B3?5BB2fBBHI")   # 120 bytes, format version 3, no implicit padding
FRAME_V2 = struct.Struct("@Q11fI2d5f3B3?5BB")      # 104 bytes: a frame of versions 1 and 2, the first 104 of FRAME
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
    "sequence",
    "latitude", "longitude",
    "gps_altitude_m", "ground_speed_mps", "course_deg",
    "fused_altitude_m", "vertical_speed_mps",
    "baro_status", "imu_status", "gps_status",
    "baro_read_ok", "imu_read_ok", "gps_read_ok",
    "baro_altitude_validity", "pitch_validity", "roll_validity",
    "fused_altitude_validity", "vertical_speed_validity",
    "flags",
    "gps_vertical_rate_mps", "gps_pressure_altitude_m",
    "gps_fix_quality", "gps_satellites",
    "voter_status", "reserved_d",
)
EVENT_FIELDS = ("timestamp_ms", "channel", "type", "reserved", "value")

STATUS_NAMES = {0: "NOMINAL", 1: "DEGRADED", 2: "NO_DATA", 3: "NOT_AVAILABLE"}   # ChannelStatus
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
# The flags byte's bits (FrameFlag): facts a source states about a frame (REQ-PROC-008, REQ-SENS-005,
# REQ-SENS-008); the voter's status bits (VoterStatus), zero meaning no voter ran.
FLAG_NAMES = {0x01: "NEW_VEHICLE", 0x02: "VERTICAL_RATE", 0x04: "PRESSURE_ALTITUDE", 0x08: "FIX_QUALITY"}
VOTER_STATUS_NAMES = {0x0001: "VOTED", 0x0002: "LANE_1_FAULTED", 0x0004: "LANE_2_FAULTED",
                      0x0008: "LANE_3_FAULTED", 0x0010: "NO_MAJORITY"}
# RunMode as the log header records it (REQ-LOG-007): UNKNOWN in a file older than version 3.
RUN_MODE_NAMES = {0: "UNKNOWN", 1: "LIVE", 2: "SIMULATED", 3: "REPLAY", 4: "ADSB"}

# The session status message (REQ-TRANS-004): src/transport/session_message.h.
SESSION_MAGIC = b"FTSS"
SESSION_MESSAGE_VERSION = 2                                     # kSessionMessageVersion: version 1 (eight bytes) is no longer read
SESSION = struct.Struct("@4sBBBB3s8s")                          # 19 bytes: magic, version, mode, state, reserved, icao24, callsign
NO_IDENTITY = bytes(11)                                         # the eleven identity bytes zero: nothing selected, or another mode
MODE_NAMES = {1: "LIVE", 2: "SIMULATED", 3: "REPLAY", 4: "ADSB"}   # RunMode as the status names the run
STATE_NAMES = {1: "RUNNING", 2: "ENDED"}                        # StreamState
FAULT_TYPE_NAMES = {0: "COMM_TIMEOUT", 1: "STUCK", 2: "OUT_OF_RANGE", 3: "RECOVERY"}   # FaultType

assert len(FRAME_FIELDS) == len(FRAME.unpack(bytes(FRAME.size)))
# The prefix rule (REQ-LOG-005): a frame of versions 1 and 2 is the first 104 bytes of a version-3
# frame, every one of its fields where it was, so it reads into the same shape with a zero tail.
assert FRAME.format.startswith(FRAME_V2.format), "a version-2 frame must be a prefix of a version-3 frame"
FRAME_V2_FIELDS = FRAME_FIELDS[:len(FRAME_V2.unpack(bytes(FRAME_V2.size)))]   # the 32 fields of versions 1 and 2
_ZERO_TAIL = FRAME.unpack(bytes(FRAME.size))[len(FRAME_V2_FIELDS):]           # the six they lack, as zeros
assert len(EVENT_FIELDS) == len(EVENT.unpack(bytes(EVENT.size)))

Frame = namedtuple("Frame", FRAME_FIELDS)
FaultEvent = namedtuple("FaultEvent", EVENT_FIELDS)


class Header(NamedTuple):
    magic: bytes
    version: int
    frame_size: int
    event_size: int
    run_mode: int             # RunMode: how the session that wrote the file ran (REQ-LOG-007); 0 before version 3
    source_mode: int          # RunMode: how its frames were originally produced; 0 before version 3
    endian_marker: int


class SessionStatus(NamedTuple):
    """One status message, by name: the run mode, the stream's state and, in ADS-B mode, the
    selected aircraft's identity (REQ-SENS-009, REQ-DISP-007)."""
    mode: str                                 # LIVE, SIMULATED, REPLAY or ADSB
    state: str                                # RUNNING or ENDED
    identity: tuple[str, str] | None = None   # (icao24 as six hex digits, callsign stripped), or None


class FormatError(ValueError):
    """The bytes do not match the layout this module describes."""


def parse_header(buf: bytes, offset: int = 0) -> Header:
    if len(buf) - offset < HEADER.size:
        raise FormatError(f"need {HEADER.size} header bytes, have {len(buf) - offset}")
    return Header._make(HEADER.unpack_from(buf, offset))


def frame_size_for(version: int) -> int:
    """The frame size a version's layout implies (log_format.h's frame_size_for); 0 for a version
    this module does not define."""
    if version in (1, 2):
        return FRAME_V2.size
    return FRAME.size if version == LOG_FORMAT_VERSION else 0


def frame_layout(version: int) -> struct.Struct:
    """The Struct a file of `version` holds its frames in: 104 bytes before version 3, 120 since."""
    return FRAME_V2 if version in (1, 2) else FRAME


def check_header(header: Header) -> None:
    """Refuse anything this module cannot read correctly, naming the mismatch."""
    if header.magic != MAGIC:
        raise FormatError(f"bad magic {header.magic!r}, expected {MAGIC!r}")
    if not OLDEST_READABLE_LOG_FORMAT_VERSION <= header.version <= LOG_FORMAT_VERSION:
        raise FormatError(f"format version {header.version}; this module reads "
                          f"{OLDEST_READABLE_LOG_FORMAT_VERSION} to {LOG_FORMAT_VERSION}")
    expected = frame_size_for(header.version)   # the header's own version's, not this module's newest
    if header.frame_size != expected:
        raise FormatError(f"frame size {header.frame_size} in the header, {expected} for version "
                          f"{header.version} in this module")
    if header.event_size != EVENT.size:
        raise FormatError(f"event size {header.event_size} in the header, {EVENT.size} in this module")
    if header.endian_marker != ENDIAN_MARKER:
        raise FormatError(f"endianness marker {header.endian_marker:#010x}: written on a host with the other byte order")


def parse_record(buf: bytes, offset: int, version: int = LOG_FORMAT_VERSION) -> tuple[Frame | FaultEvent, int]:
    """The tagged record at `offset` of a file or message of `version`; returns it and the offset
    just after it. A frame of version 1 or 2 is the first 104 bytes of a version-3 frame and comes
    back with a zero tail (the prefix rule), so every Frame has the same fields."""
    if offset >= len(buf):
        raise FormatError("no record at the end of the data")
    tag = buf[offset]
    if tag == TAG_FRAME:
        layout = frame_layout(version)
        make = Frame._make if layout is FRAME else (lambda values: Frame._make(values + _ZERO_TAIL))
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
    header = parse_header(buf)
    check_header(header)
    offset = HEADER.size
    while offset < len(buf):
        record, offset = parse_record(buf, offset, header.version)
        yield record


def parse_session(buf: bytes) -> SessionStatus:
    """The status in `buf`, by name; a length, version, mode or state this module does not
    know is refused, as a header it cannot read is. The identity is None when its eleven
    bytes are zero; otherwise the address as hex digits and the callsign with its padding
    stripped (an aircraft without a callsign reads as an empty string, not as None)."""
    if len(buf) != SESSION.size:
        raise FormatError(f"session status of {len(buf)} bytes, expected {SESSION.size}")
    _magic, version, mode, state, _reserved, icao24, callsign = SESSION.unpack(buf)
    if version != SESSION_MESSAGE_VERSION:
        raise FormatError(f"session status version {version}; this module reads {SESSION_MESSAGE_VERSION}")
    if mode not in MODE_NAMES:
        raise FormatError(f"session status mode {mode} is not one this module names")
    if state not in STATE_NAMES:
        raise FormatError(f"session status state {state} is not one this module names")
    identity = None
    if icao24 + callsign != NO_IDENTITY:
        identity = (icao24.hex(), callsign.decode("ascii", "replace").rstrip(" \0"))
    return SessionStatus(MODE_NAMES[mode], STATE_NAMES[state], identity)


def parse_message(buf: bytes) -> Frame | FaultEvent | SessionStatus:
    """A message from either transport: a header and exactly one record, nothing after it; or
    a session status, told apart by its first four bytes (REQ-TRANS-004)."""
    if buf[:4] == SESSION_MAGIC:
        return parse_session(buf)
    header = parse_header(buf)
    check_header(header)
    record, end = parse_record(buf, HEADER.size, header.version)
    if end != len(buf):
        raise FormatError(f"message has {len(buf) - end} bytes after its record")
    return record


if __name__ == "__main__":
    print(f"header {HEADER.size} bytes, frame {FRAME.size} bytes ({FRAME_V2.size} before format version 3), "
          f"event {EVENT.size} bytes")
