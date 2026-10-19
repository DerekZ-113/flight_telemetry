"""The voter as the binary, the receiver and the Python reader present it (REQ-RED-001 to REQ-RED-004,
REQ-RED-006; TC-032 step 11, TC-034; also REQ-LOG-003, REQ-TEST-002).

The voter's events ride new values of the event's existing channel and type bytes (FTS-DD-001
Section 9), so the twin must name them as the C++ side does and print them as the console does.
"""
from __future__ import annotations

from pipeline import frame_format
from pipeline.frame_format import FaultEvent
from pipeline.receiver import format_event


# The twin's names for the voter's channel and the four lane types, pinned with their numbers, which
# are what a log carries (FTS-FM-001 FAULT-019 to FAULT-022), and the receiver's line for one, the
# console's [FAULT] line (TC-032 step 11).
def test_voter_event_names():
    assert frame_format.CHANNEL_NAMES == {0: "BARO", 1: "IMU", 2: "GPS", 3: "VOTER"}
    assert frame_format.FAULT_TYPE_NAMES == {0: "COMM_TIMEOUT", 1: "STUCK", 2: "OUT_OF_RANGE", 3: "RECOVERY",
                                             4: "LANE_DISAGREEMENT", 5: "LANE_FAULTED", 6: "NO_MAJORITY",
                                             7: "LANE_OVERRUN"}
    event = FaultEvent(timestamp_ms=60, channel=3, type=5, reserved=0, value=3.0)
    assert format_event(event) == "[FAULT] t=60 ms channel=VOTER type=LANE_FAULTED value=3.0"
