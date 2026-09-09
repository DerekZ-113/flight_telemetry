// Unit tests for src/processing/altitude.cpp (REQ-PROC-001).
// Test card: TC-001 in docs/test_plan.md. Only the framework smoke test is
// here tonight; the remaining TC-001 cases follow.

#include <gtest/gtest.h>

#include "processing/altitude.h"

// At the ISA sea-level reference pressure the hypsometric formula reduces
// to 44330 * (1 - 1^0.1903) = 0 exactly, so the tolerance only has to
// absorb float rounding in pow().
TEST(AltitudeTest, SeaLevelPressureReturnsZero) {
    EXPECT_NEAR(pressure_to_altitude(1013.25f), 0.0f, 0.01f);
}
