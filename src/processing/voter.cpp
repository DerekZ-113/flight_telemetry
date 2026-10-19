#include "processing/voter.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

static_assert(sizeof(float) == sizeof(uint32_t), "the comparison reads a float's bits as 32 bits");

// A float's bit pattern. memcpy is the defined way to read an object's
// bytes as another type: reading the float through a reinterpret_cast'd
// pointer would break the strict-aliasing rule, and std::bit_cast is
// C++20. The compiler turns this into a register move.
uint32_t bits(float value) {
    uint32_t out = 0;
    std::memcpy(&out, &value, sizeof out);
    return out;
}

// Two floats agree when their bits are equal and they are not NaN. == is
// not identity on floats: NaN is unequal to itself, so two lanes holding
// one would never agree by ==, but they would by bits, and the voter must
// not vote a NaN through; and -0.0 == +0.0, a difference identical lanes
// cannot produce. One NaN is enough to refuse: equal bits mean both are.
bool floats_agree(float a, float b) {
    return !std::isnan(a) && bits(a) == bits(b);
}

}  // namespace

bool lanes_agree(const TelemetryFrame& a, const TelemetryFrame& b) {
    // The five computed values, their five validities, and the three
    // channel statuses the lanes carry from the detector: the thirteen
    // fields of REQ-RED-002, one per line.
    return floats_agree(a.baro_altitude_m, b.baro_altitude_m) &&
           floats_agree(a.pitch_deg, b.pitch_deg) &&
           floats_agree(a.roll_deg, b.roll_deg) &&
           floats_agree(a.fused_altitude_m, b.fused_altitude_m) &&
           floats_agree(a.vertical_speed_mps, b.vertical_speed_mps) &&
           a.baro_altitude_validity == b.baro_altitude_validity &&
           a.pitch_validity == b.pitch_validity &&
           a.roll_validity == b.roll_validity &&
           a.fused_altitude_validity == b.fused_altitude_validity &&
           a.vertical_speed_validity == b.vertical_speed_validity &&
           a.baro_status == b.baro_status &&
           a.imu_status == b.imu_status &&
           a.gps_status == b.gps_status;
}
