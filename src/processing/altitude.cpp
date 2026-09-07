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
