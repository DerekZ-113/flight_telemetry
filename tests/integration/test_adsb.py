"""TC-029 steps 12 through 18 and TC-021 step 10 at the integration level (REQ-SENS-008,
REQ-SENS-009, REQ-SENS-010, REQ-SENS-011, REQ-CFG-004, REQ-TRANS-004, REQ-FAULT-001,
REQ-FAULT-007; also REQ-LOG-003, REQ-LOG-004, REQ-TEST-002).

A local HTTP server on the loopback stands in for the OpenSky Network at its two paths, the
token endpoint and `states/all`, and the binary reaches it through --adsb-origin: CI never
calls the service, and every response here is synthetic (two aircraft on the 28R final, the
geometry of tests/unit/opensky_examples.h; nothing came from the service). The script, in the
order the binary asks: a token for the credentials the test wrote; states responses whose
position times advance, the nearer aircraft landing at the fourth; a 401 at the fifth states
request, repaired by a refresh and a retry with the new token; a 429 with Retry-After 1 at the
seventh. One run of 600 cycles, 12 s, at a 1,000 ms poll interval and a 4,000 ms channel
timeout, read by the UDP receiver, feeds every check below; its log is then replayed.

Every test that runs the binary with --adsb passes --adsb-credentials with a path of its own,
so no test can read the real file at the default location; the credentials file sits outside
the run directory, which is searched afterwards for the secret and the token.
"""
from __future__ import annotations

import json
import re
import threading
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import NamedTuple
from urllib.parse import parse_qs, urlparse

import pytest

from conftest import CONFIG_ARGS, SHIPPED_CONFIG, PublishedRun, assert_replay_identical, run_published
from pipeline.frame_format import SessionStatus
from pipeline.log_reader import read_log
from pipeline.receiver import FrameReceiver, UdpSource
from test_config import run

TOKEN_PATH = "/auth/realms/opensky-network/protocol/openid-connect/token"
STATES_PATH = "/api/states/all"
CYCLES = 600
PERIOD_MS = 20
POLL_MS = 1000
TIMEOUT_MS = 4000
CLIENT_ID = "pytest-client"
SECRET = "pytest-secret-7f3a9c"      # exists nowhere else, so every file the run writes can be searched for it
TOKEN_PREFIX = "pytest-token-"
T0 = 1760000000
LANDS_AT = 4                        # the states request at which the nearer aircraft is on the ground
UNAUTHORIZED_AT = 5                 # the states request refused with 401, whatever it carries
RATE_LIMITED_AT = 7                 # the states request answered 429 with Retry-After 1
NOMINAL, NOT_AVAILABLE = 0, 3
NO_COMPUTED_DATA, NORMAL = 1, 3
ENDED_REPEATS = 3

# Two synthetic aircraft on the 28R final at KSFO: A about 5 km from the reference point and
# descending, B about 9 km behind it.
A = {"icao24": "000001", "callsign": "TEST001 ", "lon": -122.3289, "lat": 37.6002, "geo": 480.0, "vel": 72.0}
B = {"icao24": "000002", "callsign": "TEST002 ", "lon": -122.2888, "lat": 37.5833, "geo": 730.0, "vel": 80.0}

# The binary's exit line for the source (main.cpp).
ADSB_LINE = re.compile(
    r"^ADS-B:\s+(?P<polls>\d+) polls \((?P<ok>\d+) ok, (?P<auth>\d+) auth, (?P<refused>\d+) token refused, "
    r"(?P<unavailable>\d+) token unavailable, (?P<unauthorized>\d+) unauthorized, (?P<rate_limited>\d+) rate-limited, "
    r"(?P<http_errors>\d+) http errors, (?P<transport_errors>\d+) transport errors, (?P<malformed>\d+) malformed\), "
    r"(?P<token_requests>\d+) token requests, (?P<readings>\d+) readings, (?P<changes>\d+) selection changes, "
    r"(?P<aircraft>\d+) aircraft and (?P<candidates>\d+) candidates in the last response", re.M)


def state_row(aircraft: dict, time_position: int, on_ground: bool = False) -> list:
    """One row of `states` in the service's order, the 17 elements without extended=1."""
    return [aircraft["icao24"], aircraft["callsign"], "United States", time_position, time_position + 1,
            aircraft["lon"], aircraft["lat"], aircraft["geo"] - 30.0, on_ground, aircraft["vel"], 298.0, -3.5,
            None, None if on_ground else aircraft["geo"], "1200", False, 0]


class Script:
    """The server's answers in the order the binary asks, and a record of every request."""

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.tokens = 0
        self.states_requests = 0
        self.requests: list[tuple[str, str, dict[str, str], bytes]] = []   # method, path, headers, body

    def record(self, method: str, path: str, headers: dict[str, str], body: bytes) -> None:
        with self.lock:
            self.requests.append((method, path, headers, body))

    def token(self) -> str:
        with self.lock:
            self.tokens += 1
            return f"{TOKEN_PREFIX}{self.tokens}"

    def states(self, bearer: str | None) -> tuple[int, dict[str, str], bytes]:
        with self.lock:
            self.states_requests += 1
            n = self.states_requests
            current = f"Bearer {TOKEN_PREFIX}{self.tokens}"
        if n == UNAUTHORIZED_AT or bearer != current:
            # Refused as an expired token would be; every other request must carry the latest token.
            return 401, {}, b'{"error":"invalid_token"}'
        if n == RATE_LIMITED_AT:
            return 429, {"X-Rate-Limit-Retry-After-Seconds": "1"}, b""
        time = T0 + n
        rows = [state_row(B, time - 2)]
        if n < LANDS_AT:
            rows.append(state_row(A, time - 1))
        elif n == LANDS_AT:
            rows.append(state_row(A, time - 1, on_ground=True))
        return 200, {"Content-Type": "application/json"}, json.dumps({"time": time, "states": rows}).encode()


def make_handler(script: Script):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args) -> None:   # quiet
            pass

        def reply(self, status: int, headers: dict[str, str], body: bytes) -> None:
            self.send_response(status)
            for name, value in headers.items():
                self.send_header(name, value)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self) -> None:
            body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
            script.record("POST", self.path, dict(self.headers), body)
            if urlparse(self.path).path != TOKEN_PATH:
                self.reply(404, {}, b"")
                return
            form = parse_qs(body.decode())
            if (form.get("grant_type"), form.get("client_id"), form.get("client_secret")) != \
                    (["client_credentials"], [CLIENT_ID], [SECRET]):
                self.reply(401, {}, b'{"error":"invalid_client"}')
                return
            token = script.token()
            self.reply(200, {"Content-Type": "application/json"},
                       json.dumps({"access_token": token, "expires_in": 1800, "token_type": "Bearer"}).encode())

        def do_GET(self) -> None:
            script.record("GET", self.path, dict(self.headers), b"")
            if urlparse(self.path).path != STATES_PATH:
                self.reply(404, {}, b"")
                return
            self.reply(*script.states(self.headers.get("Authorization")))

    return Handler


@contextmanager
def opensky_stand_in():
    """The local stand-in for the service: its origin and its script."""
    script = Script()
    server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(script))
    thread = threading.Thread(target=server.serve_forever, name="opensky-stand-in", daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{server.server_address[1]}", script
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


def write_credentials(directory: Path) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / "opensky_credentials.json"
    path.write_text(json.dumps({"clientId": CLIENT_ID, "clientSecret": SECRET}))
    path.chmod(0o600)   # the loader refuses a file other users can read
    return path


def write_config(directory: Path, with_block: bool = True) -> Path:
    """A copy of the shipped configuration with the test's ADS-B interval and timeout, or with
    the adsb block removed."""
    text = SHIPPED_CONFIG.read_text()
    if with_block:
        text, n = re.subn(r"^(\s+poll_interval_ms:\s*)\d+", rf"\g<1>{POLL_MS}", text, flags=re.M)
        assert n == 1
        text, n = re.subn(r"^(\s+timeout_ms:\s*)\d+", rf"\g<1>{TIMEOUT_MS}", text, flags=re.M)
        assert n == 1
    else:
        lines = text.splitlines(keepends=True)
        start = next(i for i, line in enumerate(lines) if line.startswith("adsb:"))
        end = next((i for i in range(start + 1, len(lines)) if re.match(r"^[A-Za-z_]+:", lines[i])), len(lines))
        text = "".join(lines[:start] + lines[end:])
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / "telemetry_config.yaml"
    path.write_text(text)
    return path


def adsb_args(origin: str, credentials: Path, config: Path) -> list[str]:
    return ["--adsb", "--adsb-credentials", str(credentials), "--adsb-origin", origin, "--config", str(config)]


def exit_counts(stdout: str) -> dict[str, int]:
    match = ADSB_LINE.search(stdout)
    assert match, f"no ADS-B exit line in:\n{stdout[-1500:]}"
    return {name: int(value) for name, value in match.groupdict().items()}


def first_frame_with(frames, aircraft: dict) -> int:
    return next(i for i, f in enumerate(frames) if abs(f.latitude - aircraft["lat"]) < 1e-9)


class AdsbRun(NamedTuple):
    run: PublishedRun
    script: Script
    cwd: Path
    config: Path
    credentials: Path


@pytest.fixture(scope="module")
def adsb_run(telemetry_bin, tmp_path_factory) -> AdsbRun:
    """TC-029 step 12: the one run every check below reads."""
    root = tmp_path_factory.mktemp("adsb")
    credentials = write_credentials(root / "secrets")
    config = write_config(root / "config")
    cwd = root / "run"
    cwd.mkdir()
    with opensky_stand_in() as (origin, script):
        source = UdpSource("127.0.0.1", 0, timeout_ms=1000)
        receiver = FrameReceiver(source, PERIOD_MS)
        completed = run_published(telemetry_bin, [*adsb_args(origin, credentials, config), "--udp",
                                                 f"127.0.0.1:{source.port}", "--cycles", str(CYCLES)],
                                  cwd, receiver, timeout_s=90)
    assert completed.returncode == 0, completed.stdout[-1500:]
    return AdsbRun(completed, script, cwd, config, credentials)


# --- step 12: the exit line's counters are what the script implies ----------------------------


def test_exit_line_counts_what_the_script_implies(adsb_run):
    counts = exit_counts(adsb_run.run.stdout)
    gets = adsb_run.script.states_requests
    assert gets >= RATE_LIMITED_AT + 2, f"only {gets} states requests in 12 s"
    assert counts["polls"] == gets - 1, "the 401 and its retry are one poll"
    assert 10 <= counts["polls"] <= 14, counts
    assert counts["ok"] == counts["polls"] - 1, "every poll but the 429"
    assert counts["auth"] == 1 and counts["unauthorized"] == 0, "one 401, repaired"
    assert counts["rate_limited"] == 1
    assert counts["refused"] == 0 and counts["unavailable"] == 0
    assert counts["http_errors"] == 0 and counts["transport_errors"] == 0 and counts["malformed"] == 0
    assert counts["token_requests"] == 2 == adsb_run.script.tokens
    assert counts["readings"] == counts["ok"], "every response carried a later position or a new aircraft"
    assert counts["changes"] == 2
    assert (counts["aircraft"], counts["candidates"]) == (1, 1), "only B in the last response"
    assert "position age min/mean/max = 1 / " in adsb_run.run.stdout and " / 2 s" in adsb_run.run.stdout
    assert "selected TEST002 (000002)" in adsb_run.run.stdout
    assert f"credentials from {adsb_run.credentials}" in adsb_run.run.stdout
    assert "ADS-B:" in adsb_run.run.stdout and "Timing:" in adsb_run.run.stdout
    print(f"ADS-B run over the local server: {counts}")


# --- step 13: the log is the source's and the pipeline's -------------------------------------


def test_log_frames_are_the_sources_and_the_pipelines(adsb_run):
    log = read_log(adsb_run.run.log)
    frames = log.frames
    assert len(frames) == CYCLES
    assert not log.events, "no fault event: the absent channels are not failures (REQ-FAULT-007)"
    for i, f in enumerate(frames):
        assert (f.baro_status, f.imu_status) == (NOT_AVAILABLE, NOT_AVAILABLE), i
        assert not f.baro_read_ok and not f.imu_read_ok, i
        assert (f.pressure_hpa, f.temperature_c, f.accel_x, f.accel_y, f.accel_z,
                f.gyro_x, f.gyro_y, f.gyro_z) == (0,) * 8, i
        assert (f.baro_altitude_validity, f.pitch_validity, f.roll_validity) == (NO_COMPUTED_DATA,) * 3, i
        assert f.gps_status == NOMINAL, i   # a reading on frame 0, never silent for 4 s: never NO_DATA or DEGRADED
        assert (f.fused_altitude_validity, f.vertical_speed_validity) == (NORMAL, NORMAL), i
    fresh = [i for i, f in enumerate(frames) if f.gps_read_ok]
    counts = exit_counts(adsb_run.run.stdout)
    assert fresh[0] == 0, "the first poll ran before the loop"
    assert len(fresh) == counts["readings"]
    assert all(b - a >= 40 for a, b in zip(fresh, fresh[1:])), f"one fresh cycle per poll, a second apart: {fresh}"
    assert frames[0].fused_altitude_m == frames[0].gps_altitude_m == pytest.approx(A["geo"]), "born from the GPS reading"
    switch = first_frame_with(frames, B)
    assert frames[switch].gps_read_ok
    assert all(abs(f.latitude - A["lat"]) < 1e-9 and f.gps_altitude_m == pytest.approx(A["geo"]) for f in frames[:switch])
    assert all(abs(f.latitude - B["lat"]) < 1e-9 and f.ground_speed_mps == pytest.approx(B["vel"]) for f in frames[switch:])
    print(f"{len(fresh)} fresh cycles at frames {fresh}; the switch at frame {switch}")


# --- step 14 and TC-021 step 10: the statuses -------------------------------------------------


def test_statuses_carry_the_mode_and_the_identity(adsb_run):
    statuses = adsb_run.run.statuses
    frames = read_log(adsb_run.run.log).frames
    assert len(adsb_run.run.frames) == CYCLES, "every datagram must arrive on the loopback for the positions below"
    assert statuses and all(s.mode == "ADSB" for _, s in statuses)
    running = [(n, s) for n, s in statuses if s.state == "RUNNING"]
    assert running[0] == (0, SessionStatus("ADSB", "RUNNING", ("000001", "TEST001"), "ADSB")), \
        "the first poll's aircraft before frame 0"
    switch = first_frame_with(frames, B)
    assert (switch, SessionStatus("ADSB", "RUNNING", ("000002", "TEST002"), "ADSB")) in running, \
        "a status with the new identity before the first frame that carries it"
    assert all(s.identity == ("000001", "TEST001") for n, s in running if n < switch)
    assert all(s.identity == ("000002", "TEST002") for n, s in running if n >= switch)
    assert [n for n, _ in running if n % 50 == 0] == list(range(0, CYCLES, 50)), "the periodic statuses"
    ended = [(n, s) for n, s in statuses if s.state == "ENDED"]
    assert [n for n, _ in ended] == [CYCLES] * ENDED_REPEATS
    assert all(s.identity == ("000002", "TEST002") for _, s in ended)


# --- step 15: the selection record beside the log ----------------------------------------------


def test_selection_record_beside_the_log(adsb_run):
    frames = read_log(adsb_run.run.log).frames
    switch = first_frame_with(frames, B)
    lines = (adsb_run.cwd / "logs" / "telemetry_adsb.csv").read_text().splitlines()
    assert lines[0] == "timestamp_ms,poll,previous_icao24,previous_callsign,icao24,callsign,reason,distance_m"
    assert len(lines) == 3
    initial, landed = (line.split(",") for line in lines[1:])
    assert initial[:7] == ["0", "1", "", "", "000001", "TEST001", "initial"]
    assert 4800 < float(initial[7]) < 5200
    assert landed[:7] == [str(frames[switch].timestamp_ms), str(LANDS_AT), "000001", "TEST001", "000002", "TEST002", "landed"]
    assert 8800 < float(landed[7]) < 9300
    # The console says the same, at the same frame.
    assert "[ADS-B] t=0 ms none -> TEST001 (000001): initial, " in adsb_run.run.stdout
    assert f"[ADS-B] t={frames[switch].timestamp_ms} ms TEST001 (000001) -> TEST002 (000002): landed, " in adsb_run.run.stdout


# --- step 16: nothing written carries the secret or the token ---------------------------------


def test_nothing_written_carries_the_secret_or_the_token(adsb_run):
    written = [p for p in adsb_run.cwd.rglob("*") if p.is_file()]
    assert any(p.name == "stdout.txt" for p in written) and any(p.suffix == ".bin" for p in written)
    for path in written:
        data = path.read_bytes()
        assert SECRET.encode() not in data, path
        assert TOKEN_PREFIX.encode() not in data, path
    # The secret travelled in the token requests' bodies and nowhere else: never in a URL or a header.
    posts = [r for r in adsb_run.script.requests if r[0] == "POST"]
    assert len(posts) == 2 and all(SECRET in body.decode() for _, _, _, body in posts)
    for method, path, headers, _ in adsb_run.script.requests:
        assert SECRET not in path and all(SECRET not in value for value in headers.values()), (method, path)
    gets = [r for r in adsb_run.script.requests if r[0] == "GET"]
    assert all(h.get("Authorization", "").startswith("Bearer " + TOKEN_PREFIX) for _, _, h, _ in gets)
    assert all("lamin=37.4500" in path for _, path, _, _ in gets), "the box in every states request"


# --- step 17: the replay, identical but for its header's run mode, says REPLAY with no identity ------------------------


def test_replay_reproduces_the_records_and_names_no_identity(telemetry_bin, adsb_run):
    cwd = adsb_run.cwd.parent / "replay"
    cwd.mkdir()
    source = UdpSource("127.0.0.1", 0, timeout_ms=1000)
    receiver = FrameReceiver(source, PERIOD_MS)
    rerun = run_published(telemetry_bin, ["--replay", str(adsb_run.run.log), "--config", str(adsb_run.config),
                                         "--udp", f"127.0.0.1:{source.port}"], cwd, receiver)
    assert rerun.returncode == 0, rerun.stdout[-800:]
    # The replay knows the recording's source from its header (REQ-LOG-007), never from a frame,
    # and takes the ADS-B channel timeout for it (REQ-FAULT-001), which is what keeps it identical.
    assert "run mode ADSB, source mode ADSB" in rerun.stdout, rerun.stdout[:600]
    assert "an ADS-B recording by its header" in rerun.stdout, "the replay takes the ADS-B channel timeout"
    assert_replay_identical(cwd / "logs" / "replay_000.bin", adsb_run.run.log)
    # REPLAY with no identity, and since status version 3 the recording's source mode from its header,
    # which is what lets a page label the replay's datum and attribution (REQ-DISP-007).
    assert rerun.statuses and all(s.mode == "REPLAY" and s.identity is None and s.source_mode == "ADSB"
                                  for _, s in rerun.statuses)
    assert [n for n, s in rerun.statuses if s.state == "ENDED"] == [len(rerun.frames)] * ENDED_REPEATS
    assert rerun.frames and all(f.baro_status == NOT_AVAILABLE and f.imu_status == NOT_AVAILABLE for f in rerun.frames)
    assert "ADS-B:" not in rerun.stdout.split("Replay input:")[1].split("\n", 1)[1][:200]


# --- step 18: the refusals, nothing built -----------------------------------------------------


@pytest.mark.parametrize("args, named", [
    (["--adsb", "--live"], "--live"),
    (["--adsb", "--replay", "nothing.bin"], "--replay"),
    (["--adsb", "--motion", "demo"], "--motion"),
    (["--adsb-credentials", "nowhere.json"], "--adsb-credentials"),
    (["--adsb-origin", "http://127.0.0.1:1"], "--adsb-origin"),
])
def test_adsb_flags_are_refused_where_they_have_no_meaning(telemetry_bin, tmp_path, args, named):
    completed = run(telemetry_bin, tmp_path, *CONFIG_ARGS, *args, "--cycles", "5")
    assert completed.returncode == 1
    assert "--adsb" in completed.stderr and named in completed.stderr, completed.stderr
    assert not (tmp_path / "logs").exists(), "nothing may be built after a refused flag"


def test_missing_credentials_file_is_refused_before_anything_is_built(telemetry_bin, tmp_path):
    missing = tmp_path / "secrets" / "opensky_credentials.json"
    completed = run(telemetry_bin, tmp_path, *CONFIG_ARGS, "--adsb", "--adsb-credentials", str(missing),
                    "--adsb-origin", "http://127.0.0.1:9", "--cycles", "5")
    assert completed.returncode == 1
    assert "cannot read credentials file" in completed.stderr and str(missing) in completed.stderr, completed.stderr
    assert not (tmp_path / "logs").exists()


def test_configuration_without_the_block_is_refused(telemetry_bin, tmp_path):
    credentials = write_credentials(tmp_path / "secrets")
    config = write_config(tmp_path / "config", with_block=False)
    completed = run(telemetry_bin, tmp_path, "--config", str(config), "--adsb", "--adsb-credentials",
                    str(credentials), "--adsb-origin", "http://127.0.0.1:9", "--cycles", "5")
    assert completed.returncode == 1
    assert "adsb" in completed.stderr, completed.stderr
    assert not (tmp_path / "logs").exists()
