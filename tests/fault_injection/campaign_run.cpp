#include "campaign_run.h"

#include <memory>

#include "drivers/simulated_source.h"
#include "processing/lane_fault_hook.h"
#include "processing/lane_fault_injector.h"
#include "processing/redundant_processor.h"

namespace {

// The five computed values and their validities, in frame order.
std::array<float, kComputedCount> computed(const TelemetryFrame& f) {
    return {f.baro_altitude_m, f.pitch_deg, f.roll_deg, f.fused_altitude_m, f.vertical_speed_mps};
}

std::array<Validity, kComputedCount> validities(const TelemetryFrame& f) {
    return {f.baro_altitude_validity, f.pitch_validity, f.roll_validity, f.fused_altitude_validity,
            f.vertical_speed_validity};
}

std::array<float, kRawFieldCount> raw_fields(const TelemetryFrame& f) {
    return {f.pressure_hpa, f.temperature_c, f.accel_x, f.accel_y, f.accel_z,
            f.gyro_x, f.gyro_y, f.gyro_z, f.gps_altitude_m};
}

// The injector as the processor sees it, with the outputs it handed the
// voter kept: the lane-fault interface twice, the inner one doing the work.
class ObservedLanes : public LaneFaultHook {
public:
    explicit ObservedLanes(LaneFaultInjector& inner) : inner_(inner) {}

    void perturb_input(int lane, TelemetryFrame& lane_input) override {
        inner_.perturb_input(lane, lane_input);
    }
    void perturb_output(int lane, TelemetryFrame& lane_output) override {
        inner_.perturb_output(lane, lane_output);
        handed_[static_cast<std::size_t>(lane - 1)] = lane_output;
    }

    const std::array<TelemetryFrame, kLaneCount>& handed() const { return handed_; }

private:
    LaneFaultInjector& inner_;
    std::array<TelemetryFrame, kLaneCount> handed_{};
};

void switch_sensor_fault(FaultInjectingSource& source, const SensorFault& fault, bool on) {
    switch (fault.mode) {
        case SensorMode::NONE:
            break;
        case SensorMode::FAIL_READS:
            for (Channel channel : fault.channels) {
                source.fail_reads(channel, on);
            }
            break;
        case SensorMode::HOLD_CHANNEL:
            for (Channel channel : fault.channels) {
                source.hold_values(channel, on);
            }
            break;
        case SensorMode::HOLD_FIELD:
            source.hold_field(fault.field, on);
            break;
        case SensorMode::OVERRIDE_FIELD:
            if (on) {
                source.override_field(fault.field, fault.value);
            } else {
                source.clear_override(fault.field);
            }
            break;
        case SensorMode::BIAS_FIELD:
            if (on) {
                source.bias_field(fault.field, fault.value, fault.rate_per_s);
            } else {
                source.clear_bias(fault.field);
            }
            break;
    }
}

Timeline run_once(const RunSchedule& s, const PipelineSettings& settings, bool inject_sensor, BinaryLogger* log) {
    const CampaignTiming& timing = settings.timing;
    FaultInjectingSource source(std::make_unique<SimulatedSource>(timing.period_ms, s.sim_seed, timing.gps_interval_ms,
                                                                  settings.noise));
    FaultDetector detector(settings.fault);
    LaneFaultInjector injector(s.lane_faults);
    ObservedLanes observed(injector);
    RedundantProcessor processor(settings.processing, settings.fault.i2c_timeout_ms, s.persistence, &observed);
    const int reference = reference_lane(s);
    const std::size_t ref = reference > 0 ? static_cast<std::size_t>(reference - 1) : 0;

    Timeline timeline;
    const uint64_t frames = s.end_ms / timing.period_ms;
    timeline.frames.reserve(frames);
    for (uint64_t i = 0; i < frames; i++) {
        const uint64_t t = i * timing.period_ms;
        if (inject_sensor && t == s.sensor.onset_ms) {
            switch_sensor_fault(source, s.sensor, true);
        }
        if (inject_sensor && t == s.sensor.removal_ms) {
            switch_sensor_fault(source, s.sensor, false);
        }
        const TelemetryFrame raw = source.read_frame();
        const TelemetryFrame checked = detector.check(raw);
        const TelemetryFrame out = processor.process(checked);

        TimelineFrame f;
        f.t_ms = checked.timestamp_ms;
        f.status = {checked.baro_status, checked.imu_status, checked.gps_status};
        f.read_ok = {checked.baro_read_ok, checked.imu_read_ok, checked.gps_read_ok};
        f.raw = raw_fields(checked);
        f.voter_status = out.voter_status;
        f.voted = computed(out);
        f.validity = validities(out);
        const TelemetryFrame& healthy = injector.real_outputs()[ref];
        f.reference = computed(healthy);
        for (std::size_t k = 0; k < kLaneCount; k++) {
            const TelemetryFrame& handed = observed.handed()[k];
            f.stale[k] = handed.timestamp_ms != checked.timestamp_ms || handed.sequence != checked.sequence;
            f.outside[k] = f.stale[k] || !lanes_agree(handed, healthy);
        }
        const auto& h = observed.handed();
        f.pair_agree = {lanes_agree(h[0], h[1]), lanes_agree(h[0], h[2]), lanes_agree(h[1], h[2])};
        timeline.frames.push_back(f);

        // As main logs them: the frame, the detector's events, the
        // injector's, then the voter's.
        if (log != nullptr) {
            log->log_frame(out);
        }
        for (const std::vector<FaultEvent>& batch : {detector.take_events(), injector.take_events(),
                                                     processor.take_events()}) {
            for (const FaultEvent& e : batch) {
                timeline.events.push_back(TimelineEvent{e.timestamp_ms, e.channel, e.type, e.value});
                if (log != nullptr) {
                    log->log_event(e);
                }
            }
        }
    }
    return timeline;
}

}  // namespace

Timeline run_schedule(const RunSchedule& schedule, const PipelineSettings& settings, BinaryLogger* log) {
    Timeline timeline = run_once(schedule, settings, true, log);
    if (schedule.twin) {
        // The same seed with no sensor fault: the injectors draw no random
        // number, so the twin is the run's exact counterfactual, and its
        // voted values are what the faulted run should have shown.
        const Timeline twin = run_once(schedule, settings, false, nullptr);
        for (std::size_t i = 0; i < timeline.frames.size(); i++) {
            timeline.frames[i].reference = twin.frames[i].voted;
        }
    }
    return timeline;
}
