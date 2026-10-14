"""Fixtures for the pytest suite (REQ-TEST-002): the telemetry binary and one simulated session.

One 10-second simulated run per session (500 frames at 50 Hz, seed 42) and its replay,
in a temporary directory so the binary's `logs/` never touches the developer's. The
replay-identity test, the reader test and the noise oracle all read the same two files.
The transport tests (TC-005, TC-010) run the binary themselves, once per test, against a
receiver that is already listening: `run_published` below.
"""
from __future__ import annotations

import os
import re
import socket
import subprocess
import time
from pathlib import Path
from typing import Callable, NamedTuple

import pytest

from pipeline.frame_format import FaultEvent, Frame
from pipeline.receiver import FrameReceiver

REPO_ROOT = Path(__file__).resolve().parents[2]
SESSION_CYCLES = 500

# The configuration of record (REQ-CFG-001). The binary reads it relative to the working
# directory by default, and every run here happens in a temporary directory, so each run
# names it explicitly; the run's copy of it beside the log is the session record (REQ-CFG-003).
SHIPPED_CONFIG = REPO_ROOT / "config" / "telemetry_config.yaml"
CONFIG_ARGS = ["--config", str(SHIPPED_CONFIG)]


class Session(NamedTuple):
    directory: Path
    log: Path                   # logs/telemetry_000.bin from the simulated run
    replay: Path                # logs/replay_000.bin from replaying it
    jitter_csv: Path            # logs/telemetry_jitter.csv from the paced run
    stdout: str
    replay_stdout: str
    config_record: Path         # logs/telemetry_config.yaml, the run's copy of the configuration
    replay_config_record: Path  # logs/replay_config.yaml, the replay's copy of what it used


@pytest.fixture(scope="session")
def telemetry_bin() -> Path:
    """build/telemetry, or $TELEMETRY_BIN. Missing is a failure, not a skip: a test that
    silently skips is a test that never ran."""
    path = Path(os.environ.get("TELEMETRY_BIN", REPO_ROOT / "build" / "telemetry")).resolve()
    if not (path.is_file() and os.access(path, os.X_OK)):
        pytest.fail(f"telemetry binary not found at {path}; build it (cmake --build build) or set TELEMETRY_BIN")
    return path


def _run(binary: Path, args: list[str], cwd: Path) -> str:
    completed = subprocess.run([str(binary), *CONFIG_ARGS, *args], cwd=cwd, capture_output=True, text=True,
                               timeout=120, check=True)
    return completed.stdout


@pytest.fixture(scope="session")
def sim_session(telemetry_bin: Path, tmp_path_factory: pytest.TempPathFactory) -> Session:
    directory = tmp_path_factory.mktemp("sim")
    stdout = _run(telemetry_bin, ["--cycles", str(SESSION_CYCLES)], directory)
    log = directory / "logs" / "telemetry_000.bin"
    replay_stdout = _run(telemetry_bin, ["--replay", str(log), "--cycles", str(SESSION_CYCLES)], directory)
    return Session(directory, log, directory / "logs" / "replay_000.bin",
                   directory / "logs" / "telemetry_jitter.csv", stdout, replay_stdout,
                   directory / "logs" / "telemetry_config.yaml", directory / "logs" / "replay_config.yaml")


# --- the transport tests' harness (TC-005, TC-010 steps 6 and 7) -----------------------------

# The binary's exit lines, one per transport and one for timing (main.cpp).
TRANSPORT_LINE = re.compile(r"^Transport: (?P<published>\d+) frames published (?:on|to) (?P<where>\S+) "
                            r"\((?P<name>ZeroMQ|UDP)\), send failures = (?P<failures>\d+)", re.M)
TIMING_LINE = re.compile(r"^Timing:\s+(?P<cycles>\d+) cycles .*missed cycles = (?P<missed>\d+)", re.M)


class PublishedRun(NamedTuple):
    frames: list[Frame]          # in arrival order
    events: list[FaultEvent]
    stdout: str
    returncode: int
    log: Path                    # logs/telemetry_000.bin of this run
    elapsed_s: float


def free_tcp_port() -> int:
    """A port the binary can bind a ZeroMQ publisher on: bound to 0 here, read, released."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def run_published(binary: Path, args: list[str], cwd: Path, receiver: FrameReceiver,
                  on_frame: Callable[[int], None] | None = None, timeout_s: float = 60) -> PublishedRun:
    """Run the binary with its stdout in a file while this thread reads the receiver.

    The receiver must already be listening (TC-005 step 1). Collection ends when the process
    has exited and the receiver has been quiet for its timeout; `on_frame` is called with the
    running frame count after each frame, which is how the stall test stops reading."""
    stdout_path = cwd / "stdout.txt"
    frames: list[Frame] = []
    events: list[FaultEvent] = []
    start = time.perf_counter()
    with open(stdout_path, "w") as out:
        process = subprocess.Popen([str(binary), *CONFIG_ARGS, *args], cwd=cwd, stdout=out, stderr=subprocess.STDOUT)
        try:
            while True:
                record = receiver.recv()
                if record is None:
                    if process.poll() is not None:
                        break
                    if time.perf_counter() - start > timeout_s:
                        process.kill()
                        raise TimeoutError(f"telemetry {args} ran past {timeout_s} s")
                    continue
                if isinstance(record, Frame):
                    frames.append(record)
                    if on_frame is not None:
                        on_frame(len(frames))
                else:
                    events.append(record)
        finally:
            process.wait(timeout=timeout_s)
    elapsed = time.perf_counter() - start
    receiver.close()
    return PublishedRun(frames, events, stdout_path.read_text(), process.returncode,
                        cwd / "logs" / "telemetry_000.bin", elapsed)
