#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "telemetry_frame.h"
#include "processing/fault_detector.h"
#include "processing/lane_fault_hook.h"
#include "processing/processor.h"
#include "processing/voter.h"

// The processing pipeline as three identical lanes and a voter
// (REQ-RED-001, design.md section 13). Each lane is a TelemetryProcessor
// with state of its own; every lane processes its own copy of every frame
// the detector checked, one after another in the caller's thread; the
// voter chooses the frame the system emits. The detector stays outside,
// ahead of the lanes, so a sensor fault is one event and a channel's
// status keeps one writer; the only events this class hands out are the
// voter's.
//
// All three lanes run on every cycle, an excluded one included, so the
// cycle's work is the same on every cycle and the timing measured is the
// worst case; an excluded lane's output is ignored.
class RedundantProcessor {
public:
    // Every lane is built from the same tuning and IMU timeout, which main
    // takes from the configuration (REQ-CFG-001); the voter's persistence
    // count is the constructor's for the fault-injection harness to sweep;
    // `hook` is the lane-fault interface of REQ-RED-005, not owned, null in
    // the binary (the "absent" idiom of the scheduler and the pacer).
    explicit RedundantProcessor(const ProcessorConfig& tuning = ProcessorConfig{},
                                uint64_t imu_timeout_ms = TelemetryProcessor::kDefaultImuTimeoutMs,
                                int persistence = Voter::kLanePersistence,
                                LaneFaultHook* hook = nullptr);

    // One cycle: each lane processes a copy of `checked`, the hook altering
    // its input and output when there is one, and the voter chooses.
    TelemetryFrame process(const TelemetryFrame& checked);

    // The voter's events since the previous call.
    std::vector<FaultEvent> take_events();

private:
    // Built element by element in the constructor, never by default:
    // TelemetryProcessor's constructor defaults every argument to the test
    // baseline, which equals the shipped file, so a default-built array
    // would run the wrong tuning and no session test would notice
    // (RedundantProcessorTest.TuningReachesEveryLane).
    std::array<TelemetryProcessor, kLaneCount> lanes_;
    Voter voter_;
    LaneFaultHook* hook_;
};
