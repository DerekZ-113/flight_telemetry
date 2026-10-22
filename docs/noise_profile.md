# Flight Telemetry System — Sensor Noise Profile

> **Document ID:** FTS-NP-001
> **Version:** 0.2.0
> **Status:** Draft (the method of 0.1.0 and the results of the stationary capture of 2026-10-21; rules amended after the data are marked where they stand, dated)
> **Last Updated:** 2026-10-21
> **Related:** FTS-SRD-001 (REQ-PROC-004, REQ-SENS-006), FTS-DD-001 (Sections 5, 6 and 12), FTS-TP-001 (TC-015), FTS-TM-001, FTS-VR-001 (Entries 8 and 9)

This document is the measurement REQ-PROC-004 names. It states, before the data exists, what is measured on a stationary capture of the real sensors, how each number is estimated, what the datasheets lead us to expect, which Kalman constants the capture can set and which it cannot, and the rules that decide the open questions once the numbers are in. Version 0.1.0 is the method; 0.2.0 adds the results. Writing the method first is the test plan's own discipline (a card before its execution) applied to a measurement, so that the analysis cannot drift toward a wanted answer.

In 0.2.0 the method's text stands as written. Where the data showed a rule's premise wrong, the rule is amended after it, dated 2026-10-21 and marked as written after the data, with the reason and with what the rule as written gave, so both stay visible (Sections 2.1, 2.2, 3.2, 3.3 and 3.5). The capture of record is the one of 2026-10-21 on GY-521 unit 3; a capture of 2026-10-20 on unit 1, a nonconforming module (FTS-VR-001 Entry 8 §8.8), is reported beside it as a second barometer capture.

## 1. Purpose and scope

The Kalman filter (FTS-DD-001 Section 5) needs the variance of a barometric altitude measurement, and the complementary filter's behaviour on hardware depends on the gyroscope's bias. Until this document carries measured values, the filter's constants (the `processing` keys of `config/telemetry_config.yaml` and the equal defaults of `ProcessorConfig`, REQ-CFG-001) are placeholders chosen from the simulator's own noise. The capture also sets the simulator's sigmas (`simulation.noise` in the file and the `SimulationNoise` defaults, REQ-SENS-006), so the simulator becomes the sensor's twin, and gives the fault detector's stuck-value rule its margin. Since 0.2.0 the measured values stand in both places (Section 6).

**Measured here:** per channel, on fresh samples only, the mean, the population sigma, the Allan deviation over averaging times from one sample up, the lag-1 autocorrelation, the quantization step, and the longest run of identical samples; the gyroscope bias per axis and the tilt it predicts; the accelerometer offsets; the barometric altitude noise in metres two ways; the pipeline's own outputs after a settle window and the startup transient inside it; the scheduler's jitter over the run.

**Not measured here, and why:** scale-factor errors (a stationary board gives the gyro a zero input and the accelerometer one known input, 1 g on one axis; scale needs a known rotation or a flip test); the accelerometer's x and y offsets separately from the board's tilt (the two are the same number on a still board; a six-position tumble separates them); GPS noise (the receiver exists since 2026-10-14, but this capture runs with it off; a capture with it fixing is its own); the process noise (Section 3.4); anything about the sensors in motion.

## 2. Method

### 2.1 Setup and settings

| Item | Value |
|---|---|
| Hardware | Raspberry Pi 3 Model B+; barometer at I2C 0x76 (driver `Bmp280`, chip ID 0x58 or 0x60); MPU6050 at 0x68. Read on 2026-10-20: chip ID 0x60, a BME280, whose pressure and temperature registers and compensation are the BMP280's (FTS-DD-001 Section 12, FTS-VR-001 Entry 8 §8.6); WHO_AM_I 0x68 on a GY-521 module, unit 3 of a pack of three for the capture of record |
| Barometer settings | temperature ×1, pressure ×4 oversampling, normal mode, 0.5 ms standby, IIR filter off (`src/drivers/bmp280.cpp`); about 12 ms per conversion, so a 50 Hz read never repeats one. The BME280 datasheet (BST-BME280-DS001-24 §9.1) gives 11.5 ms typical and 13.325 ms at most at these settings, a new conversion at least every 13.8 ms with the standby |
| IMU settings | 100 Hz internal sample rate, DLPF setting 4 (about 20 Hz), ±250 deg/s, ±2 g (`src/drivers/mpu6050.cpp`) |
| Loop | 50 Hz (`telemetry --live --cycles 45000`), frames stamped with the actual read time |
| Duration | 15 minutes, 45,000 frames, five rotated log files of 9,986 frames and one of 5,056. Since format version 3 (2026-10-15, 121-byte records) the same run rotates into six: five of 8,665 frames and one of 1,675 |
| Conditions | board flat on a solid surface and untouched, the Pi otherwise idle; room temperature and a METAR sea-level pressure noted at start and end. **Amended 2026-10-21, after the data:** no METAR is recorded. Comparing a capture with a METAR needs the site's elevation, which stays out of the repository, so the barometer's checks are its noise and its drift, not its absolute value; the room temperature is noted at the start |
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
| Repeats | the count of consecutive samples that are bit-identical, against the count float32 chance predicts, N × step / (2σ√π): the chip read faster than it converts would repeat conversions far beyond chance. **Amended 2026-10-21, after the data (Section 3.2):** for the pressure, the chance count is taken on the conversion's own step, not float32's. With the IIR filter off the BME280's pressure has 16 + (osrs_p − 1) bits, 18 at ×4 (BST-BME280-DS001-24 §3.4.2), a step of 0.66 Pa (the BMP280's figure for 18 bits, BST-BMP280-DS001-26 §3.3.1 Table 4, the register-compatible chip); and the compensated pressure repeats bit for bit only when the temperature conversion repeats too, since both enter the compensation. The chance count is therefore the temperature's repeats × q/(2σ√π), with q = 0.66 Pa and σ the pressure's ADEV(τ₀) in pascals |
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

The estimators are verified before they meet real data (TC-015 steps 1 through 8, `tests/integration/test_noise_profile.py`): synthetic logs of 45,000 frames with planted sigmas, a planted bias, a planted drift, planted identical runs and an LSB grid, with tolerances stated as multiples of the standard error; then the binary's own seed-42 log, whose true sigmas are the simulator's (the `simulation.noise` keys of the configuration, equal to the `SimulationNoise` defaults). The simulator's barometric variance derived this way was 2.774 m² = (0.2 hPa × 8.327 m/hPa)² before 0.2.0, and the placeholder of 3 m² was that number plus sampling luck; with the measured sigmas it is 0.0551 m² = (0.0282 hPa × 8.327 m/hPa)², the measured variance by construction (Section 6), and steps 7 and 8 assert against that.

### 2.4 Figures

Four PNGs under `docs/img/`: histograms with bins one LSB wide where the LSB is known; the time series over the run; the Allan deviation on log axes with the confidence band and a τ^−½ reference; the pipeline's outputs with the accelerometer-only tilt and the bias-predicted tilt drawn across pitch and roll. For the capture of record: `noise_profile_2026-10-21-unit3_histograms.png`, `_timeseries.png`, `_allan.png` and `_outputs.png`.

## 3. Expectations and decision rules, written before the data

### 3.1 What the datasheets lead us to expect

| Channel | Expected at the driver's settings | Source |
|---|---|---|
| Pressure | a few pascals RMS, 0.02 to 0.04 hPa (×4 oversampling, IIR off); barometric altitude 0.2 to 0.35 m; variance 0.04 to 0.12 m², 25 to 75 times below the 3 m² placeholder | BMP280 datasheet noise table at ×4 (**confirm the figure when the results are written**). Confirmed 2026-10-21 on the chip's own datasheet: 2.1 Pa RMS, typical at 25 °C, at ×4 pressure and ×1 temperature oversampling with the filter off (BST-BME280-DS001-24 §3.6, Table 12); 0.021 hPa, inside the band |
| Gyroscope | about 0.022 deg/s per axis: rate noise density 0.005 deg/s/√Hz × √(20 Hz); bias of a few deg/s, moving in the first minutes as the die warms | MPU-6050 PS-MPU-6000A-00 §6.1: 0.005 deg/s/√Hz, total RMS 0.05 deg/s at the 100 Hz DLPF, initial zero-rate output ±20 deg/s, ±20 deg/s over −40 to 85 °C |
| Accelerometer | about 0.0175 m/s² per axis: 400 µg/√Hz × √(20 Hz); x and y means up to ±0.5 m/s² (±50 mg initial offset), indistinguishable from 2.9° of tilt; z up to ±0.8 m/s² from 9.81 (±80 mg) | §6.2: 400 µg/√Hz, initial calibration tolerance ±50 mg (x, y), ±80 mg (z) |
| Temperature | a rise of one or two degrees over the run, mostly early; 0.01 °C steps | self-heating; the compensated output's resolution |
| Correlation | barometer: detrended ρ₁ within sampling error of zero (independent conversions), so σ/ADEV(τ₀) near 1 unless the weather adds a trend. IMU: detrended ρ₁ around 0.1 to 0.3 and σ/ADEV(τ₀) around 1.1 to 1.2 (ADEV/σ ≈ 0.85), because the 20 Hz DLPF correlates samples 20 ms apart. That is the filter, not a drift finding, and no IMU variance feeds the Kalman filter | the driver settings |
| Stuck runs | under 3 on every tracked channel; the stuck rule's margin is the gap to 10 | FAULT-005, 006, 007 |
| Timing | 0 missed cycles over 45,000 | TC-009 step 7 |

### 3.2 The barometric measurement variance

R is the per-sample scatter the filter sees at each update, which the Kalman gain is built for. The plain sigma over 15 minutes includes the weather: a 0.3 hPa drift over the run is 2.5 m, more than the sensor's noise, and would inflate R several-fold, and the trend is something the filter cannot see either way. Two estimators remove it: ADEV(τ₀)² (half the first-difference variance, immune to any slow drift, but equal to σ²(1 − ρ₁), so it understates the per-sample variance when consecutive samples are positively correlated and would leave the filter overconfident) and the variance of the linearly detrended series (immune to a linear drift only, but it keeps correlated noise whole). A trend and positive correlation both raise σ above ADEV(τ₀), so the ratio cannot tell them apart; ρ₁ of the detrended series can. **Rule, decided by ρ₁ of the linearly detrended `baro_altitude_m` against 3/√N (0.014 on the capture):** within that of zero, the samples are white, the two estimators agree, and R := ADEV(τ₀)², the one that needs no trend model; significantly above it, the samples are correlated and R := the detrended variance, with ρ₁ and the understatement ADEV(τ₀)² would have carried named in the results; significantly below it, the samples are anti-correlated, which no mechanism here produces, and the analysis stops until the cause is found. Two further checks: if the pressure's repeats exceed five times the float32 chance count, the chip is being read faster than it converts (duplicate conversions), and the analysis stops; if σ²/ADEV(τ₀)² on the barometer exceeds 2, the results name the low-frequency source (the detrended ρ₁ says whether it is the weather's trend or correlated fluctuation such as the room's pressure) and the day's METAR trend. The plain sigma, the detrended sigma, the ratio, both ρ₁ values, the trend per hour, the first-minute and last-minute means, and the Allan curve are all reported so the choice is visible. The script applies the rule and prints which estimator it took (`r_baro_rule` in the JSON).

**Amended 2026-10-21, after the data, by Derek's decision.** On the capture of record the detrended ρ₁ is 0.867, so the rule as written took the detrended variance, 0.4152 m², 7.5 times ADEV(τ₀)² (0.05513 m²). The correlated branch assumed the correlation lay in the sensor's per-sample noise. It does not: with the IIR filter off each conversion is independent by the chip's design, and the Allan deviation falls as τ^−½ from one sample to 0.1 s (0.0282 hPa to 0.0127, against 0.0126 for white noise) and turns flat and then upward above it (Section 5.2). The per-sample noise is white; the correlated part is slower, a fluctuation of the air pressure in the room that a still board measures correctly, of the kind the datasheet's own filter exists to smooth (BST-BME280-DS001-24 §3.4.4). REQ-PROC-004 derives R from the sensor's noise, and a real change of pressure is not the sensor's error: the filter must follow it as it follows a climb, which is what a barometric altimeter does. The rule as amended: while the conversions are independent (the IIR filter off, as the driver sets it) and the Allan deviation falls as τ^−½ within 10% over the shortest averaging times, R := ADEV(τ₀)², the white part, which Section 3.3's table had named alone, whatever the detrended ρ₁, and the detrended variance is reported beside it; otherwise the rule as written stands. On the capture the curve's ratio from one sample to five is within 1% of white. What the rule as written would have cost is in Section 8: the filter would see 90% of a 1 m/s climb after 2.12 s instead of 1.28 s. The two further checks: the repeats check as written fired (924 repeats against float32's 10) and is amended in Section 2.2, its outcome in Section 5.2; σ²/ADEV(τ₀)² is 7.5, and its source is correlated fluctuation, the detrended ρ₁ equal to the raw, with no METAR trend recorded (Section 2.1). The analysis script applies the rule as written until its update, so the capture's JSON names the detrended variance (Section 8).

### 3.3 Which constants the capture sets

| Constant | Status | Rule |
|---|---|---|
| `kBaroMeasurementVariance` | **measured** | ADEV(τ₀)² of `baro_altitude_m`, in m² |
| `kInitialAltitudeVariance` | **measured** | equals the measurement variance of the source the filter is born from (REQ-PROC-003, 0.12.0): the barometric variance on the bench, where the barometer reads on the first cycle; the GPS variance, the datasheet's 25 m² until a capture with the receiver fixing is analysed, when the barometer has not read, as in ADS-B mode, where the GPS channel carries an aircraft's GNSS height and the same figure stands until a capture measures it |
| `kGpsMeasurementVariance` | **retained**, 25 m² | no GPS capture analysed yet (the receiver landed 2026-10-14, REQ-SENS-005); the receiver's vertical error is about 5 m |
| `kAccelNoiseVariance` | **retained**, 1 (m/s²)² | Section 3.4 |
| `kInitialVelocityVariance` | **chosen by replay** | Section 3.5 |

The simulator's four sigmas (pressure, temperature, accelerometer, gyroscope) take the measured detrended sigmas, the per-sample scatter with the slow drift removed (a gyro bias that moves as the die warms is drift, not noise), so the simulator is the sensor's twin; its GPS sigma stays 2 m. The simulator is a white per-sample generator and cannot model the IMU's DLPF correlation; noted, out of scope.

**Amended 2026-10-21, after the data, with Section 3.2:** the barometer's two sigmas take ADEV(τ₀), the white part, because their detrended sigmas carry the room's pressure and the die's temperature, which are not per-sample scatter (pressure 0.0282 hPa against a detrended 0.0773; temperature 0.0114 °C against 0.1206), and so the simulator's barometric variance equals R. The IMU's take the detrended sigmas as written, one value per sensor because the simulator draws all three axes from one sigma: the RMS over the three axes, 0.0130 m/s² and 0.0254 deg/s.

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

**Amended 2026-10-21, after the replay, by Derek's decision.** At the measured R no candidate meets the bound as written: the peaks are 0.870, 0.290 and 0.292 m/s for 1.0, 0.1 and 0.01 (Section 8). The bound was written for the prior's transient, the paragraph above, and the two smaller candidates' peaks are not theirs: the same excursion at the same frame, 2.56 s, for both, which the filter at R = 3 m² shows there too (0.185 m/s), where the room's pressure moved. The prior's transient is a candidate's peak in excess of the smallest candidate's, whose prior is the tightest of the three: 0.58 m/s for 1.0, none for 0.1. The rule is read on that excess: the largest candidate whose peak exceeds the smallest candidate's by less than 0.2 m/s, with its 90% response to a 1 m/s velocity present at birth under 2 s. That is 0.1 (m/s)², at 0.96 s.

### 3.6 Gyroscope bias

Nothing is subtracted in this revision. The bias per axis, its drift over the run, the tilt it predicts (0.98 × b), and the measured tilt that confirms the formula are recorded here and in FTS-DD-001 Section 6. **Rule:** if 0.98 × |b| exceeds 1° on either axis, a requirement for subtracting a configured per-axis bias is proposed as a follow-up (text: "The attitude filter shall subtract a configured per-axis gyroscope bias from each gyroscope reading before integration; the bias shall be the stationary mean recorded in FTS-NP-001 and shall be configurable under REQ-CFG-001."), conditional on a second capture on another day agreeing within 0.2 deg/s, because the datasheet's ±20 deg/s part-to-part and over-temperature figures mean a constant measured once can be wrong by its own size later. Below 1° the accelerometer term already holds the error. Estimating the bias at startup is not planned: it assumes a still vehicle at boot and needs its own fault mode. The lasting fix is a bias state in the filter.

## 4. Provenance

From the run's metadata file written on the Pi (`telemetry_capture_meta.txt`: the commit, the platform, the chip IDs, the exit lines and the SHA-256 of every file, verified on the Mac with `shasum -c`) and the JSON the analysis writes, which carries each input file's SHA-256 and the command line. No location is recorded.

| Item | Capture of record | Second capture |
|---|---|---|
| Capture (local, PDT) | 2026-10-21, 16:35 to 16:50 | 2026-10-20, 22:32 to 22:47 |
| Commit the Pi ran | `02e36bc` (the port fix, after the pitch-rate fix of `63a3849`) | `c510034` (`main`), before both fixes: its `gyro_y` carries the chip's sign, positive nose down (FTS-VR-001 Entry 8 §8.9) |
| Board, OS, kernel, GCC | Raspberry Pi 3 Model B+, Debian GNU/Linux 13 (trixie), kernel `6.18.50+rpt-rpi-v8`, GCC 14.2.0 (Debian 14.2.0-19), the empty build type | the same |
| Chip IDs and addresses | 0x76: 0x60 (BME280); 0x68: WHO_AM_I 0x68 (MPU-6050) | the same chips at the same addresses |
| IMU module | GY-521 unit 3 (silkscreen HW-123), conforming | GY-521 unit 1, nonconforming: z offset +0.51 g (FTS-VR-001 Entry 8 §8.8) |
| Orientation and surface | flat and untouched; the accelerometer's mean reads 2.7° nose up and 0.6° right, tilt and offset together (Section 1) | flat, shimmed level, untouched |
| Room temperature | 71.9 °F (22.2 °C) at the start | 71.9 °F (22.2 °C) at the start, on the thermostat |
| The Pi | `throttled=0x0`; SoC 32.2 °C after the run | `throttled=0x0`; SoC 38.6 °C after the run |
| METAR | none (Section 2.1, as amended) | none |
| Command | `./build/telemetry --live --cycles 45000` | the same |
| Duration, frames, missed cycles | 899.98 s, 45,000 frames at 20 ms, 0 missed; every computed field NORMAL on 45,000 of 45,000 from frame 0; voter lanes 1 2 3, no majority on 0 frames, 0 voter events | the same, 0 missed, NORMAL throughout, no majority on 0 |
| Fault events | none: the GPS disabled, so NOT_AVAILABLE from the first frame with no event (REQ-FAULT-007) | none |
| Files and SHA-256 | `captures/noise-2026-10-21-unit3/`: `telemetry_000.bin` to `telemetry_005.bin` (five of 1,048,481 bytes, one of 202,691), `telemetry_jitter.csv`, `telemetry_config.yaml` (the configuration record), `telemetry_capture_meta.txt` with the eight sums, and the analysis's `noise.json` | not committed: a capture is committed when it sets constants (`.gitignore`), and this one sets none; its files, its metadata with the eight sums and the analysis's JSON stay with the bench's records |
| Analysis command and JSON | `.venv/bin/python -m analysis.noise_profile captures/noise-2026-10-21-unit3 --jitter captures/noise-2026-10-21-unit3/telemetry_jitter.csv --figures docs/img --json captures/noise-2026-10-21-unit3/noise.json --label 2026-10-21-unit3` | the same on that capture's directory, label `2026-10-20-unit1`, no figures |

## 5. Results

### 5.1 Every channel, the capture of record

45,000 fresh samples on every barometer and IMU channel, 0 on the GPS; timestamps exactly 20 ms apart; 0 torn bytes. Sigmas are population values; the trend is the least-squares slope.

| Channel | Mean | Sigma | Detrended sigma | ADEV(τ₀) | σ/ADEV | ρ₁ detrended | Trend per hour | Step | Longest run |
|---|---|---|---|---|---|---|---|---|---|
| pressure_hpa | 1011.49 | 0.07734 | 0.07729 | 0.02816 | 2.75 | 0.867 | −0.039 | 6.1e-5 | 4 |
| temperature_c | 22.351 | 0.1524 | 0.1206 | 0.0114 | 13.4 | 0.991 | −1.29 | 0.01 | 17 (not tracked) |
| baro_altitude_m | 14.658 | 0.6448 | 0.6444 | 0.2348 | 2.75 | 0.867 | +0.32 | 0.00264 | 4 |
| accel_x (m/s²) | 0.4608 | 0.01074 | 0.01073 | 0.01018 | 1.06 | 0.101 | −0.006 | 0.0012 | 4 |
| accel_y | 0.1056 | 0.01021 | 0.01020 | 0.00973 | 1.05 | 0.091 | +0.006 | 0.0012 | 4 |
| accel_z | 9.7085 | 0.01693 | 0.01693 | 0.01627 | 1.04 | 0.076 | −0.006 | 0.0012 | 4 |
| gyro_x (deg/s) | −1.5773 | 0.02452 | 0.02451 | 0.02261 | 1.08 | 0.149 | +0.012 | 0.00763 | 7 |
| gyro_y | −0.0569 | 0.02700 | 0.02699 | 0.02480 | 1.09 | 0.156 | −0.009 | 0.00763 | 6 |
| gyro_z | −1.5115 | 0.02480 | 0.02474 | 0.02273 | 1.09 | 0.156 | −0.025 | 0.00763 | 5 |

The Allan deviation at selected averaging times (the confidence about ±1/√(N/m): under 1% at 0.1 s, 3% at 1 s, 10% at 10 s, 32% at 100 s):

| Channel | 0.02 s | 0.1 s | 1 s | 10 s | 100 s |
|---|---|---|---|---|---|
| pressure_hpa | 0.0282 | 0.0127 | 0.00938 | 0.0147 | 0.0376 |
| baro_altitude_m | 0.235 | 0.106 | 0.0782 | 0.122 | 0.313 |
| temperature_c | 0.0114 | 0.00383 | 0.00206 | 0.00816 | 0.0602 |
| accel_x / y / z | 0.0102 / 0.0097 / 0.0163 | 0.0045 / 0.0043 / 0.0072 | 0.0017 / 0.0016 / 0.0028 | 0.00059 / 0.00048 / 0.00084 | 0.00029 / 0.00027 / 0.00067 |
| gyro_x / y / z | 0.0226 / 0.0248 / 0.0227 | 0.0107 / 0.0116 / 0.0110 | 0.0042 / 0.0046 / 0.0042 | 0.0017 / 0.0016 / 0.0015 | 0.0021 / 0.0033 / 0.0025 |

### 5.2 The barometer: the white part, the room's pressure, the repeats

**The white part.** The pressure's one-sample Allan deviation is 0.0282 hPa, 2.82 Pa, 0.2348 m of altitude through the slope of −8.337 m/hPa at the mean pressure, the same 0.2348 m read directly on `baro_altitude_m`. Against the datasheet's 2.1 Pa typical at these settings (Table 12) it is 1.34 times; against Section 3.1's band of 0.02 to 0.04 hPa it is inside. From 0.02 s to 0.1 s the curve falls as τ^−½ (0.0282 to 0.0127 hPa, white noise predicting 0.0126): the conversions are independent and their noise white.

**The room's pressure.** Above 0.1 s the curve stops falling: 0.0094 hPa at 1 s, where white noise alone would be 0.0040, and rising from there (0.0147 at 10 s, 0.0376 at 100 s). The plain and detrended sigmas agree (0.0773 hPa), the linear trend is small (−0.039 hPa an hour) and ρ₁ is 0.867 raw and detrended, so the slow part is not a trend but a correlated fluctuation of about 0.072 hPa RMS, √(0.0773² − 0.0282²), 0.60 m of altitude: σ²/ADEV(τ₀)² is 7.5. Three things say it is the air, not the chip: the white part is what the datasheet describes for independent conversions; the same barometer board, captured the night before at another site (Section 5.4), shows the same white part within 10% (2.57 Pa) and a quarter of the correlated part once its trend is removed (0.017 hPa); and the chip's own filter is there to smooth short-term pressure changes such as a door's (§3.4.4). This is what Section 3.2's amendment rests on.

**The repeats.** The pressure repeated bit for bit on 924 of 44,999 consecutive pairs, against 10 by the float32 chance the check was written with, so the check as written fired. Every one of the 924 fell on a pair whose temperature repeated as well, and among the 14,862 such pairs the pressure repeated on 6.2%. Chance on the 18-bit step predicts 6.6%: q/(2σ√π) with q = 0.66 Pa and σ = 2.816 Pa, an expected count of 982 against the 924 seen. The capture's own step agrees: at an unchanged temperature the pressure differences fall on multiples of about 0.685 Pa (0.68 to 0.69, 1.37, 2.05 and 2.73 Pa are the commonest gaps). A read faster than the conversion is ruled out by the timing: a conversion every 13.8 ms at most, against a read every 20 ms (Section 2.1); and a duplicate read would add repeats above chance, where the rate is a little below it, as expected when part of the temperature repeats are equal outputs of unequal conversions. The second capture gives the same picture: 971 repeats, all at a repeated temperature, 6.3% of those pairs against 7.3%. Under the amended check (Section 2.2) both captures pass; the check as written fails both.

### 5.3 The IMU

**Noise.** The detrended sigmas, 0.0102 to 0.0169 m/s² and 0.0245 to 0.0270 deg/s, sit near the datasheet's 0.0175 and 0.0224 (Section 7), the accelerometer's x and y below it and the gyroscope 10 to 20% above. σ/ADEV(τ₀) is 1.04 to 1.09 and the detrended ρ₁ 0.08 to 0.16, the 20 Hz DLPF's correlation, a little below Section 3.1's 0.1 to 0.3. The accelerometer's step is 0.0012 m/s², two of the driver's LSBs at 9.81/16,384: every accelerometer count on both units is even, while the gyroscope uses every count of its 1/131 deg/s.

**The gyroscope's bias** is −1.577, −0.057 and −1.512 deg/s on x, y and z, inside the datasheet's ±20 deg/s, and did not move over the run (first and last minute within 0.012 deg/s; under 0.025 deg/s an hour), the die already warm. Through the complementary filter it predicts a steady tilt of 0.98 × b: −1.546° of roll and −0.056° of pitch. Measured as the mean filter angle less the accelerometer-only angle, it is −1.55° and −0.056°: the formula holds to the hundredth of a degree. The same evening's tilt rerun on unit 3 showed it again at rest: −1.550° of roll against 0.98 × −1.583 = −1.551°, the gap after the seed closing with a time constant of about 1.1 s, fitted over the first two seconds, against the filter's 0.98 s (FTS-VR-001 Entry 8 §8.10). Section 3.6's rule applied to it is Section 8's.

**The accelerometer**, unit 3 beside unit 1's capture:

| Axis | Unit 3: mean (m/s²) | Unit 3: detrended sigma | Unit 3: ADEV(τ₀) | Unit 1: mean | Unit 1: detrended sigma | Unit 1: ADEV(τ₀) | Datasheet sigma |
|---|---|---|---|---|---|---|---|
| x | 0.461 | 0.0107 | 0.0102 | −0.557 | 0.0128 | 0.0108 | 0.0175 |
| y | 0.106 | 0.0102 | 0.0097 | 0.199 | 0.0121 | 0.0100 | 0.0175 |
| z | 9.708 | 0.0169 | 0.0163 | 14.692 | 0.0180 | 0.0173 | 0.0175 |

Unit 3's mean vector is 9.720 m/s² long (0.991 g at the driver's 9.81). As a tilt it is 2.72° nose up and 0.62° right; as offsets on a level board it is 47, 10.8 and −10.4 mg (z against 1 g), inside the datasheet's ±50 mg on x and y and ±80 mg on z. The two cannot be told apart on a still board (Section 1). **Unit 1's z offset:** its z mean, 14.69 m/s², is the unit's offset, about +4.9 m/s² (0.50 g), not noise, and enters no derived value; its z sigma may be up to about 8% high (9% with a gain on every axis), from a gain the edge test could not rule out (FTS-VR-001 Entry 8 §8.8). Unit 1's x and y means are its tilt and offsets together, as unit 3's.

### 5.4 The second capture, unit 1, 2026-10-20

The same barometer board, another site and day: pressure white 0.0257 hPa (2.57 Pa), sigma 0.0442, detrended 0.0308, raw ρ₁ 0.662 and detrended 0.304, a trend of +0.44 hPa an hour; ADEV(τ₀)² of the barometric altitude 0.0459 m² and the detrended variance 0.0659 m². Unit 1's gyroscope: detrended sigmas 0.0263, 0.0362 and 0.0249 deg/s, bias 0.915, −0.750 and −0.940 deg/s (y in the chip's sign, Section 4); its accelerometer is Section 5.3's table. Longest runs: pressure 4, accelerometer 4, 4 and 3, gyroscope 5, 6 and 5. Jitter 60.2 / 81.2 / 187.6 µs, 0 missed.

### 5.5 The pipeline's outputs and the timing, as recorded

The capture ran with the constants before this revision (R = 3 m², P0_vel = 1 (m/s)²). After the 10 s settle window: fused altitude sigma 0.588 m, the filter following the room's pressure; vertical speed sigma 0.053 m/s; pitch 2.661° with sigma 0.011°, roll −0.923° with sigma 0.0098°. Inside the window the vertical speed peaked at 0.185 m/s at 2.56 s and last exceeded 0.05 m/s at 7.44 s. The scheduler: 45,000 cycles, jitter 59.9 / 81.4 / 178.0 µs (minimum, mean, maximum), 111.3 µs at the 99th percentile, 0 missed. The constants' effect on these is Section 8's replay.

### 5.6 The simulated worked example

The analysis on a 30-second simulated run (1500 frames, seed 42, this Mac, 2026-10-12), kept from 0.1.0 as the oracle's reference: every sigma is the simulator's value of the day within sampling error, the two white-noise estimators agree, and the derived barometric variance is the simulator's own.

| Channel | n | Mean | Sigma | ADEV(τ₀) | σ/ADEV | ρ₁ | Simulator sigma |
|---|---|---|---|---|---|---|---|
| pressure_hpa | 1500 | 1013.00 | 0.2023 | 0.1999 | 1.01 | 0.024 | 0.2 |
| temperature_c | 1500 | 21.00 | 0.0998 | 0.0989 | 1.01 | 0.018 | 0.1 |
| baro_altitude_m | 1500 | 2.089 | 1.684 | 1.664 | 1.01 | 0.024 | 1.665 |
| accel_x / y / z | 1500 | 0.001 / 0.000 / 9.808 | 0.0494 / 0.0498 / 0.0501 | 0.0487 / 0.0493 / 0.0498 | 1.01 | 0.01 to 0.03 | 0.05 |
| gyro_x / y / z | 1500 | −0.001 / 0.002 / −0.004 | 0.1009 / 0.1021 / 0.0987 | 0.102 / 0.100 / 0.099 | 0.99 to 1.02 | −0.02 to 0.04 | 0.1 |

Derived from it: R_baro 2.77 m² by the ADEV(τ₀)² branch of the rule (detrended ρ₁ 0.024 against a threshold of 0.077; the simulator's 2.774), the ISA slope −8.327 m/hPa, the barometric sigma 1.664 m both ways, gyro bias under 0.005 deg/s, longest identical run 1 on every tracked channel, the fused altitude sigma 0.20 m after 5 s against 1.68 m raw, and the startup transient's peak 0.78 m/s at 0.62 s (Section 3.5). The rule's other branch is exercised by a synthetic log of AR(1) noise (ρ₁ = 0.3) plus 1.8 m of drift: the detrended variance recovers the true per-sample variance within 3% where ADEV(τ₀)² understates it by 30%.

With the measured sigmas, the binary's seed-42 session (500 frames, the default run) reads: pressure 0.0294 hPa against the simulator's 0.0282, temperature 0.0112 °C against 0.0114, accel_x 0.0125 m/s² against 0.0130, gyro_x 0.0256 deg/s against 0.0254; R 0.0599 m² by the ADEV(τ₀)² branch (detrended ρ₁ −0.002), 9% above the simulator's 0.0551; longest run 2; the fused altitude scattering 0.21 times the barometric. TC-015 steps 7 and 8 assert against these values (FTS-VR-001 Entry 9 §9.8).

## 6. Derived constants

| Constant (key) | Formula | Status | Value | Placeholder it replaces |
|---|---|---|---|---|
| `kBaroMeasurementVariance` (`processing.baro_measurement_variance_m2`) | ADEV(τ₀)² of `baro_altitude_m`, Section 3.2 as amended | measured | 0.0551 m² (0.05513) | 3.0 m² |
| `kInitialAltitudeVariance` (no key: derived) | = the birth source's measurement variance, the barometer's on the bench (REQ-PROC-003) | measured | 0.0551 m² | 3.0 m² |
| `kGpsMeasurementVariance` (`processing.gps_measurement_variance_m2`) | datasheet | retained | 25 m² | 25 m² |
| `kAccelNoiseVariance` (`processing.accel_noise_variance`) | Section 3.4 | retained | 1 (m/s²)² | 1 (m/s²)² |
| `kInitialVelocityVariance` (`processing.initial_velocity_variance`) | Section 3.5 as amended | chosen by replay | 0.1 (m/s)² | 1 (m/s)² |
| `simulation.noise.pressure_sigma_hpa` | ADEV(τ₀) of the pressure, Section 3.3 as amended | measured | 0.0282 hPa | 0.2 hPa |
| `simulation.noise.temperature_sigma_c` | ADEV(τ₀) of the temperature | measured | 0.0114 °C | 0.1 °C |
| `simulation.noise.accel_sigma_mps2` | RMS over the axes of the detrended sigmas | measured | 0.0130 m/s² | 0.05 m/s² |
| `simulation.noise.gyro_sigma_dps` | RMS over the axes of the detrended sigmas | measured | 0.0254 deg/s | 0.1 deg/s |

Each lands in `config/telemetry_config.yaml` and the equal default of `ProcessorConfig` or `SimulationNoise` in one commit, `ConfigTest.ShippedFileEqualsDefaults` holding the two equal. The GPS sigmas stay the simulator's (2 m and about 2 m of position).

## 7. Comparison

| Channel | Measured, unit 3: white / detrended | The simulator before 0.2.0 | The simulator since | Datasheet |
|---|---|---|---|---|
| pressure | 0.0282 / 0.0773 hPa | 0.2 hPa | 0.0282 hPa | 0.021 hPa typical (BME280 Table 12) |
| barometric altitude | 0.235 / 0.644 m | 1.665 m | 0.235 m | 0.175 m, the datasheet's 2.1 Pa through the slope |
| temperature | 0.0114 / 0.121 °C | 0.1 °C | 0.0114 °C | 0.005 °C at ×1 (Table 14), reported at 0.01 °C |
| accel x / y / z | 0.0107 / 0.0102 / 0.0169 m/s² (detrended) | 0.05 m/s² | 0.0130 m/s² | 0.0175 m/s² (400 µg/√Hz × √20 Hz) |
| gyro x / y / z | 0.0245 / 0.0270 / 0.0247 deg/s (detrended) | 0.1 deg/s | 0.0254 deg/s | 0.0224 deg/s (0.005 deg/s/√Hz × √20 Hz) |

The simulator was pessimistic by a factor of 4 to 9 on every sensor channel; since 0.2.0 it draws the sensor's own per-sample scatter, white, so its replayed outputs are smoother than a real board's, which carries the room's pressure and the DLPF's correlation.

## 8. Consequences and open items

**The constants, before and after.** File 000 of the capture of record (8,665 frames, the first 173 s) replayed on the Mac through each barometric variance (the placeholder, the value of record, the rule as written) and each initial velocity variance; the steady values and the climb from the filter's own equations at dt = 20 ms and σ_a² = 1:

| R (m²) | P0_vel ((m/s)²) | 90% of 1 m/s present at birth | Steady σ_h / σ_v | 90% of a 1 m/s climb | Replay: peak \|v\| | Replay: fused sigma / v sigma after 10 s |
|---|---|---|---|---|---|---|
| 3 (the placeholder) | 1.0 | 1.82 s | 0.253 m / 0.192 m/s | 3.50 s | 0.185 m/s at 2.56 s | 0.310 m / 0.062 m/s |
| 3 | 0.1 | 3.18 s | | | 0.151 m/s at 2.56 s | |
| 3 | 0.01 | 3.78 s | | | 0.115 m/s at 3.04 s | |
| **0.0551** (of record) | 1.0 | 0.48 s | 0.056 m / 0.116 m/s | 1.28 s | 0.870 m/s at 0.24 s | 0.308 m / 0.122 m/s |
| **0.0551** | **0.1** (of record) | 0.96 s | | | 0.290 m/s at 2.56 s | |
| 0.0551 | 0.01 | 1.34 s | | | 0.292 m/s at 2.56 s | |
| 0.4152 (the rule as written) | 1.0 | 0.94 s | 0.120 m / 0.150 m/s | 2.12 s | 0.245 m/s at 0.24 s | 0.308 m / 0.085 m/s |
| 0.4152 | 0.1 | 1.78 s | | | 0.215 m/s at 2.56 s | |
| 0.4152 | 0.01 | 2.26 s | | | 0.213 m/s at 2.56 s | |

At the measured R the filter sees a climb 2.2 s sooner than at the placeholder and the prior's transient is gone at 0.1 (m/s)² (Section 3.5, as amended); the price is a vertical speed that follows the room's pressure, its sigma on a still board 0.12 m/s where the placeholder's sluggishness gave 0.06, and above 0.05 m/s until the 10 s window's end. The fused altitude's scatter, 0.31 m, is the room's pressure under every tuning.

**The stuck margins.** Longest runs on the capture of record: pressure 4, the accelerometer 4 on each axis, the gyroscope 7, 6 and 5, against the detector's 10 (FAULT-005 to FAULT-007), where Section 3.1 expected under 3. The gyroscope's sigma is only about three of its LSBs, so it repeats a value on about one pair in ten, and its DLPF lengthens the runs: on gyro_x the runs of at least 2, 3, 4, 5, 6 and 7 samples number 3,857, 375, 46, 8, 2 and 1, each count 0.10 to 0.5 of the one before. Extrapolated at the mean ratio, a run of ten on gyro_x comes about once in 240 hours at rest, and the ratio rises with length, so that is optimistic; a false STUCK would mark the IMU DEGRADED until five good reads. The margin is 3 on gyro_x and 6 on the barometer and accelerometer. Open: whether `fault.stuck_count` or the stuck rule wants review for the gyroscope, an input for the campaign sourced from the capture (FTS-DD-001 §12).

**The gyroscope-bias rule (Section 3.6).** 0.98 × |b| is 1.55° on roll (gyro_x −1.577 deg/s), above 1°, and 0.06° on pitch: the rule trips on roll. The proposal it calls for is a change of its own, conditional on a second capture of unit 3 on another day agreeing within 0.2 deg/s; unit 1's capture cannot serve, being another chip.

**The barometric variance across two captures.** 0.0551 m² on the night of record and 0.0459 m² the night before, the same board at two sites: the white part repeats within 10% in sigma, 20% in variance. The value of record is the capture of record's.

**The analysis script, until its update (its own commit, before the release).** It applies Section 3.2's rule as written, so the capture's `noise.json` names the detrended variance as R (`r_baro_rule` detrended_variance, `R_baro_m2` 0.4152) and prints the repeats warning on float32's chance; its comparison column "Simulator sigma today" and the placeholders it prints are the values before 0.2.0. This document and the configuration are the values of record; the script's chance model, its R rule as amended and those two columns are its update.

**What this capture cannot measure:** the process noise, scale factors, the accelerometer's offsets apart from tilt, the IMU's DLPF correlation in the simulator, GPS (off in this capture; a capture with the receiver fixing is its own).
