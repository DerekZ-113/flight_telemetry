#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A scripted flight for the simulator (REQ-SENS-007): altitude, attitude,
// ground track, ground speed and the rates that go with them, as a
// function of time, so that the display's instruments and the replay demo
// have something that moves through the real pipeline. The stationary
// scenario in simulated_data.cpp stays the default; a profile is selected
// by name with --motion and never by a configuration key, because a
// scenario is a choice of run, not a tunable (FTS-DD-001 Section 3).
//
// A profile is a table of keyframes, linear in between, so each rate is a
// segment's slope, which is what a gyroscope on the scripted aircraft
// reads, and held after the last keyframe with zero rates. The simulator
// turns a MotionState into readings by inverting the pipeline's own models
// (SimulatedDataGenerator::scripted_readings), which is what lets the
// pipeline recover the script. The demo's keyframes are the table in
// FTS-DS-001 §7; its numbers live in motion_profile.cpp and nowhere else.

struct MotionState {
    float altitude_m = 0.0f;
    float pitch_deg = 0.0f;         // nose up positive
    float roll_deg = 0.0f;          // right roll positive (FTS-DS-001 §2.3)
    float track_deg = 0.0f;         // course over ground, 0 to 360
    float ground_speed_mps = 0.0f;
    float pitch_rate_dps = 0.0f;    // the segment's slopes: what the gyroscope reads
    float roll_rate_dps = 0.0f;
    float track_rate_dps = 0.0f;
};

// Metres per degree of latitude on a sphere of the mean Earth radius,
// 6,371,000 m; per degree of longitude, this times the cosine of the
// latitude. The profile places its fixes with it, and the bridge's
// METERS_PER_DEGREE (pipeline/bridge.py) is its twin, so the ground track
// plot's scale is the script's; within a few kilometres the error of the
// approximation is below half a percent.
constexpr double kMetersPerDegreeLatitude = 6'371'000.0 * 3.14159265358979323846 / 180.0;

class MotionProfile {
public:
    struct Keyframe {
        uint64_t t_ms;
        float altitude_m;
        float pitch_deg;
        float roll_deg;
        float track_deg;
        float ground_speed_mps;
    };

    // The profile of that name, or nullopt; main turns nullopt into a
    // refusal that lists known_names().
    static std::optional<MotionProfile> named(std::string_view name);
    static const char* known_names() { return "demo"; }

    const std::string& name() const { return name_; }
    uint64_t duration_ms() const { return keyframes_.back().t_ms; }

    // The state at a timestamp: linear within the segment that contains it,
    // with that segment's slopes as the rates (a keyframe belongs to the
    // segment it starts), and the last keyframe held with zero rates from
    // its time on.
    MotionState state_at(uint64_t timestamp_ms) const;

private:
    MotionProfile(std::string name, std::vector<Keyframe> keyframes);

    std::string name_;
    std::vector<Keyframe> keyframes_;   // ascending t_ms, the first at 0, at least one
};
