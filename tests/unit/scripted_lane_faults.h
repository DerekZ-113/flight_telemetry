#pragma once

// A LaneFaultHook for the unit tests (REQ-RED-005): each perturbation is a
// function of the cycle and the lane, so a test scripts "alter lane 2's
// output on cycle 50" in one lambda; every lane output is also recorded
// for the cycle, so a test can check that the three lanes stayed
// bit-identical. The fault-injection harness of the next topic will bring
// its own; this is the smallest one the voter's tests need.

#include <array>
#include <cstddef>
#include <cstring>
#include <functional>

#include "processing/lane_fault_hook.h"
#include "processing/voter.h"
#include "telemetry_frame.h"

class ScriptedLaneFaults : public LaneFaultHook {
public:
    using Perturbation = std::function<void(int cycle, int lane, TelemetryFrame& frame)>;

    Perturbation on_input;    // empty: inputs pass through
    Perturbation on_output;   // empty: outputs pass through

    void perturb_input(int lane, TelemetryFrame& lane_input) override {
        if (lane == 1) {
            cycle_++;
        }
        input_calls++;
        if (on_input) {
            on_input(cycle_, lane, lane_input);
        }
    }

    void perturb_output(int lane, TelemetryFrame& lane_output) override {
        output_calls++;
        if (on_output) {
            on_output(cycle_, lane, lane_output);
        }
        outputs_[static_cast<std::size_t>(lane - 1)] = lane_output;
        if (lane == static_cast<int>(kLaneCount)) {
            const bool same = std::memcmp(&outputs_[0], &outputs_[1], sizeof(TelemetryFrame)) == 0 &&
                              std::memcmp(&outputs_[1], &outputs_[2], sizeof(TelemetryFrame)) == 0;
            if (!same) {
                cycles_lanes_differed++;
            }
        }
    }

    // The cycle being processed, counted from 0 at the first.
    int cycle() const { return cycle_; }

    // The last cycle's three lane outputs, as the voter saw them.
    const std::array<TelemetryFrame, kLaneCount>& outputs() const { return outputs_; }

    int input_calls = 0;
    int output_calls = 0;
    int cycles_lanes_differed = 0;

private:
    int cycle_ = -1;
    std::array<TelemetryFrame, kLaneCount> outputs_{};
};
