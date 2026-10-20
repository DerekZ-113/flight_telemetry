"""TC-016 at the integration level, steps 11 through 23 (REQ-CFG-001, REQ-CFG-002, REQ-CFG-003;
also REQ-TRANS-002, REQ-SENS-006, REQ-LOG-003).

The binary reads one file, refuses a bad one naming every problem, records the file beside its
log, replays from that record, and says so when it runs another. A "copy" below is the shipped
text with one `key: value` line edited, so no YAML library is needed to write one. The harness
prepends the shipped file's --config to every run; a later --config on the same command line
wins, which is how a test hands the binary a copy.
"""
from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest

from conftest import CONFIG_ARGS, SHIPPED_CONFIG, assert_replay_identical, run_published
from pipeline.frame_format import FRAME_FIELDS
from pipeline.log_reader import read_log
from test_transport import assert_byte_identical_to_log, make_receiver, transport_summary

# strip_to_raw in src/replay/log_replay_source.cpp clears these; the rest are the sensors' words.
COMPUTED_FIELDS = ("baro_altitude_m", "pitch_deg", "roll_deg", "fused_altitude_m", "vertical_speed_mps",
                   "baro_status", "imu_status", "gps_status",
                   "baro_altitude_validity", "pitch_validity", "roll_validity",
                   "fused_altitude_validity", "vertical_speed_validity", "voter_status")
RAW_FIELDS = tuple(name for name in FRAME_FIELDS if name not in COMPUTED_FIELDS)


def edited(text: str, old: str, new: str) -> str:
    assert text.count(old) == 1, f"{old!r} occurs {text.count(old)} times"
    return text.replace(old, new)


def write_config(directory: Path, *replacements: tuple[str, str], name: str = "edited.yaml") -> Path:
    """A copy of the shipped file with each (old, new) substring replaced once."""
    text = SHIPPED_CONFIG.read_text()
    for old, new in replacements:
        text = edited(text, old, new)
    path = directory / name
    path.write_text(text)
    return path


def run(telemetry_bin: Path, cwd: Path, *args: str, timeout: float = 60) -> subprocess.CompletedProcess:
    return subprocess.run([str(telemetry_bin), *args], cwd=cwd, capture_output=True, text=True, timeout=timeout)


def assert_raw_equal_computed_differ(frames_a, frames_b) -> None:
    """Same sensors, a different pipeline: every raw field equal, some computed field not."""
    assert len(frames_a) == len(frames_b)
    for i, (a, b) in enumerate(zip(frames_a, frames_b)):
        for name in RAW_FIELDS:
            assert getattr(a, name) == getattr(b, name), f"frame {i}, raw field {name}"
    assert any(a.fused_altitude_m != b.fused_altitude_m for a, b in zip(frames_a, frames_b)), \
        "the changed tuning left every fused altitude unchanged"


# --- startup refusals (TC-016 steps 11 to 13) ------------------------------------------------


def test_run_without_a_config_file_is_refused(telemetry_bin, tmp_path):
    completed = run(telemetry_bin, tmp_path, "--cycles", "5")   # no --config, no file in cwd
    assert completed.returncode == 1
    assert "config/telemetry_config.yaml" in completed.stderr, completed.stderr
    assert "--config" in completed.stderr, completed.stderr
    assert not (tmp_path / "logs").exists(), "nothing may be built before the configuration passes"


def test_missing_config_path_is_refused(telemetry_bin, tmp_path):
    completed = run(telemetry_bin, tmp_path, "--config", "nope.yaml", "--cycles", "5")
    assert completed.returncode == 1
    assert "nope.yaml" in completed.stderr, completed.stderr
    assert not (tmp_path / "logs").exists()


def test_every_invalid_value_is_named(telemetry_bin, tmp_path):
    copy = write_config(tmp_path, ("period_ms: 20", "period_ms: 0"), ("stuck_count: 10", "stuck_count: 1"),
                        ("port: 5556", "port: 70000"))
    completed = run(telemetry_bin, tmp_path, "--config", str(copy), "--cycles", "5")
    assert completed.returncode == 1
    assert "has 3 problems" in completed.stderr, completed.stderr
    keys = ["timing.period_ms", "fault.stuck_count", "transport.udp.port"]
    positions = [completed.stderr.find(key) for key in keys]
    assert all(p >= 0 for p in positions), completed.stderr
    assert positions == sorted(positions), "problems must come in the file's order"
    assert not (tmp_path / "logs").exists()


# --- the record beside the log (TC-016 steps 14 to 18) ----------------------------------------


def test_shipped_config_is_recorded_and_replayed(telemetry_bin, tmp_path, sim_session):
    # The session's run recorded the shipped file; its replay was handed the same file by
    # --config, which the binary recognises as the recorded configuration by content.
    assert "Configuration: " in sim_session.stdout
    assert sim_session.config_record.read_bytes() == SHIPPED_CONFIG.read_bytes()
    assert "equal to the configuration recorded with" in sim_session.replay_stdout, sim_session.replay_stdout[:400]
    assert "not claimed" not in sim_session.replay_stdout
    assert sim_session.replay_config_record.read_bytes() == SHIPPED_CONFIG.read_bytes()

    # The plain form: --replay with no --config reads the record beside the log and says so,
    # and the replay is the identity replay of TC-004.
    logs = tmp_path / "logs"
    logs.mkdir()
    shutil.copy(sim_session.log, logs / "telemetry_000.bin")
    shutil.copy(sim_session.config_record, logs / "telemetry_config.yaml")
    completed = run(telemetry_bin, tmp_path, "--replay", "logs/telemetry_000.bin", "--cycles", "500", timeout=120)
    assert completed.returncode == 0, completed.stderr
    assert "Configuration: logs/telemetry_config.yaml (recorded with logs/telemetry_000.bin)" in completed.stdout
    assert_replay_identical(logs / "replay_000.bin", sim_session.log)
    assert (logs / "replay_config.yaml").read_bytes() == SHIPPED_CONFIG.read_bytes()


def test_changed_tuning_changes_the_log_and_is_recorded(telemetry_bin, tmp_path, sim_session):
    # The binary reads the file, not compiled defaults: a hundredfold GPS variance changes the
    # fused altitude of the same simulated frames, and the record beside the log is that copy.
    copy = write_config(tmp_path, ("gps_measurement_variance_m2: 25.0", "gps_measurement_variance_m2: 2500"))
    completed = run(telemetry_bin, tmp_path, "--config", str(copy), "--cycles", "50")
    assert completed.returncode == 0, completed.stderr
    frames = read_log(tmp_path / "logs" / "telemetry_000.bin").frames
    assert len(frames) == 50
    assert_raw_equal_computed_differ(frames, read_log(sim_session.log).frames[:50])
    assert (tmp_path / "logs" / "telemetry_config.yaml").read_bytes() == copy.read_bytes()


def test_replay_without_a_recorded_config_is_refused(telemetry_bin, tmp_path, sim_session):
    alone = tmp_path / "alone"
    alone.mkdir()
    shutil.copy(sim_session.log, alone / "telemetry_000.bin")
    completed = run(telemetry_bin, tmp_path, "--replay", "alone/telemetry_000.bin", "--cycles", "5")
    assert completed.returncode == 1
    assert "alone/telemetry_config.yaml" in completed.stderr, completed.stderr
    assert "--config" in completed.stderr, completed.stderr
    assert not (tmp_path / "logs").exists()


def test_replay_with_another_config_is_loud(telemetry_bin, tmp_path, sim_session):
    # Replaying through another configuration is a legitimate run (that is how a tuning
    # candidate is tried) but not an identity claim: the binary says so and the log differs.
    copy = write_config(tmp_path, ("gps_measurement_variance_m2: 25.0", "gps_measurement_variance_m2: 2500"))
    completed = run(telemetry_bin, tmp_path, "--config", str(copy), "--replay", str(sim_session.log),
                    "--cycles", "500", timeout=120)
    assert completed.returncode == 0, completed.stderr
    assert "not the configuration recorded with" in completed.stdout, completed.stdout[:600]
    replayed = read_log(tmp_path / "logs" / "replay_000.bin").frames
    assert_raw_equal_computed_differ(replayed, read_log(sim_session.log).frames)
    assert (tmp_path / "logs" / "replay_config.yaml").read_bytes() == copy.read_bytes()


def test_replay_of_a_log_not_named_by_prefix_needs_config(telemetry_bin, tmp_path, sim_session):
    # TC-016 step 22. A record exists by name only beside <prefix>_NNN.bin, so a log of another
    # name is refused without --config, naming the rule; with --config it replays, the line says
    # that no record was found, and the records equal the session's, since the shipped file is
    # what the session recorded.
    alone = tmp_path / "alone"
    alone.mkdir()
    shutil.copy(sim_session.log, alone / "flight.bin")
    refused = run(telemetry_bin, tmp_path, "--replay", "alone/flight.bin", "--cycles", "5")
    assert refused.returncode == 1
    assert "alone/flight.bin" in refused.stderr, refused.stderr
    assert "<prefix>_NNN.bin" in refused.stderr, refused.stderr
    assert "--config" in refused.stderr, refused.stderr
    assert not (tmp_path / "logs").exists(), "nothing may be built before the configuration passes"

    completed = run(telemetry_bin, tmp_path, "--config", str(SHIPPED_CONFIG), "--replay", "alone/flight.bin",
                    timeout=120)
    assert completed.returncode == 0, completed.stderr
    notice = (f"Configuration: {SHIPPED_CONFIG} (no configuration recorded beside alone/flight.bin, "
              "which is not named <prefix>_NNN.bin; replay identity is not claimed)")
    assert notice in completed.stdout, completed.stdout[:600]
    assert_replay_identical(tmp_path / "logs" / "replay_000.bin", sim_session.log)
    assert (tmp_path / "logs" / "replay_config.yaml").read_bytes() == SHIPPED_CONFIG.read_bytes()


def test_replay_with_config_and_no_record_says_so(telemetry_bin, tmp_path, sim_session):
    # TC-016 step 23: the log of step 16 under its own name, no record beside it, with --config.
    alone = tmp_path / "alone"
    alone.mkdir()
    shutil.copy(sim_session.log, alone / "telemetry_000.bin")
    completed = run(telemetry_bin, tmp_path, "--config", str(SHIPPED_CONFIG), "--replay",
                    "alone/telemetry_000.bin", timeout=120)
    assert completed.returncode == 0, completed.stderr
    notice = "(no configuration recorded with alone/telemetry_000.bin; replay identity is not claimed)"
    assert notice in completed.stdout, completed.stdout[:600]
    assert_replay_identical(tmp_path / "logs" / "replay_000.bin", sim_session.log)


def test_replay_refuses_to_overwrite_its_input(telemetry_bin, sim_session):
    before = sim_session.replay.read_bytes()
    completed = run(telemetry_bin, sim_session.directory, "--replay", "logs/replay_000.bin", "--cycles", "5")
    assert completed.returncode == 1
    assert "would write" in completed.stderr, completed.stderr
    assert sim_session.replay.read_bytes() == before, "the input was touched"


# --- flags against the file's transport keys (TC-016 steps 19 and 20) --------------------------


@pytest.mark.parametrize("transport", ("zmq", "udp"))
def test_flag_enables_and_overrides_a_disabled_file_transport(telemetry_bin, tmp_path, transport):
    receiver, flag = make_receiver(transport)
    run_result = run_published(telemetry_bin, [*flag, "--cycles", "50"], tmp_path, receiver)
    assert run_result.returncode == 0, run_result.stdout[-400:]
    assert len(run_result.frames) >= 45, "the file disables both transports; the flag must enable this one"
    assert_byte_identical_to_log(run_result.frames, run_result.log)
    assert transport_summary(run_result.stdout, "ZeroMQ" if transport == "zmq" else "UDP") == (50, 0)


@pytest.mark.parametrize("transport", ("zmq", "udp"))
def test_file_enabled_transport_publishes_without_a_flag(telemetry_bin, tmp_path, transport):
    # The destination comes from the file alone (REQ-TRANS-002 "configurable destination").
    receiver, flag = make_receiver(transport)
    if transport == "udp":
        host, port = flag[1].rsplit(":", 1)
        copy = write_config(tmp_path, ("  udp:\n    enabled: false", "  udp:\n    enabled: true"),
                            ("host: 127.0.0.1", f"host: {host}"), ("port: 5556", f"port: {port}"))
    else:
        copy = write_config(tmp_path, ("  zmq:\n    enabled: false", "  zmq:\n    enabled: true"),
                            ("endpoint: tcp://127.0.0.1:5555", f"endpoint: {flag[1]}"))
    run_result = run_published(telemetry_bin, ["--config", str(copy), "--cycles", "50"], tmp_path, receiver)
    assert run_result.returncode == 0, run_result.stdout[-400:]
    assert len(run_result.frames) >= 45, "the file enables the transport; no flag was given"
    assert_byte_identical_to_log(run_result.frames, run_result.log)
    assert transport_summary(run_result.stdout, "ZeroMQ" if transport == "zmq" else "UDP") == (50, 0)
    assert (tmp_path / "logs" / "telemetry_config.yaml").read_bytes() == copy.read_bytes()
