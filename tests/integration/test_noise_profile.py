"""analysis/noise_profile.py: TC-015 steps 1 to 8 (REQ-PROC-004; also REQ-SENS-006, REQ-PROC-003).

Two oracles. Synthetic logs written with numpy's seeded generator, where every planted
sigma, bias, drift and run length is known exactly and the tolerances are multiples of
the estimator's standard error. Then the binary's own seed-42 log from the session
fixture, whose true sigmas are the configuration's `simulation.noise` (SimulationNoise's
defaults, measured on the stationary capture since 2026-10-21, FTS-NP-001); those tolerances are wide because the realization differs between libc++ and libstdc++.
"""
from __future__ import annotations

import json
import math

import numpy as np
import pytest

import synthetic_log as syn
from analysis import noise_profile as npf
from pipeline.log_reader import read_log, read_logs

N = 45_000                            # the capture's length: 15 minutes at 50 Hz
TAU0 = 0.02
SIGMA_TOLERANCE = 3 / math.sqrt(2 * N)    # three standard errors of a sigma estimate: 1.0%
WHITE = {"pressure_hpa": 0.03, "temperature_c": 0.005, "baro_altitude_m": 0.25,
         "accel_x": 0.02, "accel_y": 0.02, "accel_z": 0.02, "gyro_x": 0.02, "gyro_y": 0.02, "gyro_z": 0.02}
MEANS = {"pressure_hpa": 1013.0, "temperature_c": 25.0, "baro_altitude_m": 2.0, "accel_z": 9.81,
         "accel_x": 0.05, "gyro_y": 2.5}     # 0.05 m/s^2 of x offset, 2.5 deg/s of y bias
SIM_SIGMAS = {"pressure_hpa": 0.0282, "temperature_c": 0.0114, "accel_x": 0.0130, "accel_y": 0.0130, "accel_z": 0.0130,
              "gyro_x": 0.0254, "gyro_y": 0.0254, "gyro_z": 0.0254}   # SimulationNoise, measured (FTS-NP-001)
SIM_BARO_SIGMA_M = 0.0282 * 8.327                                # 0.2348 m at 1013.00 hPa
SIM_BARO_VARIANCE_M2 = SIM_BARO_SIGMA_M ** 2                     # 0.0551 m^2


def stationary_log(path, n, rng, sigmas, means=None, drift=None):
    """n frames whose named fields are mean + drift * t + N(0, sigma); GPS never fresh."""
    t = np.arange(n) * TAU0
    columns = {}
    for field, sigma in sigmas.items():
        columns[field] = rng.normal(0.0, sigma, n) + (means or {}).get(field, 0.0) + (drift or {}).get(field, 0.0) * t
    columns["gps_read_ok"] = [False] * n
    return syn.write_log(path, syn.frames_from_columns(n, **columns))


@pytest.fixture(scope="module")
def white_report(tmp_path_factory):
    rng = np.random.default_rng(42)
    path = stationary_log(tmp_path_factory.mktemp("white") / "telemetry_000.bin", N, rng, WHITE, MEANS)
    return npf.analyze(read_log(path), settle_s=10.0, label="white")


# ------------------------------------------------------------ synthetic oracles (steps 1 to 6)

def test_white_noise_sigma_and_mean(white_report):
    for field, sigma in WHITE.items():
        channel = white_report["channels"][field]
        assert channel["n"] == N
        assert abs(channel["sigma"] / sigma - 1) < SIGMA_TOLERANCE, field
        assert abs(channel["mean"] - MEANS.get(field, 0.0)) < 3 * sigma / math.sqrt(N), field


def test_gyro_bias_recovered(white_report):
    derived = white_report["derived"]
    assert abs(derived["gyro_bias_dps"]["y"] - 2.5) < 3 * WHITE["gyro_y"] / math.sqrt(N)
    assert abs(derived["gyro_bias_dps"]["x"]) < 3 * WHITE["gyro_x"] / math.sqrt(N)
    assert derived["tilt_from_bias_deg"]["pitch"] == pytest.approx(0.98 * derived["gyro_bias_dps"]["y"])
    assert derived["tilt_from_bias_deg"]["roll"] == pytest.approx(0.98 * derived["gyro_bias_dps"]["x"])
    assert derived["tilt_from_bias_deg"]["pitch"] == pytest.approx(2.45, abs=0.01)


def test_adev_white_noise_scales_as_inverse_sqrt_tau():
    rng = np.random.default_rng(7)
    sigma = 0.5
    x = rng.normal(0.0, sigma, N)
    result = npf.allan_deviation(x, TAU0, [1, 4, 16, 64, 256])
    assert list(result.tau_s) == pytest.approx([0.02, 0.08, 0.32, 1.28, 5.12])
    for m, adev in zip([1, 4, 16, 64, 256], result.adev):
        assert abs(adev * math.sqrt(m) / sigma - 1) < 0.10, m


def test_adev_tau0_is_half_first_difference_variance():
    rng = np.random.default_rng(8)
    x = rng.normal(3.0, 0.7, 5000)
    adev0 = npf.allan_deviation(x, TAU0, [1]).adev[0]
    assert adev0 ** 2 == pytest.approx(0.5 * np.mean(np.diff(x) ** 2), rel=1e-12)


def test_drift_inflates_sigma_but_not_adev_tau0(tmp_path):
    # White noise sigma 1 plus a linear drift whose total rise over the run equals sqrt(12):
    # the plain sigma grows to sqrt(1 + 1), the one-sample Allan deviation stays 1, and at
    # the longest averaging time the Allan deviation is the drift's d*tau/sqrt(2).
    rng = np.random.default_rng(9)
    duration_s = N * TAU0
    drift = math.sqrt(12.0) / duration_s
    path = stationary_log(tmp_path / "telemetry_000.bin", N, rng, {"gyro_z": 1.0}, drift={"gyro_z": drift})
    channel = npf.analyze(read_log(path), settle_s=1.0)["channels"]["gyro_z"]
    expected_total = math.sqrt(1.0 + drift ** 2 * TAU0 ** 2 * (N ** 2 - 1) / 12.0)
    assert abs(channel["sigma"] / expected_total - 1) < 0.02
    assert abs(channel["adev_tau0"] - 1.0) < 0.01
    assert channel["sigma_over_adev_tau0"] == pytest.approx(math.sqrt(2.0), rel=0.03)
    tau_max = channel["allan"]["tau_s"][-1]
    adev_max = channel["allan"]["adev"][-1]
    assert abs(adev_max / (drift * tau_max / math.sqrt(2.0)) - 1) < 0.10


def test_lag1_autocorrelation():
    rng = np.random.default_rng(10)
    white = rng.normal(0.0, 1.0, N)
    assert abs(npf.lag1_autocorrelation(white)) < 3 / math.sqrt(N)
    phi = 0.3
    ar1 = np.empty(N)
    ar1[0] = 0.0
    noise = rng.normal(0.0, 1.0, N)
    for i in range(1, N):
        ar1[i] = phi * ar1[i - 1] + noise[i]
    assert abs(npf.lag1_autocorrelation(ar1) - phi) < 0.03


def test_longest_run_and_quantization():
    rng = np.random.default_rng(11)
    x = rng.normal(0.0, 1.0, 2000)
    assert npf.longest_identical_run(x) == 1
    x[500:510] = x[500]
    assert npf.longest_identical_run(x) == 10
    assert npf.longest_identical_run(np.array([])) == 0
    assert npf.longest_identical_run(np.array([4.0, 4.0, 4.0])) == 3
    grid = np.round(rng.normal(9.81, 0.1, 5000) / npf.ACCEL_LSB) * npf.ACCEL_LSB
    assert abs(npf.quantization_step(grid.astype(np.float32)) / npf.ACCEL_LSB - 1) < 0.01


def test_held_samples_are_excluded(tmp_path):
    # Every odd frame carries a copy of the previous IMU reading with imu_read_ok false, the
    # shape a failed read leaves (live_sensor_source.cpp). Copies must not enter n or the runs.
    rng = np.random.default_rng(12)
    gyro = rng.normal(0.0, 0.02, 200)
    gyro[1::2] = gyro[0::2]
    fresh = [i % 2 == 0 for i in range(200)]
    frames = syn.frames_from_columns(200, gyro_x=gyro, imu_read_ok=fresh, gps_read_ok=[False] * 200)
    report = npf.analyze(read_log(syn.write_log(tmp_path / "telemetry_000.bin", frames)), settle_s=1.0)
    assert report["channels"]["gyro_x"]["n"] == 100
    assert report["channels"]["gyro_x"]["longest_run"] == 1
    assert report["run"]["fresh"] == {"baro": 200, "imu": 100, "gps": 0}


def test_isa_slope_matches_finite_difference():
    assert npf.isa_slope_m_per_hpa(1013.0) == pytest.approx(-8.327, abs=0.002)
    for pressure in (950.0, 1013.0, 1030.0):
        central = (npf.isa_altitude_m(pressure + 0.005) - npf.isa_altitude_m(pressure - 0.005)) / 0.01
        assert npf.isa_slope_m_per_hpa(pressure) == pytest.approx(central, abs=1e-4)


def test_accel_tilt_matches_processor_convention():
    pitch, roll = npf.accel_tilt_deg(9.81, 0.0, 9.81)      # TC-006 step 2's 45 degree case
    assert pitch == pytest.approx(45.0, abs=0.1)
    assert roll == pytest.approx(0.0, abs=1e-9)
    pitch, roll = npf.accel_tilt_deg(0.0, -9.81, 9.81)
    assert (pitch, roll) == pytest.approx((0.0, -45.0), abs=0.1)


def test_derived_constants_formulas(white_report):
    derived = white_report["derived"]
    constants = derived["constants"]
    altitude = white_report["channels"]["baro_altitude_m"]
    assert constants["R_baro_m2"]["value"] == pytest.approx(altitude["adev_tau0"] ** 2)
    assert constants["P0_alt_m2"]["value"] == constants["R_baro_m2"]["value"]
    assert constants["R_gps_m2"]["value"] == 25.0 and constants["R_gps_m2"]["status"] == "retained"
    assert constants["Q_accel_var"]["value"] == 1.0 and constants["Q_accel_var"]["status"] == "retained"
    assert constants["P0_vel"]["value"] == 1.0
    assert derived["accel_offset_mg"]["x"] == pytest.approx(0.05 / 9.81 * 1000, rel=0.1)
    assert abs(derived["accel_offset_mg"]["z"]) < 2.0
    assert derived["baro_sigma_m_via_slope"] == pytest.approx(
        abs(derived["isa_slope_m_per_hpa"]) * white_report["channels"]["pressure_hpa"]["adev_tau0"])
    assert derived["r_baro_rule"]["estimator"] == "adev_tau0"      # white: the two estimators agree
    assert abs(derived["r_baro_rule"]["rho1_detrended"]) < derived["r_baro_rule"]["threshold"]
    assert derived["r_baro_rule"]["detrended_variance"] == pytest.approx(constants["R_baro_m2"]["value"], rel=0.03)
    assert derived["warnings"] == []
    # float32 resolves 6e-5 hPa at 1013 hPa against 0.03 hPa of noise, so about 26 of the
    # 45,000 consecutive pairs land on the same value: runs of 2, never near the detector's 10.
    assert 1 <= derived["stuck_margin"]["pressure_hpa"] <= 3 and derived["stuck_count"] == 10
    assert derived["stuck_margin"]["gyro_y"] == 1


def test_r_rule_takes_detrended_variance_when_samples_are_correlated(tmp_path):
    # AR(1) noise with phi = 0.3 plus 1.8 m of linear drift on the barometric altitude. The
    # drift raises the plain sigma; the correlation makes ADEV(tau0)^2 understate the per-sample
    # variance by 30%. The rule must notice rho1 of the detrended series and take its variance.
    rng = np.random.default_rng(13)
    phi, sigma_w = 0.3, 0.25
    innovations = rng.normal(0.0, sigma_w, N)
    ar1 = np.empty(N)
    ar1[0] = innovations[0]
    for i in range(1, N):
        ar1[i] = phi * ar1[i - 1] + innovations[i]
    t = np.arange(N) * TAU0
    altitude = 2.0 + ar1 + 0.002 * t
    pressure = 1013.0 + rng.normal(0.0, 0.03, N)
    frames = syn.frames_from_columns(N, baro_altitude_m=altitude, pressure_hpa=pressure, gps_read_ok=[False] * N)
    report = npf.analyze(read_log(syn.write_log(tmp_path / "telemetry_000.bin", frames)), settle_s=10.0)
    rule = report["derived"]["r_baro_rule"]
    true_variance = sigma_w ** 2 / (1 - phi ** 2)
    assert rule["estimator"] == "detrended_variance"
    assert rule["rho1_detrended"] == pytest.approx(phi, abs=0.03)
    assert report["derived"]["constants"]["R_baro_m2"]["value"] == pytest.approx(true_variance, rel=0.03)
    assert rule["adev_tau0_variance"] == pytest.approx(true_variance * (1 - phi), rel=0.05)
    assert report["channels"]["baro_altitude_m"]["sigma"] > 1.5 * report["channels"]["baro_altitude_m"]["detrended_sigma"]
    assert report["channels"]["baro_altitude_m"]["trend_per_hour"] == pytest.approx(7.2, rel=0.05)
    assert report["derived"]["warnings"] == []


def test_r_rule_warns_on_anticorrelation_and_repeated_conversions(tmp_path):
    rng = np.random.default_rng(14)
    n = 4000
    anti = 2.0 + np.diff(rng.normal(0.0, 0.3, n + 1))      # differenced white noise: rho1 = -0.5
    pressure = 1013.0 + rng.normal(0.0, 0.03, n)
    pressure[1::2] = pressure[0::2]                        # every other read returns the previous conversion
    frames = syn.frames_from_columns(n, baro_altitude_m=anti, pressure_hpa=pressure, gps_read_ok=[False] * n)
    report = npf.analyze(read_log(syn.write_log(tmp_path / "telemetry_000.bin", frames)), settle_s=1.0)
    warnings = report["derived"]["warnings"]
    assert any("anti-correlated" in w for w in warnings)
    assert any("faster than it converts" in w for w in warnings)
    assert report["derived"]["r_baro_rule"]["estimator"] == "adev_tau0"
    # n // 2 planted repeats, plus the few float32 coincidences chance adds between the pairs.
    assert n // 2 <= report["channels"]["pressure_hpa"]["repeats"] <= n // 2 + 10
    assert "Warning" in npf.render_markdown(report)


def test_jitter_summary(tmp_path):
    csv = tmp_path / "telemetry_jitter.csv"
    csv.write_text("cycle,scheduled_ns,actual_ns,jitter_ns,missed_cycles\n"
                   "0,1000000,1001000,1000,0\n1,21000000,21003000,3000,0\n2,41000000,41200000,200000,1\n")
    summary = npf.jitter_summary(csv)
    assert summary["cycles"] == 3 and summary["missed_cycles"] == 1
    assert (summary["min_us"], summary["max_us"]) == (1.0, 200.0)
    assert summary["mean_us"] == pytest.approx(68.0)
    assert summary["p99_us"] == pytest.approx(196.06)


def test_markdown_and_figures(white_report, tmp_path):
    text = npf.render_markdown(white_report)
    for field in WHITE:
        assert field in text
    assert "R_baro_m2" in text and "Longest identical run" in text
    log = read_log(white_report["inputs"]["files"][0]["path"])
    written = npf.save_figures(white_report, log, tmp_path, "white")
    assert [p.name for p in written] == ["white_histograms.png", "white_timeseries.png", "white_allan.png",
                                         "white_outputs.png"]
    assert all(p.stat().st_size > 1024 for p in written)


def test_non_increasing_timestamps_refused(tmp_path):
    frames = [syn.make_frame(0), syn.make_frame(1), syn.make_frame(1), syn.make_frame(1)]
    with pytest.raises(ValueError, match="timestamps must increase"):
        npf.analyze(read_log(syn.write_log(tmp_path / "t.bin", frames)))


# ------------------------------------------------------------ the binary's own log (steps 7 and 8)

@pytest.fixture(scope="module")
def sim_report(sim_session):
    return npf.analyze(read_log(sim_session.log), settle_s=2.0, label="sim")


def test_raw_sigmas_match_simulator(sim_report):
    # 500 frames: the standard error of a sigma is 3.2%, so 15% is 4.7 of them, and the
    # realization differs between libc++ and libstdc++.
    for field, sigma in SIM_SIGMAS.items():
        assert abs(sim_report["channels"][field]["sigma"] / sigma - 1) < 0.15, field


def test_means_centered(sim_report):
    channels = sim_report["channels"]
    assert channels["pressure_hpa"]["mean"] == pytest.approx(1013.0, abs=0.05)
    assert channels["temperature_c"]["mean"] == pytest.approx(21.0, abs=0.03)
    assert channels["accel_z"]["mean"] == pytest.approx(9.81, abs=0.02)
    for field in ("accel_x", "accel_y"):
        assert abs(channels[field]["mean"]) < 0.01, field
    for field in ("gyro_x", "gyro_y", "gyro_z"):
        assert abs(channels[field]["mean"]) < 0.02, field


def test_gps_fresh_once_per_second(sim_report):
    assert sim_report["gps"]["n"] == 10 == sim_report["run"]["fresh"]["gps"]
    assert sim_report["gps"]["gps_altitude_m"]["mean"] == pytest.approx(3.0, abs=2.5)


def test_baro_sigma_in_metres_two_ways(sim_report):
    derived = sim_report["derived"]
    assert derived["baro_sigma_m_white"] == pytest.approx(derived["baro_sigma_m_via_slope"], rel=0.01)
    assert abs(derived["baro_sigma_m_white"] / SIM_BARO_SIGMA_M - 1) < 0.15
    assert abs(derived["baro_sigma_m_total"] / SIM_BARO_SIGMA_M - 1) < 0.15
    assert derived["isa_slope_m_per_hpa"] == pytest.approx(-8.327, abs=0.01)


def test_white_estimators_agree(sim_report):
    for field in SIM_SIGMAS:
        channel = sim_report["channels"][field]
        assert 0.85 < (channel["adev_tau0"] / channel["sigma"]) ** 2 < 1.15, field
        assert abs(channel["rho1"]) < 0.15, field


def test_r_baro_matches_the_simulator(sim_report):
    r_baro = sim_report["derived"]["constants"]["R_baro_m2"]["value"]
    assert SIM_BARO_VARIANCE_M2 * 0.75 < r_baro < SIM_BARO_VARIANCE_M2 * 1.25


def test_no_stuck_runs(sim_report):
    assert all(run <= 3 for run in sim_report["derived"]["stuck_margin"].values())
    assert sim_report["run"]["events"] == []


def test_fused_reduces_noise_after_settle(sim_report):
    fused = sim_report["outputs"]["fields"]["fused_altitude_m"]["sigma"]
    assert fused < 0.5 * sim_report["channels"]["baro_altitude_m"]["sigma"]


def test_zero_bias_consequence(sim_report):
    derived = sim_report["derived"]
    assert all(abs(b) < 0.02 for b in derived["gyro_bias_dps"].values())
    assert abs(sim_report["outputs"]["pitch_minus_accel_tilt_deg"]) < 0.05
    assert abs(sim_report["outputs"]["roll_minus_accel_tilt_deg"]) < 0.05


def test_cli_end_to_end(sim_session, tmp_path, capsys):
    json_path = tmp_path / "noise_profile.json"
    code = npf.main([str(sim_session.log), "--jitter", str(sim_session.jitter_csv), "--figures", str(tmp_path),
                     "--json", str(json_path), "--settle-seconds", "2", "--label", "sim"])
    assert code == 0
    out = capsys.readouterr().out
    assert "### Derived constants" in out and "### Scheduler jitter" in out
    report = json.loads(json_path.read_text())
    assert report["schema"] == "FTS-NP-001/1"
    for key in ("inputs", "run", "channels", "derived", "outputs", "jitter", "figures"):
        assert key in report, key
    assert report["jitter"]["cycles"] == 500
    assert sorted(p.name for p in tmp_path.glob("noise_profile_sim_*.png")) == [
        "noise_profile_sim_allan.png", "noise_profile_sim_histograms.png", "noise_profile_sim_outputs.png",
        "noise_profile_sim_timeseries.png"]
