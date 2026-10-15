# Flight Telemetry System — Fault Model

> **Document ID:** FTS-FM-001
> **Version:** 0.4.0
> **Status:** Draft
> **Last Updated:** 2026-10-14

This document defines every known failure mode, how the system detects it, and how the system responds. Each entry traces to a requirement and a test.

Where a response says "log fault event", the event is a `FaultEvent` record (timestamp, channel, fault type, value) written by `BinaryLogger::log_event` into the same binary log as the telemetry frames, tagged so replay can skip it (FTS-DD-001 Section 9).

Every threshold below is a key of `config/telemetry_config.yaml` under `fault` (REQ-CFG-001), except FAULT-010's, which is the full-scale range the driver configures and is derived from the driver's own constants; the values quoted are the shipped file's, validated at startup (REQ-CFG-002).

Where a response says a computed field is "marked invalid" or "flagged", the frame's validity byte for that field reads FAILURE_WARNING (REQ-PROC-007): the field keeps a defined number, held or coasting, and the flag is what the display acts on. A raw field (position, heading, ground speed, GPS altitude, pressure) carries no validity byte of its own; it is invalid when its channel's status says so. See "Start-up and validity" before the degradation table.

---

## Sensor Communication Failures

### FAULT-001: BMP280 I2C timeout

- **What:** BMP280 stops responding to I2C reads (no ACK on the bus)
- **Cause:** Loose wire, sensor hardware failure, I2C bus contention
- **Likelihood:** Low
- **Detection:** I2C read returns an error (NACK or bus timeout). Fault is declared when no successful read has occurred within the configured I2C timeout (`fault.i2c_timeout_ms`, 500 ms), allowing transient errors to be retried.
- **Response:** Mark barometer channel DEGRADED. Log fault event with timestamp. Continue processing with IMU + GPS only. Fused altitude relies solely on GPS until barometer recovers: the fused fields stay NORMAL while the GPS contributes, and read FAILURE_WARNING, at once and for as long as it lasts, when no altitude source contributes (REQ-PROC-007). Barometric altitude reads FAILURE_WARNING, holding the conversion of the last accepted reading. Before the timeout expires the channel is still NOMINAL, but a frame with no reading (`baro_read_ok` false) is not applied to the filter: it coasts on prediction from the first missed read, not from the fault declaration (REQ-PROC-006). A barometer dead from power-on is NO_DATA until the timeout, then DEGRADED with this event, and its barometric altitude reads NO_COMPUTED_DATA throughout, never a flag over a number nobody measured (REQ-FAULT-006).
- **Requirement:** REQ-FAULT-001, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → `FaultDetectionTest.BaroTimeoutDetectedWithinBound`, `FaultDetectionTest.TransientReadErrorDoesNotFault` (detection logic). Driver level: `tests/unit/test_bmp280.cpp` → `Bmp280Test.FailedBurstAndSkippedMeasurementAreEmpty` (a failed burst, or data registers still at their reset value, is an empty reading, which the live source reports as `baro_read_ok` false: `LiveSensorSourceTest.FailedBaroReadHoldsFieldsAndClearsReadOk`). On hardware: TC-002 step 8.

### FAULT-002: MPU6050 I2C timeout

- **What:** MPU6050 stops responding to I2C reads
- **Cause:** Loose wire, sensor hardware failure, I2C bus contention
- **Likelihood:** Low
- **Detection:** I2C read returns an error (NACK or bus timeout). Fault is declared when no successful read has occurred within the configured I2C timeout (`fault.i2c_timeout_ms`, 500 ms).
- **Response:** Mark IMU channel DEGRADED. Log fault event. Continue processing with barometer + GPS only. Pitch and roll are held at their last values and flagged FAILURE_WARNING until the IMU recovers (REQ-PROC-007): the display shows the failure, not the number. Between the first missed read and the timeout the attitude is held and still NORMAL, because a stale cycle inside the timeout is the detector's call and not a flag, and the next fresh sample integrates the gyroscope over the whole gap (REQ-PROC-006). After a gap longer than the timeout the next accepted sample re-initializes pitch and roll from the accelerometer instead (REQ-PROC-002): one rate sample times a long outage would fabricate tilt.
- **Requirement:** REQ-FAULT-001, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → `FaultDetectionTest.ImuTimeoutDetectedWithinBound`. Driver level: `tests/unit/test_mpu6050.cpp` → `Mpu6050Test.FailedBurstReturnsEmpty` (a failed burst is an empty reading, which the live source reports as `imu_read_ok` false), `FailedIdentityReadRefusesInit`. On hardware: TC-002 step 8.

### FAULT-003: GPS timeout or fix loss

- **What:** Two distinct conditions with the same response. (a) The GPS receiver stops delivering sentences. (b) The receiver keeps sending sentences but reports no satellite fix (GGA fix quality 0, RMC status V).
- **Cause:** (a) Loose wire, wrong device path or baud rate, module power issue, the USB lead pulled. (b) Indoor operation, obstructed sky view, the minutes a cold start takes.
- **Likelihood:** (a) Low. (b) Medium, especially indoors or near windows with partial sky.
- **Detection:** Both reach the detector as one condition. The live source delivers a reading (`gps_read_ok` true) only on the cycle a valid fix completes, so a silent receiver and one without a fix both leave the channel without a reading, and the detector declares the fault when no fix has been read within the configured timeout (`fault.uart_timeout_ms`, 2000 ms). The timeout must exceed the receiver's 1 Hz fix period, so it is configurable rather than fixed at 500 ms, and the configuration refuses a value at or below the simulated receiver's fix period. The two causes are told apart at the receiver's exit line (`GPS:` sentences, epochs without a fix, bad checksums, read errors): sentences without fixes is (b); nothing, bad checksums, or read errors climbing (a device the kernel hung up) is (a). A detector-side distinction with its own event waits for the fix-quality byte parked in FTS-DD-001 Section 11.2.
- **Response:** Mark GPS channel DEGRADED. Log fault event (COMM_TIMEOUT with the elapsed ms). Continue processing with barometer + IMU only. Fused altitude relies solely on barometer and stays NORMAL. Position, heading, and ground speed are invalid by the channel's status (DEGRADED here; NO_DATA before the first fix, REQ-FAULT-006), the raw fields carrying no validity byte of their own. With `sensors.gps.enabled` false, or before the receiver's first fix, the channel is NO_DATA from the first frame and DEGRADED with this event at the timeout; the first fix after that starts the recovery of FAULT-011, five fixes at 1 Hz. Between fixes (49 of every 50 cycles at 1 Hz) `gps_read_ok` is false and the last fix is not re-applied; that is normal operation, not a fault, and it is why the timeout must exceed the fix period (REQ-PROC-006).
- **Requirement:** (a) REQ-FAULT-001. (b) REQ-FAULT-003, the fix-quality limit applied at the source. Both: REQ-FAULT-004
- **Test:** (a) `tests/unit/test_fault_detection.cpp` → `FaultDetectionTest.GpsTimeoutUsesUartBound`; `tests/unit/test_live_sensor_source.cpp` → `LiveSensorSourceTest.FixIsFreshOnlyOnTheCycleItCompleted`. (b) `tests/unit/test_nmea_gps.cpp` → `NmeaGpsTest.NoFixEpochIsNotAReading` (quality 0) and `NmeaGpsTest.FixQualityGgaWithStatusVRmcIsNoFix` (status V beside a fix-quality GGA), the two halves of the fix rule at the source; the detector's own event is deferred with the fix-quality byte.

### FAULT-004: I2C bus failure (both BMP280 and MPU6050 affected)

- **What:** Entire I2C bus becomes unresponsive — both BMP280 and MPU6050 fail simultaneously
- **Cause:** SDA or SCL wire disconnected, bus lockup, electrical issue
- **Likelihood:** Very low
- **Detection:** Both I2C devices time out within the same I2C timeout window (`fault.i2c_timeout_ms`, 500 ms)
- **Response:** Mark barometer and IMU channels DEGRADED. Log a single bus-level fault event rather than two separate sensor faults. Continue processing with GPS only. System is heavily degraded but still running — fused altitude from GPS only (NORMAL while the GPS contributes), barometric altitude and attitude FAILURE_WARNING.
- **Requirement:** REQ-FAULT-001, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → `FaultDetectionTest.BusFailureDegradesBothI2cChannels`. Both channels degrade on the same frame. The single bus-level event is deferred: the detector sees two device timeouts and logs two events, because telling a bus fault from two device faults needs driver error codes that do not exist yet. The drivers now exist and still do not supply them: `LinuxI2cBus::last_errno()` stops at the live source, which reports only `read_ok` per channel. Parked for the faults topic.

---

## Stuck Sensor Failures

### FAULT-005: BMP280 stuck pressure reading

- **What:** Barometer returns identical pressure values on consecutive reads
- **Cause:** Sensor firmware hang, register read returning stale cached data
- **Likelihood:** Low
- **Detection:** N consecutive identical pressure readings (N is `fault.stuck_count`, 10)
- **Response:** Mark barometer channel DEGRADED. Log fault event with the stuck value. Continue with IMU + GPS.
- **Requirement:** REQ-FAULT-002, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → `FaultDetectionTest.TenthIdenticalPressureIsStuck`, `FaultDetectionTest.StaleValuesDuringDropoutAreNotStuck`

### FAULT-006: MPU6050 stuck accelerometer

- **What:** Accelerometer returns identical raw values on consecutive reads on one or more axes
- **Cause:** Sensor firmware hang, I2C read returning stale data
- **Likelihood:** Low
- **Detection:** N consecutive identical raw readings on any axis (N is `fault.stuck_count`, 10). Comparison uses the raw 16-bit register values, since a live sensor at rest still shows LSB-level noise.
- **Response:** Mark IMU channel DEGRADED. Log fault event. Pitch and roll are held at their last values and flagged FAILURE_WARNING until the channel recovers (REQ-PROC-007). The flag, not the number, is what stops a frozen reading looking live: the frame keeps a defined value, and the display shows the failure in its place (FTS-DD-001 Section 11.2). Until 0.2.0 this entry asked for the value not to be held, which the frame could never express; what it wanted was the flag.
- **Requirement:** REQ-FAULT-002, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → `FaultDetectionTest.StuckAccelXWithOtherAxesVarying`, `FaultDetectionTest.RepeatsOfTwoAreNotStuck`. The detector compares the frame's float values with exact equality; a stuck register converts to a bit-identical float every time, so this is the same test as raw register comparison.

### FAULT-007: MPU6050 stuck gyroscope

- **What:** Gyroscope returns identical raw angular rate values on consecutive reads on one or more axes
- **Cause:** Sensor firmware hang, register stale
- **Likelihood:** Low
- **Detection:** N consecutive identical raw readings on any axis (N is `fault.stuck_count`, 10)
- **Response:** Mark IMU channel DEGRADED with gyroscope data excluded. Complementary filter falls back to accelerometer-only attitude, which is noisier but still valid because gravity alone determines static pitch and roll. Log fault event. Until that fallback exists the whole channel is DEGRADED and pitch and roll read FAILURE_WARNING, held (REQ-PROC-007).
- **Requirement:** REQ-FAULT-002, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → `FaultDetectionTest.StuckGyroZ`, `FaultDetectionTest.DifferingValueResetsRun`. The accelerometer-only fallback is not implemented; the whole IMU channel is DEGRADED (see FTS-DD-001 Section 6).

---

## Out-of-Range Failures

### FAULT-008: BMP280 pressure out of physical range

- **What:** Pressure reading falls outside physically possible range
- **Cause:** Sensor malfunction, calibration corruption, electrical noise
- **Likelihood:** Very low
- **Detection:** Pressure reading below 300 hPa or above 1100 hPa (`fault.pressure_min_hpa` and `fault.pressure_max_hpa`). These are the BMP280 datasheet operating limits. 300 hPa corresponds to roughly 9,000 m, far above anything this system will see. 1100 hPa is above the highest sea-level pressure ever recorded (about 1084 hPa).
- **Response:** Discard reading: the barometric altitude holds the conversion of the last accepted reading and reads FAILURE_WARNING, while the out-of-range value stays in the raw pressure field as the source's record (REQ-PROC-007). Mark barometer channel DEGRADED. Log fault event with the out-of-range value. Continue with IMU + GPS.
- **Requirement:** REQ-FAULT-003, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → `FaultDetectionTest.PressureBelowRange`, `FaultDetectionTest.PressureAboveRange`, `FaultDetectionTest.BoundaryValuesAreValid`

### FAULT-009: BMP280 temperature out of sensor range

- **What:** Temperature reading falls outside the sensor's operating range
- **Cause:** Sensor malfunction, calibration corruption
- **Likelihood:** Very low
- **Detection:** Temperature below -40°C or above 85°C (`fault.temperature_min_c` and `fault.temperature_max_c`; the BMP280 specified operating range)
- **Response:** Discard reading: the barometric altitude holds the conversion of the last accepted reading and reads FAILURE_WARNING (REQ-PROC-007). Mark barometer channel DEGRADED. Log fault event with the out-of-range value. The BMP280 computes compensated pressure from its own temperature measurement, so an invalid temperature also invalidates pressure and barometric altitude. Continue with IMU + GPS.
- **Requirement:** REQ-FAULT-003, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → `FaultDetectionTest.TemperatureOutOfRangeDegradesBaro`

### FAULT-010: MPU6050 accelerometer saturation

- **What:** Acceleration reading reaches either rail of the full-scale range the driver configures
- **Cause:** Physical shock (dropping the board), sensor configured at too narrow a range for the motion
- **Likelihood:** Low (more likely during handling)
- **Detection:** Any axis at either rail of the ±2 g full scale the driver configures: +19.6194 m/s² (count +32767) or −19.62 m/s² (count −32768), the two extreme 16-bit codes through the driver's own scaling (`Mpu6050::kAccelPositiveRailMps2` and `kAccelNegativeRailMps2`, FTS-DD-001 Section 3). Not a key and not a magnitude test: a 16-bit two's-complement range is asymmetric, so the positive rail is one count short of 2 g, and the count one inside the negative rail (−32767) converts to the negation of the positive rail. Until 0.4.0 the limit was `fault.accel_limit_mps2`, 19.62 m/s², compared by magnitude, which the positive rail never reached, so positive saturation never tripped (FTS-VR-001 Entry 8 §8.7, closed in Entry 14).
- **Response:** Discard reading. Mark IMU channel DEGRADED. Pitch and roll held and flagged FAILURE_WARNING until readings return within range (REQ-PROC-007). Log fault event with the clipped value.
- **Requirement:** REQ-FAULT-003, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → `FaultDetectionTest.AccelAtPositiveRailDegradesImu`, `AccelAtNegativeRailDegradesImu`, `AccelOneCountInsideEitherRailIsValid`; `tests/unit/test_mpu6050.cpp` → `Mpu6050Test.ExtremeCountsAreTheRails` (the driver's conversion of 0x7FFF and 0x8000 equals the two constants bit for bit, which is what ties the detector's threshold to what the sensor produces; TC-013 step 7)

> **TODO (deferred, decide in build session):** The gyroscope has an equivalent saturation mode (±250°/s at power-on default, up to ±2000°/s). No fault entry exists yet; when it is written, its rails derive from `Mpu6050::kGyroLsbPerDps` the same way (at 131 LSB per deg/s the codes ±32768 convert to ±250.1 deg/s, outside a nominal 250, so a stated number would err in the other direction: inside the rails).

---

## Recovery

### FAULT-011: Sensor recovery from DEGRADED state

- **What:** A previously failed sensor resumes producing valid readings
- **Cause:** Intermittent connection restored, sensor firmware recovers, GPS reacquires lock
- **Likelihood:** Medium (especially GPS after temporary obstruction)
- **Detection:** M consecutive valid readings after a DEGRADED state (M is `fault.recovery_count`, 5). Requiring more than a single good reading prevents a flapping sensor from toggling between states every cycle.
- **Response:** Mark channel as NOMINAL. Log recovery event with timestamp and duration of degraded state. Resume using sensor data in processing pipeline and Kalman filter; the fields that depend on the channel return to NORMAL on the first NOMINAL frame (the attitude re-seeds first when the gap exceeded the timeout, REQ-PROC-002). A channel's first reading after start-up is not a recovery: NO_DATA becomes NOMINAL without an event (REQ-FAULT-006).
- **Requirement:** REQ-FAULT-005, REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp` → `FaultDetectionTest.RecoveryAfterMValidReadings`, `FaultDetectionTest.BadReadingResetsRecoveryCount`, `FaultDetectionTest.StuckSensorRecovers`

---

## Start-up and validity

**NO_DATA (REQ-FAULT-006).** A channel is NO_DATA from start-up until its first successful reading, then NOMINAL without an event: a start is not a recovery. A fault on a NO_DATA channel, the communication timeout included, marks it DEGRADED as from NOMINAL, with its event. Before the first reading the raw fields are the zeros of a value-initialized frame, which is why the status says NO_DATA rather than letting a display show a position of 0, 0.

**Validity (REQ-PROC-007).** Every computed field carries one byte in the frame, written by the processor on every frame in this order of precedence:

| Validity | Meaning | Value in the field | Display (FTS-DD-001 Section 11.2) |
|---|---|---|---|
| NOT_JUDGED | nothing judged the field: a raw frame from a source, a version-1 log; never emitted by the pipeline | whatever the source left, 0 | dashes under a pipeline-fault annunciator |
| NO_COMPUTED_DATA | no estimate of the field has ever existed | 0 | dashes |
| FAILURE_WARNING | the channel the field depends on is DEGRADED, or no altitude source contributes to the fused fields | the last accepted or coasting estimate, held | the value removed under a flag |
| NORMAL | a current estimate | the estimate | the value |

Barometric altitude depends on the barometer; pitch and roll on the IMU and a seeded filter; fused altitude and vertical speed on an altitude source the filter has taken a measurement from, barometer or GPS, so they stay NORMAL on either alone and read FAILURE_WARNING, at once and for as long as it lasts, with neither. No field ever holds NaN.

## System-Level Degradation Summary

| Sensors Available | System Capability |
|---|---|
| All NOMINAL | Full operation — every computed field NORMAL: fused altitude, attitude, position, heading |
| BMP280 DEGRADED | Fused altitude and vertical speed NORMAL on GPS alone (noisier); barometric altitude FAILURE_WARNING, held. Attitude and position normal. |
| MPU6050 DEGRADED | Pitch and roll FAILURE_WARNING, held. Altitude and position normal. |
| GPS DEGRADED | Fused altitude NORMAL on the barometer alone (drifts with changes in sea-level pressure). Position and heading invalid by status. |
| BMP280 + MPU6050 DEGRADED | Fused altitude NORMAL on GPS alone; barometric altitude and attitude FAILURE_WARNING. Position, heading, and GPS altitude from the raw fields. Heavily degraded. |
| BMP280 + GPS DEGRADED | Attitude NORMAL. Barometric altitude, fused altitude and vertical speed FAILURE_WARNING: the fused fields coast on prediction, flagged at once. Position and heading invalid. Critically degraded. |
| MPU6050 + GPS DEGRADED | Barometric and fused altitude NORMAL on the barometer alone; attitude FAILURE_WARNING; position and heading invalid. Critically degraded. |
| All DEGRADED | System logs faults, continues running (the processing loop does not crash); every computed field FAILURE_WARNING and every raw field invalid by status. |
| A channel before its first reading (NO_DATA) | The fields that depend on it NO_COMPUTED_DATA; the others as the rows above. A fault on it degrades it as from NOMINAL. |

The system never stops. A crashed telemetry process is worse than a degraded one producing partial data. This is the fundamental principle: degrade gracefully, never crash.

Freshness is separate from degradation. A NOMINAL channel contributes a measurement only on the cycles that produced a reading; between readings its filter predicts. The table above describes what a DEGRADED channel removes; REQ-PROC-006 describes what a stale cycle removes: one update, never the channel.
