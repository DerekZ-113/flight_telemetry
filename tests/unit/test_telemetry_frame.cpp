// Tests for src/telemetry_frame.h/.cpp: the layout of format version 3 and
// the name helpers (REQ-LOG-001, REQ-LOG-005, REQ-LOG-008, REQ-PROC-005,
// REQ-PROC-007, REQ-PROC-008, REQ-FAULT-006). Test cards TC-017 step 1 and
// TC-031 step 1. The static_asserts in the header
// fail the build on a layout change; these state the same offsets as values
// a reader can compare with pipeline/frame_format.py.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

#include "processing/fault_detector.h"
#include "telemetry_frame.h"

// Every byte of the frame is a named field at the offset the log format
// and the Python layout describe. Version 3 keeps every version-2 field
// where it was, spends the two reserved fields by name and appends a
// 16-byte tail. (REQ-LOG-001, REQ-LOG-005; TC-031 step 1)
TEST(FrameLayoutTest, EveryByteIsANamedField) {
    EXPECT_EQ(sizeof(TelemetryFrame), 120u);
    EXPECT_EQ(alignof(TelemetryFrame), 8u);
    EXPECT_EQ(offsetof(TelemetryFrame, sequence), 52u);
    EXPECT_EQ(offsetof(TelemetryFrame, latitude), 56u);
    EXPECT_EQ(offsetof(TelemetryFrame, course_deg), 80u);
    EXPECT_EQ(offsetof(TelemetryFrame, fused_altitude_m), 84u);
    EXPECT_EQ(offsetof(TelemetryFrame, gps_read_ok), 97u);
    EXPECT_EQ(offsetof(TelemetryFrame, baro_altitude_validity), 98u);
    EXPECT_EQ(offsetof(TelemetryFrame, pitch_validity), 99u);
    EXPECT_EQ(offsetof(TelemetryFrame, roll_validity), 100u);
    EXPECT_EQ(offsetof(TelemetryFrame, fused_altitude_validity), 101u);
    EXPECT_EQ(offsetof(TelemetryFrame, vertical_speed_validity), 102u);
    EXPECT_EQ(offsetof(TelemetryFrame, flags), 103u);
    EXPECT_EQ(offsetof(TelemetryFrame, gps_vertical_rate_mps), 104u);
    EXPECT_EQ(offsetof(TelemetryFrame, gps_pressure_altitude_m), 108u);
    EXPECT_EQ(offsetof(TelemetryFrame, gps_fix_quality), 112u);
    EXPECT_EQ(offsetof(TelemetryFrame, gps_satellites), 113u);
    EXPECT_EQ(offsetof(TelemetryFrame, voter_status), 114u);
    EXPECT_EQ(offsetof(TelemetryFrame, reserved_d), 116u);
}

// The flags byte is named bits, one per fact a source may state about a
// frame, and a byte outside them is unnamed. (REQ-PROC-008, REQ-SENS-005,
// REQ-SENS-008; TC-031 step 1)
TEST(FrameLayoutTest, FlagBitsAreNamedAndTested) {
    EXPECT_EQ(static_cast<uint8_t>(FrameFlag::NEW_VEHICLE), 0x01);
    EXPECT_EQ(static_cast<uint8_t>(FrameFlag::VERTICAL_RATE), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(FrameFlag::PRESSURE_ALTITUDE), 0x04);
    EXPECT_EQ(static_cast<uint8_t>(FrameFlag::FIX_QUALITY), 0x08);
    EXPECT_STREQ(flag_name(FrameFlag::NEW_VEHICLE), "NEW_VEHICLE");
    EXPECT_STREQ(flag_name(FrameFlag::VERTICAL_RATE), "VERTICAL_RATE");
    EXPECT_STREQ(flag_name(FrameFlag::PRESSURE_ALTITUDE), "PRESSURE_ALTITUDE");
    EXPECT_STREQ(flag_name(FrameFlag::FIX_QUALITY), "FIX_QUALITY");
    EXPECT_STREQ(flag_name(static_cast<FrameFlag>(0x10)), "UNKNOWN");

    TelemetryFrame f{};
    EXPECT_FALSE(has_flag(f, FrameFlag::NEW_VEHICLE));
    set_flag(f, FrameFlag::FIX_QUALITY);
    set_flag(f, FrameFlag::NEW_VEHICLE);
    EXPECT_EQ(f.flags, 0x09);
    EXPECT_TRUE(has_flag(f, FrameFlag::NEW_VEHICLE));
    EXPECT_TRUE(has_flag(f, FrameFlag::FIX_QUALITY));
    EXPECT_FALSE(has_flag(f, FrameFlag::VERTICAL_RATE));
    set_flag(f, FrameFlag::NEW_VEHICLE);   // idempotent: a bit, not a count
    EXPECT_EQ(f.flags, 0x09);
}

// The voter's status is named bits too, assigned ahead of the voter so it
// never needed a format version 4: zero means no voter ran, which every
// source puts. (REQ-RED-002 to REQ-RED-004; design.md section 13; TC-031
// step 1)
TEST(FrameLayoutTest, VoterStatusBitsAreNamed) {
    EXPECT_EQ(static_cast<uint16_t>(VoterStatus::VOTED), 0x0001);
    EXPECT_EQ(static_cast<uint16_t>(VoterStatus::LANE_1_FAULTED), 0x0002);
    EXPECT_EQ(static_cast<uint16_t>(VoterStatus::LANE_2_FAULTED), 0x0004);
    EXPECT_EQ(static_cast<uint16_t>(VoterStatus::LANE_3_FAULTED), 0x0008);
    EXPECT_EQ(static_cast<uint16_t>(VoterStatus::NO_MAJORITY), 0x0010);
    EXPECT_STREQ(voter_status_name(VoterStatus::VOTED), "VOTED");
    EXPECT_STREQ(voter_status_name(VoterStatus::LANE_1_FAULTED), "LANE_1_FAULTED");
    EXPECT_STREQ(voter_status_name(VoterStatus::LANE_2_FAULTED), "LANE_2_FAULTED");
    EXPECT_STREQ(voter_status_name(VoterStatus::LANE_3_FAULTED), "LANE_3_FAULTED");
    EXPECT_STREQ(voter_status_name(VoterStatus::NO_MAJORITY), "NO_MAJORITY");
    EXPECT_STREQ(voter_status_name(static_cast<VoterStatus>(0x0100)), "UNKNOWN");
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
    EXPECT_EQ(f.sequence, 0u);
    EXPECT_EQ(f.flags, 0u);
    EXPECT_EQ(f.gps_vertical_rate_mps, 0.0f);
    EXPECT_EQ(f.gps_pressure_altitude_m, 0.0f);
    EXPECT_EQ(f.gps_fix_quality, 0u);
    EXPECT_EQ(f.gps_satellites, 0u);
    EXPECT_EQ(f.voter_status, 0u);   // no voter ran
    EXPECT_EQ(f.reserved_d, 0u);
    // Every one of the 120 bytes: the tail included, since a value-
    // initialized frame is what every source starts from.
    const std::array<uint8_t, sizeof(TelemetryFrame)> zero{};
    EXPECT_EQ(std::memcmp(&f, zero.data(), sizeof(f)), 0);
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
    EXPECT_STREQ(status_name(ChannelStatus::NOT_AVAILABLE), "NOT_AVAILABLE");
    EXPECT_STREQ(status_name(static_cast<ChannelStatus>(9)), "UNKNOWN");
}
