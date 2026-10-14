"""Noise characterization of stationary sensors: the measurement behind docs/noise_profile.md (REQ-PROC-004).

Reads a captured log (pipeline.log_reader), takes every raw channel's fresh samples, and
reports per channel the mean, the population sigma, the Allan deviation over averaging
times from one sample up, the lag-1 autocorrelation, the quantization step, and the
longest run of identical samples; derives the Kalman constants the capture can set and
names the ones it cannot; summarizes the pipeline's own outputs after a settle window;
and writes Markdown tables, a JSON of every number, and four figures.

Estimators. The population sigma (divide by n) describes the whole run, trend included.
ADEV(tau0)^2 = half the mean squared difference of consecutive samples = sigma^2 (1 - rho1)
for a stationary process: white noise gives sigma, a slow trend adds almost nothing, and
low-pass correlation lowers it. A trend and positive correlation both raise sigma above
ADEV(tau0), so the ratio cannot tell them apart; rho1 of the linearly detrended series can.
The barometric measurement variance R is ADEV(tau0)^2 of baro_altitude_m when that rho1 is
within sampling error of zero (white: the two estimators agree, and a trend the filter
cannot see does not inflate R). When it is significantly positive, consecutive samples are
correlated, ADEV(tau0)^2 would understate the per-sample variance by the factor (1 - rho1)
and leave the filter overconfident, so R is the variance of the linearly detrended series,
which removes slow drift without understating correlated noise. Everything accumulates in
float64; the frame's fields are float32.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence

import numpy as np

from pipeline.frame_format import CHANNEL_NAMES, COMPUTED_VALIDITY, FAULT_TYPE_NAMES, VALIDITY_NORMAL
from pipeline.log_reader import LogFile, find_log_files, frames_array, read_logs

SCHEMA = "FTS-NP-001/1"
GRAVITY_MPS2 = 9.81             # the project's constant: src/drivers/mpu6050.h, the simulator, FAULT-010
ALPHA = 0.98                    # ComplementaryFilter::kDefaultAlpha
PERIOD_S = 0.02                 # the 50 Hz loop
TILT_PER_DPS = ALPHA * PERIOD_S / (1.0 - ALPHA)   # 0.98 s: degrees of steady tilt per deg/s of gyro bias
ISA_SCALE_M = 44330.0           # src/processing/altitude.cpp
ISA_EXPONENT = 0.1903
ISA_SEA_LEVEL_HPA = 1013.25
ACCEL_LSB = GRAVITY_MPS2 / 16384.0        # ±2 g: 0.000599 m/s^2 per count
GYRO_LSB = 1.0 / 131.0                    # ±250 deg/s: 0.00763 deg/s per count
DLPF_BANDWIDTH_HZ = 20.0                  # MPU6050 CONFIG = 4 (src/drivers/mpu6050.h)
STUCK_COUNT = 10                          # FaultDetectorConfig::stuck_count

# The constants in src/processing/processor.cpp before the capture, for the "replaces" column.
PLACEHOLDERS = {"R_baro_m2": 3.0, "P0_alt_m2": 3.0, "R_gps_m2": 25.0, "Q_accel_var": 1.0, "P0_vel": 1.0}


@dataclass(frozen=True)
class ChannelSpec:
    field: str
    unit: str
    channel: str                       # the read_ok flag that gates it: baro, imu, gps
    lsb: float | None                  # the sensor's step, when the frame's float32 resolves it
    simulator_sigma: float | None      # src/drivers/simulated_data.cpp today
    datasheet_sigma: float | None      # at the driver's settings
    datasheet_note: str


RAW_CHANNELS: tuple[ChannelSpec, ...] = (
    ChannelSpec("pressure_hpa", "hPa", "baro", None, 0.2, None,
                "BMP280 x4 oversampling, IIR off: a few Pa RMS (datasheet table, confirm)"),
    ChannelSpec("temperature_c", "degC", "baro", 0.01, 0.1, None, "0.01 degC resolution; not stuck-tracked"),
    ChannelSpec("baro_altitude_m", "m", "baro", None, 0.2 * 8.327, None,
                "pressure through the ISA slope, about 8.3 m/hPa near sea level"),
    ChannelSpec("accel_x", "m/s^2", "imu", ACCEL_LSB, 0.05, 400e-6 * GRAVITY_MPS2 * math.sqrt(DLPF_BANDWIDTH_HZ),
                "400 ug/rtHz x sqrt(20 Hz)"),
    ChannelSpec("accel_y", "m/s^2", "imu", ACCEL_LSB, 0.05, 400e-6 * GRAVITY_MPS2 * math.sqrt(DLPF_BANDWIDTH_HZ),
                "400 ug/rtHz x sqrt(20 Hz)"),
    ChannelSpec("accel_z", "m/s^2", "imu", ACCEL_LSB, 0.05, 400e-6 * GRAVITY_MPS2 * math.sqrt(DLPF_BANDWIDTH_HZ),
                "400 ug/rtHz x sqrt(20 Hz)"),
    ChannelSpec("gyro_x", "deg/s", "imu", GYRO_LSB, 0.1, 0.005 * math.sqrt(DLPF_BANDWIDTH_HZ),
                "0.005 deg/s/rtHz x sqrt(20 Hz)"),
    ChannelSpec("gyro_y", "deg/s", "imu", GYRO_LSB, 0.1, 0.005 * math.sqrt(DLPF_BANDWIDTH_HZ),
                "0.005 deg/s/rtHz x sqrt(20 Hz)"),
    ChannelSpec("gyro_z", "deg/s", "imu", GYRO_LSB, 0.1, 0.005 * math.sqrt(DLPF_BANDWIDTH_HZ),
                "0.005 deg/s/rtHz x sqrt(20 Hz)"),
)
GPS_CHANNELS = (("gps_altitude_m", "m", 2.0), ("latitude", "deg", 0.00002), ("longitude", "deg", 0.00002))
OUTPUT_FIELDS = (("fused_altitude_m", "m"), ("vertical_speed_mps", "m/s"), ("pitch_deg", "deg"), ("roll_deg", "deg"))
STUCK_TRACKED = ("pressure_hpa", "accel_x", "accel_y", "accel_z", "gyro_x", "gyro_y", "gyro_z")
ALLAN_TABLE_TAUS_S = (0.02, 0.1, 1.0, 10.0, 100.0)


@dataclass(frozen=True)
class AllanResult:
    tau_s: np.ndarray
    adev: np.ndarray
    rel_error: np.ndarray       # 1/sqrt(N/m): the fractional one-sigma confidence of each point


# ---------------------------------------------------------------- estimators

def population_sigma(x: np.ndarray) -> float:
    return float(np.std(np.asarray(x, dtype=np.float64)))


def longest_identical_run(x: np.ndarray) -> int:
    """The longest run of bit-identical consecutive samples; the stuck detector fires at 10."""
    x = np.asarray(x)
    if x.size == 0:
        return 0
    change = np.flatnonzero(np.diff(x) != 0)
    edges = np.concatenate(([-1], change, [x.size - 1]))
    return int(np.max(np.diff(edges)))


def quantization_step(x: np.ndarray) -> float:
    """The smallest nonzero gap between distinct values: the sensor's LSB when the frame's
    float32 resolves it, float32's own step when it does not (pressure at 1013 hPa)."""
    gaps = np.diff(np.unique(np.asarray(x)))
    gaps = gaps[gaps > 0]
    return float(gaps.min()) if gaps.size else 0.0


def lag1_autocorrelation(x: np.ndarray) -> float:
    x = np.asarray(x, dtype=np.float64)
    x = x - x.mean()
    denominator = float(np.dot(x, x))
    return float(np.dot(x[:-1], x[1:]) / denominator) if denominator > 0 else 0.0


def tau_grid(n: int, points_per_decade: int = 8) -> np.ndarray:
    """Averaging factors m, log-spaced integers from 1 to n // 4."""
    max_m = max(1, n // 4)
    count = int(math.log10(max_m) * points_per_decade) + 1
    return np.unique(np.round(np.logspace(0.0, math.log10(max_m), max(count, 1))).astype(int))


def allan_deviation(x: np.ndarray, tau0_s: float, m_values: Sequence[int] | None = None) -> AllanResult:
    """Overlapping Allan deviation through cumulative sums: for each m, the cluster averages
    of m samples, the differences of averages m apart, AVAR = half their mean square."""
    x = np.asarray(x, dtype=np.float64)
    n = x.size
    ms = tau_grid(n) if m_values is None else np.asarray(m_values, dtype=int)
    sums = np.concatenate(([0.0], np.cumsum(x)))
    taus, adevs, errors = [], [], []
    for m in ms:
        m = int(m)
        if m < 1 or 2 * m > n - 1:
            continue
        averages = (sums[m:] - sums[:-m]) / m           # n - m + 1 cluster averages
        differences = averages[m:] - averages[:-m]      # n - 2m + 1 differences
        taus.append(m * tau0_s)
        adevs.append(math.sqrt(0.5 * float(np.mean(differences * differences))))
        errors.append(1.0 / math.sqrt(n / m))
    return AllanResult(np.array(taus), np.array(adevs), np.array(errors))


def isa_slope_m_per_hpa(pressure_hpa: float, sea_level_hpa: float = ISA_SEA_LEVEL_HPA) -> float:
    """d/dP of altitude.cpp's 44330 (1 - (P/P0)^0.1903): about -8.33 m/hPa at 1013 hPa."""
    return -ISA_SCALE_M * ISA_EXPONENT / sea_level_hpa * (pressure_hpa / sea_level_hpa) ** (ISA_EXPONENT - 1.0)


def isa_altitude_m(pressure_hpa: float, sea_level_hpa: float = ISA_SEA_LEVEL_HPA) -> float:
    return ISA_SCALE_M * (1.0 - (pressure_hpa / sea_level_hpa) ** ISA_EXPONENT)


def accel_tilt_deg(ax: float, ay: float, az: float) -> tuple[float, float]:
    """The complementary filter's accelerometer angles (complementary_filter.cpp), in degrees."""
    pitch = math.degrees(math.atan2(ax, math.hypot(ay, az)))
    roll = math.degrees(math.atan2(ay, math.hypot(ax, az)))
    return pitch, roll


# ---------------------------------------------------------------- per-channel report

def channel_report(x: np.ndarray, t_s: np.ndarray, spec: ChannelSpec, tau0_s: float,
                   window_s: float = 60.0) -> dict[str, Any]:
    values = np.asarray(x, dtype=np.float64)
    n = int(values.size)
    sigma = population_sigma(values)
    allan = allan_deviation(values, tau0_s)
    adev0 = allan_deviation(values, tau0_s, [1]).adev
    adev_tau0 = float(adev0[0]) if adev0.size else math.nan
    first = values[t_s <= t_s[0] + window_s]
    last = values[t_s >= t_s[-1] - window_s]
    # A straight line against time absorbs the slow drift (weather, self-heating); what is
    # left is the per-sample scatter, correlated or not, and its lag-1 autocorrelation is the
    # number that separates correlated noise from a trend (both raise sigma over ADEV).
    t_rel = t_s - t_s[0]
    slope, intercept = np.polyfit(t_rel, values, 1) if n >= 2 else (0.0, float(values.mean()))
    residual = values - (slope * t_rel + intercept)
    step = quantization_step(x)
    repeats = int(np.sum(np.diff(np.asarray(x)) == 0))
    repeats_by_chance = n * step / (2.0 * sigma * math.sqrt(math.pi)) if sigma > 0 and step > 0 else 0.0
    return {
        "field": spec.field, "unit": spec.unit, "n": n,
        "mean": float(values.mean()), "sigma": sigma,
        "min": float(values.min()), "max": float(values.max()),
        "adev_tau0": adev_tau0,
        "sigma_over_adev_tau0": sigma / adev_tau0 if adev_tau0 > 0 else math.nan,
        "rho1": lag1_autocorrelation(values),
        "detrended_sigma": float(np.std(residual)),
        "rho1_detrended": lag1_autocorrelation(residual),
        "trend_per_hour": float(slope) * 3600.0,
        "repeats": repeats, "repeats_by_chance": repeats_by_chance,
        "step": step, "lsb": spec.lsb,
        "longest_run": longest_identical_run(x),
        "first_window_mean": float(first.mean()), "last_window_mean": float(last.mean()),
        "window_s": window_s,
        "allan": {"tau_s": allan.tau_s.tolist(), "adev": allan.adev.tolist(), "rel_error": allan.rel_error.tolist()},
        "reference": {"simulator_sigma": spec.simulator_sigma, "datasheet_sigma": spec.datasheet_sigma,
                      "datasheet_note": spec.datasheet_note},
    }


def derive_constants(channels: dict[str, dict[str, Any]]) -> dict[str, Any]:
    """The five Kalman constants, each measured here or retained with its reason, plus the
    gyro bias and its tilt consequence, the accelerometer offsets, and the stuck margin."""
    pressure = channels["pressure_hpa"]
    altitude = channels["baro_altitude_m"]
    # A synthetic log may leave the pressure at zero; the ISA slope exists only above it.
    slope = isa_slope_m_per_hpa(pressure["mean"]) if pressure["mean"] > 0 else math.nan
    white_sigma_m = altitude["adev_tau0"]
    rule = r_baro_rule(altitude, pressure)
    r_baro = rule["value"]
    gyro_bias = {axis: channels[f"gyro_{axis}"]["mean"] for axis in "xyz"}
    accel_mean = {axis: channels[f"accel_{axis}"]["mean"] for axis in "xyz"}
    pitch, roll = accel_tilt_deg(accel_mean["x"], accel_mean["y"], accel_mean["z"])
    return {
        "isa_slope_m_per_hpa": slope,
        "baro_sigma_m_total": altitude["sigma"],
        "baro_sigma_m_white": white_sigma_m,
        "baro_sigma_m_via_slope": abs(slope) * pressure["adev_tau0"],
        "constants": {
            "R_baro_m2": {"value": r_baro, "status": "measured", "source": rule["source"],
                          "placeholder": PLACEHOLDERS["R_baro_m2"]},
            "P0_alt_m2": {"value": r_baro, "status": "measured",
                          "source": "equals R_baro: the filter is born from one barometric sample",
                          "placeholder": PLACEHOLDERS["P0_alt_m2"]},
            "R_gps_m2": {"value": PLACEHOLDERS["R_gps_m2"], "status": "retained",
                         "source": "no GPS until REQ-SENS-005; NEO-6M vertical error about 5 m (datasheet)",
                         "placeholder": PLACEHOLDERS["R_gps_m2"]},
            "Q_accel_var": {"value": PLACEHOLDERS["Q_accel_var"], "status": "retained",
                            "source": "the vehicle's unmodelled vertical acceleration, sigma 1 m/s^2: a tuning "
                                      "choice no bench capture can measure",
                            "placeholder": PLACEHOLDERS["Q_accel_var"]},
            "P0_vel": {"value": PLACEHOLDERS["P0_vel"], "status": "tuning, chosen by replay",
                       "source": "the vehicle's vertical speed at startup; candidates 1.0, 0.1, 0.01 replayed",
                       "placeholder": PLACEHOLDERS["P0_vel"]},
        },
        "gyro_bias_dps": gyro_bias,
        "tilt_from_bias_deg": {"pitch": TILT_PER_DPS * gyro_bias["y"], "roll": TILT_PER_DPS * gyro_bias["x"]},
        "accel_tilt_deg": {"pitch": pitch, "roll": roll},
        "accel_offset_mg": {"x": accel_mean["x"] / GRAVITY_MPS2 * 1000.0,
                            "y": accel_mean["y"] / GRAVITY_MPS2 * 1000.0,
                            "z": (accel_mean["z"] - GRAVITY_MPS2) / GRAVITY_MPS2 * 1000.0},
        "stuck_margin": {field: channels[field]["longest_run"] for field in STUCK_TRACKED if field in channels},
        "stuck_count": STUCK_COUNT,
        "r_baro_rule": rule,
        "warnings": rule["warnings"],
    }


def r_baro_rule(altitude: dict[str, Any], pressure: dict[str, Any]) -> dict[str, Any]:
    """Which estimator gives R, decided by rho1 of the linearly detrended barometric altitude.

    Within sampling error of zero (3/sqrt(N)): white, ADEV(tau0)^2 and the detrended variance
    agree, ADEV(tau0)^2 is taken because it needs no trend model. Significantly positive:
    consecutive samples are correlated and ADEV(tau0)^2 understates the per-sample variance
    by the factor (1 - rho1), so the detrended variance is taken. Significantly negative, or
    pressure values repeating far beyond float32's chance count (the chip read faster than it
    converts): no mechanism here produces it; a warning says to stop and find the cause.
    """
    n = max(int(altitude["n"]), 1)
    threshold = 3.0 / math.sqrt(n)
    rho = altitude["rho1_detrended"]
    adev_variance = altitude["adev_tau0"] ** 2
    detrended_variance = altitude["detrended_sigma"] ** 2
    warnings: list[str] = []
    if rho > threshold:
        estimator, value = "detrended_variance", detrended_variance
        source = (f"variance of the linearly detrended baro_altitude_m: rho1 of the detrended series is {rho:.3f}, "
                  f"above {threshold:.3f}, so consecutive samples are correlated and ADEV(tau0)^2 "
                  f"({adev_variance:.4g}) would understate the per-sample variance by {100 * rho:.0f}%")
    else:
        estimator, value = "adev_tau0", adev_variance
        source = (f"ADEV(tau0)^2 of baro_altitude_m, the white part of the barometric altitude noise: rho1 of the "
                  f"detrended series is {rho:.3f}, within {threshold:.3f} of zero, and the detrended variance "
                  f"({detrended_variance:.4g}) agrees")
    if rho < -threshold:
        warnings.append(f"baro_altitude_m is anti-correlated (rho1 of the detrended series {rho:.3f}): no mechanism "
                        "here produces that; stop and find the cause before using R")
    if pressure.get("repeats_by_chance", 0.0) > 0 and pressure["repeats"] > 5.0 * pressure["repeats_by_chance"]:
        warnings.append(f"pressure repeats {pressure['repeats']} times against about {pressure['repeats_by_chance']:.0f} "
                        "by float32 chance: the chip may be read faster than it converts; stop and find the cause")
    return {"estimator": estimator, "value": value, "source": source, "rho1_detrended": rho, "threshold": threshold,
            "adev_tau0_variance": adev_variance, "detrended_variance": detrended_variance, "warnings": warnings}


def output_report(frames: np.ndarray, t_s: np.ndarray, settle_s: float,
                  accel_tilt: dict[str, float] | None) -> dict[str, Any]:
    """The pipeline's computed fields after the settle window, and the startup transient inside it.

    Only frames on which a field is NORMAL enter its statistics (REQ-PROC-007): a start-up
    frame on the Pi, before the first estimate exists, holds a 0 that would pull a mean toward
    zero, and a held or coasting value under FAILURE_WARNING is not the pipeline's estimate."""
    settled = t_s >= t_s[0] + settle_s
    if not np.any(settled):
        settled = np.ones_like(t_s, dtype=bool)
    report: dict[str, Any] = {"settle_s": settle_s, "fields": {}}
    for field, unit in OUTPUT_FIELDS:
        normal = frames[COMPUTED_VALIDITY[field]] == VALIDITY_NORMAL
        values = frames[field][settled & normal].astype(np.float64)
        if values.size == 0:
            report["fields"][field] = {"unit": unit, "n": 0, "mean": None, "sigma": None, "min": None, "max": None,
                                       "note": "no NORMAL sample after the settle window"}
            continue
        report["fields"][field] = {"unit": unit, "n": int(values.size), "mean": float(values.mean()),
                                   "sigma": population_sigma(values), "min": float(values.min()),
                                   "max": float(values.max()), "excluded_not_normal": int(np.sum(settled & ~normal))}
    speed = frames["vertical_speed_mps"].astype(np.float64)
    window = t_s < t_s[0] + settle_s
    if np.any(window):
        inside = np.flatnonzero(window)
        peak = inside[np.argmax(np.abs(speed[inside]))]
        loud = inside[np.abs(speed[inside]) > 0.05]
        report["startup"] = {"peak_abs_vertical_speed_mps": float(abs(speed[peak])), "peak_frame": int(peak),
                             "peak_t_s": float(t_s[peak]),
                             "last_t_over_0p05_mps_s": float(t_s[loud[-1]]) if loud.size else None}
    if accel_tilt is not None and report["fields"]["pitch_deg"]["mean"] is not None:
        report["pitch_minus_accel_tilt_deg"] = report["fields"]["pitch_deg"]["mean"] - accel_tilt["pitch"]
        report["roll_minus_accel_tilt_deg"] = report["fields"]["roll_deg"]["mean"] - accel_tilt["roll"]
    return report


def jitter_summary(csv_path: str | Path) -> dict[str, Any]:
    """logs/telemetry_jitter.csv: cycle,scheduled_ns,actual_ns,jitter_ns,missed_cycles."""
    rows = np.genfromtxt(csv_path, delimiter=",", names=True, dtype=np.int64)
    rows = np.atleast_1d(rows)
    jitter_us = rows["jitter_ns"].astype(np.float64) / 1000.0
    return {"path": str(csv_path), "sha256": _sha256(csv_path), "cycles": int(rows.size),
            "min_us": float(jitter_us.min()), "mean_us": float(jitter_us.mean()), "max_us": float(jitter_us.max()),
            "p99_us": float(np.percentile(jitter_us, 99)), "missed_cycles": int(rows["missed_cycles"].sum())}


# ---------------------------------------------------------------- the whole report

def _sha256(path: str | Path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def analyze(log: LogFile, settle_s: float = 10.0, label: str = "", jitter_csv: str | Path | None = None,
            command: str = "") -> dict[str, Any]:
    frames = frames_array(log)
    if frames.size == 0:
        raise ValueError("no frames")
    t_ms = frames["timestamp_ms"].astype(np.int64)
    steps = np.diff(t_ms)
    if np.any(steps <= 0):
        where = int(np.flatnonzero(steps <= 0)[0])
        raise ValueError(f"timestamps must increase: frame {where + 1} at {t_ms[where + 1]} ms follows "
                         f"{t_ms[where]} ms (a replay run past its file repeats the last frame)")
    t_s = (t_ms - t_ms[0]) / 1000.0
    tau0_s = float(steps.mean()) / 1000.0 if steps.size else PERIOD_S
    channels: dict[str, dict[str, Any]] = {}
    for spec in RAW_CHANNELS:
        fresh = frames[f"{spec.channel}_read_ok"].astype(bool)
        values = frames[spec.field][fresh]
        if values.size < 3:
            channels[spec.field] = {"field": spec.field, "unit": spec.unit, "n": int(values.size),
                                    "note": "fewer than three fresh samples"}
            continue
        channels[spec.field] = channel_report(values, t_s[fresh], spec, tau0_s)
    gps: dict[str, Any] = {"n": int(frames["gps_read_ok"].sum())}
    if gps["n"] >= 3:
        fresh = frames["gps_read_ok"].astype(bool)
        for field, unit, simulator_sigma in GPS_CHANNELS:
            values = frames[field][fresh].astype(np.float64)
            gps[field] = {"unit": unit, "mean": float(values.mean()), "sigma": population_sigma(values),
                          "simulator_sigma": simulator_sigma}
    derived = derive_constants(channels) if all("sigma" in channels[s.field] for s in RAW_CHANNELS) else None
    outputs = output_report(frames, t_s, settle_s, derived["accel_tilt_deg"] if derived else None)
    report: dict[str, Any] = {
        "schema": SCHEMA, "generated_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "label": label, "command": command,
        "inputs": {"files": [{"path": str(p), "bytes": p.stat().st_size, "sha256": _sha256(p)} for p in log.paths]},
        "run": {
            "frames": int(frames.size), "events": [
                {"t_ms": int(e.timestamp_ms), "channel": CHANNEL_NAMES.get(e.channel, e.channel),
                 "type": FAULT_TYPE_NAMES.get(e.type, e.type), "value": float(e.value)} for e in log.events],
            "fresh": {ch: int(frames[f"{ch}_read_ok"].sum()) for ch in ("baro", "imu", "gps")},
            "duration_s": float(t_s[-1]), "rate_hz": (frames.size - 1) / float(t_s[-1]) if t_s[-1] > 0 else None,
            "step_ms": {"mean": float(steps.mean()) if steps.size else None,
                        "min": int(steps.min()) if steps.size else None,
                        "max": int(steps.max()) if steps.size else None,
                        "over_30ms": int(np.sum(steps > 30)) if steps.size else 0},
            "torn_bytes": log.torn_bytes,
        },
        "channels": channels, "gps": gps, "derived": derived, "outputs": outputs,
    }
    if jitter_csv is not None:
        report["jitter"] = jitter_summary(jitter_csv)
        report["inputs"]["jitter_csv"] = str(jitter_csv)
    return report


# ---------------------------------------------------------------- rendering

def _fmt(value: Any, digits: int = 4) -> str:
    if value is None:
        return "-"
    if isinstance(value, float):
        if math.isnan(value):
            return "nan"
        return f"{value:.{digits}g}"
    return str(value)


def render_markdown(report: dict[str, Any]) -> str:
    run = report["run"]
    lines = [f"### Run{(' ' + report['label']) if report['label'] else ''}", ""]
    lines.append("| Item | Value |")
    lines.append("|---|---|")
    lines.append(f"| Files | {', '.join(Path(f['path']).name for f in report['inputs']['files'])} |")
    lines.append(f"| Frames | {run['frames']} ({run['duration_s']:.2f} s, {_fmt(run['rate_hz'])} Hz) |")
    lines.append(f"| Fresh samples | baro {run['fresh']['baro']}, IMU {run['fresh']['imu']}, GPS {run['fresh']['gps']} |")
    step = run["step_ms"]
    lines.append(f"| Timestamp step | mean {_fmt(step['mean'])} ms, min {step['min']}, max {step['max']}, "
                 f"over 30 ms: {step['over_30ms']} |")
    events = "; ".join(f"t={e['t_ms']} ms {e['channel']} {e['type']} {_fmt(e['value'])}" for e in run["events"])
    lines.append(f"| Events | {events or 'none'} |")
    lines.append(f"| Torn bytes | {run['torn_bytes']} |")
    lines += ["", "### Raw channels (fresh samples, population sigma)", ""]
    lines.append("| Channel | n | Mean | Sigma | Min | Max | Step | Longest run | Repeats (chance) |")
    lines.append("|---|---|---|---|---|---|---|---|---|")
    for spec in RAW_CHANNELS:
        c = report["channels"][spec.field]
        if "sigma" not in c:
            lines.append(f"| {spec.field} ({spec.unit}) | {c['n']} | {c.get('note', '')} |||||||")
            continue
        lines.append(f"| {spec.field} ({spec.unit}) | {c['n']} | {_fmt(c['mean'], 6)} | {_fmt(c['sigma'])} | "
                     f"{_fmt(c['min'], 6)} | {_fmt(c['max'], 6)} | {_fmt(c['step'], 3)} | {c['longest_run']} | "
                     f"{c['repeats']} ({_fmt(c['repeats_by_chance'], 2)}) |")
    lines += ["", "### Drift and correlation (sigma/ADEV above 1: a trend or positive correlation; "
              "rho1 of the detrended series tells which)", ""]
    lines.append("| Channel | Sigma | Detrended sigma | ADEV(tau0) | Sigma/ADEV | rho1 | rho1 detrended | "
                 "Trend per hour | First-minute mean | Last-minute mean |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|")
    for spec in RAW_CHANNELS:
        c = report["channels"][spec.field]
        if "sigma" not in c:
            continue
        lines.append(f"| {spec.field} | {_fmt(c['sigma'])} | {_fmt(c['detrended_sigma'])} | {_fmt(c['adev_tau0'])} | "
                     f"{_fmt(c['sigma_over_adev_tau0'], 3)} | {_fmt(c['rho1'], 3)} | {_fmt(c['rho1_detrended'], 3)} | "
                     f"{_fmt(c['trend_per_hour'], 3)} | {_fmt(c['first_window_mean'], 6)} | "
                     f"{_fmt(c['last_window_mean'], 6)} |")
    lines += ["", "### Comparison", "", "| Channel | Measured sigma | Simulator sigma today | Datasheet | Note |",
              "|---|---|---|---|---|"]
    for spec in RAW_CHANNELS:
        c = report["channels"][spec.field]
        if "sigma" not in c:
            continue
        ref = c["reference"]
        lines.append(f"| {spec.field} | {_fmt(c['sigma'])} {spec.unit} | {_fmt(ref['simulator_sigma'])} | "
                     f"{_fmt(ref['datasheet_sigma'], 3)} | {ref['datasheet_note']} |")
    lines += ["", "### Allan deviation at selected averaging times", ""]
    taus = ALLAN_TABLE_TAUS_S
    lines.append("| Channel | " + " | ".join(f"tau = {t:g} s" for t in taus) + " |")
    lines.append("|---|" + "---|" * len(taus))
    for spec in RAW_CHANNELS:
        c = report["channels"][spec.field]
        if "allan" not in c:
            continue
        grid = np.array(c["allan"]["tau_s"])
        cells = []
        for tau in taus:
            if grid.size and tau <= grid.max() * 1.05:
                k = int(np.argmin(np.abs(grid - tau)))
                cells.append(f"{_fmt(c['allan']['adev'][k], 3)} (±{100 * c['allan']['rel_error'][k]:.0f}%)")
            else:
                cells.append("-")
        lines.append(f"| {spec.field} | " + " | ".join(cells) + " |")
    if report.get("gps") and report["gps"].get("n", 0) >= 3:
        lines += ["", "### GPS (fresh fixes only)", "", "| Field | n | Mean | Sigma | Simulator sigma |",
                  "|---|---|---|---|---|"]
        for field, unit, _ in GPS_CHANNELS:
            g = report["gps"][field]
            lines.append(f"| {field} ({unit}) | {report['gps']['n']} | {_fmt(g['mean'], 8)} | {_fmt(g['sigma'])} | "
                         f"{_fmt(g['simulator_sigma'])} |")
    derived = report.get("derived")
    if derived:
        lines += ["", "### Derived constants", "",
                  "| Constant | Value | Status | Source | Placeholder it replaces |", "|---|---|---|---|---|"]
        for name, c in derived["constants"].items():
            lines.append(f"| {name} | {_fmt(c['value'])} | {c['status']} | {c['source']} | {_fmt(c['placeholder'])} |")
        lines += ["", "| Quantity | Value |", "|---|---|",
                  f"| ISA slope at the mean pressure | {_fmt(derived['isa_slope_m_per_hpa'])} m/hPa |",
                  f"| Baro altitude sigma, whole run | {_fmt(derived['baro_sigma_m_total'])} m |",
                  f"| Baro altitude sigma, white part (ADEV tau0) | {_fmt(derived['baro_sigma_m_white'])} m |",
                  f"| Pressure white sigma through the slope | {_fmt(derived['baro_sigma_m_via_slope'])} m |",
                  f"| Gyro bias x / y / z | {_fmt(derived['gyro_bias_dps']['x'])} / "
                  f"{_fmt(derived['gyro_bias_dps']['y'])} / {_fmt(derived['gyro_bias_dps']['z'])} deg/s |",
                  f"| Tilt the bias predicts (0.98 x b): pitch / roll | "
                  f"{_fmt(derived['tilt_from_bias_deg']['pitch'])} / {_fmt(derived['tilt_from_bias_deg']['roll'])} deg |",
                  f"| Accelerometer-only tilt: pitch / roll | {_fmt(derived['accel_tilt_deg']['pitch'])} / "
                  f"{_fmt(derived['accel_tilt_deg']['roll'])} deg |",
                  f"| Accelerometer offsets x / y / z-g | {_fmt(derived['accel_offset_mg']['x'], 3)} / "
                  f"{_fmt(derived['accel_offset_mg']['y'], 3)} / {_fmt(derived['accel_offset_mg']['z'], 3)} mg |",
                  f"| Longest identical run (stuck fires at {derived['stuck_count']}) | "
                  + ", ".join(f"{k} {v}" for k, v in derived["stuck_margin"].items()) + " |"]
        rule = derived["r_baro_rule"]
        lines.append(f"\nR rule: estimator **{rule['estimator']}**; rho1 of the detrended barometric altitude "
                     f"{_fmt(rule['rho1_detrended'], 3)} against a threshold of {_fmt(rule['threshold'], 3)}; "
                     f"ADEV(tau0)^2 {_fmt(rule['adev_tau0_variance'])} m^2, detrended variance "
                     f"{_fmt(rule['detrended_variance'])} m^2.")
        for warning in derived["warnings"]:
            lines.append(f"\n**Warning:** {warning}")
    outputs = report["outputs"]
    lines += ["", f"### Pipeline outputs after {outputs['settle_s']:g} s", "",
              "| Field | Mean | Sigma | Min | Max |", "|---|---|---|---|---|"]
    for field, o in outputs["fields"].items():
        lines.append(f"| {field} ({o['unit']}) | {_fmt(o['mean'], 6)} | {_fmt(o['sigma'])} | {_fmt(o['min'], 6)} | "
                     f"{_fmt(o['max'], 6)} |")
    if "startup" in outputs:
        s = outputs["startup"]
        lines.append(f"\nStartup: peak |vertical speed| {_fmt(s['peak_abs_vertical_speed_mps'], 3)} m/s at "
                     f"{_fmt(s['peak_t_s'], 3)} s (frame {s['peak_frame']}); last excursion over 0.05 m/s at "
                     f"{_fmt(s['last_t_over_0p05_mps_s'], 3)} s.")
    if "pitch_minus_accel_tilt_deg" in outputs:
        lines.append(f"Mean pitch minus accelerometer-only tilt: {_fmt(outputs['pitch_minus_accel_tilt_deg'], 3)} deg; "
                     f"roll: {_fmt(outputs['roll_minus_accel_tilt_deg'], 3)} deg (the bias predicts the tilt table above).")
    if "jitter" in report:
        j = report["jitter"]
        lines += ["", "### Scheduler jitter", "", "| Cycles | Min | Mean | Max | 99th percentile | Missed cycles |",
                  "|---|---|---|---|---|---|",
                  f"| {j['cycles']} | {_fmt(j['min_us'])} us | {_fmt(j['mean_us'])} us | {_fmt(j['max_us'])} us | "
                  f"{_fmt(j['p99_us'])} us | {j['missed_cycles']} |"]
    return "\n".join(lines) + "\n"


def save_figures(report: dict[str, Any], log: LogFile, out_dir: str | Path, stem: str = "noise_profile") -> list[Path]:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    frames = frames_array(log)
    t_min = (frames["timestamp_ms"].astype(np.float64) - float(frames["timestamp_ms"][0])) / 60000.0
    written: list[Path] = []
    title = report.get("label") or "noise profile"

    # 1. Histograms, bins one LSB wide where the LSB is known.
    fig, axes = plt.subplots(3, 3, figsize=(13, 9))
    for axis, spec in zip(axes.flat, RAW_CHANNELS):
        fresh = frames[f"{spec.channel}_read_ok"].astype(bool)
        values = frames[spec.field][fresh].astype(np.float64)
        c = report["channels"][spec.field]
        if values.size == 0:
            axis.set_title(f"{spec.field}: no fresh samples")
            continue
        if spec.lsb and (values.max() - values.min()) / spec.lsb < 400:
            edges = np.arange(values.min() - spec.lsb / 2, values.max() + spec.lsb, spec.lsb)
            axis.hist(values, bins=edges, color="tab:blue")
        else:
            axis.hist(values, bins=100, color="tab:blue")
        axis.set_title(f"{spec.field}  mean {c.get('mean', float('nan')):.5g}  sigma {c.get('sigma', float('nan')):.3g} {spec.unit}",
                       fontsize=9)
        axis.tick_params(labelsize=8)
    fig.suptitle(f"{title}: histograms of fresh samples")
    fig.tight_layout()
    path = out_dir / f"{stem}_histograms.png"
    fig.savefig(path, dpi=120)
    plt.close(fig)
    written.append(path)

    # 2. Time series over the whole run, decimated.
    stride = max(1, frames.size // 5000)
    sl = slice(None, None, stride)
    fig, axes = plt.subplots(4, 1, figsize=(12, 9), sharex=True)
    axes[0].plot(t_min[sl], frames["pressure_hpa"][sl], lw=0.5)
    axes[0].set_ylabel("pressure (hPa)")
    axes[1].plot(t_min[sl], frames["temperature_c"][sl], lw=0.6, color="tab:red")
    axes[1].set_ylabel("temperature (degC)")
    for axis_name in "xyz":
        values = frames[f"accel_{axis_name}"].astype(np.float64)
        axes[2].plot(t_min[sl], (values - values.mean())[sl], lw=0.5, label=f"accel_{axis_name} - mean")
        axes[3].plot(t_min[sl], frames[f"gyro_{axis_name}"][sl], lw=0.5, label=f"gyro_{axis_name}")
    axes[2].set_ylabel("accel about mean (m/s^2)")
    axes[2].legend(fontsize=8, loc="upper right")
    axes[3].set_ylabel("gyro (deg/s)")
    axes[3].legend(fontsize=8, loc="upper right")
    axes[3].set_xlabel("minutes")
    fig.suptitle(f"{title}: time series")
    fig.tight_layout()
    path = out_dir / f"{stem}_timeseries.png"
    fig.savefig(path, dpi=100)
    plt.close(fig)
    written.append(path)

    # 3. Allan deviation, log-log, with the confidence band and a tau^-1/2 reference.
    fig, axes = plt.subplots(1, 3, figsize=(15, 5))
    groups = (("baro_altitude_m",), ("gyro_x", "gyro_y", "gyro_z"), ("accel_x", "accel_y", "accel_z"))
    for axis, group in zip(axes, groups):
        for field in group:
            c = report["channels"][field]
            if "allan" not in c or not c["allan"]["tau_s"]:
                continue
            tau = np.array(c["allan"]["tau_s"])
            adev = np.array(c["allan"]["adev"])
            err = np.array(c["allan"]["rel_error"])
            line, = axis.loglog(tau, adev, marker=".", lw=1, label=field)
            axis.fill_between(tau, adev * (1 - err), adev * (1 + err), color=line.get_color(), alpha=0.15)
        c0 = report["channels"][group[0]]
        if "allan" in c0 and c0["allan"]["tau_s"]:
            tau = np.array(c0["allan"]["tau_s"])
            axis.loglog(tau, c0["allan"]["adev"][0] * np.sqrt(tau[0] / tau), "k--", lw=0.8, label="white noise, tau^-1/2")
        axis.set_xlabel("averaging time tau (s)")
        axis.set_ylabel("Allan deviation")
        axis.grid(True, which="both", lw=0.3)
        axis.legend(fontsize=8)
    fig.suptitle(f"{title}: Allan deviation (bands: ±1/sqrt(N/m))")
    fig.tight_layout()
    path = out_dir / f"{stem}_allan.png"
    fig.savefig(path, dpi=120)
    plt.close(fig)
    written.append(path)

    # 4. The pipeline's outputs.
    fig, axes = plt.subplots(4, 1, figsize=(12, 9), sharex=True)
    axes[0].plot(t_min[sl], frames["baro_altitude_m"][sl], lw=0.4, label="baro_altitude_m", alpha=0.6)
    axes[0].plot(t_min[sl], frames["fused_altitude_m"][sl], lw=0.8, label="fused_altitude_m")
    axes[0].set_ylabel("altitude (m)")
    axes[0].legend(fontsize=8, loc="upper right")
    axes[1].plot(t_min[sl], frames["vertical_speed_mps"][sl], lw=0.6, color="tab:green")
    axes[1].set_ylabel("vertical speed (m/s)")
    derived = report.get("derived")
    for axis, field, key in ((axes[2], "pitch_deg", "pitch"), (axes[3], "roll_deg", "roll")):
        axis.plot(t_min[sl], frames[field][sl], lw=0.6, label=field)
        if derived:
            tilt = derived["accel_tilt_deg"][key]
            axis.axhline(tilt, color="gray", lw=0.8, ls=":", label="accelerometer-only tilt")
            axis.axhline(tilt + derived["tilt_from_bias_deg"][key], color="tab:red", lw=0.8, ls="--",
                         label="accel tilt + 0.98 x gyro bias")
        axis.set_ylabel(f"{field}")
        axis.legend(fontsize=8, loc="upper right")
    axes[3].set_xlabel("minutes")
    fig.suptitle(f"{title}: pipeline outputs")
    fig.tight_layout()
    path = out_dir / f"{stem}_outputs.png"
    fig.savefig(path, dpi=100)
    plt.close(fig)
    written.append(path)
    return written


def _jsonable(value: Any) -> Any:
    if isinstance(value, dict):
        return {str(k): _jsonable(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_jsonable(v) for v in value]
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, (np.floating, np.integer, np.bool_)):
        return value.item()
    if isinstance(value, float) and math.isnan(value):
        return None
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="python -m analysis.noise_profile",
                                     description="Noise characterization of a stationary capture (FTS-NP-001).")
    parser.add_argument("paths", nargs="+", help="log files in rotation order, or one directory of them")
    parser.add_argument("--prefix", default="telemetry", help="file prefix when a directory is given")
    parser.add_argument("--jitter", help="the run's telemetry_jitter.csv")
    parser.add_argument("--figures", help="directory for the four PNG figures")
    parser.add_argument("--json", dest="json_path", help="write every number here")
    parser.add_argument("--settle-seconds", type=float, default=10.0,
                        help="pipeline outputs are summarized after this window (default 10)")
    parser.add_argument("--label", default="", help="a name for the capture, used in titles and file names")
    args = parser.parse_args(argv)
    paths = [Path(p) for p in args.paths]
    if len(paths) == 1 and paths[0].is_dir():
        directory = paths[0]
        paths = find_log_files(directory, args.prefix)
        if not paths:
            print(f"error: no {args.prefix}_NNN.bin files in {directory}", file=sys.stderr)
            return 2
    try:
        log = read_logs(paths)
        report = analyze(log, settle_s=args.settle_seconds, label=args.label, jitter_csv=args.jitter,
                         command=" ".join(sys.argv if argv is None else ["noise_profile", *argv]))
    except (ValueError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    if args.figures:
        stem = f"noise_profile_{args.label}" if args.label else "noise_profile"
        report["figures"] = [str(p) for p in save_figures(report, log, args.figures, stem)]
    if args.json_path:
        Path(args.json_path).write_text(json.dumps(_jsonable(report), indent=2) + "\n")
    print(render_markdown(report), end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
