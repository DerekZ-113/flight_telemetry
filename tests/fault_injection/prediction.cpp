#include "prediction.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <string>

#include "fault_catalogue.h"

namespace {

std::size_t index_of(Channel channel) {
    return static_cast<std::size_t>(channel);
}

template <typename T>
std::string text(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

// The first departure from a prediction, named; later ones add nothing, the
// first being the one to read.
class Checker {
public:
    void expect(bool ok, const std::string& what) {
        if (!ok && verdict_.as_predicted) {
            verdict_.as_predicted = false;
            verdict_.mismatch = what;
        }
    }
    template <typename A, typename B>
    void equal(const A& got, const B& want, const std::string& what) {
        expect(got == want, what + ": " + text(got) + ", predicted " + text(want));
    }
    const Verdict& verdict() const { return verdict_; }

private:
    Verdict verdict_;
};

// The field a hold freezes: the barometer's pressure for a channel hold,
// the stuck tracker's one quantity there; else the field itself.
RawField held_field(const SensorFault& f) {
    return f.mode == SensorMode::HOLD_CHANNEL ? RawField::PRESSURE : f.field;
}

std::size_t frame_index(uint64_t t_ms, const CampaignTiming& t) {
    return static_cast<std::size_t>(t_ms / t.period_ms);
}

int64_t frames_from(uint64_t t_ms, const RunSchedule& s, const CampaignTiming& t) {
    return static_cast<int64_t>((s.end_ms - t_ms) / t.period_ms);
}

FaultType declared_type(SensorMode mode) {
    switch (mode) {
        case SensorMode::HOLD_CHANNEL:
        case SensorMode::HOLD_FIELD:
            return FaultType::STUCK;
        case SensorMode::OVERRIDE_FIELD:
            return FaultType::OUT_OF_RANGE;
        case SensorMode::NONE:
        case SensorMode::FAIL_READS:
        case SensorMode::BIAS_FIELD:
            break;
    }
    return FaultType::COMM_TIMEOUT;
}

void judge_detected(Checker& c, const RunSchedule& s, const Timeline& tl, const RunOutcome& o,
                    const CampaignTiming& t) {
    const SensorFault& f = s.sensor;
    const std::vector<Channel> touched = channels_touched(f);
    for (Channel channel : touched) {
        const ChannelOutcome& ch = o.channel[index_of(channel)];
        const std::string name = channel_name(channel);
        c.expect(ch.declared, name + ": no declaration inside the window");
        c.equal(fault_type_name(ch.type), std::string(fault_type_name(declared_type(f.mode))), name + " type");
        int64_t latency = 0;
        double value = f.value;
        int64_t recovery = recovery_time(0, t);
        if (f.mode == SensorMode::FAIL_READS && channel == Channel::GPS) {
            latency = gps_declared_at(f.onset_ms, t) - static_cast<int64_t>(f.onset_ms);
            value = static_cast<double>(gps_event_value(t));
            recovery = gps_recovery(f.removal_ms, t);
            c.equal(o.gps_fix_latency_ms, gps_fix_latency(t), "GPS latency from the first suppressed fix");
        } else if (f.mode == SensorMode::FAIL_READS) {
            latency = i2c_timeout_latency(t);
            value = static_cast<double>(timeout_event_value(latency, t));
        } else if (f.mode == SensorMode::HOLD_CHANNEL || f.mode == SensorMode::HOLD_FIELD) {
            latency = stuck_latency(prior_run(s, tl, t), t);
            const std::size_t onset_index = frame_index(f.onset_ms, t);
            value = tl.frames[onset_index].raw[static_cast<std::size_t>(held_field(f))];
            recovery = recovery_time(repeats_at_removal(s, tl, t), t);
        }
        c.equal(ch.latency_ms, latency, name + " latency");
        c.equal(static_cast<double>(ch.value), value, name + " event value");
        c.equal(ch.repeats, 0, name + " repeats");
        c.expect(ch.recovered, name + ": no recovery after removal");
        c.equal(ch.recovery_ms, recovery, name + " recovery");
    }
    for (std::size_t k = 0; k < 3; k++) {
        c.equal(o.channel[k].false_alarms, 0, std::string(channel_name(static_cast<Channel>(k))) + " false alarms");
        c.equal(o.channel[k].wrong_channel, 0, std::string(channel_name(static_cast<Channel>(k))) + " wrong channel");
    }
    c.equal(o.voter_events, 0, "voter events");
}

void judge_sub_threshold(Checker& c, const RunSchedule& s, const Timeline& tl, const RunOutcome& o,
                         const CampaignTiming& t) {
    const SensorFault& f = s.sensor;
    int expected = 0;
    const bool hold = f.mode == SensorMode::HOLD_CHANNEL || f.mode == SensorMode::HOLD_FIELD;
    if (hold) {
        // A hold of L frames trips only if the run before it and the frames
        // after it that happen to equal it bring it to the stuck count.
        const auto length = static_cast<int>((f.removal_ms - f.onset_ms) / t.period_ms);
        const int run = prior_run(s, tl, t) + length + repeats_at_removal(s, tl, t);
        expected = run >= t.stuck_count ? 1 : 0;
    }
    c.equal(o.declarations, expected, "declarations below the threshold");
    c.equal(o.voter_events, 0, "voter events");
}

void judge_uncovered(Checker& c, const RunSchedule& s, const RunOutcome& o, const CampaignTiming& t) {
    const std::string id = kinds()[s.kind].id;
    c.equal(o.declarations, 0, "declarations");
    if (id == "U06") {
        c.equal(o.voter_events, 1, "voter events");
        c.equal(o.nm_episodes, 1, "no-majority episodes");
        c.equal(o.nm_first_ms, static_cast<int64_t>(s.sensor.onset_ms), "no majority from");
        c.equal(o.nm_lanes, 3.0f, "lanes in the vote at no majority");
        c.equal(o.nm_frames, frames_from(s.sensor.onset_ms, s, t), "no-majority frames");
    } else if (id == "U07") {
        c.equal(o.voter_events, 0, "voter events");
        c.expect(o.roll_infinite_normal, "the roll is not +inf, NORMAL and voted on every frame from onset");
    } else {
        c.equal(o.voter_events, 0, "voter events");
        c.equal(o.nm_episodes, 0, "no-majority episodes");
    }
}

// The voter's count rule (REQ-RED-003, REQ-RED-006) replayed over the
// frames: a lane outside the healthy majority adds a miss, inside resets,
// the first miss of a run is an onset (an overrun when it was stale), and
// the persistence-th excludes it. With one lane altered every cycle has a
// majority, so this is the voter's rule, independently written.
struct Oracle {
    int onsets = 0;
    int overruns = 0;
    int64_t excluded_ms = -1;
};

Oracle replay_count(const Timeline& tl, int lane, int persistence) {
    Oracle o;
    const auto k = static_cast<std::size_t>(lane - 1);
    int misses = 0;
    for (const TimelineFrame& f : tl.frames) {
        if (!f.outside[k]) {
            misses = 0;
            continue;
        }
        misses++;
        if (misses == 1) {
            o.onsets++;
            if (f.stale[k]) {
                o.overruns++;
            }
        }
        if (misses >= persistence) {
            o.excluded_ms = static_cast<int64_t>(f.t_ms);
            break;
        }
    }
    return o;
}

int outside_frames(const Timeline& tl, int lane, bool stale_only) {
    const auto k = static_cast<std::size_t>(lane - 1);
    return static_cast<int>(std::count_if(tl.frames.begin(), tl.frames.end(), [&](const TimelineFrame& f) {
        return stale_only ? f.stale[k] : f.outside[k];
    }));
}

void judge_single_lane(Checker& c, const RunSchedule& s, const Timeline& tl, const RunOutcome& o,
                       const CampaignTiming& t, const std::string& id) {
    const LaneFault& fault = s.lane_faults.front();
    const Oracle oracle = replay_count(tl, fault.lane, s.persistence);
    const int exclusions = oracle.excluded_ms >= 0 ? 1 : 0;
    c.equal(o.onset_events, oracle.onsets, "onset events");
    c.equal(o.overrun_onsets, oracle.overruns, "overrun onsets");
    c.equal(o.exclusions, exclusions, "exclusions");
    c.equal(o.voter_events, oracle.onsets + exclusions, "voter events");
    if (exclusions == 1) {
        c.equal(o.excluded_lane, fault.lane, "excluded lane");
        c.equal(o.excluded_ms, oracle.excluded_ms, "excluded at");
    }
    c.equal(o.healthy_excluded, 0, "healthy lanes excluded");
    c.equal(o.nm_episodes, 0, "no-majority episodes");
    c.equal(o.wrong_output_frames, 0, "wrong outputs");

    // The injector did what the kind says (the oracle covers the rest).
    const int64_t from_onset = frames_from(fault.onset_ms, s, t);
    const int64_t isolation = isolation_latency(s.persistence, t);
    if (id == "V01" || id == "V02" || id == "V03") {
        c.equal(static_cast<int64_t>(outside_frames(tl, fault.lane, false)), from_onset, "frames outside the majority");
        c.equal(o.excluded_ms, static_cast<int64_t>(fault.onset_ms) + isolation, "excluded at onset + (P - 1) frames");
        c.equal(o.isolation_ms, isolation, "isolation latency");
    }
    if (id == "V03") {
        c.equal(static_cast<int64_t>(outside_frames(tl, fault.lane, true)), from_onset, "stale frames");
        c.equal(o.overrun_onsets, 1, "overrun onsets");
    }
    if (id == "V04" || id == "V05" || id == "V06") {
        c.equal(outside_frames(tl, fault.lane, false), 1, "frames outside the majority");
        c.equal(o.exclusions, s.persistence == 1 ? 1 : 0, "exclusions");
    }
    if (id == "V06") {
        c.equal(outside_frames(tl, fault.lane, true), 1, "stale frames");
    }
    if (id == "V07") {
        c.equal(outside_frames(tl, fault.lane, false), s.length_cycles, "frames outside the majority");
        c.equal(o.exclusions, s.length_cycles >= s.persistence ? 1 : 0, "exclusions");
    }
}

void judge_two_lanes(Checker& c, const RunSchedule& s, const RunOutcome& o, const CampaignTiming& t,
                     const std::string& id) {
    const int a = s.lane_faults[0].lane;
    const int b = s.lane_faults[1].lane;
    const int healthy = 6 - a - b;
    const uint64_t t1 = s.lane_faults[0].onset_ms;
    const uint64_t t2 = s.lane_faults[1].onset_ms;
    const int g = s.gap_cycles;
    const int p = s.persistence;
    const int64_t isolation = isolation_latency(p, t);
    c.expect(healthy >= 1 && healthy <= 3 && a != b, "two distinct lanes");
    if (id == "V09") {
        c.equal(o.onset_events, 1, "onset events");
        c.equal(o.exclusions, 1, "exclusions");
        c.equal(o.excluded_lane, healthy, "excluded lane");
        c.equal(o.excluded_ms, static_cast<int64_t>(t1) + isolation, "excluded at");
        c.equal(o.healthy_excluded, 1, "healthy lanes excluded");
        c.equal(o.nm_episodes, 0, "no-majority episodes");
        c.equal(static_cast<int64_t>(o.wrong_output_frames), frames_from(t1, s, t), "wrong outputs");
    } else if (id == "V10") {
        c.equal(o.voter_events, 1, "voter events");
        c.equal(o.nm_episodes, 1, "no-majority episodes");
        c.equal(o.nm_first_ms, static_cast<int64_t>(t1), "no majority from");
        c.equal(o.nm_lanes, 3.0f, "lanes in the vote at no majority");
        c.equal(static_cast<int64_t>(o.nm_frames), frames_from(t1, s, t), "no-majority frames");
        c.equal(o.wrong_output_frames, 0, "wrong outputs");
    } else if (id == "V11" || (id == "V12" && g >= p)) {
        c.equal(o.onset_events, g >= 1 ? 1 : 0, "onset events");
        c.equal(o.exclusions, g >= p ? 1 : 0, "exclusions");
        if (g >= p) {
            c.equal(o.excluded_lane, a, "excluded lane");
            c.equal(o.excluded_ms, static_cast<int64_t>(t1) + isolation, "excluded at");
        }
        c.equal(o.nm_episodes, 1, "no-majority episodes");
        c.equal(o.nm_first_ms, static_cast<int64_t>(t2), "no majority from");
        c.equal(o.nm_lanes, g >= p ? 2.0f : 3.0f, "lanes in the vote at no majority");
        c.equal(static_cast<int64_t>(o.nm_frames), frames_from(t2, s, t), "no-majority frames");
        c.equal(o.healthy_excluded, 0, "healthy lanes excluded");
        c.equal(o.wrong_output_frames, 0, "wrong outputs");
    } else {   // V12 with g < P: the two wrong lanes form a majority
        c.equal(o.onset_events, (g >= 1 ? 1 : 0) + 1, "onset events");
        c.equal(o.exclusions, 1, "exclusions");
        c.equal(o.excluded_lane, healthy, "excluded lane");
        c.equal(o.excluded_ms, static_cast<int64_t>(t2) + isolation, "excluded at");
        c.equal(o.healthy_excluded, 1, "healthy lanes excluded");
        c.equal(o.nm_episodes, 0, "no-majority episodes");
        c.equal(static_cast<int64_t>(o.wrong_output_frames), frames_from(t2, s, t), "wrong outputs");
    }
}

bool fault_active(const RunSchedule& s, int lane, uint64_t t_ms) {
    return std::any_of(s.lane_faults.begin(), s.lane_faults.end(), [lane, t_ms](const LaneFault& f) {
        return f.lane == lane && f.onset_ms <= t_ms && t_ms < f.end_ms;
    });
}

// The replayed vote against the voter's own events and wrong outputs.
void judge_replayed(Checker& c, const RunSchedule& s, const Timeline& tl, const RunOutcome& o) {
    const ReplayedVote vote = replay_vote(s, tl);
    std::vector<TimelineEvent> measured;
    for (const TimelineEvent& e : tl.events) {
        const bool voters = e.channel == Channel::VOTER && e.type != FaultType::LANE_FAULT_INJECTED;
        if (voters) {
            measured.push_back(e);
        }
    }
    c.equal(measured.size(), vote.events.size(), "voter events against the replayed vote");
    for (std::size_t i = 0; i < std::min(measured.size(), vote.events.size()); i++) {
        const std::string at = "replayed vote event " + std::to_string(i);
        c.equal(measured[i].t_ms, vote.events[i].t_ms, at + " time");
        c.equal(fault_type_name(measured[i].type), std::string(fault_type_name(vote.events[i].type)), at + " type");
        c.equal(measured[i].value, vote.events[i].value, at + " value");
    }
    c.equal(o.wrong_output_frames, vote.altered_representative_frames, "wrong outputs against the replayed vote");
}

Verdict judge_cells(const RunSchedule& s, const Timeline& tl, const RunOutcome& o, const CampaignTiming& t,
                    bool as_written) {
    Checker c;
    c.expect(o.valid, "the precondition at onset: every channel NOMINAL and every value NORMAL");
    c.equal(o.invariant_violations, 0, "invariant violations");
    const KindSpec& kind = kinds()[s.kind];
    const std::string id = kind.id;
    switch (kind.cls) {
        case CellClass::DETECTED:
            judge_detected(c, s, tl, o, t);
            break;
        case CellClass::SUB_THRESHOLD:
            judge_sub_threshold(c, s, tl, o, t);
            break;
        case CellClass::UNCOVERED:
            judge_uncovered(c, s, o, t);
            break;
        case CellClass::CLEAN:
            c.equal(o.declarations, 0, "declarations");
            c.equal(o.voter_events, 0, "voter events");
            break;
        case CellClass::LANE:
            c.equal(o.declarations, 0, "declarations");
            if (s.lane_faults.size() == 1) {
                judge_single_lane(c, s, tl, o, t, id);
            } else if (!as_written && (id == "V09" || id == "V12")) {
                judge_replayed(c, s, tl, o);
            } else {
                judge_two_lanes(c, s, o, t, id);
            }
            break;
    }
    return c.verdict();
}

}  // namespace

ReplayedVote replay_vote(const RunSchedule& s, const Timeline& tl) {
    // The pairs in the voter's order; each frame's agreement of a pair is
    // recorded once, and a lane trivially agrees with itself (a
    // representative comes from an agreeing pair, so it holds no NaN).
    constexpr std::array<std::array<std::size_t, 2>, 3> kPairs{{{0, 1}, {0, 2}, {1, 2}}};
    const auto agree = [](const TimelineFrame& f, std::size_t i, std::size_t j) {
        if (i == j) {
            return true;
        }
        const std::size_t low = std::min(i, j);
        const std::size_t high = std::max(i, j);
        return f.pair_agree[low == 0 ? high - 1 : 2];
    };
    ReplayedVote out;
    std::array<bool, kLaneCount> excluded{};
    std::array<int, kLaneCount> misses{};
    bool in_no_majority = false;
    for (const TimelineFrame& f : tl.frames) {
        std::array<bool, kLaneCount> eligible{};
        for (std::size_t k = 0; k < kLaneCount; k++) {
            eligible[k] = !excluded[k] && !f.stale[k];
        }
        const auto majority = std::find_if(kPairs.begin(), kPairs.end(), [&](const auto& pair) {
            return eligible[pair[0]] && eligible[pair[1]] && agree(f, pair[0], pair[1]);
        });
        const int representative = majority == kPairs.end() ? -1 : static_cast<int>((*majority)[0]);
        if (representative < 0) {
            if (!in_no_majority) {
                in_no_majority = true;
                const auto in_vote = static_cast<float>(std::count(excluded.begin(), excluded.end(), false));
                out.events.push_back(TimelineEvent{f.t_ms, Channel::VOTER, FaultType::NO_MAJORITY, in_vote});
            }
            continue;
        }
        in_no_majority = false;
        if (fault_active(s, representative + 1, f.t_ms)) {
            out.altered_representative_frames++;
        }
        const auto rep = static_cast<std::size_t>(representative);
        for (std::size_t k = 0; k < kLaneCount; k++) {
            if (excluded[k]) {
                continue;
            }
            if (!f.stale[k] && agree(f, k, rep)) {
                misses[k] = 0;
                continue;
            }
            misses[k]++;
            const float lane = static_cast<float>(k + 1);
            if (misses[k] == 1) {
                const FaultType onset = f.stale[k] ? FaultType::LANE_OVERRUN : FaultType::LANE_DISAGREEMENT;
                out.events.push_back(TimelineEvent{f.t_ms, Channel::VOTER, onset, lane});
            }
            if (misses[k] >= s.persistence) {
                excluded[k] = true;
                out.events.push_back(TimelineEvent{f.t_ms, Channel::VOTER, FaultType::LANE_FAULTED, lane});
            }
        }
    }
    return out;
}

int64_t i2c_timeout_latency(const CampaignTiming& t) {
    return static_cast<int64_t>(t.i2c_timeout_ms / t.period_ms * t.period_ms);
}

int64_t timeout_event_value(int64_t latency_ms, const CampaignTiming& t) {
    return latency_ms + static_cast<int64_t>(t.period_ms);
}

int64_t gps_event_value(const CampaignTiming& t) {
    return static_cast<int64_t>((t.uart_timeout_ms / t.period_ms + 1) * t.period_ms);
}

int64_t gps_declared_at(uint64_t onset_ms, const CampaignTiming& t) {
    const uint64_t last_fix = (onset_ms - 1) / t.gps_interval_ms * t.gps_interval_ms;
    return static_cast<int64_t>(last_fix) + gps_event_value(t);
}

int64_t gps_fix_latency(const CampaignTiming& t) {
    return gps_event_value(t) - static_cast<int64_t>(t.gps_interval_ms);
}

int64_t gps_recovery(uint64_t removal_ms, const CampaignTiming& t) {
    const uint64_t first_fix = (removal_ms + t.gps_interval_ms - 1) / t.gps_interval_ms * t.gps_interval_ms;
    const uint64_t recovered = first_fix + static_cast<uint64_t>(t.recovery_count - 1) * t.gps_interval_ms;
    return static_cast<int64_t>(recovered - removal_ms);
}

int64_t stuck_latency(int prior, const CampaignTiming& t) {
    return static_cast<int64_t>(t.stuck_count - 1 - prior) * static_cast<int64_t>(t.period_ms);
}

int64_t recovery_time(int repeats, const CampaignTiming& t) {
    return static_cast<int64_t>(t.recovery_count - 1 + repeats) * static_cast<int64_t>(t.period_ms);
}

int64_t isolation_latency(int persistence, const CampaignTiming& t) {
    return static_cast<int64_t>(persistence - 1) * static_cast<int64_t>(t.period_ms);
}

int prior_run(const RunSchedule& s, const Timeline& tl, const CampaignTiming& t) {
    const auto field = static_cast<std::size_t>(held_field(s.sensor));
    const std::size_t before = frame_index(s.sensor.onset_ms, t) - 1;
    const float value = tl.frames[before].raw[field];
    int run = 1;
    for (std::size_t j = before; j > 0 && run < t.stuck_count; j--) {
        if (tl.frames[j - 1].raw[field] != value) {
            break;
        }
        run++;
    }
    return run;
}

int repeats_at_removal(const RunSchedule& s, const Timeline& tl, const CampaignTiming& t) {
    const auto field = static_cast<std::size_t>(held_field(s.sensor));
    const float held = tl.frames[frame_index(s.sensor.onset_ms, t)].raw[field];
    int repeats = 0;
    for (std::size_t j = frame_index(s.sensor.removal_ms, t); j < tl.frames.size(); j++) {
        if (tl.frames[j].raw[field] != held) {
            break;
        }
        repeats++;
    }
    return repeats;
}

Verdict judge(const RunSchedule& s, const Timeline& tl, const RunOutcome& o, const CampaignTiming& t) {
    return judge_cells(s, tl, o, t, false);
}

Verdict judge_as_written(const RunSchedule& s, const Timeline& tl, const RunOutcome& o, const CampaignTiming& t) {
    return judge_cells(s, tl, o, t, true);
}
