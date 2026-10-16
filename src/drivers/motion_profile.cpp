#include "drivers/motion_profile.h"

#include <cmath>
#include <utility>

namespace {

// FTS-DS-001 §7: a 150 s flight. Parked; a takeoff roll east; rotation; a
// climb at 8° of pitch and 3 m/s to 122 m; a right turn through 180° at
// 20° of bank; a descent at -4° and -2 m/s to 62 m; level, then slowing.
// Times in milliseconds, every keyframe on the 20 ms frame grid, so a
// segment's slope changes on a frame boundary and never inside a frame.
const std::vector<MotionProfile::Keyframe> kDemo = {
    //   t_ms    alt    pitch   roll   track  speed
    {      0,   2.0f,   0.0f,  0.0f,  90.0f,  0.0f},
    {   5000,   2.0f,   0.0f,  0.0f,  90.0f,  0.0f},
    {  15000,   2.0f,   0.0f,  0.0f,  90.0f, 30.0f},
    {  17000,   2.0f,   8.0f,  0.0f,  90.0f, 30.0f},
    {  57000, 122.0f,   8.0f,  0.0f,  90.0f, 30.0f},
    {  62000, 122.0f,   2.0f, 20.0f,  90.0f, 30.0f},
    {  92000, 122.0f,   2.0f, 20.0f, 270.0f, 30.0f},
    {  97000, 122.0f,   2.0f,  0.0f, 270.0f, 30.0f},
    { 100000, 122.0f,  -4.0f,  0.0f, 270.0f, 30.0f},
    { 130000,  62.0f,  -4.0f,  0.0f, 270.0f, 30.0f},
    { 133000,  62.0f,   0.0f,  0.0f, 270.0f, 30.0f},
    { 150000,  62.0f,   0.0f,  0.0f, 270.0f, 20.0f},
};

float lerp(float from, float to, float u) {
    return from + (to - from) * u;
}

// A course stays in [0, 360): the script may interpolate across north one
// day, and the frame's course_deg is "0-360" by contract.
float wrap_degrees(float degrees) {
    const float wrapped = std::fmod(degrees, 360.0f);
    return wrapped < 0.0f ? wrapped + 360.0f : wrapped;
}

}  // namespace

MotionProfile::MotionProfile(std::string name, std::vector<Keyframe> keyframes)
    : name_(std::move(name)), keyframes_(std::move(keyframes)) {}

std::optional<MotionProfile> MotionProfile::named(std::string_view name) {
    if (name == "demo") {
        return MotionProfile("demo", kDemo);
    }
    return std::nullopt;
}

MotionState MotionProfile::state_at(uint64_t timestamp_ms) const {
    MotionState s;
    const Keyframe& last = keyframes_.back();
    if (timestamp_ms >= last.t_ms) {
        // Held after the end: a still aircraft, so every rate stays zero.
        s.altitude_m = last.altitude_m;
        s.pitch_deg = last.pitch_deg;
        s.roll_deg = last.roll_deg;
        s.track_deg = wrap_degrees(last.track_deg);
        s.ground_speed_mps = last.ground_speed_mps;
        return s;
    }
    // The segment [from, to) that contains the timestamp. The first keyframe
    // is at 0 and the timestamp is before the last one, so the search ends
    // inside the table.
    size_t i = 0;
    while (keyframes_[i + 1].t_ms <= timestamp_ms) {
        i++;
    }
    const Keyframe& from = keyframes_[i];
    const Keyframe& to = keyframes_[i + 1];
    const float span_s = static_cast<float>(to.t_ms - from.t_ms) / 1000.0f;
    const float u = (static_cast<float>(timestamp_ms - from.t_ms) / 1000.0f) / span_s;
    s.altitude_m = lerp(from.altitude_m, to.altitude_m, u);
    s.pitch_deg = lerp(from.pitch_deg, to.pitch_deg, u);
    s.roll_deg = lerp(from.roll_deg, to.roll_deg, u);
    s.track_deg = wrap_degrees(lerp(from.track_deg, to.track_deg, u));
    s.ground_speed_mps = lerp(from.ground_speed_mps, to.ground_speed_mps, u);
    s.pitch_rate_dps = (to.pitch_deg - from.pitch_deg) / span_s;
    s.roll_rate_dps = (to.roll_deg - from.roll_deg) / span_s;
    s.track_rate_dps = (to.track_deg - from.track_deg) / span_s;
    return s;
}
