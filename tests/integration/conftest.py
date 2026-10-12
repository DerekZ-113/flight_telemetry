"""Fixtures for the pytest suite (REQ-TEST-002): the telemetry binary and one simulated session.

One 10-second simulated run per session (500 frames at 50 Hz, seed 42) and its replay,
in a temporary directory so the binary's `logs/` never touches the developer's. The
replay-identity test, the reader test and the noise oracle all read the same two files.
"""
from __future__ import annotations

import os
import subprocess
from pathlib import Path
from typing import NamedTuple

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
SESSION_CYCLES = 500


class Session(NamedTuple):
    directory: Path
    log: Path          # logs/telemetry_000.bin from the simulated run
    replay: Path       # logs/replay_000.bin from replaying it
    jitter_csv: Path   # logs/telemetry_jitter.csv from the paced run
    stdout: str
    replay_stdout: str


@pytest.fixture(scope="session")
def telemetry_bin() -> Path:
    """build/telemetry, or $TELEMETRY_BIN. Missing is a failure, not a skip: a test that
    silently skips is a test that never ran."""
    path = Path(os.environ.get("TELEMETRY_BIN", REPO_ROOT / "build" / "telemetry")).resolve()
    if not (path.is_file() and os.access(path, os.X_OK)):
        pytest.fail(f"telemetry binary not found at {path}; build it (cmake --build build) or set TELEMETRY_BIN")
    return path


def _run(binary: Path, args: list[str], cwd: Path) -> str:
    completed = subprocess.run([str(binary), *args], cwd=cwd, capture_output=True, text=True,
                               timeout=120, check=True)
    return completed.stdout


@pytest.fixture(scope="session")
def sim_session(telemetry_bin: Path, tmp_path_factory: pytest.TempPathFactory) -> Session:
    directory = tmp_path_factory.mktemp("sim")
    stdout = _run(telemetry_bin, ["--cycles", str(SESSION_CYCLES)], directory)
    log = directory / "logs" / "telemetry_000.bin"
    replay_stdout = _run(telemetry_bin, ["--replay", str(log), "--cycles", str(SESSION_CYCLES)], directory)
    return Session(directory, log, directory / "logs" / "replay_000.bin",
                   directory / "logs" / "telemetry_jitter.csv", stdout, replay_stdout)
