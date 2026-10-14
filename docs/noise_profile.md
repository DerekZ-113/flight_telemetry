# Flight Telemetry System — Sensor Noise Profile

> **Document ID:** FTS-NP-001
> **Version:** 0.1.0
> **Status:** Draft (method and acceptance criteria; results pending the capture of 2026-10-13)
> **Last Updated:** 2026-10-12
> **Related:** FTS-SRD-001 (REQ-PROC-004, REQ-SENS-006), FTS-DD-001 (Sections 5 and 6), FTS-TP-001 (TC-015), FTS-TM-001, FTS-VR-001 (Entry 9)

This document is the measurement REQ-PROC-004 names. It states, before the data exists, what is measured on a stationary capture of the real sensors, how each number is estimated, what the datasheets lead us to expect, which Kalman constants the capture can set and which it cannot, and the rules that decide the open questions once the numbers are in. Version 0.1.0 is the method; 0.2.0 adds the results. Writing the method first is the test plan's own discipline (a card before its execution) applied to a measurement, so that the analysis cannot drift toward a wanted answer.

## 1. Purpose and scope

The Kalman filter (FTS-DD-001 Section 5) needs the variance of a barometric altitude measurement, and the complementary filter's behaviour on hardware depends on the gyroscope's bias. Until this document carries measured values, the filter's constants (the `processing` keys of `config/telemetry_config.yaml` and the equal defaults of `ProcessorConfig`, REQ-CFG-001) are placeholders chosen from the simulator's own noise. The capture also sets the simulator's sigmas (`simulation.noise` in the file and the `SimulationNoise` defaults, REQ-SENS-006), so the simulator becomes the sensor's twin, and gives the fault detector's stuck-value rule its margin.

**Measured here:** per channel, on fresh samples only, the mean, the population sigma, the Allan deviation over averaging times from one sample up, the lag-1 autocorrelation, the quantization step, and the longest run of identical samples; the gyroscope bias per axis and the tilt it predicts; the accelerometer offsets; the barometric altitude noise in metres two ways; the pipeline's own outputs after a settle window and the startup transient inside it; the scheduler's jitter over the run.

**Not measured here, and why:** scale-factor errors (a stationary board gives the gyro a zero input and the accelerometer one known input, 1 g on one axis; scale needs a known rotation or a flip test); the accelerometer's x and y offsets separately from the board's tilt (the two are the same number on a still board; a six-position tumble separates them); GPS noise (no receiver until REQ-SENS-005); the process noise (Section 3.4); anything about the sensors in motion.

## 2. Method

### 2.1 Setup and settings

| Item | Value |
|---|---|
| Hardware | Raspberry Pi 3 Model B+; barometer at I2C 0x76 (driver `Bmp280`, chip ID 0x58 or 0x60); MPU6050 at 0x68 |
| Barometer settings | temperature ×1, pressure ×4 oversampling, normal mode, 0.5 ms standby, IIR filter off (`src/drivers/bmp280.cpp`); about 12 ms per conversion, so a 50 Hz read never repeats one |
| IMU settings | 100 Hz internal sample rate, DLPF setting 4 (about 20 Hz), ±250 deg/s, ±2 g (`src/drivers/mpu6050.cpp`) |
| Loop | 50 Hz (`telemetry --live --cycles 45000`), frames stamped with the actual read time |
| Duration | 15 minutes, 45,000 frames, five rotated log files of 9,986 frames and one of 5,056 |
| Conditions | board flat on a solid surface and untouched, the Pi otherwise idle; room temperature and a METAR sea-level pressure noted at start and end |
| Tools | `pipeline/log_reader.py` (files, rotation, numpy layout), `analysis/noise_profile.py` (this document's tables, JSON, and figures) |

### 2.2 Estimators

Every statistic is computed in float64 on the frame's float32 fields, over fresh samples only (`read_ok` true for the channel): a value held across a failed read is a copy, and counting it would shrink the sigma and manufacture identical runs, the same rule REQ-PROC-006 applies to the filters.

| Quantity | Estimator |
|---|---|
| Mean, sigma | population values (divide by n). At N = 45,000 the standard error of a sigma is 1/√(2N) = 0.33%; at 500 it is 3.2% |
| Allan deviation | overlapping estimator through cumulative sums: for an averaging factor m, the cluster averages of m samples, the differences of averages m apart, AVAR(τ) = ½ × their mean square, τ = m τ₀; m log-spaced from 1 to N/4 (20 ms to 225 s on the capture); the confidence of each point about ±1/√(N/m) |
| ADEV(τ₀) | the one-sample Allan deviation: ADEV(τ₀)² = ½ E[(xᵢ₊₁ − xᵢ)²] = σ²(1 − ρ₁) for a stationary process. White noise gives σ; a slow trend adds almost nothing; low-pass correlation lowers it |
| σ/ADEV(τ₀) | = 1/√(1 − ρ₁): 1 for white noise; above 1 when a trend or positive (low-pass) correlation is present, which the ratio alone cannot tell apart; below 1 only for anti-correlated samples, which no mechanism here produces |
| ρ₁ | the lag-1 autocorrelation of the samples about their mean (raw), and of the linearly detrended series: a trend makes the raw ρ₁ large on its own, so the detrended value is the one that separates correlated noise from drift |
| Detrended sigma | the sigma of the residuals after a least-squares line against time: the slow drift removed, the per-sample scatter kept whether correlated or not; the trend's slope is reported per hour |
| Repeats | the count of consecutive samples that are bit-identical, against the count float32 chance predicts, N × step / (2σ√π): the chip read faster than it converts would repeat conversions far beyond chance |
| Quantization step | the smallest nonzero gap between distinct values: the sensor's LSB where the frame's float32 resolves it (0.000599 m/s², 0.00763 deg/s, 0.01 °C); float32's own step for pressure (0.00006 hPa at 1013 hPa, coarser than the chip's 1/256 Pa) |
| Longest identical run | the longest run of bit-identical consecutive samples, against the detector's threshold of 10 (FAULT-005, 006, 007); the barometer is tracked on pressure only, temperature is not tracked |
| Drift | the mean of the first minute against the last; the time series; the turn of the Allan curve |
| ISA slope | dh/dP = −44330 × 0.1903 / 1013.25 × (P/1013.25)^(0.1903 − 1), the derivative of `altitude.cpp`'s formula: −8.33 m/hPa at 1013 hPa |
| Barometric altitude sigma | two ways: the `baro_altitude_m` channel directly (the pipeline computed it from each pressure), and the pressure's white sigma through the slope; they agree to the linearization |
| Tilt from gyro bias | 0.98 × b degrees for a bias b in deg/s: the fixed point of angle ← α(angle + b dt) + (1 − α) × accel angle at α = 0.98 and dt = 20 ms, α dt/(1 − α) = 0.98 s (FTS-DD-001 Section 6); pitch from gyro_y, roll from gyro_x |
| Accelerometer-only tilt | pitch = atan2(aₓ, √(a_y² + a_z²)), roll = atan2(a_y, √(aₓ² + a_z²)) on the mean vector, the filter's own formula |
| Pipeline outputs | fused altitude, vertical speed, pitch, roll summarized after a settle window of 10 s; inside it the peak |vertical speed| and the last time it exceeded 0.05 m/s |
| Jitter | `logs/telemetry_jitter.csv`: min, mean, max, 99th percentile in µs, missed cycles |

### 2.3 Oracles

The estimators are verified before they meet real data (TC-015 steps 1 through 8, `tests/integration/test_noise_profile.py`): synthetic logs of 45,000 frames with planted sigmas, a planted bias, a planted drift, planted identical runs and an LSB grid, with tolerances stated as multiples of the standard error; then the binary's own seed-42 log, whose true sigmas are the literals in `src/drivers/simulated_data.cpp`. The simulator's barometric variance derived this way is 2.774 m² = (0.2 hPa × 8.327 m/hPa)², and the placeholder of 3 m² was that number plus sampling luck.

### 2.4 Figures

Four PNGs under `docs/img/`: histograms with bins one LSB wide where the LSB is known; the time series over the run; the Allan deviation on log axes with the confidence band and a τ^−½ reference; the pipeline's outputs with the accelerometer-only tilt and the bias-predicted tilt drawn across pitch and roll.

## 3. Expectations and decision rules, written before the data

### 3.1 What the datasheets lead us to expect

| Channel | Expected at the driver's settings | Source |
|---|---|---|
| Pressure | a few pascals RMS, 0.02 to 0.04 hPa (×4 oversampling, IIR off); barometric altitude 0.2 to 0.35 m; variance 0.04 to 0.12 m², 25 to 75 times below the 3 m² placeholder | BMP280 datasheet noise table at ×4 (**confirm the figure when the results are written**) |
| Gyroscope | about 0.022 deg/s per axis: rate noise density 0.005 deg/s/√Hz × √(20 Hz); bias of a few deg/s, moving in the first minutes as the die warms | MPU-6050 PS-MPU-6000A-00 §6.1: 0.005 deg/s/√Hz, total RMS 0.05 deg/s at the 100 Hz DLPF, initial zero-rate output ±20 deg/s, ±20 deg/s over −40 to 85 °C |
| Accelerometer | about 0.0175 m/s² per axis: 400 µg/√Hz × √(20 Hz); x and y means up to ±0.5 m/s² (±50 mg initial offset), indistinguishable from 2.9° of tilt; z up to ±0.8 m/s² from 9.81 (±80 mg) | §6.2: 400 µg/√Hz, initial calibration tolerance ±50 mg (x, y), ±80 mg (z) |
| Temperature | a rise of one or two degrees over the run, mostly early; 0.01 °C steps | self-heating; the compensated output's resolution |
| Correlation | barometer: detrended ρ₁ within sampling error of zero (independent conversions), so σ/ADEV(τ₀) near 1 unless the weather adds a trend. IMU: detrended ρ₁ around 0.1 to 0.3 and σ/ADEV(τ₀) around 1.1 to 1.2 (ADEV/σ ≈ 0.85), because the 20 Hz DLPF correlates samples 20 ms apart. That is the filter, not a drift finding, and no IMU variance feeds the Kalman filter | the driver settings |
| Stuck runs | under 3 on every tracked channel; the stuck rule's margin is the gap to 10 | FAULT-005, 006, 007 |
| Timing | 0 missed cycles over 45,000 | TC-009 step 7 |

### 3.2 The barometric measurement variance

R is the per-sample scatter the filter sees at each update, which the Kalman gain is built for. The plain sigma over 15 minutes includes the weather: a 0.3 hPa drift over the run is 2.5 m, more than the sensor's noise, and would inflate R several-fold, and the trend is something the filter cannot see either way. Two estimators remove it: ADEV(τ₀)² (half the first-difference variance, immune to any slow drift, but equal to σ²(1 − ρ₁), so it understates the per-sample variance when consecutive samples are positively correlated and would leave the filter overconfident) and the variance of the linearly detrended series (immune to a linear drift only, but it keeps correlated noise whole). A trend and positive correlation both raise σ above ADEV(τ₀), so the ratio cannot tell them apart; ρ₁ of the detrended series can. **Rule, decided by ρ₁ of the linearly detrended `baro_altitude_m` against 3/√N (0.014 on the capture):** within that of zero, the samples are white, the two estimators agree, and R := ADEV(τ₀)², the one that needs no trend model; significantly above it, the samples are correlated and R := the detrended variance, with ρ₁ and the understatement ADEV(τ₀)² would have carried named in the results; significantly below it, the samples are anti-correlated, which no mechanism here produces, and the analysis stops until the cause is found. Two further checks: if the pressure's repeats exceed five times the float32 chance count, the chip is being read faster than it converts (duplicate conversions), and the analysis stops; if σ²/ADEV(τ₀)² on the barometer exceeds 2, the results name the low-frequency source (the detrended ρ₁ says whether it is the weather's trend or correlated fluctuation such as the room's pressure) and the day's METAR trend. The plain sigma, the detrended sigma, the ratio, both ρ₁ values, the trend per hour, the first-minute and last-minute means, and the Allan curve are all reported so the choice is visible. The script applies the rule and prints which estimator it took (`r_baro_rule` in the JSON).

### 3.3 Which constants the capture sets

| Constant | Status | Rule |
|---|---|---|
| `kBaroMeasurementVariance` | **measured** | ADEV(τ₀)² of `baro_altitude_m`, in m² |
| `kInitialAltitudeVariance` | **measured** | equals the barometric variance: the filter is born from one barometric sample |
| `kGpsMeasurementVariance` | **retained**, 25 m² | no GPS until REQ-SENS-005; the NEO-6M's vertical error is about 5 m |
| `kAccelNoiseVariance` | **retained**, 1 (m/s²)² | Section 3.4 |
| `kInitialVelocityVariance` | **chosen by replay** | Section 3.5 |

The simulator's four sigmas (pressure, temperature, accelerometer, gyroscope) take the measured detrended sigmas, the per-sample scatter with the slow drift removed (a gyro bias that moves as the die warms is drift, not noise), so the simulator is the sensor's twin; its GPS sigma stays 2 m. The simulator is a white per-sample generator and cannot model the IMU's DLPF correlation; noted, out of scope.

### 3.4 Why the process noise stays a tuning choice

The process noise is the vehicle's unmodelled vertical acceleration: the predict step is constant velocity with no accelerometer input (`kalman_filter.cpp`), and Q = σ_a² × [dt⁴/4, dt³/2; dt³/2, dt²] admits that the vehicle may accelerate between frames. A stationary board has zero true process noise, and a Q taken from the accelerometer's own electronic noise (about 0.0003 (m/s²)²) would describe the sensor, not the vehicle. The consequences, computed from the filter's own equations at dt = 20 ms with a barometric variance of 0.07 m²:

| Tuning | k₀ | k₁ | Steady σ_h | Steady σ_v | 90% of a 1 m/s climb seen after |
|---|---|---|---|---|---|
| Today: R = 3 m², σ_a² = 1 | 0.021 | 0.011 | 0.25 m | 0.19 m/s | 3.5 s |
| Measured R = 0.07 m², σ_a² = 1 retained | 0.054 | 0.074 | 0.06 m | 0.12 m/s | 1.4 s |
| Measured R = 0.07 m², σ_a² = 0.0004 from the accelerometer's noise | 0.008 | 0.0015 | 0.02 m | 0.006 m/s | 9.7 s |

σ_a = 1 m/s², a gentle maneuver, stays. The row that looks best on a still board is the one that would lag a real climb by ten seconds.

### 3.5 The initial velocity variance, chosen by replay

The startup transient of about 0.7 m/s in vertical speed on a still board is this constant's: the prior velocity uncertainty random-walks until the altitude data pins it, and the measured barometric variance does not remove it. From a Monte Carlo of the filter as written (500 still frames, the filter born at the first measurement; peak |vertical speed|, median and 90th percentile):

| R (m²) | P0_vel ((m/s)²) | Peak |v| median / 90th |
|---|---|---|
| 3.0 | 1.0 | 0.74 / 1.17 m/s |
| 0.07 | 1.0 | 0.67 / 0.99 m/s |
| 0.07 | 0.01 | 0.17 / 0.20 m/s |

The cost of the small value: a vehicle already climbing at 1 m/s when the filter is born is seen at 90% after about 1.4 s instead of 0.5 s; with σ_a² = 1 the velocity variance re-inflates to its steady state within about 0.7 s regardless. **Rule:** replay the capture's file 000 through 1.0, 0.1 and 0.01 (m/s)² and choose the largest candidate whose peak |vertical speed| on the still capture stays under 0.2 m/s while its 90% response to a 1 m/s velocity present at birth stays under 2 s; record all three. A telemetry unit boots on the ground, so the small value is the expected outcome.

### 3.6 Gyroscope bias

Nothing is subtracted in this revision. The bias per axis, its drift over the run, the tilt it predicts (0.98 × b), and the measured tilt that confirms the formula are recorded here and in FTS-DD-001 Section 6. **Rule:** if 0.98 × |b| exceeds 1° on either axis, a requirement for subtracting a configured per-axis bias is proposed as a follow-up (text: "The attitude filter shall subtract a configured per-axis gyroscope bias from each gyroscope reading before integration; the bias shall be the stationary mean recorded in FTS-NP-001 and shall be configurable under REQ-CFG-001."), conditional on a second capture on another day agreeing within 0.2 deg/s, because the datasheet's ±20 deg/s part-to-part and over-temperature figures mean a constant measured once can be wrong by its own size later. Below 1° the accelerometer term already holds the error. Estimating the bias at startup is not planned: it assumes a still vehicle at boot and needs its own fault mode. The lasting fix is a bias state in the filter.

## 4. Provenance

To be filled from the capture; the machine part comes from `provenance.txt` and `SHA256SUMS` written on the Pi, the JSON the analysis writes carries each input file's SHA-256 and the command line.

| Item | Value |
|---|---|
| Capture start / end (local) | pending |
| Commit the Pi ran | pending |
| Board, OS, kernel, GCC | pending |
| Chip IDs and addresses | pending |
| Orientation and surface | pending |
| Room temperature start / end | pending |
| METAR station, sea-level pressure and time, start / end | pending |
| Command | `./build/telemetry --live --cycles 45000` |
| Duration, frames, missed cycles | pending |
| Fault events | pending (one GPS COMM_TIMEOUT near 2020 ms expected) |
| Files and SHA-256 | pending (`captures/noise-2026-10-13/`) |
| Analysis command and JSON | pending |

## 5. Results

Pending the capture. The worked example below is the analysis on a 30-second simulated run (1500 frames, seed 42, this Mac, 2026-10-12), kept as the oracle's reference: every sigma is the simulator's literal within sampling error, the two white-noise estimators agree, and the derived barometric variance is the simulator's own.

| Channel | n | Mean | Sigma | ADEV(τ₀) | σ/ADEV | ρ₁ | Simulator sigma |
|---|---|---|---|---|---|---|---|
| pressure_hpa | 1500 | 1013.00 | 0.2023 | 0.1999 | 1.01 | 0.024 | 0.2 |
| temperature_c | 1500 | 21.00 | 0.0998 | 0.0989 | 1.01 | 0.018 | 0.1 |
| baro_altitude_m | 1500 | 2.089 | 1.684 | 1.664 | 1.01 | 0.024 | 1.665 |
| accel_x / y / z | 1500 | 0.001 / 0.000 / 9.808 | 0.0494 / 0.0498 / 0.0501 | 0.0487 / 0.0493 / 0.0498 | 1.01 | 0.01 to 0.03 | 0.05 |
| gyro_x / y / z | 1500 | −0.001 / 0.002 / −0.004 | 0.1009 / 0.1021 / 0.0987 | 0.102 / 0.100 / 0.099 | 0.99 to 1.02 | −0.02 to 0.04 | 0.1 |

Derived from it: R_baro 2.77 m² by the ADEV(τ₀)² branch of the rule (detrended ρ₁ 0.024 against a threshold of 0.077; the simulator's 2.774), the ISA slope −8.327 m/hPa, the barometric sigma 1.664 m both ways, gyro bias under 0.005 deg/s, longest identical run 1 on every tracked channel, the fused altitude sigma 0.20 m after 5 s against 1.68 m raw, and the startup transient's peak 0.78 m/s at 0.62 s (Section 3.5). The rule's other branch is exercised by a synthetic log of AR(1) noise (ρ₁ = 0.3) plus 1.8 m of drift: the detrended variance recovers the true per-sample variance within 3% where ADEV(τ₀)² understates it by 30%.

## 6. Derived constants

| Constant | Formula | Status | Value | Placeholder it replaces |
|---|---|---|---|---|
| `kBaroMeasurementVariance` | Section 3.2's rule: ADEV(τ₀)² of `baro_altitude_m` when its detrended ρ₁ is within sampling error of zero, its detrended variance when ρ₁ is significantly positive | measured | pending | 3.0 m² |
| `kInitialAltitudeVariance` | = `kBaroMeasurementVariance` | measured | pending | 3.0 m² |
| `kGpsMeasurementVariance` | datasheet | retained | 25 m² | 25 m² |
| `kAccelNoiseVariance` | Section 3.4 | retained | 1 (m/s²)² | 1 (m/s²)² |
| `kInitialVelocityVariance` | Section 3.5 | chosen by replay | pending | 1 (m/s)² |

## 7. Comparison

Pending: measured sigma, the simulator's sigma before the change, and the datasheet figure per channel (the analysis prints this table).

## 8. Consequences and open items

- Before/after the constants change, on file 000 replayed through the same build: fused altitude sigma, vertical speed sigma, the startup transient's peak and settle time, one row per initial-velocity-variance candidate. Pending.
- The gyro-bias rule of Section 3.6 applied to the measured value. Pending.
- The stuck-value margin per channel against the detector's 10. Pending.
- What this capture cannot measure: the process noise, scale factors, the accelerometer's offsets apart from tilt, the IMU's DLPF correlation in the simulator, GPS.
