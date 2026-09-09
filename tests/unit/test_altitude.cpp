// Unit tests for src/processing/altitude.cpp (REQ-PROC-001).
// Test card: TC-001 in docs/test_plan.md. Expected values and tolerances
// are taken from that table; the tolerance grows with altitude because the
// 0.1903 exponent is a four-digit rounding of the exact ISA value.

#include <gtest/gtest.h>

#include "processing/altitude.h"

// TC-001 step 1. At the ISA sea-level reference pressure the hypsometric
// formula reduces to 44330 * (1 - 1^0.1903) = 0 exactly, so the tolerance
// only has to absorb float rounding in pow(). (REQ-PROC-001)
TEST(AltitudeTest, SeaLevelPressureReturnsZero) {
    EXPECT_NEAR(pressure_to_altitude(1013.25f), 0.0f, 0.01f);
}

// TC-001 step 2. ISA table: 898.76 hPa at 1000 m. (REQ-PROC-001)
TEST(AltitudeTest, ThousandMeters) {
    EXPECT_NEAR(pressure_to_altitude(898.76f), 1000.0f, 1.0f);
}

// TC-001 step 3. ISA table: 795.01 hPa at 2000 m. (REQ-PROC-001)
TEST(AltitudeTest, TwoThousandMeters) {
    EXPECT_NEAR(pressure_to_altitude(795.01f), 2000.0f, 2.0f);
}

// TC-001 step 4. 300 hPa is the bottom of the BMP280 operating range,
// about 9164 m. The formula is least accurate here, hence the wide band.
// (REQ-PROC-001)
TEST(AltitudeTest, ExtremeAltitude) {
    EXPECT_NEAR(pressure_to_altitude(300.0f), 9164.0f, 20.0f);
}

// TC-001 step 5. Measured pressure 10 hPa BELOW the reference means the
// sensor is ABOVE the reference level: about +82.8 m. (Lower pressure is
// higher altitude; a negative result needs the arguments the other way
// round.) Proves the second parameter is honored and not silently replaced
// by the default. (REQ-PROC-001)
TEST(AltitudeTest, CustomSeaLevelReference) {
    EXPECT_NEAR(pressure_to_altitude(1013.25f, 1023.25f), 82.8f, 1.0f);
}

// Mirror of step 5: a reference below the measurement puts the sensor
// below the reference level, so the sign flips. Guards against a future
// "fix" that clamps altitude at zero. (REQ-PROC-001)
TEST(AltitudeTest, PressureAboveReferenceIsNegative) {
    EXPECT_NEAR(pressure_to_altitude(1023.25f, 1013.25f), -82.9f, 1.0f);
}

// TC-001 step 6. One hPa below reference is about 8.3 m up. This is the
// sensitivity the fault model's "8 m per hPa" reasoning relies on.
// (REQ-PROC-001)
TEST(AltitudeTest, AboveSeaLevelReference) {
    EXPECT_NEAR(pressure_to_altitude(1012.25f, 1013.25f), 8.3f, 0.2f);
}

// Not a TC-001 step, but a property the formula must have: lower pressure
// is always higher altitude. A sign error or a non-monotonic curve fit
// would pass the six point checks and still fail this. (REQ-PROC-001)
TEST(AltitudeTest, MonotonicDecrease) {
    float previous_altitude = pressure_to_altitude(1013.0f);
    for (float pressure = 1012.0f; pressure >= 500.0f; pressure -= 1.0f) {
        const float altitude = pressure_to_altitude(pressure);
        EXPECT_GT(altitude, previous_altitude) << "at pressure " << pressure << " hPa";
        previous_altitude = altitude;
    }
}
