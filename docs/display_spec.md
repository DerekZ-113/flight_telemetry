# Flight Telemetry System — Display Specification

> **Document ID:** FTS-DS-001
> **Version:** 0.3.0
> **Status:** Draft
> **Last Updated:** 2026-10-15
> **Related:** FTS-SRD-001 v0.13.0 (REQ-DISP-001 to REQ-DISP-007, REQ-LOG-007, REQ-SENS-007, REQ-SENS-011, REQ-FAULT-007), FTS-DD-001 (design, Sections 7, 9, 11.2 and 12), FTS-TP-001 (test plan, TC-022 to TC-026, TC-030, TC-031), FTS-TM-001 (traceability), FTS-VR-001 (verification results)

## 1. Purpose and Scope

This document specifies the Primary Flight Display built in `dashboard/` (REQ-DISP-001 to REQ-DISP-006): for each instrument, its source field by name, the rule that decides whether it shows a value, its units and the constant that converts them, its range, its scale, its rounding, how it updates, and what it shows under every flag. It binds the page's code (`dashboard/src/pfd/`) and its tests: a number in a test that disagrees with this document is a defect in one or the other. The frame's fields and units are defined in `src/telemetry_frame.h`; the JSON that carries them to the page is `pipeline/bridge.py`'s (FTS-DD-001 Section 9); the feed's states come from `dashboard/src/useTelemetryFeed.ts`.

The display is a ground-station view of telemetry, not a certified cockpit display. It follows cockpit conventions where they are the right thing to follow (a flag replaces a value; the aircraft symbol is fixed and the horizon moves; red warns, amber cautions) and says so where it departs from them (a ground track plot in metres; a track indicator where a heading indicator would stand).

## 2. Conventions

### 2.1 Values, dashes and flags (REQ-DISP-001)

| State of the source | The instrument shows |
|---|---|
| computed field NORMAL; channel NOMINAL | the value |
| NO_COMPUTED_DATA; channel NO_DATA | dashes (`---`) in white and no flag: nothing has failed, nothing is known yet |
| NOT_JUDGED | dashes, and the amber PIPELINE annunciator in the strip: the pipeline never judged the field (a source's raw frame, a version-1 log) |
| FAILURE_WARNING; channel DEGRADED | the value removed; a red flag with the instrument's name over the instrument's centre |
| the feed stale, lagging or disconnected (Section 5) | every value removed; a red flag with each instrument's name; the strip names the cause |
| the stream ended (Section 5) | every value removed; a grey flag with each instrument's name; the strip reads ENDED |
| channel NOT_AVAILABLE (REQ-FAULT-007): the source has no sensor for it | the value removed; a no-source flag with the instrument's name, a white outline on no fill, and no cause in the strip: nothing failed, and nothing will come |

An instrument driven by more than one field (the attitude indicator: pitch and roll) shows the most severe state of its fields: the failure flag, then dashes with PIPELINE, then the no-source flag, then dashes, then the value. The feed's state takes precedence over every field's. A removed value is never shown struck through, dimmed or greyed on an instrument; that rendering is the diagnostic view's (Section 8). The rule is one function, `instrumentState` in `dashboard/src/pfd/instrumentState.ts`, and every instrument calls it.

### 2.2 Colours

| Colour | Meaning | Value |
|---|---|---|
| red | a value withdrawn because it cannot be trusted: FAILURE_WARNING, DEGRADED, STALE, LAG, BRIDGE DISCONNECTED | `#d0342c`, white text |
| grey box | a value withdrawn because the stream ended; nothing failed | `#5a6570`, white text |
| amber | a caution about the system: PIPELINE, MODE UNKNOWN, and the SIM and REPLAY labels, boxed | `#f0a830` |
| white | information and normal values; the LIVE and ADS-B labels, plain, and the aircraft's identity; the no-source flag, an outline on no fill | `#e8edf2` |
| sky, ground | the attitude ball's halves | `#2a6fd6`, `#8a5a2b` |
| yellow | the fixed aircraft symbol | `#ffcc33` |
| panel | tapes, boxes and the instrument background | `#1e2530` on `#0f1318` |

The convention is the cockpit's: red for a warning, amber for a caution, white for an advisory. The diagnostic view below the instruments keeps its own palette (Section 8).

### 2.3 Signs (REQ-DISP-002)

`pitch_deg` positive is nose up; `roll_deg` positive is a right roll, the right wing down. These are the bench test's signs (FTS-TP-001 TC-014 step 13). FTS-DD-001 Section 6 records that the driver's axis mapping is the assumption the first hardware day settles, and that the display's rule does not move with it: if the bench shows a sign reversed, the fix is the driver's mapping. On the attitude indicator a nose-up pitch moves the horizon down and a right roll rotates the horizon counterclockwise about the fixed symbol (Section 4.1). The simulator's scripted flight (Section 7) generates its readings as the inverse of the pipeline's own formulas, so a scripted right roll is a right roll on this display by construction.

### 2.4 Units (REQ-DISP-002)

The frame is SI (`src/telemetry_frame.h`). The display converts in `dashboard/src/pfd/units.ts` and nowhere else; the feed's state holds the bridge's numbers untouched, which a test asserts.

| Quantity | Frame | Display | Constant |
|---|---|---|---|
| altitude | m | ft | 1 / 0.3048 = 3.280 839 895 ft per m |
| vertical speed | m/s | ft/min | 60 / 0.3048 = 196.850 393 7 ft/min per m/s |
| ground speed | m/s | kn (shown as `kt`) | 3600 / 1852 = 1.943 844 492 kn per m/s |
| pitch, roll, track | degrees | degrees | 1 |
| relative position | m | m, with km on the scale bar above 1,000 m | 1 |

### 2.5 Rounding at the readouts

Altitude to 10 ft; vertical speed to 50 ft/min, signed; ground speed to 1 kn; track to 1°, 0 to 359 (360 reads 0). The tapes, the pointer and the horizon move continuously, from the unrounded value.

## 3. Layout

One SVG, `viewBox="0 0 800 520"`, scaled to its container; the coordinates below are viewBox units, so tests never depend on a window. Beside it, the ground track plot, its own SVG with `viewBox="0 0 300 300"` (Section 6). Below both, the diagnostic view (Section 8).

```
y   0..36    the strip:  [mode label · identity]   [feed state and its cause]   [rate · lag]
y 100..420   [GS box x 40..150]   [attitude indicator: centre (400,260), r 160]   [altitude tape x 600..690]  [VS x 710..760]
y 448..520   the track indicator: the top of a rose arc, centre (400,640), r 180, under a lubber line at (400,462); the readout box at y 434
```

The mode label sits inside the SVG, at the strip's left, so that no crop of the instruments can lose it (REQ-DISP-004); the page's header bar repeats it above the panel for the diagnostic view. In ADS-B mode the aircraft's identity follows the label after a middle dot, and in a REPLAY of an ADS-B recording `ID UNAVAILABLE` does (Section 5; the recording's source mode comes from the session status, REQ-LOG-007). Each component module under `dashboard/src/pfd/` exports a component only (React's Fast Refresh keeps the feed's state across an edit only then); the geometry lives in `geometry.ts` as pure functions with their own tests.

## 4. Instruments

Each subsection gives the source, the rule, units and range, geometry, update behaviour and flags. The flag's text is the instrument's name; the strip names the cause.

### 4.1 Attitude indicator (ATT)

- **Source:** `pitch_deg` with `pitch_validity`; `roll_deg` with `roll_validity` (REQ-PROC-002, REQ-PROC-007). The two validities are equal today (FTS-DD-001 Section 7); the instrument takes the worse of the two regardless. Since 0.2.0 the IMU channel's `imu_status` enters for its availability only (REQ-FAULT-007): NOT_AVAILABLE shows the no-source flag, while a DEGRADED IMU before the first seed keeps NO_COMPUTED_DATA's rendering, as the validity's precedence says.
- **Rule:** Section 2.1 over both fields.
- **Geometry:** centre (400, 260), radius 160, clipped to the circle. The horizon group carries the sky half (above the horizon), the ground half (below), the horizon line and the pitch ladder, with `transform="rotate(-roll 400 260) translate(0 pitch·4)"`: the translation first, along the ball's own vertical at 4 px per degree, positive (down) for nose up; then the rotation about the centre, and since SVG's positive rotation is clockwise, a right roll (positive) turns the horizon counterclockwise. The pitch ladder: long lines with labels at ±10°, ±20° and ±30° (y = 260 ∓ 40, 80, 120 inside the group) and short lines at ±5°, ±15° and ±25°. The roll scale is fixed to the case at the top: ticks at 0, ±10°, ±20°, ±30°, ±45° and ±60°; a pointer in a group rotated by −roll points at it (a sky pointer). The aircraft symbol, a centre dot and two wing bars in yellow, is fixed.
- **Range:** ±40° of pitch visible at this scale; a larger pitch moves the horizon out of the clip, which is the right picture. Any roll.
- **Update:** every frame.
- **Flags:** the ball becomes a grey disc with the symbol; the ladder and the roll pointer are removed; the flag `ATT` sits at the centre, red, or grey when ended. Dashes have no place on a ball, so NO_COMPUTED_DATA and NOT_JUDGED show the grey disc with no flag (NOT_JUDGED also lights PIPELINE in the strip). A NOT_AVAILABLE IMU, ADS-B mode's, shows the grey disc with the symbol and the no-source flag `ATT`, outlined.

### 4.2 Altitude tape (ALT)

- **Source:** `fused_altitude_m` with `fused_altitude_validity` (REQ-PROC-003, REQ-PROC-007).
- **Units:** ft (Section 2.4); the readout rounded to 10 ft. Whenever the session status's source mode is ADS-B, live or replayed, the unit label above the tape reads `ft HAE` (REQ-DISP-007, REQ-LOG-007): the aircraft's geometric altitude is its GNSS height above the WGS-84 ellipsoid, not above mean sea level as the bench receiver's GPS altitude is, about 30 m apart over San Francisco Bay, so an aircraft on the runway reads below zero; under every other source mode the label stays `ft`. The datum is a property of the data, not of the run: until 0.3.0 a replayed approach read `ft` about 90 ft low with nothing to say so (FTS-VR-001 Entry 18 §18.5).
- **Geometry:** x 600 to 690, y 100 to 420, the centre line at y = 260, 0.4 px per ft, so ±400 ft is visible. Ticks every 20 ft, labels every 100 ft; each tick's y is 260 + (value − tick) × 0.4, computed per tick and clipped to the tape, not a group translation. The readout box sits on the centre line, its text right-aligned; `ft` is written above the tape.
- **Range:** any; a negative altitude is drawn with its sign, since the barometric reference can put the ground below zero.
- **Update:** every frame.
- **Flags:** the ticks and labels are removed; the readout shows dashes for NO_COMPUTED_DATA and NOT_JUDGED; the flag `ALT` sits over the tape's centre for every other cause.

### 4.3 Vertical speed indicator (VS)

- **Source:** `vertical_speed_mps` with `vertical_speed_validity` (REQ-PROC-003, REQ-PROC-007).
- **Units:** ft/min, signed; the readout rounded to 50.
- **Geometry:** x 710 to 760, y 100 to 420, zero at y = 260, linear at 0.06 px per ft/min, so ±2,000 ft/min spans ±120 px; beyond that the pointer is pinned at ±120 px while the readout stays unclamped. Scale marks at 0, ±500, ±1,000 and ±2,000, labelled in hundreds (5, 10, 20); `fpm` written above.
- **Update:** every frame.
- **Flags:** as the tape's, with the flag `VS`.

### 4.4 Ground speed readout (GS)

- **Source:** `ground_speed_mps`, the GPS speed over ground; the rule is the GPS channel's `gps_status` (REQ-DISP-002).
- **Units:** kn, rounded to 1, shown as `kt`.
- **Geometry:** a box at x 40 to 150, y 236 to 284, `GS` above it and `kt` beside the number.
- **Update:** the field changes once per fix (1 Hz on the receiver and in the simulator) and is held between fixes. The readout shows the held value: a GPS reading at its own rate, not a stale one. A hold past `fault.uart_timeout_ms` is the detector's DEGRADED, and shows as the flag.
- **Flags:** dashes for NO_DATA; the flag `GS` for DEGRADED and for the feed's causes; the no-source flag `GS` for NOT_AVAILABLE, a receiver disabled in the configuration.

### 4.5 Track indicator (TRK)

- **Source:** `course_deg` (`heading_deg` until format version 3), the GPS course over ground (degrees true, 0 to 360, from the RMC sentence; the aircraft's track in ADS-B mode), with `ground_speed_mps` for the speed rule and `gps_status` for the channel's. The instrument is labelled TRK and never HDG: there is no magnetometer, and a course says where the aircraft moves, not where its nose points.
- **Rule:** the channel's status (Section 2.1); then, with NOMINAL, the track is shown only while `ground_speed_mps` is at or above the threshold `hello` names (`track_min_speed_mps`, 1.0 m/s as shipped, the bridge's `--track-min-speed`), because a stationary receiver's course is noise.
- **Units:** degrees, rounded to 1, 0 to 359.
- **Geometry:** a compass rose centred at (400, 640) with radius 180, of which the top is visible between y 448 and 520 under a fixed lubber line at (400, 462); the rose group rotates by −track so that the current track sits under the lubber line (each tick is drawn rotated by its own angle, so the tick at the current track comes up under the lubber line); ticks every 10°, labels every 30° (N, 3, 6, E, 12, 15, S, 21, 24, W, 30, 33). The readout box at y 434, above the lubber line, reads `TRK 087°`.
- **Below the speed threshold:** the readout reads `TRK ---` and the rose is not drawn; no flag, since nothing failed.
- **Update:** per fix, held between, as 4.4.
- **Flags:** dashes for NO_DATA; the flag `TRK` for DEGRADED and for the feed's causes; the no-source flag `TRK` for NOT_AVAILABLE.

### 4.6 Ground track plot (POS)

Section 6.

## 5. The feed's states (REQ-DISP-001, REQ-DISP-003, REQ-DISP-004, REQ-DISP-006)

The strip at the top of the panel shows, left to right, the mode label, the feed's state with its cause, and the rate with the lag.

| Feed state | The strip | The instruments | Precedence |
|---|---|---|---|
| no socket yet, or no frame yet | CONNECTING, then WAITING | dashes everywhere, no flags | |
| running | RUNNING, `50 fps · lag 12 ms` | by their fields | |
| the bridge disconnected after frames were seen | BRIDGE DISCONNECTED, red | red flags | 1 |
| ended | ENDED | grey flags; the plot's trail kept | 2 |
| stale | STALE with the age counting, red | red flags | 3 |
| lag beyond the threshold | LAG with the lag, red | red flags | 4 |
| any field NOT_JUDGED | PIPELINE in amber beside the state | that instrument's dashes | |

**The mode label (REQ-DISP-004):** `LIVE` and `ADS-B` plain; `SIM` and `REPLAY` in an amber box; `MODE UNKNOWN` in amber until a status has named a mode. It is never absent and never defaults to LIVE, so no screenshot or recording of this display can pass simulated, replayed or ADS-B data off as live. **The identity (REQ-DISP-007):** after `ADS-B`, a middle dot and the identity of the aircraft the instruments show, the callsign, or the ICAO address when no callsign is reported, or `---` when none is selected, from the session status; after `REPLAY`, `ID UNAVAILABLE` when the recording's source mode is ADS-B, because a replay carries no identity (the status is never logged; the file beside the log says which aircraft the recorded run tracked), and nothing when it is not, a replayed simulator or bench run never having had an identity to lose (0.3.0; until it, every replay said `ID UNAVAILABLE`); nothing after `LIVE` and `SIM`, which are own-ship. With the bridge's `--hide-identity` the identity reads `---` throughout, for recordings made for publication under the data license.

**The lag (REQ-DISP-006):** `lag = (arrival − arrival₀) − (t − t₀)`, the page's clock against the frame's timestamps, computed on every frame, not on the ticker (silence is staleness's business); beside it in the diagnostic view, `bridge lag = (received_at − received_at₀) − (t − t₀)` from the bridge's `received_at_ms`, to say on which side of the bridge a queue is. The origins are the first frame after a connection, after ENDED, or after a timestamp that goes backwards (a restarted binary with no ENDED seen). The threshold is `hello`'s `lag_ms` (500 ms as shipped, the bridge's `--lag-ms`). An unpaced replay's lag is negative, its frames arriving faster than their timestamps advance, and is not flagged; the rate shows it.

## 6. The ground track plot (REQ-DISP-002, REQ-DISP-005)

- **Source:** `position.east_m` and `position.north_m` from the bridge, in metres from the first NOMINAL fresh fix the bridge saw after its start, after an ENDED, after a backwards timestamp, or after the identity the session status names changed (the origin is never sent); `gps_read_ok` marks a fresh fix; `heading_deg` and the speed rule of 4.5 orient the marker; `gps_status` is the channel rule.
- **The trail:** one point per fresh fix, capped at 3,600 (an hour at 1 Hz), cleared when the origin resets and when the identity changes.
- **Geometry:** `viewBox="0 0 300 300"`, north up, the origin at the centre. The half-extent is max(10 m, 1.2 × the trail's largest |east| or |north|), so the scale is 150 px per half-extent; the grid step is the 1-2-5 value nearest to a quarter of the half-extent (2 m at the 10 m minimum, 1 km for a 3 km trail), with grid lines at multiples of the step through the origin; the scale bar at the bottom left is one step long, labelled in m, or in km from 1,000 m. The trail is one polyline; the current position is a triangle rotated to the track while the track is shown (4.5), a dot otherwise. A `N` arrow and the title `GROUND TRACK · relative to first fix`. No image element, no tiles, no coordinate anywhere: a test searches the DOM.
- **Update:** the trail on each fresh fix; the marker every frame.
- **Flags:** dashes have no place on a plot; before any fix the grid is drawn empty with `no fix`. Under a flag the trail stays drawn, since it is history, and the marker is replaced by the flag `POS`.

## 7. The replay demo (REQ-SENS-007, REQ-TIME-004)

The profile `demo` (`src/drivers/motion_profile.cpp`) is a 150 s scripted flight, piecewise linear between these keyframes and held after the last one with zero rates:

| t (s) | altitude (m) | pitch (°) | roll (°) | track (°) | ground speed (m/s) | phase |
|---|---|---|---|---|---|---|
| 0 | 2 | 0 | 0 | 90 | 0 | parked |
| 5 | 2 | 0 | 0 | 90 | 0 | the takeoff roll begins |
| 15 | 2 | 0 | 0 | 90 | 30 | rotation begins |
| 17 | 2 | 8 | 0 | 90 | 30 | lift-off |
| 57 | 122 | 8 | 0 | 90 | 30 | the climb ends (3 m/s) |
| 62 | 122 | 2 | 20 | 90 | 30 | rolled into a right turn |
| 92 | 122 | 2 | 20 | 270 | 30 | the turn ends (6°/s) |
| 97 | 122 | 2 | 0 | 270 | 30 | rolled out |
| 100 | 122 | −4 | 0 | 270 | 30 | pushed over |
| 130 | 62 | −4 | 0 | 270 | 30 | the descent ends (−2 m/s) |
| 133 | 62 | 0 | 0 | 270 | 30 | level |
| 150 | 62 | 0 | 0 | 270 | 20 | slowing; the end |

The readings are the pipeline's inverse: the pressure from the scripted altitude by `altitude_to_pressure` at the ISA 1013.25 hPa reference; the accelerometer `g · (sin θ, sin φ, √(1 − sin²θ − sin²φ))`, which the complementary filter's two `atan2` forms invert to exactly θ and φ (a physical gravity vector would give the roll as `asin(sin φ · cos θ)`, 0.01° off at this flight's 2° of pitch in the turn); the gyroscope the segment's angular rates on x (roll), y (pitch) and z (track); the GPS fixes integrated from the scripted ground speed and track once per generated frame and converted to latitude and longitude around the stationary scenario's position by the spherical approximation (R = 6,371,000 m), with the scripted altitude as the GPS altitude; the configured noise on top of all of it, in the same seven draws per frame as the stationary scenario, so the default is byte-identical.

What the instruments show, by phase (seen on 2026-10-15, FTS-VR-001 Entry 17): parked, ALT reads 10 ft (2 m under the 10 ft rounding), VS settles from the Kalman filter's startup transient to 0, GS 0, `TRK ---` with no rose, the plot a dot; the takeoff roll, GS rising to 58 kt, TRK 090° and the rose from 2 kt, the marker moving east; rotation and climb, the horizon 32 px down (8°), VS +600, ALT rising to 400 ft; the turn, the horizon rotated 20° counterclockwise, TRK sweeping 090 → 270, the plot's half circle; the descent, the horizon 16 px up, VS −400, ALT to 200 ft; level and slowing; then ENDED, grey flags, the trail kept.

Commands: record with `./build/telemetry --motion demo --cycles 7500` (150 s; 7,500 frames, 787,516 bytes, under one rotation); then, with the bridge (`.venv/bin/python -m pipeline.bridge --endpoint tcp://127.0.0.1:5555`) and the page (`cd dashboard && npm run dev`, `http://127.0.0.1:5173`) running, replay with `./build/telemetry --replay logs/telemetry_000.bin --pace --zmq tcp://127.0.0.1:5555`. The page labels the recording SIM and the replay REPLAY; the replay's log is byte-identical to the recording (REQ-TIME-004, REQ-LOG-003). The log is 7,500 frames in 787,516 bytes; its bytes depend on the toolchain's `normal_distribution`, so its hash is recorded per platform in FTS-VR-001 (Entry 17: `caba9aeb…` on the development Mac) and the log is regenerated, not committed.

## 8. The diagnostic view

The verdict table of REQ-DISP-003's first screen stays below the instruments, unchanged in its rendering: each computed field with its validity by name beside the number in SI, a FAILURE_WARNING value struck through in the warning colour, every value struck through and dimmed while the feed is stale, each channel's status with fresh or held, the counters, and the two lag numbers. It exists to show what the feed carries; the instruments exist to show only what can be trusted (FTS-DD-001 Section 12).

In ADS-B mode the diagnostic view ends with the attribution the data license requires: `Traffic data: The OpenSky Network (opensky-network.org)`.
