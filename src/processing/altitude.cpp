#include "altitude.h"
#include <cmath>  // for std::pow

// Hypsometric formula:
//   altitude = 44330 * (1 - (P / P0) ^ 0.1903)
//
// Where:
//   P  = measured pressure (hPa)
//   P0 = sea-level reference pressure (hPa)
//   44330 = constant in meters (derived from atmospheric model)
//   0.1903 = exponent (derived from gas constant, gravity, lapse rate)
//
// This gives altitude in meters above the reference pressure level.
//
// Accuracy is dominated by the reference pressure, not the math: a 1 hPa
// error in sea_level_hpa shifts the result by about 8 m near sea level.
// The formula also assumes the ISA temperature profile, so a day warmer or
// colder than standard adds roughly 0.4% of altitude per degree of deviation.
// Near sea level that second term is negligible.

float pressure_to_altitude(float pressure_hpa, float sea_level_hpa) {
    return 44330.0f * (1.0f - std::pow(pressure_hpa / sea_level_hpa, 0.1903f));
}

// Solved for P: P = P0 * (1 - altitude / 44330) ^ (1 / 0.1903). The same
// two constants, so the two functions are inverses to float rounding
// (AltitudeTest.PressureRoundTrips), not to the ISA's own precision.
float altitude_to_pressure(float altitude_m, float sea_level_hpa) {
    return sea_level_hpa * std::pow(1.0f - altitude_m / 44330.0f, 1.0f / 0.1903f);
}
