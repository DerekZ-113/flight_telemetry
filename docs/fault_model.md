# Flight Telemetry System — Fault Model

> **Document ID:** FTS-FM-001
> **Version:** 0.9.0
> **Status:** Draft
> **Last Updated:** 2026-10-19

This document defines every known failure mode, how the system detects it, and how the system responds. Each entry traces to a requirement and a test.

Where a response says "log fault event", the event is a `FaultEvent` record (timestamp, channel, fault type, value) written by `BinaryLogger::log_event` into the same binary log as the telemetry frames, tagged so replay can skip it (FTS-DD-001 Section 9). The channel is a sensor channel, or VOTER for the voter's own events (FAULT-019 to FAULT-022) and, since 0.8.0, for the one lane fault the demonstration option injects (LANE_FAULT_INJECTED, REQ-RED-007), new values of the existing channel and type bytes since 0.7.0, with no format change.

Since 0.8.0 a seeded fault-injection campaign (REQ-TEST-005, FTS-FI-001) injects every entry that names a detection (FAULT-001 to FAULT-011, FAULT-019 to FAULT-022) the entry under *Uncovered Failures* (FAULT-023) and FAULT-024, uncovered until 0.9.0, and its coverage table gives each one's measured detection, latency and recovery; the ADS-B entries (FAULT-012 to FAULT-017) and FAULT-018 keep their unit tests, the campaign's source being the simulator.

Every threshold below is a key of `config/telemetry_config.yaml` under `fault` (REQ-CFG-001), except FAULT-010's, which is the full-scale range the driver configures and is derived from the driver's own constants; the ADS-B entries' (FAULT-012 to FAULT-015), whose keys sit in the optional `adsb` block (REQ-SENS-008, REQ-SENS-009, REQ-FAULT-001); and the lane entries' persistence count, three, which REQ-RED-003 states and the voter holds as a named constant (FTS-DD-001 Section 12); the values quoted are the shipped file's, validated at startup (REQ-CFG-002).

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
- **Detection:** Both reach the detector as one condition. The live source delivers a reading (`gps_read_ok` true) only on the cycle a valid fix completes, so a silent receiver and one without a fix both leave the channel without a reading, and the detector declares the fault when no fix has been read within the configured timeout (`fault.uart_timeout_ms`, 2000 ms). The timeout must exceed the receiver's 1 Hz fix period, so it is configurable rather than fixed at 500 ms, and the configuration refuses a value at or below the simulated receiver's fix period. The two causes are told apart at the receiver's exit line (`GPS:` sentences, epochs without a fix, bad checksums, read errors): sentences without fixes is (b); nothing, bad checksums, or read errors climbing (a device the kernel hung up) is (a). Since format version 3 (0.6.0) the frame carries the fix quality and the satellite count the GGA sentence reported (`gps_fix_quality`, `gps_satellites`, present when the flags byte's FIX_QUALITY bit is set; the live source copies them from the fix, the simulator writes a healthy receiver's 1 and 8, the ADS-B source leaves them absent), judged by nobody yet: a detector-side distinction with its own event (FAULT-003b) stays parked in FTS-DD-001 Section 11.2, now with its bytes in place.
- **Response:** Mark GPS channel DEGRADED. Log fault event (COMM_TIMEOUT with the elapsed ms). Continue processing with barometer + IMU only. Fused altitude relies solely on barometer and stays NORMAL. Position, heading, and ground speed are invalid by the channel's status (DEGRADED here; NO_DATA before the first fix, REQ-FAULT-006), the raw fields carrying no validity byte of their own. Before the receiver's first fix the channel is NO_DATA from the first frame and DEGRADED with this event at the timeout; the first fix after that starts the recovery of FAULT-011, five fixes at 1 Hz. With `sensors.gps.enabled` false the channel is not provided and is NOT_AVAILABLE from the first frame with no event (FAULT-018; until 0.5.0 it was NO_DATA and then DEGRADED with this event, a failure that never happened). In ADS-B mode (REQ-SENS-008) the channel carries the aircraft's position, its delivery period is the poll interval, the timeout is `adsb.timeout_ms` (20,000 ms, four polls), and the exit line that tells the causes apart is the `ADS-B:` line (FAULT-012 to FAULT-015). Between fixes (49 of every 50 cycles at 1 Hz) `gps_read_ok` is false and the last fix is not re-applied; that is normal operation, not a fault, and it is why the timeout must exceed the fix period (REQ-PROC-006).
- **Requirement:** (a) REQ-FAULT-001. (b) REQ-FAULT-003, the fix-quality limit applied at the source. Both: REQ-FAULT-004
- **Test:** (a) `tests/unit/test_fault_detection.cpp` → `FaultDetectionTest.GpsTimeoutUsesUartBound`; `tests/unit/test_live_sensor_source.cpp` → `LiveSensorSourceTest.FixIsFreshOnlyOnTheCycleItCompleted`. (b) `tests/unit/test_nmea_gps.cpp` → `NmeaGpsTest.NoFixEpochIsNotAReading` (quality 0) and `NmeaGpsTest.FixQualityGgaWithStatusVRmcIsNoFix` (status V beside a fix-quality GGA), the two halves of the fix rule at the source; the detector's own event is deferred (FAULT-003b); the bytes it will judge are in the frame since format version 3 (TC-031 step 6).

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

### FAULT-024: Non-finite reading

- **What:** A raw reading holds NaN or an infinity.
- **Cause:** A defect in a driver or a parser: arithmetic on a corrupt value, a conversion that accepts "nan" or overflows to infinity. The drivers make it remote: the NMEA parser refuses a number that is not finite (`src/drivers/nmea.cpp`), and the BMP280 and MPU6050 values are integer counts scaled by constants, so a finite count always gives a finite value.
- **Likelihood:** Remote: a software fault, not a sensor's.
- **Detection:** First on every fresh reading, ahead of the range and stuck tests: the barometer's pressure or temperature, any of the IMU's six axes, or any of a GPS fix's latitude, longitude, altitude, ground speed and course, not a finite number. The range tests alone would let a NaN through, since every comparison with NaN is false, and the rotation rates and the GPS have no range test. A GPS frame that is not a fix carries the held fix and is not judged again. The ADS-B source's vertical rate and pressure altitude are not judged: nothing consumes them until Phase B.
- **Response:** Discard the reading. OUT_OF_RANGE on the channel's frame, the event's value the non-finite one; the channel DEGRADED until M consecutive valid readings (FAULT-011). The value never reaches a filter, which gates on the status the detector wrote on the same frame (REQ-PROC-006):
  - the barometric altitude holds its last conversion under FAILURE_WARNING;
  - the attitude holds its angles under FAILURE_WARNING;
  - the Kalman filter takes no measurement from it, the fused altitude staying NORMAL while the GPS contributes (REQ-PROC-007).

  No voter event: every lane sees the same checked frame, so the lanes agree. Until 0.9.0 the detector missed it. A NaN pressure entered the Kalman state of all three lanes and left the voter no majority to the end of the run, every computed field held under FAILURE_WARNING; an infinite rotation rate was voted through as a NORMAL roll. FTS-FI-001 measures both before and after the check (cells U06 and U07, its Sections 5.6 and 7).
- **Requirement:** REQ-FAULT-003 (the non-finite clause since 0.16.0), REQ-FAULT-004
- **Test:** `tests/unit/test_fault_detection.cpp`, TC-038; the fault-injection campaign's cells U06 and U07 (TC-035, TC-036, TC-038)

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

## External Data Source Failures

The ADS-B source (REQ-SENS-008) reads one aircraft from the OpenSky Network's REST API on a thread of its own, every `adsb.poll_interval_ms` (5,000 ms). Every failure below leaves the held reading in the frame with `gps_read_ok` false, so the detector's timeout (FAULT-003's rule at `adsb.timeout_ms`) is what decides when a silence matters; the source counts each failure by kind and prints the counts on the `ADS-B:` exit line. No credential, token, request header or response body is ever printed or written (REQ-CFG-004).

### FAULT-012: ADS-B service unreachable or answering with an error

- **What:** The request does not complete, or completes with a status other than 2xx, 401 and 429.
- **Cause:** No network (the hotspot down, name resolution failing), the service down or overloaded (5xx), a request past its timeout (10 s in all, 5 s to connect), an origin given to `--adsb-origin` that is not a server.
- **Likelihood:** Medium. The service is a research network with no availability promise.
- **Detection:** The HTTP seam reports a transport error with libcurl's text, or a status the client does not handle; counted as `transport errors` and `http errors` on the exit line, with the last status and curl code.
- **Response:** The poll yields nothing and is not retried inside the interval; the held reading stays; the next poll at the regular interval. If it lasts, the GPS channel times out through FAULT-003's rule at `adsb.timeout_ms` and the display flags GS, TRK and POS; the first readings after it start FAULT-011's recovery, five polls.
- **Requirement:** REQ-SENS-008, REQ-FAULT-001, REQ-FAULT-004
- **Test:** `tests/unit/test_opensky_client.cpp` → `OpenSkyClientTest.ServerErrorsAreCountedAndTheTokenKept`, `TransportErrorIsCountedWithCurlsText`, `OtherClientErrorIsCounted`; `tests/unit/test_adsb_source.cpp` → `AdsbSourceTest.FailedPollHoldsAndCounts`

### FAULT-013: Token refused or expired

- **What:** The token endpoint refuses the client's credentials, or the states endpoint answers 401 because the bearer token expired or was revoked.
- **Cause:** A wrong or revoked client secret; a token older than its `expires_in` (30 minutes); a clock margin missed.
- **Likelihood:** Low in a session; certain over a day without the refresh.
- **Detection:** 400, 401 or 403 from the token endpoint; 401 from the states endpoint. Counted as `auth` on the exit line, the token requests beside it.
- **Response:** On 401 from the states endpoint, one refresh and one retry of the same poll; a second 401 ends the poll with nothing delivered, and the next poll fetches a token again. A token refused at startup refuses the run, naming the status and the credentials path, never the response body; a token endpoint unreachable at startup is FAULT-012 and the run starts without a reading. The token is refreshed 60 s before its expiry on the monotonic clock, so a session never meets the expiry in the ordinary course.
- **Requirement:** REQ-CFG-004, REQ-SENS-008
- **Test:** `OpenSkyClientTest.UnauthorizedGetRefreshesOnceAndRetries`, `SecondUnauthorizedIsReportedNotLooped`, `RefusedTokenIsTokenRefused`, `TokenIsReusedUntilTheMarginBeforeExpiry`; `AdsbSourceTest.StartRefusesARefusedTokenByStatus`; `tests/integration/test_adsb.py` (the scripted 401; the secret and the token absent from every file the run writes)

### FAULT-014: Credit budget exhausted

- **What:** The service answers 429: the account's daily credits are spent.
- **Cause:** Polling faster or longer than the budget allows: 4,000 credits a day for a registered account, one per call for a box under 25 square degrees, 720 an hour at the shipped interval.
- **Likelihood:** Low for attended sessions; certain for an unattended day, which the data license does not allow anyway.
- **Detection:** 429, with the `X-Rate-Limit-Retry-After-Seconds` header when the service sends it. Counted as `rate-limited`, the last Retry-After beside it.
- **Response:** The poll yields nothing; the next poll waits the header's seconds when present and the regular interval when not (the header is the service's own back-pressure; a second policy would be untested tuning); the held reading stays and the channel times out as in FAULT-012 if it lasts.
- **Requirement:** REQ-SENS-008
- **Test:** `OpenSkyClientTest.RateLimitedReadsRetryAfterHeader`, `RateLimitedWithoutTheHeaderHasNoRetryAfter`; `AdsbSourceTest.NextDelayIsTheIntervalOrTheRetryAfter`; `tests/integration/test_adsb.py` (the scripted 429)

### FAULT-015: The selected aircraft is gone

- **What:** The aircraft the source presents landed, left the box, stopped being reported, or reports an incomplete state.
- **Cause:** The approach ended; the aircraft flew out of the box; a coverage gap at the receivers; a transponder that stops sending its GNSS height or velocity.
- **Likelihood:** Certain, every few minutes on an approach.
- **Detection:** In a response: `on_ground` true (`landed`); a required field null (`incomplete`, FAULT-016); the aircraft absent from `adsb.gone_after_polls` (2) consecutive responses (`left`). A failed poll is not a response and counts nothing, so a service outage never counts an aircraft gone.
- **Response:** Reselection by REQ-SENS-009's rule: the nearest descending candidate (`nearer` when a candidate beat a kept aircraft by more than `adsb.switch_margin_m`, 2,000 m; `initial` from none) or none. Every change is one console line and one row of `<prefix>_adsb.csv` beside the log, with both identities, the reason, the distance and the timestamp of the first frame carrying the new aircraft, and a session status goes out at once so the display re-takes the plot's origin before that frame (REQ-TRANS-004, REQ-DISP-007). The first frame carrying the new aircraft is marked with the frame's new-vehicle flag and the processor restarts every filter on it (REQ-PROC-008), so the fused altitude and vertical speed are the new aircraft's from that frame rather than a climb of ±150 m/s nobody flew (FTS-VR-001 Entry 18 §18.5); the detector is not told, a new aircraft's first reading being a reading. With no candidate the fields hold with `gps_read_ok` false and the channel times out by FAULT-003's rule.
- **Requirement:** REQ-SENS-009, REQ-PROC-008, REQ-TRANS-004, REQ-DISP-007
- **Test:** `tests/unit/test_aircraft_selector.cpp` → `AircraftSelectorTest.*` (one test per reason); `AdsbSourceTest.SwitchIsRecordedWithTheFrameTimestamp`; `tests/integration/test_adsb.py` (the scripted landing and its status); the flag and the re-birth, TC-031 steps 8, 9 and 14

### FAULT-016: Incomplete state vector

- **What:** A row of `states` lacks a field a reading needs (latitude, longitude, geometric altitude, velocity, track, the time of the position) or has fewer than fourteen elements.
- **Cause:** Nulls are normal in the service's data: an aircraft without a GNSS height (an older transponder, a position from multilateration), a position older than fifteen seconds (the service nulls its time), a row cut short.
- **Likelihood:** High for some aircraft in every response.
- **Detection:** Per field, at the parser: a null or a wrongly typed field reads as absent; a short row is skipped and counted.
- **Response:** The aircraft is not a candidate and its vector is not a reading; the rest of the response is used. If it is the selected aircraft, FAULT-015 with the reason `incomplete`. Counted on the exit line.
- **Requirement:** REQ-SENS-008, REQ-SENS-009
- **Test:** `tests/unit/test_opensky_json.cpp` → `OpenSkyJsonTest.EveryNullableFieldReadsAsAbsent`, `WrongTypeInAFieldReadsAsAbsent`, `ShortOrNonArrayRowsAreSkippedAndCounted`; `AircraftSelectorTest.PicksTheNearestDescendingAircraft` (a climbing departure and a grounded aircraft nearer than the one chosen), `IncompleteStateIsReplaced`

### FAULT-017: Malformed response

- **What:** The body is not what the schema says: not JSON, `states` neither an array nor null, `time` missing, a top-level array.
- **Cause:** An error page served with a 200, a body truncated by a dropped connection or the 4 MiB cap, a schema change at the service.
- **Likelihood:** Low.
- **Detection:** The parser refuses the document by name; it is told not to throw and every field is type-checked, so no exception is involved. Counted as `malformed`.
- **Response:** The response is discarded; the held reading stays; the selector is not consulted, because a response that says nothing about the sky is not a miss; the next poll at the interval.
- **Requirement:** REQ-SENS-008
- **Test:** `OpenSkyJsonTest.MalformedJsonIsRefused`, `UnexpectedShapeIsRefused`; `OpenSkyClientTest.MalformedStatesBodyIsCounted`

---

## Start-up and validity

**NO_DATA (REQ-FAULT-006).** A channel is NO_DATA from start-up until its first successful reading, then NOMINAL without an event: a start is not a recovery. A fault on a NO_DATA channel, the communication timeout included, marks it DEGRADED as from NOMINAL, with its event. Before the first reading the raw fields are the zeros of a value-initialized frame, which is why the status says NO_DATA rather than letting a display show a position of 0, 0.

**NOT_AVAILABLE (REQ-FAULT-007).** A channel the active source does not provide is reported NOT_AVAILABLE by the source on every frame, from the first: availability is the source's fact, health the detector's judgment, and the detector leaves the fact alone. The fields that depend on such a channel stay NO_COMPUTED_DATA, the display shows a no-source flag rather than a failure flag, and a replay reproduces the status from the recorded frames.

### FAULT-018: Channel not provided by the source

- **What:** The active source has no sensor for a channel: the barometer and the IMU in ADS-B mode (the broadcast carries neither pressure nor attitude, REQ-SENS-011), the GPS receiver when `sensors.gps.enabled` is false.
- **Cause:** Configuration, not failure.
- **Likelihood:** Certain in those configurations.
- **Detection:** None needed: the source reports the channel NOT_AVAILABLE on every frame, the one status a source may assert.
- **Response:** The detector leaves the channel as reported: no timeout, no stuck or range check, no event. The processor never consumes it, so the attitude and the barometric altitude stay NO_COMPUTED_DATA in ADS-B mode and the fused fields rest on the GPS channel alone (REQ-PROC-003's birth from either source). The display shows the no-source flag, outlined and not red, with no cause in the strip (FTS-DS-001 §2.1). Until 0.5.0 a disabled receiver was reported NO_DATA and then DEGRADED at 2 s with a COMM_TIMEOUT in every log.
- **Requirement:** REQ-FAULT-007, REQ-SENS-011, REQ-DISP-001
- **Test:** `tests/unit/test_fault_detection.cpp` → `FaultDetectionTest.NotAvailableChannelIsLeftAloneWithNoEvent`; `tests/unit/test_live_sensor_source.cpp` → `LiveSensorSourceTest.GpsWithoutAReceiverIsNotAvailable`; `tests/unit/test_replay.cpp` → `ReplayTest.NotAvailableSurvivesTheStrip`; `tests/unit/test_validity.cpp` → `ProcessorValidityTest.NotAvailableChannelNeverContributes`; `tests/unit/test_adsb_source.cpp` → `AdsbSourceTest.FrameIsRawOnlyWithTheAbsentChannelsNotAvailable`

**Validity (REQ-PROC-007).** Every computed field carries one byte in the frame, written by the processor on every frame in this order of precedence:

| Validity | Meaning | Value in the field | Display (FTS-DD-001 Section 11.2) |
|---|---|---|---|
| NOT_JUDGED | nothing judged the field: a raw frame from a source, a version-1 log; never emitted by the pipeline | whatever the source left, 0 | dashes under a pipeline-fault annunciator |
| NO_COMPUTED_DATA | no estimate of the field has ever existed | 0 | dashes |
| FAILURE_WARNING | the channel the field depends on is DEGRADED, or no altitude source contributes to the fused fields | the last accepted or coasting estimate, held | the value removed under a flag |
| NORMAL | a current estimate | the estimate | the value |

Barometric altitude depends on the barometer; pitch and roll on the IMU and a seeded filter; fused altitude and vertical speed on an altitude source the filter has taken a measurement from, barometer or GPS, so they stay NORMAL on either alone and read FAILURE_WARNING, at once and for as long as it lasts, with neither. A channel reported NOT_AVAILABLE is not NOMINAL, so it contributes nothing and the fields that depend on it stay NO_COMPUTED_DATA. No field ever holds NaN.

---

## Processing Lane Failures

Since 0.7.0 every checked frame is processed by three identical lanes and a voter chooses the frame the system emits (REQ-RED-001 to REQ-RED-006, FTS-DD-001 Section 13). Identical code on identical input agrees bit for bit, so every entry below is a defect, an upset confined to one lane, or an injection through REQ-RED-005's interface; none is a sensor fault, which every lane sees alike and the detector reports once (the entries above). A single fault is assumed: the voter masks one wrong lane on every frame and isolates it, and a second fault leaves no majority. The voter's events carry the channel VOTER; their value is the lane's number (1 to 3), or for FAULT-021 the number of lanes still in the vote.

### FAULT-019: Transient lane disagreement

- **What:** One lane's output differs from the majority in a computed field, a validity or a channel status on one or two consecutive cycles that have a majority, and agrees again.
- **Cause:** Nondeterminism in the processor (undefined behaviour, a read of uninitialized memory), an upset confined to one lane's output for one cycle, or an injection.
- **Likelihood:** Remote. None occurred over 10,000 simulated frames by test (TC-034).
- **Detection:** `lanes_agree`, bit for bit, against the representative of the majority (REQ-RED-002); a NaN agrees with nothing.
- **Response:** The lane is outvoted, so the output is the majority's and unchanged by it. A LANE_DISAGREEMENT event (value: the lane) is logged when the lane first falls outside the majority; its count resets on the next cycle it agrees. No bit of the voter status marks it, and the display does not show it (REQ-DISP-008): the log and the console carry it.
- **Requirement:** REQ-RED-002, REQ-RED-003
- **Test:** `tests/unit/test_voter.cpp` → `VoterTest.OneBadLaneIsOutvotedInEveryField`, `VoterTest.TransientDisagreementResetsTheCount`; `tests/unit/test_redundant_processor.cpp` → `RedundantProcessorTest.PerturbedLaneOutputIsIsolated`

### FAULT-020: Persistent lane fault

- **What:** One lane outside the majority on 3 consecutive cycles that have a majority.
- **Cause:** Damage to a lane's state that its filters carry forward (an upset in its input or its memory), a persistent defect in one lane, or an injection.
- **Likelihood:** Remote.
- **Detection:** The lane's persistence count, which advances on each majority cycle the lane is outside the majority and resets on each it is inside (REQ-RED-003); cycles with no majority leave it unchanged.
- **Response:** A LANE_FAULTED event (value: the lane) on the third cycle. The lane is excluded from every later vote until restart, and every later frame carries its LANE_k_FAULTED bit. Processing continues on the remaining two lanes with full capability (REQ-RED-004 (a)). The excluded lane still runs every cycle, so the cycle's work stays the same; its output is ignored.
- **Requirement:** REQ-RED-003, REQ-RED-004
- **Test:** `VoterTest.ThreeConsecutiveDisagreementsFaultTheLane`, `VoterTest.FaultedLaneStaysOutUntilRestart`, `VoterTest.PersistenceIsAParameter`; `RedundantProcessorTest.PerturbedLaneInputDivergesOnlyThatLane`

### FAULT-021: No majority

- **What:** No two lanes in the vote agree: the remaining two disagree after an exclusion, or every pair of three disagrees.
- **Cause:** A second fault after a lane was excluded, two simultaneous faults in different fields, or a NaN every lane shares.
- **Likelihood:** Extremely remote with identical lanes (it needs two faults).
- **Detection:** No eligible pair of lanes agrees (REQ-RED-002).
- **Response:** The frame carries NO_MAJORITY. Each computed field holds its last voted value under FAILURE_WARNING, or 0 under NO_COMPUTED_DATA where nothing was voted (REQ-RED-004 (b)); the channel statuses stay the detector's, so the raw instruments are unaffected. One NO_MAJORITY event (value: the lanes still in the vote) when the condition begins. No lane's count moves while it lasts, so no lane is excluded without a majority and at least two lanes always remain in the vote. The loop continues; the display shows the VOTE failure flag (REQ-DISP-008) beside the instruments' own flags.
- **Requirement:** REQ-RED-004, REQ-PROC-007
- **Test:** `VoterTest.TwoDisagreeingLanesHaveNoMajority`, `VoterTest.NoMajorityAmongThreeLanes`, `VoterTest.NoMajorityHoldsTheCounters`, `VoterTest.NeverBelowTwoLanes`, `VoterTest.TwoNaNLanesNeverFormAMajority`

### FAULT-022: Lane timing overrun

- **What:** A lane hands the voter an output for another frame than the one it was given: its timestamp or its sequence number differs from the checked frame's.
- **Cause:** A defect that returns a cached output, or an injection modelling a lane computer that did not finish in time and left its previous output. In the processing loop's one thread the lanes run one after another and cannot be late in time; a cycle that overruns as a whole is the scheduler's (REQ-TIME-001, REQ-TIME-003: re-phased, counted in the jitter file, never in the log).
- **Likelihood:** Remote.
- **Detection:** The output's `timestamp_ms` and `sequence` against the checked frame's (REQ-RED-006); both, because frames of a version-1 or version-2 log all carry sequence 0.
- **Response:** The lane takes no part in that cycle's vote, so two stale lanes can never outvote a current one. On a cycle with a majority it counts as outside it toward FAULT-020's three, and its onset event is LANE_OVERRUN (value: the lane) when staleness is that cycle's reason.
- **Requirement:** REQ-RED-006, REQ-RED-003
- **Test:** `VoterTest.StaleLaneIsAnOverrun`, `VoterTest.TwoStaleLanesNeverOutvoteACurrentOne`, `VoterTest.StaleThenDisagreeingCountsOnce`

## Uncovered Failures

Since 0.8.0 the fault model lists the failure modes that no check catches. They are here because the fault-injection campaign (REQ-TEST-005) injects them, and because a failure mode left out of the fault model reads as covered; each has a measured consequence instead of a detection (FTS-FI-001). FAULT-024, a non-finite reading, stood here too until 0.9.0, when REQ-FAULT-003's amendment gave the detector a check; it is under *Out-of-Range Failures* now.

### FAULT-023: Reading in error inside the limits (a bias step, a drift, a GPS fix that stops changing)

- **What:** A sensor reports a value offset from the truth by a constant (a bias step) or by an amount that grows (a drift), every reading inside its limits and, for the barometer and IMU, still varying as a live reading does; or the GPS keeps delivering the same fix.
- **Cause:** A calibration shift, temperature drift, a damaged sensing element, a partly blocked static port; for the GPS, receiver firmware repeating its last solution.
- **Likelihood:** Medium over a sensor's life for a bias or a drift; low for a repeated fix.
- **Detection:** None. Each check compares a channel with fixed limits (FAULT-008 to FAULT-010), with its own previous value (FAULT-005 to FAULT-007, which the GPS does not have, FTS-DD-001 Section 12), or with the clock (FAULT-001 to FAULT-003); an error inside the limits that keeps the readings changing passes all three. Detecting it needs a reference: another sensor of the same quantity, the barometric altitude against the GPS altitude, or a model of the vehicle, an analytical-redundancy check parked in FTS-DD-001 Section 12.
- **Response:** None. The error reaches the computed fields with validity NORMAL: a barometric bias of b hPa moves the barometric and fused altitude by about 8.3 b m; a drift shows as a false vertical speed; an accelerometer bias tilts the attitude by atan(b/g). FTS-FI-001 measures how far (campaign cells U01 to U05).
- **Requirement:** None: no requirement asks for this detection. The entry exists so that the fault model lists what the campaign shows nothing catches.
- **Test:** `tests/fault_injection/` cells U01 to U05 (TC-035, TC-036): no declaration, and the error against the run's clean twin recorded.

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
| A channel the source does not provide (NOT_AVAILABLE) | The fields that depend on it NO_COMPUTED_DATA, with no event ever; the others as the rows above. |
| ADS-B mode | Barometer and IMU NOT_AVAILABLE; attitude and barometric altitude NO_COMPUTED_DATA; fused altitude and vertical speed on the GPS channel alone, stepping at each poll; position and track from the aircraft, flagged when it is gone (FAULT-015) for longer than the timeout. |

The processing lanes degrade apart from the sensors (the rows above are unchanged by them):

| Lanes | System Capability |
|---|---|
| Three in the vote | Full operation; one wrong lane is outvoted on every frame (FAULT-019) |
| One excluded (FAULT-020) | Full operation on the remaining two; the frame names the excluded lane, the display shows it as a caution |
| No majority (FAULT-021) | Every computed field FAILURE_WARNING over its last voted value (NO_COMPUTED_DATA where nothing was voted); channel statuses and raw instruments as the rows above; the VOTE failure flag; the loop continues |

The system never stops. A crashed telemetry process is worse than a degraded one producing partial data. This is the fundamental principle: degrade gracefully, never crash.

Freshness is separate from degradation. A NOMINAL channel contributes a measurement only on the cycles that produced a reading; between readings its filter predicts. The table above describes what a DEGRADED channel removes; REQ-PROC-006 describes what a stale cycle removes: one update, never the channel.
