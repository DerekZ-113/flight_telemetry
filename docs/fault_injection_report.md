# Flight Telemetry System — Fault-Injection Campaign Report

> **Document ID:** FTS-FI-001
> **Version:** 0.1.0
> **Status:** Draft. Sections 1, 2, 4 and 5, the method and the predictions, were written and committed on 2026-10-18 before the campaign's code existed; Sections 3 and 6 to 8 are filled by the first run.
> **Last Updated:** 2026-10-18
> **Related:** FTS-SRD-001 (REQ-TEST-005, REQ-TEST-006, REQ-RED-007), FTS-FM-001 (the failure modes injected), FTS-DD-001 (Section 14, the campaign's design; Section 13, the voter), FTS-TP-001 (TC-035 to TC-037), FTS-VR-001 (the summary of each campaign of record)

This report measures how the flight telemetry system's fault detector and its three-lane voter behave under tens of thousands of randomized, seeded faults: how fast each check declares the fault it is built for, how often anything is declared on a channel with nothing wrong, how a channel recovers, where no check looks, and what the voter's persistence count buys and costs. Every number in it is measured against a prediction written down before the campaign ran. It is meant to be read on its own: Section 1 defines every term it uses, and nothing else in the repository is needed to follow it.

---

## 1. The system and its terms

The system produces one record, a *frame*, every 20 ms. Each frame carries the raw readings of three *channels*: a barometer (pressure and temperature), an inertial measurement unit or IMU (three accelerations and three rotation rates), and a GPS receiver, which delivers a fresh fix once a second and repeats the last one between fixes, marked not fresh. Each frame also carries five computed values, the barometric altitude, the pitch, the roll, the fused altitude and the vertical speed, each with a *validity*: NORMAL (a current estimate), FAILURE_WARNING (held or coasting, not to be trusted) or NO_COMPUTED_DATA (never existed).

```
 simulated     ──►  fault      ──►  fault detector  ──►  lane 1 ─┐
 sensors            injector         timeout, stuck,     lane 2 ─┼─►  voter  ──►  the frame
 (baro, IMU,        (sensor          range; each         lane 3 ─┘                emitted
  GPS at 1 Hz)       faults)         channel's status       ▲
                                                lane-fault injector (lane faults)
```

The *fault detector* judges each channel on every frame and gives it a status: NOMINAL, or DEGRADED once it *declares* a fault. It has three checks:

- a **communication timeout**: no successful reading for more than 500 ms on the barometer or the IMU, or 2,000 ms on the GPS;
- a **stuck reading**: ten identical readings in a row of the barometer's pressure or of any one IMU axis;
- a **range check**: a pressure outside 300 to 1,100 hPa, a temperature outside −40 to 85 °C, or an acceleration at either rail of the sensor's ±2 g scale.

A DEGRADED channel *recovers* to NOMINAL after five consecutive valid readings. Every declaration and every recovery is logged as an *event*.

The processing runs in three identical *lanes*, each with its own filters, all given the same checked frame. A *voter* compares their outputs bit for bit, emits the majority's, and sets bits in the frame's voter status: VOTED on every frame it produced, a lane's bit once that lane is excluded, NO_MAJORITY when no two lanes agree. A lane outside the majority on P consecutive cycles is excluded until restart; P is the *persistence count*, 3 in the shipped system. With no majority the voter holds its last voted values under FAILURE_WARNING.

**The terms of this report.**

- **Onset** and **removal**: the frames on which a fault is switched on and off, the operator's clock (when the cable is pulled). For the GPS, whose dropout changes nothing until the next fix is due, the *first suppressed fix* is also given.
- **Detection latency**: the time from onset to the first declaration on the faulted channel.
- **Missed detection**: no declaration on the faulted channel while the fault is on.
- **False alarm**: a declaration on a channel with nothing injected, counted on clean runs and on the untouched channels of every injected run.
- **Wrong-channel declaration**: a declaration on another channel while the fault is on.
- **Recovery time**: from removal to the channel's return to NOMINAL.
- **Isolation latency**: from a lane fault's onset to the lane's exclusion.
- **Healthy lane thrown out**: an excluded lane that was never altered.
- **No-majority episode**: one stretch of frames with no majority, from its first frame to the next frame that has one.
- **Wrong output**: a frame the voter marked VOTED, with a majority, whose computed values are not a healthy lane's.

---

## 2. Status

This is a **preview**, run against the simulator's noise model: Gaussian noise of fixed standard deviations on continuous floating-point readings, never quantized, never a failed read. The campaign of record reruns after the bench session measures the real sensors' noise and the measured constants land in the configuration, at the commit that holds them; Section 9 says what that rerun can change and what it cannot.

---

## 3. What was run

<!-- generated:run -->
*Not yet run.*
<!-- end generated -->

---

## 4. Method

### 4.1 A run

Each run builds the system's own objects in one process: the simulator, behind a fault injector, then the detector, then the three lanes and the voter at the cell's persistence count. It steps them frame by frame, unpaced, with the configuration of record. Every channel is NOMINAL from the first frame, and the filters are born on it. A fault's onset is drawn uniformly over the frames of [2 s, 12 s) for a sensor fault, ten whole GPS periods so that the onset's place in the fix second is uniform, and of [2 s, 4 s) for a lane fault. Its duration is drawn from the cell's range. A sensor fault's run ends 1 s after removal, or 6 s for the GPS, so that a recovery has time to show. Each run checks at onset that every channel is NOMINAL and every validity NORMAL; a run that fails the check is counted invalid and none is expected.

Each frame's channel statuses, voter bits, voted values and validities are recorded with every event, beside a reference. For a lane fault the reference is an unaltered lane's own output. For a fault no check covers, it is the voted output of the run's *twin*: the same seed with nothing injected. The injectors draw no random number, so the twin is the run's exact counterfactual.

### 4.2 The cells

A cell is one fault kind at one persistence count. Sensor and clean cells run at the shipped count, 3. Each lane kind runs at P = 1, 2, 3, 4, 5 and 8. The failure-mode numbers are FTS-FM-001's.

| Cell | Failure mode | Injection | Ranges (uniform) |
|---|---|---|---|
| D01 | FAULT-001 barometer timeout | every read fails, fields held | 1.0–5.0 s |
| D02 | FAULT-002 IMU timeout | every read fails | 1.0–5.0 s |
| D03 | FAULT-003 GPS timeout | every fix fails | 3.0–8.0 s |
| D04 | FAULT-004 bus failure | barometer and IMU fail together | 1.0–5.0 s |
| D05 | FAULT-005 barometer stuck | pressure and temperature repeat the last good reading | 0.5–5.0 s |
| D06 | FAULT-006 accelerometer stuck | one axis, x, y or z, repeats its value from the frame before | 0.5–5.0 s |
| D07 | FAULT-007 gyroscope stuck | one axis, x, y or z, the same | 0.5–5.0 s |
| D08 | FAULT-008 pressure out of range | pressure replaced by a constant | 100–299 or 1,101–1,500 hPa, 0.5–5.0 s |
| D09 | FAULT-009 temperature out of range | temperature replaced by a constant | −80 to −41 or 86 to 150 °C, 0.5–5.0 s |
| D10 | FAULT-010 accelerometer saturated | one axis at exactly one rail, +19.6194 or −19.62 m/s² | 0.5–5.0 s |
| T01 | below FAULT-001/002's timeout | a barometer or IMU dropout | 1–25 frames |
| T02 | below FAULT-003's timeout | a GPS dropout | 1–50 frames |
| T03 | below FAULT-005/006/007's count | a barometer or one IMU axis held | 1–8 frames |
| U01 | FAULT-023 bias step | pressure offset | ±0.5–5.0 hPa, to the end |
| U02 | FAULT-023 drift | pressure offset growing at a rate | ±0.005–0.05 hPa/s for 60 s |
| U03 | FAULT-023 bias step | acceleration offset on x or y | ±0.05–0.5 m/s², to the end |
| U04 | FAULT-023 bias step | GPS altitude offset | ±5–50 m, to the end |
| U05 | FAULT-023 frozen fix | the GPS reading held, still marked fresh at 1 Hz | 30 s |
| U06 | FAULT-024 NaN | pressure NaN | one frame |
| U07 | FAULT-024 infinity | x rotation rate +∞ | one frame |
| C00 | none | nothing | 60 s runs |
| V01 | FAULT-020 persistent value | one lane's output: one bit of one computed value flipped, every cycle | lane, value and bit (0–31) |
| V02 | FAULT-020 persistent NaN | that value NaN, every cycle | lane, value |
| V03 | FAULT-022 persistent overrun | the lane's previous cycle's output handed to the voter | lane |
| V04 | FAULT-019 transient value | V01 for one cycle | as V01 |
| V05 | FAULT-019 transient NaN | V02 for one cycle | as V02 |
| V06 | FAULT-022 transient overrun | V03 for one cycle | as V03 |
| V07 | FAULT-019/020 upset of L cycles | V01 for L cycles | L 2–6 |
| V08 | FAULT-020 state upset | one bit of one raw input a lane's filters read, one cycle | lane; pressure, accel x or y, gyro x or y; bit |
| V09 | FAULT-021 common mode | two lanes, the identical flip, every cycle | two lanes, value, bit |
| V10 | FAULT-021 common NaN | two lanes, the identical NaN | two lanes, value |
| V11 | FAULT-021 two different faults | lane a flipped; lane b flipped differently from g cycles later | g 0–10 |
| V12 | FAULT-021 two identical, delayed | lane b with lane a's flip from g cycles later | g 0–10 |

### 4.3 Seeds and reproducibility

A run's seed is its id, `cell × 100,000 + replicate`, and the id alone reproduces the run. The schedule (onset, duration, magnitude, lane, value, bit, gap) and the simulator's seed are derived from the kind and the replicate, not from the persistence count. The six counts of one lane kind therefore run over the same faults and the same noise, and their differences are the count's alone.

The schedule is drawn by the harness's own arithmetic over `std::mt19937` seeded by `std::seed_seq`, both fixed by the C++ standard, so a seed gives the same schedule on every platform. The simulator's noise comes from the standard library's normal distribution, whose algorithm is not fixed, so the measured distributions belong to the platform Section 3 names.

### 4.4 Outcomes

Every declaration falls in exactly one bucket.

- **On the faulted channel:**
  - *the detection*: inside the window, which runs from onset to the channel's first fresh reading after removal (for the GPS a timeout can fall on or just after the removal frame; that is a late detection, not a false alarm);
  - *a repeat*: a later one inside the window;
  - *a false alarm*: one before onset or after the channel recovered.
- **On another channel:**
  - *a wrong-channel declaration* while the fault or its recovery window lasts;
  - *a false alarm* otherwise.
- **The recovery time** comes from the recovery event after removal.

For the voter:
- the isolation latency;
- healthy lanes thrown out;
- wrong outputs;
- no-majority episodes, whose events are checked against the rising edges of the NO_MAJORITY bit.

Four invariants are checked on every frame: at least two lanes in the vote, at most one lane bit, VOTED set, and no NaN in a voted value.

### 4.5 Statistics

- **Rates.** A rate is k of n runs, stated with its 95% Wilson interval. It stays inside [0, 1] and is honest at k = 0 and k = n, where the textbook normal interval collapses to a point.
- **Zero counts.** A count of zero also carries the rule-of-three bound, 3/n: the rate below which zero events in n runs would happen at least 5% of the time. The exact value, 1 − 0.05^(1/n), is printed beside it.
- **False alarms** are counted against their exposure: the channel-hours on which nothing was injected. Exposure is counted once per kind and replicate, because the six persistence counts of a lane kind show the detector the same frames.
- **Latencies** are distributions, reported as their median, 95th percentile and maximum by the nearest-rank rule, so every reported value is one the campaign observed, on the 20 ms grid.

### 4.6 Run counts, chosen from the bounds

| Class | Runs per cell | What it buys at 95% |
|---|---|---|
| D01–D10, T01–T03 | 1,000 | a miss or false-declaration rate below 0.30% if none is seen; a 95th percentile from 1,000 values |
| U01–U07 | 300, each with its twin | a detection rate below 1.0% if none is seen; the consequence's median, 95th percentile and maximum |
| C00 | 10,000 runs of 60 s | a false-alarm rate below 0.03% per run, and below 0.018 per channel-hour from these runs alone |
| V01–V12, at each count | 1,000 | each rate within about ±3 points; a per-run rule checked on every run |

A fixed subset, ten seeds per cell and the lane cells at P = 1 and 3, runs on every build of the code and fails it on any departure from a per-run prediction (FTS-TP-001 TC-035).

---

## 5. Predictions

Written and committed on 2026-10-18, before the campaign's code existed, from the shipped configuration:
- 20 ms frames;
- timeouts of 500 ms (I2C) and 2,000 ms (GPS);
- a stuck count of 10 and a recovery count of 5;
- the limits of Section 1;
- one GPS fix a second.

Each is also a formula of the configuration in the campaign's catalogue, and a test holds the formulas to these numbers. Where a prediction depends on the simulator's own values (a reading before onset that repeated its predecessor; a removal frame whose live value equals a held one), each run's prediction is computed from the frames it generated, so the coincidence is predicted rather than tolerated. No prediction will be edited after the campaign runs: a gap is a finding (Section 7).

### 5.1 Faults the detector is built for

| Cell | Predicted detection | Predicted recovery |
|---|---|---|
| D01, D02 | **exactly 500 ms**: the 26th failed frame, the event's value 520 ms (the time since the last good reading). A dropout of 25 frames or fewer is never declared. | **80 ms** after removal: the fifth valid frame |
| D03 | the last delivered fix + 2,020 ms, so on the operator's clock **uniform over 1,020 to 2,000 ms** in 20 ms steps: nearest-rank median 1,500, 95th percentile 1,960, maximum 2,000. From the first suppressed fix, **always 1,020 ms**. The event's value always 2,020. | **4,000 to 4,980 ms** after removal: the fifth fix at or after it; always 4,000 ms from the first fix after removal |
| D04 | two declarations on one frame, barometer then IMU, both at 500 ms | 80 ms each |
| D05 | **160 ms**: the ninth held frame, the last good reading counted as the first of ten. 140 ms when the reading before onset happened to repeat its predecessor (a float step of 6.1e-5 hPa against noise of 0.2 hPa: about 1 run in 12,000). | 80 ms; 100 ms when the removal frame's live value equals the held one (about 1 run in 12,000) |
| D06, D07 | 160 ms, on the held axis; the z rotation rate, which the attitude filter ignores, is checked like the others | 80 ms |
| D08, D09, D10 | **0 ms**: on the onset frame, the event's value the injected one | 80 ms |

No D cell raises a voter event: every lane sees the same frame.

### 5.2 Faults below the thresholds

| Cell | Prediction |
|---|---|
| T01 | no declaration: the time since the last good reading reaches at most 500 ms, and the rule is "more than" |
| T02 | no declaration: at most one fix is lost, so the time since the last fix peaks at 1,980 ms |
| T03 | no declaration, except for an 8-frame hold whose reading before onset repeated its predecessor (about 1 run in 100,000) |

### 5.3 Faults no check covers

| Cell | Prediction |
|---|---|
| U01 | **no event.** The barometric altitude shifts by about 8.3 m per hPa of bias (the standard atmosphere's slope at 1,013 hPa) with validity NORMAL; the fused altitude follows nearly all of it, the GPS carrying about 0.2% of the weight. |
| U02 | **no event.** The altitude error grows to 2.5–25 m over the minute, and a false vertical speed of about 8.3 m/s per hPa/s of drift shows as NORMAL. The pressure stays more than 80 hPa inside its limits. |
| U03 | **no event.** The pitch or roll is off by atan(offset / g), 0.3° to 2.9°, reached within a few seconds (the attitude filter's time constant is about 1 s). |
| U04 | **no event.** Under 1% of the offset reaches the fused altitude (about 0.24%). |
| U05 | **no event**: the GPS has no stuck check. The position freezes; the fused altitude barely moves. |
| U06 | **no detector event.** The NaN passes every check (every comparison with NaN is false), enters the Kalman filter's state and never leaves it. No majority **from that frame to the end of the run**: one NO_MAJORITY event, value 3 (the lanes in the vote), every computed value held under FAILURE_WARNING, no NaN in the output. |
| U07 | **no event.** The roll becomes +∞ in all three lanes and stays so; the lanes agree, because the voter refuses NaN only, and the frame leaves **VOTED with the roll NORMAL**. |

### 5.4 Clean runs

C00 and the untouched channels of every injected run: **zero declarations.** This is all but certain by construction: the chance of a Gaussian float repeating itself ten times, or straying 87 hPa from its mean, is far below 1e-30 a frame (Section 9). There is no voter event, and every frame is VOTED; that part is certain, since identical lanes on identical input agree.

### 5.5 Lane faults, at persistence count P

| Cell | Prediction |
|---|---|
| V01, V02 | the onset event on the onset frame; the lane excluded **(P − 1) × 20 ms** after onset (40 ms at P = 3), its bit set from the next frame; the voted output a healthy lane's on every frame; no NO_MAJORITY |
| V03 | the onset event names an overrun; then as V01 |
| V04, V05, V06 | one onset event; the lane excluded **only at P = 1**, on the onset frame, which throws out a lane whose state was intact; the output a healthy lane's throughout |
| V07 | excluded if and only if L ≥ P: every run at P = 1 and 2; 80%, 60% and 40% at P = 3, 4 and 5; none at P = 8 |
| V08 | the lane's filters carry the upset for seconds, so the lane is excluded (P − 1) × 20 ms after onset at every P, unless the arithmetic rounds the flip away; that fraction is measured, not predicted. Excluded exactly when the lane disagrees with a healthy lane on P consecutive cycles, which the campaign checks run by run. |
| V09 | the two wrong lanes are the majority: **the healthy lane is thrown out** (P − 1) × 20 ms after onset, and **the wrong value is voted** on every frame from onset |
| V10 | no majority from onset to the end, one event (value 3), no exclusion |
| V11 | **no majority from the second fault to the end, at every P**: if g ≥ P, lane a excluded first and the event's value 2; if g < P, value 3 and lane a never excluded; the held output the same either way |
| V12 | if g ≥ P, lane a excluded first and then no majority, value 2: safe. If g < P, **a wrong majority**: the healthy lane excluded 20 × (g + P − 1) ms after the first fault, the wrong value voted. That happens in P out of 11 runs, **more often as P grows**. |

On every frame, at least two lanes stay in the vote, at most one lane bit is set, and no voted value is NaN.

---

## 6. Results

<!-- generated:detector -->
*Not yet run.*
<!-- end generated -->

<!-- generated:transients -->
*Not yet run.*
<!-- end generated -->

<!-- generated:false-alarms -->
*Not yet run.*
<!-- end generated -->

<!-- generated:uncovered -->
*Not yet run.*
<!-- end generated -->

<!-- generated:voter -->
*Not yet run.*
<!-- end generated -->

---

## 7. Measured against predicted

*Written after the first run: every gap between Section 5 and Section 6, with its explanation.*

---

## 8. Blind spots

What this campaign shows no check catches, and why:

1. **An error inside the limits** (FAULT-023: U01 to U05). Each check compares a channel with fixed limits, with its own previous value, or with the clock; a bias, a drift or a frozen GPS fix that keeps every reading inside its limits passes all three, and its error leaves the system marked NORMAL. Catching it needs a reference, such as the barometric altitude against the GPS altitude.
2. **A non-finite reading** (FAULT-024: U06, U07). The detector's comparisons are all false for NaN, and it has no rotation-rate limit. The drivers keep such values out today. If one got through, a NaN would take the voted output down for the rest of the run, on the voter's channel rather than the sensor's, and an infinity would be voted through as valid.
3. **A fault common to two lanes** (V09, V12). The voter trusts a majority. Two lanes wrong in the same way outvote the healthy one and exclude it, and the wrong value leaves marked VOTED.
4. **Any sensor fault, as far as the voter is concerned.** Every lane is given the same checked frame, so a bad reading goes in and three agreeing lanes come out. The voter sees only faults inside a lane.
5. **Faults shorter than a threshold** (T01 to T03), by design: a dropout of up to 500 ms on the barometer or IMU, the loss of one GPS fix, a reading repeated fewer than ten times.

---

## 9. What this campaign does not prove

- **The faults are simulated.** They are injected into the frame, not on the bus, at the register or on a wire. A real dropout's timing is the driver's, and the bench's wire-pull test is that evidence.
- **One processor.** The three lanes share one CPU, one compiler and one math library, so a defect common to every lane is invisible to the voter, and nothing here shows survival of a hardware failure.
- **The noise model.** The simulator's readings are continuous floats with Gaussian noise far inside every limit, never repeated and never dropped. Its false-alarm figures are zero by construction and bound only the detector's logic: a real register is quantized, and a quiet sensor can repeat a count. The rerun after the bench session uses the measured noise but stays unquantized, so it cannot change that figure. A quantized simulator, or a campaign sourced from the stationary capture's log, could.
- **One scenario.** The vehicle sits still. A bias or a drift is easier to see on a still vehicle than on one that climbs.
- **One platform.** The figures are this platform's (Section 3). A seed reproduces its schedule everywhere and its noise only on the same standard library.
- **Assumed fault distributions.** The voter's upset lengths and fault gaps are assumptions, so the sweep shows the shape of the persistence trade-off, not its operating point on real hardware.
- **Coverage is not correctness.** A campaign that matches its predictions shows that the implementation follows its rules. It does not show that the rules, the thresholds or the persistence count are right for an aircraft.

---

## 10. How to rerun

```
scripts/fault_campaign.sh                     # the full campaign on a clean tree: build, run, generate this report's blocks
build/fault_campaign --seed <id> --log <dir>  # any one run, from its seed alone, with its binary log
```

The script records the commit, the platform and the configuration's hash, runs every cell at its stated count, and rewrites only the generated blocks of Sections 3 and 6 and the figures, so `git diff` shows which numbers moved. The rerun after the bench session changes the configuration's measured constants and the simulator's noise, and with them the uncovered faults' consequences; it cannot change the detection latencies, which follow from the thresholds and the frame period alone, or the false-alarm figure, which the unquantized noise fixes at zero.
