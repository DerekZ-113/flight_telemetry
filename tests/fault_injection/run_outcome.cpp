#include "run_outcome.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr uint16_t kVoted = 0x0001;
constexpr uint16_t kLaneBits = 0x000E;
constexpr uint16_t kNoMajority = 0x0010;

std::size_t index_of(Channel channel) {
    return static_cast<std::size_t>(channel);
}

bool is_declaration(FaultType type) {
    return type == FaultType::COMM_TIMEOUT || type == FaultType::STUCK || type == FaultType::OUT_OF_RANGE;
}

bool is_voter_event(FaultType type) {
    return type == FaultType::LANE_DISAGREEMENT || type == FaultType::LANE_FAULTED || type == FaultType::NO_MAJORITY ||
           type == FaultType::LANE_OVERRUN;
}

bool same_bits(float a, float b) {
    uint32_t x = 0;
    uint32_t y = 0;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
}

int bit_count(uint16_t bits) {
    int n = 0;
    for (; bits != 0; bits = static_cast<uint16_t>(bits & (bits - 1))) {
        n++;
    }
    return n;
}

bool lane_altered(const RunSchedule& s, int lane) {
    return lane_onset(s, lane) != kNever;
}

}  // namespace

uint64_t window_end(const RunSchedule& s, const Timeline& timeline, Channel channel) {
    if (s.sensor.removal_ms == kNever) {
        return s.end_ms;
    }
    if (s.sensor.mode != SensorMode::FAIL_READS) {
        return s.sensor.removal_ms;   // the reading stayed fresh: the window ends at the removal
    }
    const std::size_t c = index_of(channel);
    for (const TimelineFrame& f : timeline.frames) {
        if (f.t_ms >= s.sensor.removal_ms && f.read_ok[c]) {
            return f.t_ms;
        }
    }
    return s.end_ms;
}

RunOutcome classify(const RunSchedule& s, const Timeline& timeline, const CampaignTiming& timing) {
    RunOutcome out;
    const std::vector<Channel> touched = channels_touched(s.sensor);
    const uint64_t onset = s.sensor.onset_ms;
    const uint64_t run_ms = s.end_ms;

    // The injected channels' windows, and the span a declaration elsewhere
    // counts as wrong-channel: from onset to the last window's end plus the
    // recovery window the run leaves after removal.
    std::array<uint64_t, 3> window{};
    for (Channel c : touched) {
        out.channel[index_of(c)].injected = true;
        window[index_of(c)] = window_end(s, timeline, c);
    }
    // Every run ends a recovery window after its removal, so the span runs
    // to the run's end.
    const uint64_t fault_span_end = touched.empty() ? 0 : run_ms;
    for (std::size_t c = 0; c < 3; c++) {
        out.channel[c].exposure_ms = out.channel[c].injected ? std::min(onset, run_ms) : run_ms;
    }

    // The precondition: the frame before onset NOMINAL everywhere and NORMAL
    // in every value (FTS-FI-001 Section 4.1).
    uint64_t first_altered = onset;
    for (int lane = 1; lane <= static_cast<int>(kLaneCount); lane++) {
        first_altered = std::min(first_altered, lane_onset(s, lane));
    }
    if (first_altered != kNever && first_altered >= timing.period_ms) {
        for (const TimelineFrame& f : timeline.frames) {
            if (f.t_ms + timing.period_ms == first_altered) {
                const bool nominal = std::all_of(f.status.begin(), f.status.end(),
                                                 [](ChannelStatus st) { return st == ChannelStatus::NOMINAL; });
                const bool normal = std::all_of(f.validity.begin(), f.validity.end(),
                                                [](Validity v) { return v == Validity::NORMAL; });
                out.valid = nominal && normal;
            }
        }
    }

    for (const TimelineEvent& e : timeline.events) {
        if (is_declaration(e.type)) {
            out.declarations++;
            const std::size_t c = index_of(e.channel);
            ChannelOutcome& ch = out.channel[c];
            if (ch.injected) {
                const bool before = e.t_ms < onset;
                const bool inside = !before && e.t_ms < window[c];
                if (inside && !ch.declared) {
                    ch.declared = true;
                    ch.latency_ms = static_cast<int64_t>(e.t_ms - onset);
                    ch.type = e.type;
                    ch.value = e.value;
                } else if (inside) {
                    ch.repeats++;
                } else {
                    ch.false_alarms++;
                }
            } else if (!touched.empty() && e.t_ms >= onset && e.t_ms < fault_span_end) {
                ch.wrong_channel++;
            } else {
                ch.false_alarms++;
            }
        } else if (e.type == FaultType::RECOVERY) {
            ChannelOutcome& ch = out.channel[index_of(e.channel)];
            const bool after_removal = s.sensor.removal_ms != kNever && e.t_ms >= s.sensor.removal_ms;
            if (ch.injected && after_removal && !ch.recovered) {
                ch.recovered = true;
                ch.recovery_ms = static_cast<int64_t>(e.t_ms - s.sensor.removal_ms);
            }
        } else if (is_voter_event(e.type)) {
            out.voter_events++;
            if (e.type == FaultType::LANE_DISAGREEMENT || e.type == FaultType::LANE_OVERRUN) {
                out.onset_events++;
            }
            if (e.type == FaultType::LANE_OVERRUN) {
                out.overrun_onsets++;
            }
            if (e.type == FaultType::LANE_FAULTED) {
                out.exclusions++;
                const int lane = static_cast<int>(e.value);
                if (out.excluded_lane == 0) {
                    out.excluded_lane = lane;
                    out.excluded_ms = static_cast<int64_t>(e.t_ms);
                }
                if (lane_altered(s, lane)) {
                    if (out.isolation_ms < 0) {
                        out.isolation_ms = static_cast<int64_t>(e.t_ms - lane_onset(s, lane));
                    }
                } else {
                    out.healthy_excluded++;
                }
            }
            if (e.type == FaultType::NO_MAJORITY) {
                if (out.nm_episodes == 0) {
                    out.nm_first_ms = static_cast<int64_t>(e.t_ms);
                    out.nm_lanes = e.value;
                }
                out.nm_episodes++;
            }
        }
    }

    // The GPS on the fix clock: from the first fix the dropout suppressed.
    const ChannelOutcome& gps = out.channel[index_of(Channel::GPS)];
    if (gps.injected && gps.declared) {
        const uint64_t interval = timing.gps_interval_ms;
        const uint64_t first_fix = (onset + interval - 1) / interval * interval;
        out.gps_fix_latency_ms = static_cast<int64_t>(onset) + gps.latency_ms - static_cast<int64_t>(first_fix);
    }

    // The frames: the no-majority bit's edges and frames, wrong outputs,
    // the invariants, and the uncovered cells' error against the twin.
    const bool lane_cell = !s.lane_faults.empty();
    bool previous_nm = false;
    bool roll_inf_all = s.twin;
    bool saw_after_onset = false;
    for (const TimelineFrame& f : timeline.frames) {
        const bool nm = (f.voter_status & kNoMajority) != 0;
        if (nm) {
            out.nm_frames++;
        }
        if (nm && !previous_nm) {
            out.nm_edges++;
        }
        previous_nm = nm;
        const bool voted = (f.voter_status & kVoted) != 0;
        const bool values_nan = std::any_of(f.voted.begin(), f.voted.end(), [](float v) { return std::isnan(v); });
        if (!voted || bit_count(static_cast<uint16_t>(f.voter_status & kLaneBits)) > 1 || values_nan) {
            out.invariant_violations++;
        }
        if (lane_cell && voted && !nm) {
            for (std::size_t v = 0; v < kComputedCount; v++) {
                if (!same_bits(f.voted[v], f.reference[v])) {
                    out.wrong_output_frames++;
                    break;
                }
            }
        }
        if (s.twin && f.t_ms >= onset) {
            saw_after_onset = true;
            bool off = false;
            for (std::size_t v = 0; v < kComputedCount; v++) {
                if (f.validity[v] != Validity::NORMAL) {
                    continue;
                }
                const double error = std::fabs(static_cast<double>(f.voted[v]) - static_cast<double>(f.reference[v]));
                const double counted = std::isnan(error) ? INFINITY : error;
                out.max_error[v] = std::max(out.max_error[v], counted);
                if (counted > 0.0) {
                    off = true;
                }
            }
            if (off) {
                out.normal_error_frames++;
            }
            const bool roll_inf = std::isinf(f.voted[2]) && f.voted[2] > 0.0f && f.validity[2] == Validity::NORMAL &&
                                  voted && !nm;
            roll_inf_all = roll_inf_all && roll_inf;
        }
    }
    if (out.nm_edges != out.nm_episodes) {
        out.invariant_violations++;
    }
    out.roll_infinite_normal = roll_inf_all && saw_after_onset;
    return out;
}
