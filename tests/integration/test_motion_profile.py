"""TC-026 steps 5 through 7 at the integration level (REQ-SENS-007; also REQ-SENS-006,
REQ-LOG-003, REQ-LOG-004).

The binary runs the simulator's scripted flight under `--motion demo`: the log shows the
aircraft moving as the script says, through the real pipeline (the filter's pitch, the GPS
fields, every validity NORMAL), its unpaced replay is byte-identical (the demo's claim of
determinism, REQ-LOG-003), and the flag is refused where it has no meaning. The stationary
default's bytes are the session fixture's, checked by every byte-identity test already in the
suite (TC-026 step 8).
"""
from __future__ import annotations

import math

import pytest

from conftest import CONFIG_ARGS
from pipeline.log_reader import read_log
from test_config import run
from test_replay_pacing import REPLAY_LINE_END, replay

PERIOD_MS = 20
CYCLES = 1000                       # 20 s: the takeoff roll and three seconds of the climb
METERS_PER_DEGREE = 6_371_000 * math.pi / 180   # the profile's and the bridge's spherical approximation
VALIDITY_NORMAL = 3


def test_scripted_flight_moves_and_its_replay_is_byte_identical(telemetry_bin, tmp_path):
    cwd = tmp_path / "run"
    cwd.mkdir()
    completed = run(telemetry_bin, cwd, *CONFIG_ARGS, "--motion", "demo", "--cycles", str(CYCLES), timeout=120)
    assert completed.returncode == 0, completed.stderr
    assert "Motion:    profile demo" in completed.stdout, completed.stdout[-600:]

    log_path = cwd / "logs" / "telemetry_000.bin"
    frames = read_log(log_path).frames
    assert len(frames) == CYCLES
    last = frames[-1]
    # At 20 s the script is three seconds into the climb at 8 degrees of pitch and 30 m/s.
    assert abs(last.pitch_deg - 8.0) < 0.3, last.pitch_deg
    assert abs(last.roll_deg) < 0.3, last.roll_deg
    assert abs(last.ground_speed_mps - 30.0) < 1e-3
    assert abs(last.heading_deg - 90.0) < 1e-3
    for name in ("baro_altitude_validity", "pitch_validity", "roll_validity",
                 "fused_altitude_validity", "vertical_speed_validity"):
        assert getattr(last, name) == VALIDITY_NORMAL, name
    first_fix = frames[0]
    east = (last.longitude - first_fix.longitude) * METERS_PER_DEGREE * math.cos(math.radians(first_fix.latitude))
    assert east > 200, f"the GPS moved {east:.0f} m east of the first fix; the script says about 300"

    stdout, replay_log = replay(telemetry_bin, tmp_path / "replay", log_path)
    assert REPLAY_LINE_END.format(n=CYCLES) in stdout, stdout[-400:]
    assert replay_log.read_bytes() == log_path.read_bytes(), "the replay of the scripted flight differs"
    print(f"the 20 s scripted flight: pitch {last.pitch_deg:.2f}, {east:.0f} m east, replay byte-identical")


@pytest.mark.parametrize("args, named", [
    (["--motion", "demo", "--replay", "nothing.bin"], "--replay"),
    (["--motion", "demo", "--live"], "--live"),
    (["--motion", "nope"], "demo"),
])
def test_motion_is_refused_where_it_has_no_meaning(telemetry_bin, tmp_path, args, named):
    completed = run(telemetry_bin, tmp_path, *CONFIG_ARGS, *args, "--cycles", "5")
    assert completed.returncode == 1
    assert "--motion" in completed.stderr and named in completed.stderr, completed.stderr
    assert not (tmp_path / "logs").exists(), "nothing may be built after a refused flag"
