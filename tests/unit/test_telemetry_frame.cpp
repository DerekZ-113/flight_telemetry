// Tests for src/telemetry_frame.h/.cpp: the layout of format version 2 and
// the two name helpers (REQ-LOG-001, REQ-PROC-005, REQ-PROC-007,
// REQ-FAULT-006). Test card TC-017 step 1. The static_asserts in the header
// fail the build on a layout change; these state the same offsets as values
// a reader can compare with pipeline/frame_format.py.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>

#include "processing/fault_detector.h"
#include "telemetry_frame.h"

// Every byte of the frame is a named field at the offset the log format
// and the Python layout describe. (REQ-LOG-001)
TEST(FrameLayoutTest, EveryByteIsANamedField) {
    EXPECT_EQ(sizeof(TelemetryFrame), 104u);
    EXPECT_EQ(alignof(TelemetryFrame), 8u);
    EXPECT_EQ(offsetof(TelemetryFrame, reserved_a), 52u);
    EXPECT_EQ(offsetof(TelemetryFrame, latitude), 56u);
    EXPECT_EQ(offsetof(TelemetryFrame, fused_altitude_m), 84u);
    EXPECT_EQ(offsetof(TelemetryFrame, gps_read_ok), 97u);
    EXPECT_EQ(offsetof(TelemetryFrame, baro_altitude_validity), 98u);
    EXPECT_EQ(offsetof(TelemetryFrame, pitch_validity), 99u);
    EXPECT_EQ(offsetof(TelemetryFrame, roll_validity), 100u);
    EXPECT_EQ(offsetof(TelemetryFrame, fused_altitude_validity), 101u);
    EXPECT_EQ(offsetof(TelemetryFrame, vertical_speed_validity), 102u);
    EXPECT_EQ(offsetof(TelemetryFrame, reserved_b), 103u);
}

// The event's two bytes between `type` and `value` are a field now, not
// padding. (REQ-LOG-001, REQ-FAULT-004)
TEST(FrameLayoutTest, EventReservedSitsBetweenTypeAndValue) {
    EXPECT_EQ(sizeof(FaultEvent), 16u);
    EXPECT_EQ(offsetof(FaultEvent, type), 9u);
    EXPECT_EQ(offsetof(FaultEvent, reserved), 10u);
    EXPECT_EQ(offsetof(FaultEvent, value), 12u);
}

// A value-initialized frame is unjudged: NOT_JUDGED on every computed
// field and NOMINAL on every channel, the two vocabularies' zeros, and the
// reserved fields zero. A source hands out exactly this. (REQ-PROC-007)
TEST(FrameLayoutTest, ValueInitializedFrameIsNotJudged) {
    const TelemetryFrame f{};
    EXPECT_EQ(f.baro_altitude_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.pitch_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.roll_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.fused_altitude_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.vertical_speed_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.baro_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(f.imu_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(f.gps_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(f.reserved_a, 0u);
    EXPECT_EQ(f.reserved_b, 0u);
}

// The encoding is ordered worst first, so the smaller of two verdicts is
// the one a consumer must honour: a voter's worst-wins combiner is min().
// (REQ-PROC-007)
TEST(FrameLayoutTest, WorstValidityIsTheSmaller) {
    EXPECT_LT(Validity::NOT_JUDGED, Validity::NO_COMPUTED_DATA);
    EXPECT_LT(Validity::NO_COMPUTED_DATA, Validity::FAILURE_WARNING);
    EXPECT_LT(Validity::FAILURE_WARNING, Validity::NORMAL);
    EXPECT_EQ(std::min(Validity::NORMAL, Validity::FAILURE_WARNING), Validity::FAILURE_WARNING);
    EXPECT_EQ(std::min(Validity::NO_COMPUTED_DATA, Validity::NORMAL), Validity::NO_COMPUTED_DATA);
}

// The names a log or a display shows; a byte outside the enumeration is a
// corrupt log and says so rather than borrowing a state. (REQ-PROC-007)
TEST(FrameLayoutTest, ValidityNamesAreReadable) {
    EXPECT_STREQ(validity_name(Validity::NOT_JUDGED), "NOT_JUDGED");
    EXPECT_STREQ(validity_name(Validity::NO_COMPUTED_DATA), "NO_COMPUTED_DATA");
    EXPECT_STREQ(validity_name(Validity::FAILURE_WARNING), "FAILURE_WARNING");
    EXPECT_STREQ(validity_name(Validity::NORMAL), "NORMAL");
    EXPECT_STREQ(validity_name(static_cast<Validity>(7)), "UNKNOWN");
}

// (REQ-FAULT-004, REQ-FAULT-006)
TEST(FrameLayoutTest, StatusNamesAreReadable) {
    EXPECT_STREQ(status_name(ChannelStatus::NOMINAL), "NOMINAL");
    EXPECT_STREQ(status_name(ChannelStatus::DEGRADED), "DEGRADED");
    EXPECT_STREQ(status_name(ChannelStatus::NO_DATA), "NO_DATA");
    EXPECT_STREQ(status_name(static_cast<ChannelStatus>(9)), "UNKNOWN");
}
