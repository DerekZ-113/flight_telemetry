#include "processing/redundant_processor.h"

RedundantProcessor::RedundantProcessor(const ProcessorConfig& tuning, uint64_t imu_timeout_ms, int persistence,
                                       LaneFaultHook* hook)
    : lanes_{{TelemetryProcessor(tuning, imu_timeout_ms), TelemetryProcessor(tuning, imu_timeout_ms),
              TelemetryProcessor(tuning, imu_timeout_ms)}},
      voter_(persistence),
      hook_(hook)
{
}

TelemetryFrame RedundantProcessor::process(const TelemetryFrame& checked) {
    // Each lane gets its own copy of the checked frame, so nothing one lane
    // or the hook does to its input reaches another lane.
    std::array<TelemetryFrame, kLaneCount> outputs{};
    for (std::size_t k = 0; k < kLaneCount; k++) {
        const int lane = static_cast<int>(k) + 1;
        TelemetryFrame input = checked;
        if (hook_ != nullptr) {
            hook_->perturb_input(lane, input);
        }
        outputs[k] = lanes_[k].process(input);
        if (hook_ != nullptr) {
            hook_->perturb_output(lane, outputs[k]);
        }
    }
    return voter_.vote(checked, outputs);
}

std::vector<FaultEvent> RedundantProcessor::take_events() {
    return voter_.take_events();
}
