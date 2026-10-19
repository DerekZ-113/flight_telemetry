#pragma once

#include <array>
#include <cstdint>

#include "campaign_run.h"
#include "run_schedule.h"

// What one run measured (REQ-TEST-005, FTS-FI-001 Section 4.4): every
// detector declaration in exactly one bucket, each channel's detection,
// latency and recovery, and the voter's outcomes. Pure: a function of the
// schedule and the timeline, so the tests hand it timelines built by hand.

struct ChannelOutcome {
    bool injected = false;          // the run's sensor fault touches this channel
    bool declared = false;          // a declaration inside the detection window
    int64_t latency_ms = -1;        // the first one's time minus the onset
    FaultType type = FaultType::COMM_TIMEOUT;
    float value = 0.0f;
    int repeats = 0;                // later declarations inside the window
    bool recovered = false;
    int64_t recovery_ms = -1;       // the first RECOVERY after removal, minus the removal
    int false_alarms = 0;
    int wrong_channel = 0;          // declared here while another channel's fault was on
    uint64_t exposure_ms = 0;       // time with nothing injected on this channel
};

struct RunOutcome {
    bool valid = true;              // every channel NOMINAL and every validity NORMAL before the onset
    std::array<ChannelOutcome, 3> channel{};   // BARO, IMU, GPS
    int64_t gps_fix_latency_ms = -1;           // a GPS detection from the first suppressed fix
    int declarations = 0;           // every declaration of the run, any bucket

    int voter_events = 0;           // LANE_DISAGREEMENT, LANE_FAULTED, NO_MAJORITY, LANE_OVERRUN
    int onset_events = 0;           // LANE_DISAGREEMENT and LANE_OVERRUN
    int overrun_onsets = 0;
    int exclusions = 0;
    int excluded_lane = 0;          // the first LANE_FAULTED's lane, 0 for none
    int64_t excluded_ms = -1;
    int64_t isolation_ms = -1;      // the first exclusion of an altered lane, from that lane's onset
    int healthy_excluded = 0;       // LANE_FAULTED naming a lane no fault altered
    int nm_episodes = 0;            // NO_MAJORITY events
    int nm_edges = 0;               // rising edges of the NO_MAJORITY bit
    int nm_frames = 0;
    int64_t nm_first_ms = -1;
    float nm_lanes = 0.0f;          // the first NO_MAJORITY event's value: the lanes in the vote
    int wrong_output_frames = 0;    // VOTED, a majority, and not the healthy lane's values (lane cells)
    int invariant_violations = 0;

    // The uncovered cells' consequence: the largest error against the twin
    // over frames from onset whose validity is NORMAL, per computed value.
    std::array<double, kComputedCount> max_error{};
    int normal_error_frames = 0;    // frames from onset with a NORMAL value off the twin's
    bool roll_infinite_normal = false;   // every frame from onset: roll +inf, NORMAL, voted with a majority

    // The validity's story over every frame of the run (FTS-FI-001 Section 5.6).
    int held_frames = 0;                 // every computed value FAILURE_WARNING
    bool normal_at_end = false;          // the run's last frame has every computed value NORMAL
    int nonfinite_voted_frames = 0;      // the emitted frame carries a computed value that is NaN or infinite
};

RunOutcome classify(const RunSchedule& schedule, const Timeline& timeline, const CampaignTiming& timing);

// The end of a channel's detection window: the first frame at or after the
// removal on which the channel has a fresh reading, the removal itself for
// a channel whose reading stays fresh, the run's end for a fault never
// removed.
uint64_t window_end(const RunSchedule& schedule, const Timeline& timeline, Channel channel);
