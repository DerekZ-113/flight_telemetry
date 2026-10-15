#pragma once

// Converts barometric pressure to altitude using the hypsometric formula.
// This is the same math real aircraft altimeters use.
//
// Implements REQ-PROC-001:
//   "The system shall convert barometric pressure to altitude using
//    the hypsometric formula, referenced to a configurable sea-level pressure."
//
// Parameters:
//   pressure_hpa  - measured pressure in hectopascals (typical range: 300-1100)
//   sea_level_hpa - reference sea-level pressure. The default is the ISA
//                   standard atmosphere constant and serves the unit tests;
//                   TelemetryProcessor passes `processing.sea_level_pressure_hpa`
//                   from the configuration (REQ-CFG-001), so the default is
//                   never a hidden magic number in the binary.
//
// Returns:
//   altitude in meters above the reference pressure level. Inputs are assumed
//   already range-checked (REQ-FAULT-003); a zero or negative reference
//   produces inf or NaN.

float pressure_to_altitude(float pressure_hpa, float sea_level_hpa = 1013.25f);

// The inverse (REQ-SENS-007): the pressure at which pressure_to_altitude
// returns altitude_m, from the same two constants, so that a scripted
// altitude round-trips through the pipeline's own relation (the simulator
// writes this pressure, the processor reads the altitude back). Altitudes
// at or above 44330 m have no pressure in this model; the inputs are the
// simulator's, a few kilometres at most.
float altitude_to_pressure(float altitude_m, float sea_level_hpa = 1013.25f);
