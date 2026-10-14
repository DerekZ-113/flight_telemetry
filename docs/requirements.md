# Flight Telemetry System — Software Requirements

> **Document ID:** FTS-SRD-001
> **Version:** 0.7.0
> **Status:** Draft
> **Last Updated:** 2026-10-13
> **Related:** FTS-TM-001 (traceability), FTS-DD-001 (design), FTS-TP-001 (test plan), FTS-FM-001 (fault model), FTS-VR-001 (verification results), FTS-NP-001 (noise profile)

## Conventions

"Shall" marks a binding requirement. Every shall statement has a row in FTS-TM-001 that traces it to the code that implements it and the test that verifies it. Text after **Rationale:** explains a requirement and is not itself binding.

A *channel* is one sensor's data path: barometer (BMP280), IMU (MPU6050), or GPS (NEO-6M). Each channel is NO_DATA before its first reading and NOMINAL or DEGRADED thereafter (REQ-FAULT-004, REQ-FAULT-006). Each computed field of a frame carries a *validity*: NO_COMPUTED_DATA, FAILURE_WARNING or NORMAL (REQ-PROC-007). A *cycle* is one pass of the processing loop (REQ-TIME-001). A *TelemetryFrame* is the record the system produces once per cycle; its field names and units are defined in `src/telemetry_frame.h`. *Configurable* means set through the configuration file of REQ-CFG-001.

---

## Sensor Data Acquisition

REQ-SENS-001: The system shall read barometric pressure from the BMP280 sensor via I2C at a configurable sample rate between 1 Hz and 50 Hz.

> **Rationale:** the chip converts continuously at a rate fixed by its oversampling and standby settings (roughly 80 Hz as driven) and the system reads it once per cycle, so its sample rate is the processing rate of REQ-TIME-001: the 1 Hz to 50 Hz range is the range of `timing.period_ms` under REQ-CFG-001, 1000 ms to 20 ms. A period shorter than the chip's conversion would read repeated conversions and trip the stuck-value fault.

REQ-SENS-002: The system shall read temperature from the BMP280 sensor via I2C with each pressure reading.

> **Rationale:** the BMP280 compensates pressure using its own temperature measurement, so the two are read together and an invalid temperature invalidates the pressure (FAULT-009).

REQ-SENS-003: The system shall read 3-axis accelerometer data from the MPU6050 sensor via I2C at a configurable IMU sample rate between 4 Hz and 100 Hz.

> **Rationale:** the MPU6050 derives its sample rate from a 1 kHz base through an 8-bit divider, 1 kHz / (1 + n), so the slowest rate the chip can produce is 3.9 Hz; a requirement must not promise a rate the hardware cannot deliver. The upper bound is the loop's need, not the chip's limit. REQ-SENS-004 inherits the range through "the IMU sample rate configured under REQ-SENS-003".

REQ-SENS-004: The system shall read 3-axis gyroscope data from the MPU6050 sensor via I2C at the IMU sample rate configured under REQ-SENS-003.

> **Rationale:** the MPU6050 has a single sample-rate divider for both sensors; accelerometer and gyroscope rates cannot be set independently.

REQ-SENS-005: The system shall read position (latitude, longitude, altitude), ground speed, and course over ground (heading) from the NEO-6M GPS module via UART as NMEA sentences.

REQ-SENS-006: The system shall provide a simulated data source that produces TelemetryFrames of raw sensor readings with configurable noise, delivering each channel's readings at a configurable rate and marking the frames between readings as not fresh (`read_ok` false).

> **Rationale:** the pipeline can be developed and tested without hardware, so the simulator must model the one timing fact that changes the pipeline's behavior: a GPS that fixes once per second while the barometer and IMU are read every cycle. The GPS rate and the noise are keys of REQ-CFG-001 (`simulation.gps_interval_ms`, `simulation.noise.*`); the barometer and IMU are delivered every cycle at the processing rate. Replay of recorded sessions is a separate function (REQ-LOG-003).

## Data Processing

REQ-PROC-001: The system shall convert barometric pressure to altitude using the ISA hypsometric formula, referenced to a configurable sea-level pressure.

REQ-PROC-002: The system shall compute pitch and roll angles from accelerometer and gyroscope data using a complementary filter. When the interval since the last IMU reading the filter accepted exceeds the IMU communication timeout of REQ-FAULT-001, the filter shall re-initialize pitch and roll from the accelerometer on the next accepted reading instead of integrating the gyroscope across the gap.

> **Rationale:** one rate sample says nothing about the rotation during an outage; multiplied by the outage it fabricates tilt. An uncalibrated MPU6050 gyroscope offset is several degrees per second, so a 10 s outage would show tens of degrees of false tilt on recovery, which the accelerometer term would take about a time constant to undo. The accelerometer knows the tilt at recovery; re-seeding from it costs nothing when the vehicle is steady and is no worse than the first frame after boot when it is not. Gaps up to the timeout are integrated (REQ-PROC-006): a few missed reads are rotation to account for, not an outage.

REQ-PROC-003: The system shall fuse barometric altitude and GPS altitude using a 1D (single spatial axis) Kalman filter with two states, altitude and vertical velocity.

REQ-PROC-004: The Kalman filter's measurement noise variances shall be derived from sensor noise measured on stationary sensors and recorded in docs/noise_profile.md (FTS-NP-001), and the initial altitude variance shall equal the barometric measurement variance. The process noise variance and the initial velocity variance shall be recorded in the same document with the rationale for their values.

> **Rationale:** a stationary capture shows a sensor's own noise, which is what a measurement variance is. Process noise describes the vehicle, not a sensor: it is the acceleration the constant-velocity model ignores, which no bench can show and which sets how quickly the filter follows a real climb. The initial velocity variance is an assumption about the vehicle's state at startup. Both are tuning choices, so the document records why they hold the values they do. The GPS variance stays the receiver's datasheet figure until REQ-SENS-005 delivers a GPS to measure. Until 0.5.0 this requirement asked for all five to be measured, which no capture could satisfy.

REQ-PROC-005: Once per processing cycle (REQ-TIME-001), the system shall produce a TelemetryFrame containing the raw sensor readings, the computed and fused values, and the status of every channel.

REQ-PROC-006: The processing pipeline shall run each filter's prediction step every cycle, and shall apply a channel's reading as a measurement update only on a cycle in which that channel is NOMINAL and the reading is fresh (`read_ok` true). A reading shall be used as a measurement at most once.

> **Rationale:** sensors run at their own rates; the loop runs at its own. A channel's health (REQ-FAULT-004) says whether its readings can be trusted over time; `read_ok` says whether this cycle produced one. Re-applying a reading treats a copy as new evidence: the filter's covariance shrinks as if independent sensors had agreed, and the state is pinned to a value that is no longer current. With a 1 Hz GPS in a 50 Hz loop, one fix applied 50 times carries the information weight of a 0.7 m sensor instead of the 5 m sensor it is.

REQ-PROC-007: Every computed field of a TelemetryFrame (barometric altitude, pitch, roll, fused altitude, vertical speed) shall carry a validity in the same frame, written by the processing pipeline on every cycle, in this order of precedence: NO_COMPUTED_DATA while no estimate of the field has ever existed; FAILURE_WARNING while the channel the field depends on is DEGRADED or, for the fused fields, while no contributing altitude source is NOMINAL; NORMAL otherwise. A consumer shall treat any validity other than NORMAL as the absence of a value.

> **Rationale:** a value and its validity must travel together: ARINC 429 carries the sign/status matrix in the same word as the data so that no receiver can take one without the other, and this frame carries one validity byte per computed field in the same 104 bytes. The dependences are the fault model's: barometric altitude on the barometer; the fused fields on an altitude source the filter has taken a measurement from, barometer or GPS (FAULT-001); pitch and roll on the IMU and a seeded filter (FAULT-002, FAULT-006, FAULT-010). Precedence puts "never existed" first, so a barometer dead or out of range from power-on reports NO_COMPUTED_DATA rather than a flag over a number nobody measured. A field that is not NORMAL still holds a defined number, 0 before its first estimate and the last accepted or coasting estimate under FAILURE_WARNING, never NaN: NaN is silent in arithmetic and false in every comparison, so a consumer that forgot the flag would fail quietly. Zero in the encoding means "not judged" and is never written by the pipeline, so a frame nobody judged (a source's raw frame, a version-1 log) cannot pass as valid.

## Fault Detection

REQ-FAULT-001: The system shall declare a communication fault on an I2C channel when no successful read has occurred within a configurable timeout (500 ms by default), and on the GPS channel when no NMEA sentence has been received within a configurable timeout longer than the module's output period.

REQ-FAULT-002: The system shall detect a stuck sensor condition: N consecutive identical readings on any channel, or on any single axis of a channel, where N is configurable.

REQ-FAULT-003: The system shall detect out-of-range or invalid sensor readings against configurable limits. Default limits: pressure 300 to 1100 hPa; temperature −40 to 85 °C; accelerometer within its configured full-scale range (±2 g at power-on); GPS fix quality greater than 0.

REQ-FAULT-004: Upon detecting a sensor fault, the system shall mark the affected channel DEGRADED, log a fault event carrying the timestamp, channel, and fault type, and continue processing with the remaining NOMINAL channels.

> **Rationale:** DEGRADED is the detector's judgment over time, distinct from `read_ok`, which is one cycle's fact. A channel that has never produced a reading is NO_DATA (REQ-FAULT-006), not NOMINAL: the raw fields it carries are not readings.

REQ-FAULT-005: Upon fault clearance (M consecutive valid readings on the affected channel, where M is configurable), the system shall mark the channel NOMINAL and log a recovery event.

REQ-FAULT-006: Each channel shall be reported NO_DATA from start-up until its first successful reading, and NOMINAL or DEGRADED thereafter as REQ-FAULT-004 and REQ-FAULT-005 decide; a fault detected on a NO_DATA channel, including the communication timeout of REQ-FAULT-001, shall mark it DEGRADED as from NOMINAL.

> **Rationale:** before its first reading a channel's raw fields are the zeros of a value-initialized frame (a position of 0, 0 is a real place, a heading of 0 is north, a pressure of 0 is 44 km up) with `read_ok` false and, from a source, status NOMINAL, which a display would show as a plausible value. NO_DATA is the raw-field twin of NO_COMPUTED_DATA (REQ-PROC-007). The first reading makes the channel NOMINAL without an event: it is a start, not a recovery.

## Timing

REQ-TIME-001: The main processing loop shall execute at a configurable fixed rate using absolute-time scheduling: on the target platform, clock_nanosleep with TIMER_ABSTIME.

REQ-TIME-002: The system shall measure and log, for every cycle, the timing jitter: the difference between the cycle's scheduled start time and its actual start time.

REQ-TIME-003: The system shall sustain the configured processing rate, with no missed cycles and a maximum per-cycle jitter below 1 ms, under normal operating conditions.

> **Rationale:** normal operating conditions means the target hardware with the standard configuration and no competing load, as exercised by TC-009 step 7 of FTS-TP-001.

## Transport

REQ-TRANS-001: The system shall publish every processed TelemetryFrame over ZeroMQ using the publish/subscribe pattern for local inter-process communication.

> **Rationale:** *publish* means hand the frame to the transport. Delivery to every subscriber is not required: pub/sub has no acknowledgement, a subscriber that joins late misses what was sent before it joined, and a subscriber that falls behind by more than the high-water mark loses frames rather than slowing the publisher (REQ-TRANS-003). The binary log (REQ-LOG-001), not the wire, is the record of a run.

REQ-TRANS-002: The system shall publish every processed TelemetryFrame over UDP to a configurable destination address and port.

> **Rationale:** the UDP transport is the network analog of a telemetry downlink: one datagram per frame, laid out as the same one-record log message the ZeroMQ transport sends (FTS-DD-001 Section 9), to one unicast destination, with no acknowledgement and no retransmission, because a frame resent after the next one is due is stale. Loss and reordering are measured at the receiver from frame timestamps; the sender counts only what the kernel refuses (REQ-TRANS-003). The destination is `transport.udp.host` and `transport.udp.port` in the configuration file of REQ-CFG-001; `--udp <ipv4>:<port>` enables the transport for one run and overrides them. The binary log, not the wire, is the record of a run.

REQ-TRANS-003: A transport failure shall not block the processing loop or cause it to miss a cycle deadline (REQ-TIME-001).

> **Rationale:** a transport failure includes no subscriber connected, a subscriber too slow to keep up, an endpoint that cannot be bound at startup, and a send that returns an error. Each is reported (counted, printed at exit, or refused at startup) and none is waited on inside the loop. For the UDP transport the same rule reads: a destination that cannot be routed or is not a unicast address is refused at startup; a send the kernel refuses, whether its buffer is full or it is reporting the port-unreachable it learned from an earlier datagram, is counted and never retried.

## Logging and Replay

REQ-LOG-001: The system shall log every processed TelemetryFrame to a binary file on local storage.

REQ-LOG-002: The system shall rotate to a new log file when the current file would exceed a configurable maximum size.

REQ-LOG-003: When replaying a recorded binary log, the system shall produce processing outputs identical, bit for bit, to those of the original session recorded by the same build on the same platform.

> **Rationale:** cross-platform identity is not claimed, because standard-library math and random-number implementations differ between toolchains (FTS-DD-001 Section 8). Identity also presumes the configuration recorded with the session (REQ-CFG-003): a replay through another configuration is reported as such and claims nothing. Identity is claimed within one format version (REQ-LOG-005): a version-1 log replayed by this build yields a version-2 log whose raw fields equal the input's and whose computed fields are recomputed, which is replay, not identity.

REQ-LOG-004: Replay mode shall use the same processing pipeline as live mode, differing only in data source.

REQ-LOG-005: The log header shall carry the format version. A reader shall accept every version whose layout this build defines and shall refuse any other before any frame is processed, naming the version found and the versions it reads.

> **Rationale:** logs must replay forever. The raw fields are the record and the pipeline recomputes everything else (REQ-LOG-004), so a version-1 log replays through the version-2 build and the replay is a version-2 log; a log the build cannot read must stop the run at startup by name, not feed zero frames through the pipeline (FTS-VR-001 Entry 11 §11.8). Bit identity (REQ-LOG-003) is claimed within one format version.

## Configuration

REQ-CFG-001: The following system parameters shall be configurable through a YAML configuration file: sensor sample rates and the processing rate; fault detection timeouts, counts, and limits; filter tuning (Kalman covariances, complementary filter gain, and the sea-level reference pressure); transport addresses and ports; log file paths and maximum file size; and simulated-source noise.

> **Rationale:** the file (`config/telemetry_config.yaml`, or the path given by `--config`) is the standing configuration and the only source of the values the binary runs with; there is no built-in fallback, so a missing file is refused at startup. `--live`, `--replay` and `--cycles` choose the run; `--zmq` and `--udp` enable a transport for one run and override its address, the only place a flag and a key overlap. Every key is required, and an unknown or duplicate key is an error, so a misspelled key cannot silently leave a value at some other setting. The defaults compiled into the module structs are the unit tests' baseline, never the binary's fallback; a test holds the shipped file equal to them.

REQ-CFG-002: At startup, the system shall validate every configuration value and report an error naming each invalid or out-of-range parameter.

> **Rationale:** *each* is binding: one pass over the file collects every problem and the report names them all, so the operator corrects the file once. Validation happens before any object is built and never again: the loop reads values, not the file (FTS-DD-001 Section 3).

REQ-CFG-003: The system shall record the configuration in effect beside each session's log, and replay mode shall use that recorded configuration unless another file is named explicitly, reporting which configuration it used.

> **Rationale:** the computed fields are a function of the raw frames, the code, and the configuration. The log header pins the code's layout (REQ-LOG-001) and the frames are the data; nothing pinned the configuration, so a changed filter constant could break the identity of REQ-LOG-003 silently. The record is the file's text copied verbatim beside the log (`<prefix>_config.yaml`), so a replay reads exactly what the session read. Replaying a session through a different configuration is a legitimate use (FTS-NP-001 chooses the initial velocity variance that way), so it is reported, not refused. Rejected placements: a hash in the log header's reserved field (the field's job is to be zero, and a one-record wire message would stop being a byte-exact log file), a configuration record in the stream (a format version bump), and operator discipline (no evidence).

## Testing

REQ-TEST-001: Every C++ module (drivers, processing, timing, logging, replay, transport) shall have unit tests written with Google Test.

REQ-TEST-002: The full data pipeline (C++ processing, transport, Python receiver) shall have integration tests written with pytest.

REQ-TEST-003: Structural coverage (statement and branch) shall be measured with gcov and lcov on every CI run, reported, and shall meet the thresholds defined in FTS-TP-001 Section 6.3.

REQ-TEST-004: cppcheck shall run on every build, and any release-tagged build shall have zero findings at the severity levels defined in FTS-TP-001.
