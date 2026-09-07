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
//                   standard atmosphere constant, kept only as a safe fallback.
//                   Once REQ-CFG-001 lands, callers pass the value from
//                   telemetry_config.yaml so this never becomes a hidden
//                   magic number.
//
// Returns:
//   altitude in meters above the reference pressure level. Inputs are assumed
//   already range-checked (REQ-FAULT-003); a zero or negative reference
//   produces inf or NaN.

float pressure_to_altitude(float pressure_hpa, float sea_level_hpa = 1013.25f);
