"""TC-022 steps 1 through 7 and TC-025 steps 1 through 4 (REQ-DISP-005; also REQ-DISP-002,
REQ-DISP-006, REQ-TRANS-004, REQ-PROC-007, REQ-TEST-002).

The bridge is driven in-process: a scripted byte source stands in for the transport, a
websockets client stands in for the page, and asyncio.run drives each check. TC-022 step 7 runs
the binary as a paced replay through a real ZeroMQ socket and the bridge to a client. TC-025
adds the position relative to the first fix (the fix itself never sent, the origin reset with
the lag's) and the two display thresholds that ride hello.
"""
from __future__ import annotations

import asyncio
import json
import math
import queue
import subprocess
import time

import pytest
from websockets.asyncio.client import connect

from conftest import CONFIG_ARGS, free_tcp_port
from pipeline.bridge import DEFAULT_HOST, Bridge
from pipeline.frame_format import FRAME_FIELDS
from pipeline.receiver import ZmqSource
from synthetic_log import log_bytes, make_frame

PERIOD_MS = 20


def status(mode: int, state: int, identity: bytes = bytes(11)) -> bytes:
    """A status as the binary writes one (version 2): the eleven identity bytes zero unless given."""
    return b"FTSS" + bytes([2, mode, state, 0]) + identity


RUNNING = status(3, 1)   # REPLAY, RUNNING
ENDED = status(3, 2)     # REPLAY, ENDED
IDENTITY_A = bytes.fromhex("a1b2c3") + b"TEST001 "
IDENTITY_B = bytes.fromhex("b2c3d4") + b"TEST002 "
ADSB_A = status(4, 1, IDENTITY_A)   # ADSB, RUNNING, the first aircraft
ADSB_B = status(4, 1, IDENTITY_B)


class QueueSource:
    """A byte source the test feeds: recv() hands out the next message, or None after a short quiet."""

    endpoint = "queue"

    def __init__(self) -> None:
        self._queue: queue.Queue[bytes] = queue.Queue()

    def feed(self, *messages: bytes) -> None:
        for message in messages:
            self._queue.put(message)

    def recv(self) -> bytes | None:
        try:
            return self._queue.get(timeout=0.05)
        except queue.Empty:
            return None

    def close(self) -> None:
        pass


def frame_bytes(index: int, **overrides) -> bytes:
    return log_bytes([make_frame(index, **overrides)])


async def recv_json(ws, timeout: float = 5.0) -> dict:
    return json.loads(await asyncio.wait_for(ws.recv(), timeout))


def ws_url(bridge: Bridge) -> str:
    host, port = bridge.address
    return f"ws://{host}:{port}"


# --- step 1: loopback unless told otherwise -------------------------------------------------


def test_bridge_listens_on_loopback_by_default():
    async def check():
        async with Bridge(QueueSource(), port=0) as bridge:
            host, port = bridge.address
            assert host == DEFAULT_HOST == "127.0.0.1"
            assert port > 0
        async with Bridge(QueueSource(), host="0.0.0.0", port=0) as bridge:
            assert bridge.address[0] == "0.0.0.0"
    asyncio.run(check())


# --- step 2: hello first ----------------------------------------------------------------------


def test_client_is_greeted_with_hello():
    async def check():
        async with Bridge(QueueSource(), port=0, stale_ms=1000, period_ms=PERIOD_MS) as bridge:
            async with connect(ws_url(bridge)) as ws:
                hello = await recv_json(ws)
        assert hello == {"type": "hello", "stale_ms": 1000, "period_ms": PERIOD_MS, "session": None,
                         "frames_received": 0, "gaps": 0, "out_of_order": 0,
                         "track_min_speed_mps": 1.0, "lag_ms": 500}
    asyncio.run(check())


# --- step 3: a frame by name, every field but the coordinates ---------------------------------


def test_frame_is_forwarded_by_name_without_coordinates():
    source = QueueSource()

    async def check():
        async with Bridge(source, port=0) as bridge:
            async with connect(ws_url(bridge)) as ws:
                await recv_json(ws)   # hello
                source.feed(frame_bytes(2, pressure_hpa=1001.5, temperature_c=21.5, gps_altitude_m=12.5,
                                        latitude=37.5, longitude=-122.25,
                                        baro_status=0, imu_status=1, gps_status=2, imu_read_ok=False,
                                        baro_altitude_validity=3, pitch_validity=2, roll_validity=1,
                                        fused_altitude_validity=0, vertical_speed_validity=3))
                message = await recv_json(ws)
            assert bridge.forwarded == 1
        return message

    message = asyncio.run(check())
    assert message["type"] == "frame"
    assert set(message["frame"]) == set(FRAME_FIELDS) - {"latitude", "longitude"}
    assert message["frame"]["timestamp_ms"] == 2 * PERIOD_MS
    assert message["frame"]["pressure_hpa"] == pytest.approx(1001.5)
    assert message["frame"]["gps_altitude_m"] == pytest.approx(12.5)
    assert message["frame"]["imu_read_ok"] is False and message["frame"]["baro_read_ok"] is True
    assert message["validity"] == {"baro_altitude_m": "NORMAL", "pitch_deg": "FAILURE_WARNING",
                                   "roll_deg": "NO_COMPUTED_DATA", "fused_altitude_m": "NOT_JUDGED",
                                   "vertical_speed_mps": "NORMAL"}
    assert message["status"] == {"baro": "NOMINAL", "imu": "DEGRADED", "gps": "NO_DATA"}
    assert isinstance(message["received_at_ms"], (int, float))
    text = json.dumps(message)
    assert "latitude" not in text and "longitude" not in text
    assert "37.5" not in text and "122.25" not in text


# --- steps 4 and 5: the session status, to clients present and to come -----------------------


def test_session_status_is_forwarded_and_greets_a_later_client():
    source = QueueSource()

    async def check():
        async with Bridge(source, port=0) as bridge:
            async with connect(ws_url(bridge)) as first:
                await recv_json(first)
                source.feed(RUNNING)
                session = await recv_json(first)
                assert session["type"] == "session"
                assert (session["mode"], session["state"]) == ("REPLAY", "RUNNING")
                assert isinstance(session["received_at_ms"], (int, float))
                async with connect(ws_url(bridge)) as second:
                    hello = await recv_json(second)
                    assert hello["session"] == {"mode": "REPLAY", "state": "RUNNING", "identity": None}
                    source.feed(ENDED)
                    for ws in (first, second):
                        ended = await recv_json(ws)
                        assert (ended["type"], ended["state"]) == ("session", "ENDED")
            assert bridge.statuses == 2
    asyncio.run(check())


# --- step 6: several clients, one leaving ------------------------------------------------------


def test_every_client_receives_every_frame_and_one_leaving_stops_nothing():
    source = QueueSource()

    async def check():
        async with Bridge(source, port=0) as bridge:
            async with connect(ws_url(bridge)) as stayer:
                await recv_json(stayer)
                async with connect(ws_url(bridge)) as leaver:
                    await recv_json(leaver)
                    source.feed(*(frame_bytes(i) for i in range(10)))
                    for ws in (stayer, leaver):
                        got = [(await recv_json(ws))["frame"]["timestamp_ms"] for _ in range(10)]
                        assert got == [i * PERIOD_MS for i in range(10)]
                source.feed(*(frame_bytes(i) for i in range(10, 20)))
                got = [(await recv_json(stayer))["frame"]["timestamp_ms"] for _ in range(10)]
                assert got == [i * PERIOD_MS for i in range(10, 20)]
            assert bridge.forwarded == 20
    asyncio.run(check())


# --- step 7: end to end, a paced replay through a real socket and the bridge -----------------


def test_paced_replay_reaches_a_client_at_the_recorded_cadence(telemetry_bin, tmp_path, sim_session):
    port = free_tcp_port()
    source = ZmqSource(f"tcp://127.0.0.1:{port}", timeout_ms=200, reconnect_ivl_ms=1)
    cwd = tmp_path / "run"
    cwd.mkdir()

    async def check():
        async with Bridge(source, port=0) as bridge:
            async with connect(ws_url(bridge)) as ws:
                await recv_json(ws)
                with open(cwd / "stdout.txt", "w") as out:
                    process = subprocess.Popen(
                        [str(telemetry_bin), *CONFIG_ARGS, "--replay", str(sim_session.log), "--pace",
                         "--cycles", "100", "--zmq", f"tcp://127.0.0.1:{port}"],
                        cwd=cwd, stdout=out, stderr=subprocess.STDOUT)
                    frames: list[dict] = []
                    arrivals: list[float] = []
                    ended = None
                    try:
                        while ended is None:
                            message = await recv_json(ws, timeout=10)
                            if message["type"] == "frame":
                                frames.append(message)
                                arrivals.append(time.perf_counter())
                            elif message["type"] == "session" and message["state"] == "ENDED":
                                ended = message
                    finally:
                        process.wait(timeout=30)
                assert process.returncode == 0, (cwd / "stdout.txt").read_text()[-400:]
        return frames, arrivals, ended

    frames, arrivals, ended = asyncio.run(check())
    timestamps = [message["frame"]["timestamp_ms"] for message in frames]
    join = timestamps[0] // PERIOD_MS
    assert join <= 5, f"the subscription reached the publisher only at frame {join}"
    assert timestamps == list(range(join * PERIOD_MS, 100 * PERIOD_MS, PERIOD_MS))
    span = arrivals[-1] - arrivals[0]
    expected = (len(frames) - 1) * PERIOD_MS / 1000
    assert 0.8 * expected < span < 1.3 * expected, f"{len(frames)} frames over {span:.2f} s, not about {expected:.2f}"
    assert ended["mode"] == "REPLAY"
    print(f"{len(frames)} paced frames through the bridge over {span:.2f} s (recorded span {expected:.2f} s)")


# --- TC-025 step 1: the display's thresholds ride hello (REQ-DISP-002, REQ-DISP-006) ----------


def test_hello_carries_the_display_thresholds():
    async def check():
        async with Bridge(QueueSource(), port=0) as bridge:
            async with connect(ws_url(bridge)) as ws:
                shipped = await recv_json(ws)
        async with Bridge(QueueSource(), port=0, track_min_speed_mps=2.5, lag_ms=250) as bridge:
            async with connect(ws_url(bridge)) as ws:
                flagged = await recv_json(ws)
        return shipped, flagged

    shipped, flagged = asyncio.run(check())
    assert (shipped["track_min_speed_mps"], shipped["lag_ms"]) == (1.0, 500)
    assert (flagged["track_min_speed_mps"], flagged["lag_ms"]) == (2.5, 250)


# --- TC-025 steps 2 and 3: a position relative to the first fix; the fix itself never sent ------

LATITUDE, LONGITUDE = 51.234567, -0.987654
METERS_PER_DEGREE = 6_371_000 * math.pi / 180     # the bridge's spherical approximation, and the profile's
ONE_METRE_NORTH = 1 / METERS_PER_DEGREE
ONE_METRE_EAST = 1 / (METERS_PER_DEGREE * math.cos(math.radians(LATITUDE)))


def fix(index: int, north_m: float = 0.0, east_m: float = 0.0, **overrides) -> bytes:
    """A NOMINAL fresh fix north_m and east_m of the test's spot, unless overridden."""
    return frame_bytes(index, latitude=LATITUDE + north_m * ONE_METRE_NORTH,
                       longitude=LONGITUDE + east_m * ONE_METRE_EAST, **overrides)


def test_position_is_relative_to_the_first_fix_and_the_origin_never_leaves():
    source = QueueSource()

    async def check():
        async with Bridge(source, port=0) as bridge:
            async with connect(ws_url(bridge)) as ws:
                await recv_json(ws)
                source.feed(fix(0, gps_status=2),                                  # NO_DATA: no fix yet
                            fix(1),                                                # the first NOMINAL fresh fix
                            fix(2, north_m=1.0, east_m=1.0),                       # a metre north and east
                            fix(3, north_m=1.0, east_m=1.0, gps_read_ok=False))    # the same fix, held
                return [await recv_json(ws) for _ in range(4)]

    messages = asyncio.run(check())
    positions = [message["position"] for message in messages]
    assert positions[0] is None
    assert positions[1] == {"east_m": 0.0, "north_m": 0.0}
    assert positions[2]["east_m"] == pytest.approx(1.0, abs=0.01)
    assert positions[2]["north_m"] == pytest.approx(1.0, abs=0.01)
    assert positions[3] == positions[2]
    for message in messages:
        text = json.dumps(message)
        assert "latitude" not in text and "longitude" not in text
        assert "51.23" not in text and "0.9876" not in text


# --- TC-025 step 4: the origin resets as the lag's does (REQ-DISP-006) ------------------------


def test_origin_resets_after_a_backwards_timestamp_and_after_ended():
    source = QueueSource()

    async def check():
        async with Bridge(source, port=0) as bridge:
            async with connect(ws_url(bridge)) as ws:
                await recv_json(ws)
                source.feed(fix(5), fix(6, north_m=1.0))      # the origin at t=100, then a metre north
                first = [await recv_json(ws) for _ in range(2)]
                source.feed(fix(2, north_m=2.0))               # t=40 after t=120: a restarted binary
                restarted = await recv_json(ws)
                source.feed(ENDED, fix(3, north_m=3.0))        # an end, then a frame: a new session
                ended = await recv_json(ws)
                after_end = await recv_json(ws)
                return first, restarted, ended, after_end

    first, restarted, ended, after_end = asyncio.run(check())
    assert first[0]["position"] == {"east_m": 0.0, "north_m": 0.0}
    assert first[1]["position"]["north_m"] == pytest.approx(1.0, abs=0.01)
    assert restarted["position"] == {"east_m": 0.0, "north_m": 0.0}, "a backwards timestamp must re-take the origin"
    assert (ended["type"], ended["state"]) == ("session", "ENDED")
    assert after_end["position"] == {"east_m": 0.0, "north_m": 0.0}, "a frame after ENDED must re-take the origin"


# --- TC-030 steps 1 to 3: the aircraft's identity (REQ-DISP-007, REQ-SENS-009) -----------------


def test_identity_is_forwarded_in_session_and_hello():
    source = QueueSource()

    async def check():
        async with Bridge(source, port=0) as bridge:
            async with connect(ws_url(bridge)) as first:
                await recv_json(first)
                source.feed(ADSB_A)
                session = await recv_json(first)
                async with connect(ws_url(bridge)) as second:
                    hello = await recv_json(second)
                source.feed(status(4, 1))   # ADSB, nothing selected
                none_selected = await recv_json(first)
        return session, hello, none_selected

    session, hello, none_selected = asyncio.run(check())
    assert (session["type"], session["mode"], session["state"]) == ("session", "ADSB", "RUNNING")
    assert session["identity"] == {"icao24": "a1b2c3", "callsign": "TEST001"}
    assert hello["session"] == {"mode": "ADSB", "state": "RUNNING", "identity": {"icao24": "a1b2c3", "callsign": "TEST001"}}
    assert none_selected["identity"] is None


def test_origin_is_retaken_when_the_identity_changes():
    source = QueueSource()

    async def check():
        async with Bridge(source, port=0) as bridge:
            async with connect(ws_url(bridge)) as ws:
                await recv_json(ws)
                source.feed(ADSB_A, fix(1), fix(2, north_m=1.0))                 # the first aircraft: its origin, a metre north
                first = [await recv_json(ws) for _ in range(3)]
                source.feed(ADSB_A, fix(3, north_m=2.0))                         # the same aircraft repeated: kept
                repeated = [await recv_json(ws) for _ in range(2)]
                source.feed(ADSB_B, fix(4, north_m=5.0), fix(5, north_m=6.0))   # a new aircraft: a new track
                switched = [await recv_json(ws) for _ in range(3)]
                return first, repeated, switched

    first, repeated, switched = asyncio.run(check())
    assert first[1]["position"] == {"east_m": 0.0, "north_m": 0.0}
    assert first[2]["position"]["north_m"] == pytest.approx(1.0, abs=0.01)
    assert repeated[1]["position"]["north_m"] == pytest.approx(2.0, abs=0.01), "a repeated identity keeps the origin"
    assert switched[0]["identity"] == {"icao24": "b2c3d4", "callsign": "TEST002"}
    assert switched[1]["position"] == {"east_m": 0.0, "north_m": 0.0}, "a new identity re-takes the origin"
    assert switched[2]["position"]["north_m"] == pytest.approx(1.0, abs=0.01)


def test_hide_identity_forwards_none_and_keeps_the_origin_rule():
    source = QueueSource()

    async def check():
        async with Bridge(source, port=0, hide_identity=True) as bridge:
            async with connect(ws_url(bridge)) as ws:
                await recv_json(ws)
                source.feed(ADSB_A, fix(1), fix(2, north_m=1.0), ADSB_B, fix(3, north_m=2.0))
                messages = [await recv_json(ws) for _ in range(5)]
                async with connect(ws_url(bridge)) as later:
                    hello = await recv_json(later)
        return messages, hello

    messages, hello = asyncio.run(check())
    assert messages[0]["identity"] is None and messages[3]["identity"] is None
    assert hello["session"] == {"mode": "ADSB", "state": "RUNNING", "identity": None}
    assert messages[1]["position"] == {"east_m": 0.0, "north_m": 0.0}
    assert messages[2]["position"]["north_m"] == pytest.approx(1.0, abs=0.01)
    assert messages[4]["position"] == {"east_m": 0.0, "north_m": 0.0}, "the hidden identity still re-takes the origin"
    for message in messages:
        assert "a1b2c3" not in json.dumps(message) and "TEST00" not in json.dumps(message)
