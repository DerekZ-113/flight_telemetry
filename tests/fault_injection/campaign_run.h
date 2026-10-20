#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "drivers/fault_injecting_source.h"
#include "drivers/simulated_data.h"
#include "logging/binary_logger.h"
#include "processing/fault_detector.h"
#include "processing/processor.h"
#include "processing/voter.h"
#include "run_schedule.h"
#include "telemetry_frame.h"

// One run of the campaign (REQ-TEST-005, FTS-DD-001 Section 14): the
// system's own objects, built for one schedule and stepped frame by frame,
// unpaced, with every fact the classifier and the predictions read recorded
// on the way.

// The configuration a campaign runs with: the CLI's from the file, the
// tests' the struct defaults (equal to the file by ConfigTest).
struct PipelineSettings {
    FaultDetectorConfig fault{};
    ProcessorConfig processing{};
    SimulationNoise noise{};
    CampaignTiming timing{};
};

// The five computed values in frame order, and their validities.
constexpr std::size_t kComputedCount = 5;

struct TimelineFrame {
    uint64_t t_ms = 0;
    std::array<ChannelStatus, 3> status{};             // BARO, IMU, GPS as the detector set them
    std::array<bool, 3> read_ok{};
    std::array<float, kRawFieldCount> raw{};           // the detector's input, RawField's order
    uint16_t voter_status = 0;
    std::array<float, kComputedCount> voted{};         // baro altitude, pitch, roll, fused altitude, vertical speed
    std::array<Validity, kComputedCount> validity{};
    std::array<float, kComputedCount> reference{};     // the healthy lane's real values, or the twin's voted ones
    std::array<bool, kLaneCount> outside{};            // each lane outside the healthy majority: stale, or disagreeing
    std::array<bool, kLaneCount> stale{};              // each lane's handed output for another frame
    std::array<bool, 3> pair_agree{};                  // lanes_agree over the handed outputs: pairs (1,2), (1,3), (2,3)
};

struct TimelineEvent {
    uint64_t t_ms = 0;
    Channel channel = Channel::BARO;
    FaultType type = FaultType::COMM_TIMEOUT;
    float value = 0.0f;
};

struct Timeline {
    std::vector<TimelineFrame> frames;
    std::vector<TimelineEvent> events;   // the detector's, the injector's and the voter's, as main logs them
};

// Runs the schedule; with a twin (the uncovered cells) runs its clean twin
// too, the same seed with no sensor fault, whose voted values become the
// reference. With `log`, the run's frames and events go to it as main
// writes them (`fault_campaign --seed <id> --log <dir>`).
Timeline run_schedule(const RunSchedule& schedule, const PipelineSettings& settings, BinaryLogger* log = nullptr);
