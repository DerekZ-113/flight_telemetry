// Tests for the voter (REQ-RED-002 to REQ-RED-004, REQ-RED-006): the one
// comparison between two lanes' outputs, bit for bit over the thirteen
// fields a lane writes or carries from the detector. Test card TC-032
// steps 1 and 2. Lane outputs are real processor outputs over the
// simulator, so every compared field holds a value a lane can produce.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

#include "drivers/simulated_source.h"
#include "processing/fault_detector.h"
#include "processing/processor.h"
#include "processing/voter.h"
#include "telemetry_frame.h"

namespace {

constexpr uint64_t kDtMs = 20;

// A lane's output after `n` frames of the seed-42 simulator: every channel
// NOMINAL, every validity NORMAL, every computed value non-zero.
TelemetryFrame lane_output(int n = 10) {
    SimulatedSource source(kDtMs, 42, kDtMs);
    FaultDetector detector;
    TelemetryProcessor processor;
    TelemetryFrame out{};
    for (int i = 0; i < n; i++) {
        out = processor.process(detector.check(source.read_frame()));
    }
    return out;
}

// The same value with its lowest bit flipped: the smallest difference a
// float can have, which a tolerance would swallow and the voter must not.
float flip_low_bit(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    bits ^= 1u;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

Validity other(Validity v) {
    return v == Validity::NORMAL ? Validity::FAILURE_WARNING : Validity::NORMAL;
}

ChannelStatus other(ChannelStatus s) {
    return s == ChannelStatus::NOMINAL ? ChannelStatus::DEGRADED : ChannelStatus::NOMINAL;
}

// The thirteen compared fields, each as a change to one of them.
struct FieldChange {
    const char* name;
    std::function<void(TelemetryFrame&)> change;
};

std::vector<FieldChange> compared_fields() {
    return {
        {"baro_altitude_m", [](TelemetryFrame& f) { f.baro_altitude_m = flip_low_bit(f.baro_altitude_m); }},
        {"pitch_deg", [](TelemetryFrame& f) { f.pitch_deg = flip_low_bit(f.pitch_deg); }},
        {"roll_deg", [](TelemetryFrame& f) { f.roll_deg = flip_low_bit(f.roll_deg); }},
        {"fused_altitude_m", [](TelemetryFrame& f) { f.fused_altitude_m = flip_low_bit(f.fused_altitude_m); }},
        {"vertical_speed_mps", [](TelemetryFrame& f) { f.vertical_speed_mps = flip_low_bit(f.vertical_speed_mps); }},
        {"baro_altitude_validity", [](TelemetryFrame& f) { f.baro_altitude_validity = other(f.baro_altitude_validity); }},
        {"pitch_validity", [](TelemetryFrame& f) { f.pitch_validity = other(f.pitch_validity); }},
        {"roll_validity", [](TelemetryFrame& f) { f.roll_validity = other(f.roll_validity); }},
        {"fused_altitude_validity", [](TelemetryFrame& f) { f.fused_altitude_validity = other(f.fused_altitude_validity); }},
        {"vertical_speed_validity", [](TelemetryFrame& f) { f.vertical_speed_validity = other(f.vertical_speed_validity); }},
        {"baro_status", [](TelemetryFrame& f) { f.baro_status = other(f.baro_status); }},
        {"imu_status", [](TelemetryFrame& f) { f.imu_status = other(f.imu_status); }},
        {"gps_status", [](TelemetryFrame& f) { f.gps_status = other(f.gps_status); }},
    };
}

}  // namespace

// TC-032 step 1: two lanes given the same input agree.
TEST(LanesAgreeTest, IdenticalFramesAgree) {
    const TelemetryFrame a = lane_output();
    const TelemetryFrame b = lane_output();
    EXPECT_TRUE(lanes_agree(a, b));
    EXPECT_TRUE(lanes_agree(a, a));
}

// TC-032 step 1: one bit in any of the thirteen compared fields breaks
// agreement, in either order (REQ-RED-002: bit for bit, no tolerance).
TEST(LanesAgreeTest, EveryComparedFieldBreaksAgreement) {
    const TelemetryFrame a = lane_output();
    const std::vector<FieldChange> fields = compared_fields();
    ASSERT_EQ(fields.size(), 13u);
    for (const FieldChange& field : fields) {
        TelemetryFrame b = a;
        field.change(b);
        EXPECT_NE(std::memcmp(&a, &b, sizeof a), 0) << field.name << " did not change";
        EXPECT_FALSE(lanes_agree(a, b)) << field.name;
        EXPECT_FALSE(lanes_agree(b, a)) << field.name;
    }
}

// TC-032 step 1: the fields the voter takes from the checked frame, not
// from a lane, take no part: the raw readings, the timestamp and sequence
// (the overrun's check, REQ-RED-006), the flags, the tail, the voter's own
// status.
TEST(LanesAgreeTest, UncomparedFieldsDoNotTakePart) {
    const TelemetryFrame a = lane_output();
    TelemetryFrame b = a;
    b.timestamp_ms += 20;
    b.sequence += 1;
    b.pressure_hpa += 1.0f;
    b.temperature_c += 1.0f;
    b.accel_x += 1.0f;
    b.gyro_z += 1.0f;
    b.latitude += 0.001;
    b.longitude += 0.001;
    b.gps_altitude_m += 1.0f;
    b.ground_speed_mps += 1.0f;
    b.course_deg += 1.0f;
    b.baro_read_ok = !b.baro_read_ok;
    b.imu_read_ok = !b.imu_read_ok;
    b.gps_read_ok = !b.gps_read_ok;
    b.flags = 0xFF;
    b.gps_vertical_rate_mps = 3.0f;
    b.gps_pressure_altitude_m = 300.0f;
    b.gps_fix_quality = 2;
    b.gps_satellites = 12;
    b.voter_status = 0x0011;
    b.reserved_d = 7;
    EXPECT_TRUE(lanes_agree(a, b));
}

// TC-032 step 2: -0.0 == +0.0 under ==, but their bits differ, and
// identical lanes cannot produce the two; the voter compares bits.
TEST(LanesAgreeTest, NegativeZeroDisagreesWithZero) {
    TelemetryFrame a = lane_output();
    TelemetryFrame b = a;
    a.vertical_speed_mps = 0.0f;
    b.vertical_speed_mps = -0.0f;
    ASSERT_TRUE(a.vertical_speed_mps == b.vertical_speed_mps);   // what == would have said
    EXPECT_FALSE(lanes_agree(a, b));
}

// TC-033 step 4's comparison half: a NaN agrees with nothing, not even the
// same NaN bits, so two lanes holding one cannot form a majority; a number
// beside a NaN disagrees in either order. The processor never emits NaN
// (REQ-PROC-007), so a NaN is a fault wherever it appears.
TEST(LanesAgreeTest, NaNAgreesWithNothing) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    TelemetryFrame a = lane_output();
    TelemetryFrame b = a;
    a.pitch_deg = nan;
    b.pitch_deg = nan;
    ASSERT_EQ(std::memcmp(&a, &b, sizeof a), 0);   // the same bits
    EXPECT_FALSE(lanes_agree(a, b));
    const TelemetryFrame number = lane_output();
    EXPECT_FALSE(lanes_agree(a, number));
    EXPECT_FALSE(lanes_agree(number, a));
}
