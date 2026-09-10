# Flight Telemetry System — Software Design Description

> **Document ID:** FTS-DD-001
> **Version:** 0.1.0
> **Status:** Draft
> **Last Updated:** 2026-09-09
> **Related:** FTS-SRD-001 (requirements), FTS-TM-001 (traceability), FTS-TP-001 (test plan), FTS-FM-001 (fault model)

## 1. Purpose and Scope

This document records the architecture of the Flight Telemetry System and the reasoning behind its design decisions. It exists so that a reviewer can see why the system is shaped this way without reading the source, and so that later decisions are made against a written baseline rather than memory.

It is not an API reference, a build guide, or a tutorial. Interfaces are documented in the headers; build and run instructions are in the README. Where this document and the code disagree, the code is wrong or this document is stale, and either way the disagreement is a defect to record.

---

## 2. Software Level: DAL C

This project assumes Design Assurance Level C under DO-178C. DAL C corresponds to a *major* failure condition: a failure that significantly reduces safety margins or significantly increases crew workload, but from which a safe landing remains possible. A telemetry display that shows wrong altitude or attitude fits that class. It could mislead a pilot and raise workload; on its own it does not cause loss of the aircraft, because certified primary instruments remain.

The practical consequence is in DO-178C Table A-5 (verification of verification process outputs). At DAL C the structural coverage objectives are statement coverage and decision coverage. Modified Condition/Decision Coverage is required only at DAL A. This is the formal basis for the branch coverage thresholds in FTS-TP-001 Section 6 and for MC/DC being out of scope there. The thresholds are not arbitrary targets; they are the DAL C ceiling.

DAL C is an assumption for a development system, not a certification claim. No safety assessment (ARP4761) has been performed, and none is planned. The level was chosen because it is the lowest level at which structural coverage is required, which makes it the lowest level at which the coverage tooling in this project has a reason to exist.

---

## 3. DataSource Abstraction

Every frame enters the system through the `DataSource` interface (`src/data_source.h`): one pure virtual method, `read_frame()`, returning a `TelemetryFrame`. The processing engine holds a reference to the base class and never learns the concrete type. The factory in `main.cpp` is the only place that does.

**Raw-only contract.** A source fills raw sensor fields only: pressure, temperature, accelerometer, gyroscope, GPS position and speed, and channel status. Every computed field (barometric altitude, pitch, roll, fused altitude, vertical speed) is written by the pipeline. A source that computed its own altitude would be a source and a pipeline at once, and replay could no longer prove that the pipeline reproduces live behavior.

**Why.** REQ-LOG-004 requires replay to use the same processing pipeline as live mode, differing only in data source. That is only meaningful if the pipeline is a pure function of the raw frames it receives. The abstraction makes the source the single variable.

**Planned implementations.**

| Source | Status | Role |
|---|---|---|
| `SimulatedSource` | Built | Seeded Gaussian noise around a stationary Foster City fix. Deterministic per seed on a given toolchain. |
| Live sensor source | Sprint 1 week 2 | BMP280, MPU6050, NEO-6M drivers behind one source. |
| `LogReplaySource` | Built | Reads the binary log written by the logger, strips computed fields and status, feeds the raw frames back through the same pipeline. Selected by `telemetry --replay <file>`. TC-004. |
| `ADSBSource` | Sprint 2 | Real aircraft state vectors from OpenSky, presented as if they were the vehicle's own sensors. IMU channel permanently DEGRADED because ADS-B carries no attitude. See Section 10. |

---

## 4. Processing Pipeline Order

```
DataSource  →  FaultDetector  →  TelemetryProcessor  →  Logger / ZeroMQ / UDP / Display
 raw + read_ok     raw + status        raw + computed
```

The fault detector sits before the processor. This is the most important ordering decision in the system.

The detector takes its notion of time from `frame.timestamp_ms`, never from a wall clock. That keeps it a pure function of the frame sequence: a replayed log produces the same faults at the same frames (REQ-LOG-003), and tests advance time by handing in frames with later timestamps. It also fixes the strictness of the timeout comparison. With frames every 20 ms and the last success at t−20, elapsed `> 500` declares the fault 500 ms after the first failed read; `>=` would declare it at 480 ms, which TC-002 treats as a false alarm on a single transient error.

Both filters in `TelemetryProcessor` gate on `ChannelStatus`: a DEGRADED channel contributes nothing that frame. That gate only protects the filters if status is correct *before* the frame reaches them. A stuck or out-of-range reading that arrives marked NOMINAL enters the Kalman update or the complementary filter and corrupts state that persists across every subsequent frame. Detecting the fault afterward does not undo that. So detection must run first, on the raw frame, and the processor must trust the status it is handed.

The processor itself is a class rather than a free function because both filters carry state between frames. It is passed by non-const reference through the loop and created once, outside it, so the same instance sees every frame in order.

---

## 5. Kalman Filter Design

`KalmanFilter1D` (`src/processing/kalman_filter.h`) fuses barometric and GPS altitude (REQ-PROC-003).

**Why 1D, not an extended Kalman filter.** One spatial axis, a linear motion model, and direct measurements of the state mean the standard linear filter is exact. An EKF linearizes a nonlinear model; there is nothing here to linearize. Choosing the simplest filter that is correct keeps every line explainable, which is a project goal in its own right.

**Why two states.** Altitude alone cannot be extrapolated between measurements. Adding vertical velocity gives the predict step something to integrate, and the covariance coupling between the two states lets an altitude-only measurement correct velocity. Vertical speed is therefore estimated without a vertical speed sensor.

**Constant-velocity model and its limits.** The predict step assumes velocity does not change between frames. Process noise Q, built from a single acceleration variance, admits that it does. The model lags under sustained acceleration (rotation, flare, strong thermals). A larger Q shortens the lag at the cost of smoothing. A third state (acceleration) or an IMU-driven predict step would address this and is out of scope.

**Tuning.** Measurement variances (3 m² baro, 25 m² GPS), initial covariance, and process noise are placeholders in `processor.cpp` until REQ-PROC-004 is met by measured sensor noise. They will move to `config/telemetry_config.yaml` under REQ-CFG-001. A startup transient of about 1 m/s in vertical speed has been observed on a stationary simulated board; it is driven by the initial velocity variance and will be revisited during noise characterization.

**Verified behavior.** Fused altitude sigma is 0.24 m against 1.65 m raw barometric over 500 simulated frames. Two runs are bit-identical. See TC-007.

---

## 6. Complementary Filter Design

`ComplementaryFilter` (`src/processing/complementary_filter.h`) estimates pitch and roll (REQ-PROC-002).

**Axis and sign convention.** Right-handed body frame: x forward, y right, z up. Pitch is rotation about y and integrates `gyro_y`; roll is rotation about x and integrates `gyro_x`. Accelerometer tilt uses `atan2(accel_x, sqrt(accel_y² + accel_z²))` for pitch and the y-axis equivalent for roll. **This convention is an assumption.** Whether the mounted MPU6050 agrees in sign cannot be confirmed without hardware. If it does not, the gyro and accelerometer paths fight and the filter converges to garbage; the fix is a sign flip in configuration, and the check is part of hardware week.

**Alpha as a time constant.** The gyro weight alpha (default 0.98) is meaningless without the sample interval. The accelerometer correction has a time constant of about alpha × dt / (1 − alpha), which at 50 Hz is about one second. A steady gyro bias b settles to an error of alpha × b × dt / (1 − alpha) rather than growing without bound. TC-006 verifies that closed form.

**First-frame seeding.** The first update adopts the accelerometer angles directly. Starting from zero would leave the display wrong for about one time constant after boot, for no benefit.

**DEGRADED behavior and the FAULT-006 conflict.** When the IMU channel is DEGRADED the filter is not updated and the previous angles are held. FAULT-006 in FTS-FM-001 requires the opposite: attitude marked invalid, not held, so the display shows a clear failure rather than a frozen reading that looks live. The frame has no validity marker to carry that today. Hold is the interim behavior and this conflict is recorded as an open decision (Section 10). FAULT-007's accelerometer-only fallback for a stuck gyroscope is likewise not implemented.

**Why not a quaternion EKF.** A full attitude estimator would handle yaw, large angles, and coupled rotations. For a board on a desk and a PFD attitude indicator, pitch and roll from a complementary filter are sufficient and every line is explainable. A production flight system would upgrade; the interface (`pitch_deg()`, `roll_deg()`) would not change.

---

## 7. Health Representation

Channel health is a `ChannelStatus` enum (NOMINAL, DEGRADED) per sensor inside `TelemetryFrame`. This is deliberately minimal: it is what the filters need to gate on, and nothing more. Only the fault detector writes it.

Alongside status, each channel carries a `read_ok` flag. This is not health. It says whether the driver obtained a fresh reading this cycle; `false` means the channel's fields are stale from the previous cycle. One failed I2C transaction is normal, so a source reports the fact and the detector decides when staleness has lasted long enough (500 ms for I2C, longer for the 1 Hz GPS) to be a fault. Keeping the two separate is what lets the transient tolerance of REQ-FAULT-001 live in one place instead of in every driver.

Fault events (REQ-FAULT-004: timestamp, channel, type, value) are `FaultEvent` structs the detector accumulates and hands out through `take_events()`. Tests assert on the structs and `main` prints them; the binary logger will persist the same structs when it exists.

Two forthcoming needs do not fit inside the frame: the redundancy voter (Section 9) must report per-lane state, and FAULT-006 needs per-field validity. The decision between extending the frame and adding a separate `SystemHealth` sidecar struct is deferred to Sprint 2 week 1, when the voter is the first consumer. The criteria: whether the log format should carry health inline, and whether the display needs health at a different rate from telemetry.

---

## 8. Determinism

Same-platform replay is bit-exact. `KalmanTest.DeterministicOutput` runs the pipeline twice with the same seed and compares every fused altitude with exact equality.

Cross-platform determinism is not guaranteed and is not attempted. `std::normal_distribution` is implemented differently in libc++ (macOS) and libstdc++ (Linux, Raspberry Pi), and `std::pow` and `std::atan2` may differ in the last bit between math libraries. A log recorded on the Pi and replayed on the Mac may therefore differ at the least significant digits. This is documented here and in the simulator source, and TC-004 is scoped to same-platform replay. Replacing the standard distribution with a hand-written generator was considered and rejected: the property that matters, live-to-replay equality on the Pi, does not need it.

---

## 9. Log Format and Replay

The binary log (REQ-LOG-001) is a header followed by tagged records:

```
LogFileHeader (16 bytes): magic "FTLG", format version, sizeof(TelemetryFrame),
                          sizeof(FaultEvent), reserved, endianness marker
{ tag (1 byte), record }   tag 1 = TelemetryFrame (104 bytes), tag 2 = FaultEvent (16 bytes)
```

**Raw struct records.** Both record types are written as their in-memory bytes. That is licensed by `static_assert(std::is_trivially_copyable)` on each: the bytes are the value. It is the simplest correct format for the scope this project claims (Section 8: same platform, same build), and it costs nothing per frame. Field-by-field serialization with fixed widths would be portable across compilers and byte orders and is the right answer for a shipped product; it was considered and deferred as roughly three times the code for a property the project does not claim.

**Header guards.** The header carries everything a reader needs to refuse a file rather than misread it: magic so a wrong file fails instantly, a version so the format can evolve, both record sizes so a frame-layout change is caught at open time, and an endianness marker for honesty about the platform limit. **Rule: any change to `TelemetryFrame` or `FaultEvent` bumps `kLogFormatVersion` in the same commit.** The `read_ok` flags added on 9/9 were the last free frame change.

**Rotation (REQ-LOG-002)** happens before a record that would push the file past the configured limit, never in the middle of one. A torn record makes the tail of a file unreadable and a replay would silently lose a frame. Every file, rotated ones included, starts with its own header, so each is readable alone and `list_log_files` returns them in name order (`prefix_000.bin`, `_001`, ...).

**Fault events** (REQ-FAULT-004) are tag-2 records in the same stream, so an event sits between the frames it occurred at. Replay skips them and reproduces them by re-running the detector; the identity test compares the reproduced events to the logged ones.

**Replay strips to raw.** `LogReplaySource` zeroes every computed field and resets every `ChannelStatus` to NOMINAL before a frame leaves it, keeping the timestamp, sensor fields, and `read_ok` flags. This is the raw-only contract of Section 3 applied to replay: the detector recomputes status from `read_ok` (observation, kept) and the processor recomputes the rest. A replay that passed the logged results through would let the pipeline be skipped entirely and still "match."

**Verified behavior.** `ReplayTest.ReplayReproducesLiveSession` logs 500 simulated frames with an injected barometer dropout and a stuck IMU across rotated files, replays them through a fresh detector and processor, and finds every processed frame and every fault event bit-identical. At file level, `telemetry` followed by `telemetry --replay logs/telemetry_000.bin` produces `logs/replay_000.bin` byte-identical to the original. A replay run logs under a different prefix because the replay source opens its input before the logger opens its output in the same directory.

**End of stream.** `DataSource` has no way to say "no more frames." After the last frame, `read_frame()` returns that frame again and `exhausted()` reports true. Adding an end-of-stream signal changes the interface every source implements and is an open decision (Section 11).

---

## 10. Parked Requirement Drafts (Sprint 2)

The following are **not** in FTS-SRD-001, not implemented, and not traced. They are recorded here because the current design already accommodates them (the `DataSource` seam and `ChannelStatus`), and writing the intent down keeps Sprint 1 decisions from closing the door. When each moves into scope it is added to FTS-SRD-001, given a row in FTS-TM-001, and only then coded.

### 10.1 Redundancy and voting (REQ-RED)

| Draft ID | Intent |
|---|---|
| REQ-RED-001 | Run three independent processing lanes over the same raw frame. |
| REQ-RED-002 | Select the system output by majority vote across lanes. |
| REQ-RED-003 | Detect lane disagreement beyond a configurable threshold and log it. |
| REQ-RED-004 | Continue on two agreeing lanes when one is excluded. |
| REQ-RED-005 | Readmit an excluded lane after M consecutive agreeing frames. |

Associated fault entries FAULT-012 through FAULT-015 and test cards TC-009 through TC-011 are reserved.

### 10.2 ADS-B data source (REQ-SENS-007..010)

| Draft ID | Intent |
|---|---|
| REQ-SENS-007 | Provide a `DataSource` that presents real aircraft state from the OpenSky Network as telemetry. |
| REQ-SENS-008 | Select the nearest airborne aircraft within a configurable bounding box, with hysteresis. |
| REQ-SENS-009 | Sample-and-hold the last state vector between network updates, stamped with the local clock. |
| REQ-SENS-010 | Mark the IMU channel permanently DEGRADED in this mode, since ADS-B carries no attitude. |

---

## 11. Open Design Decisions

- **Health sidecar vs frame fields.** Decide in Sprint 2 week 1 (Section 7). Blocks FAULT-006 compliance and voter reporting. Any frame change now also bumps the log format version (Section 9).
- **End-of-stream signal on `DataSource`.** Replay repeats its last frame after the log ends. A `bool has_next()` or an `std::optional<TelemetryFrame>` return would be cleaner and touches every source; decide before the live-sensor source lands.
- **Gyroscope saturation fault.** No FAULT entry exists for the gyro's full-scale limit (FTS-FM-001 TODO). Still open after the detector landed; FAULT-010 covers the accelerometer only.
- **GPS stuck detection.** REQ-FAULT-002 says "any channel", but no FAULT entry covers a stuck GPS and a stationary receiver, or a sample-and-hold ADS-B source, legitimately repeats. Either add a FAULT entry with a rule that tolerates legitimate repeats or narrow the requirement.
- **Bus-level fault event (FAULT-004).** Two simultaneous I2C timeouts log two events today. Collapsing them into one needs driver error codes that distinguish a bus fault from two device faults.
- **`TelemetryProcessor` configuration.** The processor has a default constructor and hardcoded tuning. The shape of the config struct it will take (REQ-CFG-001) is undecided: one struct for the whole pipeline, or one per filter.
- **Where alpha lives.** The complementary filter's alpha is a compile-time default in its header. Whether it moves to YAML with the Kalman tuning, or stays fixed because it is a time constant rather than a noise parameter, is undecided.
- **Accelerometer-only fallback (FAULT-007).** Requires the detector to distinguish a stuck gyroscope from a stuck accelerometer, which the single DEGRADED state cannot express. Tied to the sidecar decision.
