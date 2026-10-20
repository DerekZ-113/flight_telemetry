#include "campaign_record.h"

#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "fault_catalogue.h"

namespace {

const char* kChannelPrefix[3] = {"baro", "imu", "gps"};

const char* mode_name(SensorMode mode) {
    switch (mode) {
        case SensorMode::NONE: return "NONE";
        case SensorMode::FAIL_READS: return "FAIL_READS";
        case SensorMode::HOLD_CHANNEL: return "HOLD_CHANNEL";
        case SensorMode::HOLD_FIELD: return "HOLD_FIELD";
        case SensorMode::OVERRIDE_FIELD: return "OVERRIDE_FIELD";
        case SensorMode::BIAS_FIELD: return "BIAS_FIELD";
    }
    return "UNKNOWN";
}

const char* raw_field_name(RawField field) {
    static const char* names[kRawFieldCount] = {"PRESSURE", "TEMPERATURE", "ACCEL_X", "ACCEL_Y", "ACCEL_Z",
                                                "GYRO_X", "GYRO_Y", "GYRO_Z", "GPS_ALTITUDE"};
    return names[static_cast<std::size_t>(field)];
}

const char* lane_kind_name(LaneFaultKind kind) {
    switch (kind) {
        case LaneFaultKind::OUTPUT_BIT_FLIP: return "OUTPUT_BIT_FLIP";
        case LaneFaultKind::OUTPUT_NAN: return "OUTPUT_NAN";
        case LaneFaultKind::OUTPUT_STALE: return "OUTPUT_STALE";
        case LaneFaultKind::INPUT_BIT_FLIP: return "INPUT_BIT_FLIP";
    }
    return "UNKNOWN";
}

const char* lane_field_name(LaneField field) {
    static const char* names[kLaneFieldCount] = {"BARO_ALTITUDE", "PITCH", "ROLL", "FUSED_ALTITUDE", "VERTICAL_SPEED",
                                                 "PRESSURE", "ACCEL_X", "ACCEL_Y", "ACCEL_Z", "GYRO_X", "GYRO_Y",
                                                 "GYRO_Z", "GPS_ALTITUDE"};
    return names[static_cast<std::size_t>(field)];
}

const char* class_name(CellClass cls) {
    switch (cls) {
        case CellClass::DETECTED: return "DETECTED";
        case CellClass::SUB_THRESHOLD: return "SUB_THRESHOLD";
        case CellClass::UNCOVERED: return "UNCOVERED";
        case CellClass::CLEAN: return "CLEAN";
        case CellClass::LANE: return "LANE";
    }
    return "UNKNOWN";
}

// One field: quoted when it holds a comma or a quote, its quotes doubled.
std::string field(const std::string& text) {
    if (text.find_first_of(",\"") == std::string::npos) {
        return text;
    }
    std::string out = "\"";
    for (char c : text) {
        out += c;
        if (c == '"') {
            out += '"';
        }
    }
    return out + "\"";
}

// Enough digits for a float or a double to read back as itself.
std::string number(double value, int digits = 17) {
    std::ostringstream out;
    out << std::setprecision(digits) << value;
    return out.str();
}

std::string ms(uint64_t value) {
    return value == kNever ? "" : std::to_string(value);
}

class Row {
public:
    Row& add(const std::string& text) {
        if (!first_) {
            out_ << ',';
        }
        out_ << field(text);
        first_ = false;
        return *this;
    }
    Row& add(int64_t value) { return add(std::to_string(value)); }
    Row& add(bool value) { return add(std::string(value ? "1" : "0")); }
    Row& add_float(float value) { return add(number(value, 9)); }
    Row& add_double(double value) { return add(number(value)); }
    std::string str() const { return out_.str(); }

private:
    std::ostringstream out_;
    bool first_ = true;
};

const char* kComputedNames[kComputedCount] = {"baro_alt_m", "pitch_deg", "roll_deg", "fused_alt_m", "vs_mps"};

}  // namespace

std::string csv_header() {
    Row r;
    for (const char* name : {"run_id", "cell", "kind", "persistence", "replicate", "sim_seed", "sensor_mode",
                             "channels", "field", "value", "rate_per_s", "onset_ms", "removal_ms", "end_ms",
                             "lane_faults", "length_cycles", "gap_cycles", "valid", "as_predicted", "as_written",
                             "mismatch"}) {
        r.add(std::string(name));
    }
    for (const char* c : kChannelPrefix) {
        for (const char* suffix : {"injected", "declared", "latency_ms", "type", "value", "repeats", "recovered",
                                   "recovery_ms", "false_alarms", "wrong_channel", "exposure_ms"}) {
            r.add(std::string(c) + "_" + suffix);
        }
    }
    for (const char* name : {"gps_fix_latency_ms", "declarations", "voter_events", "onset_events", "overrun_onsets",
                             "exclusions", "excluded_lane", "excluded_ms", "isolation_ms", "healthy_excluded",
                             "nm_episodes", "nm_edges", "nm_frames", "nm_first_ms", "nm_lanes", "wrong_output_frames",
                             "invariant_violations"}) {
        r.add(std::string(name));
    }
    for (const char* name : kComputedNames) {
        r.add(std::string("err_") + name);
    }
    r.add(std::string("normal_error_frames")).add(std::string("roll_infinite_normal"));
    r.add(std::string("held_frames")).add(std::string("normal_at_end")).add(std::string("nonfinite_voted_frames"));
    return r.str();
}

std::string csv_row(const RunSchedule& s, const RunOutcome& o, const Verdict& v, const Verdict& written) {
    Row r;
    std::string channels;
    for (Channel c : s.sensor.channels) {
        channels += (channels.empty() ? "" : "+") + std::string(channel_name(c));
    }
    std::string lanes;
    for (const LaneFault& f : s.lane_faults) {
        lanes += (lanes.empty() ? "" : ";") + std::to_string(f.lane) + ":" + lane_kind_name(f.kind) + ":" +
                 lane_field_name(f.field) + ":" + std::to_string(static_cast<int>(f.bit)) + ":" +
                 std::to_string(f.onset_ms) + ":" + (f.end_ms == LaneFault::kPersistent ? "" : std::to_string(f.end_ms));
    }
    const bool field_mode = s.sensor.mode == SensorMode::HOLD_FIELD || s.sensor.mode == SensorMode::OVERRIDE_FIELD ||
                            s.sensor.mode == SensorMode::BIAS_FIELD;
    r.add(static_cast<int64_t>(s.run_id))
        .add(cell_name(s.cell))
        .add(std::string(kinds()[s.kind].id))
        .add(static_cast<int64_t>(s.persistence))
        .add(static_cast<int64_t>(s.replicate))
        .add(static_cast<int64_t>(s.sim_seed))
        .add(std::string(mode_name(s.sensor.mode)))
        .add(channels)
        .add(std::string(field_mode ? raw_field_name(s.sensor.field) : ""))
        .add_float(s.sensor.value)
        .add_float(s.sensor.rate_per_s)
        .add(ms(s.sensor.onset_ms))
        .add(ms(s.sensor.removal_ms))
        .add(ms(s.end_ms))
        .add(lanes)
        .add(static_cast<int64_t>(s.length_cycles))
        .add(static_cast<int64_t>(s.gap_cycles))
        .add(o.valid)
        .add(v.as_predicted)
        .add(written.as_predicted)
        .add(v.as_predicted ? written.mismatch : v.mismatch);
    for (const ChannelOutcome& ch : o.channel) {
        r.add(ch.injected)
            .add(ch.declared)
            .add(ch.latency_ms)
            .add(std::string(ch.declared ? fault_type_name(ch.type) : ""))
            .add_float(ch.value)
            .add(static_cast<int64_t>(ch.repeats))
            .add(ch.recovered)
            .add(ch.recovery_ms)
            .add(static_cast<int64_t>(ch.false_alarms))
            .add(static_cast<int64_t>(ch.wrong_channel))
            .add(static_cast<int64_t>(ch.exposure_ms));
    }
    r.add(o.gps_fix_latency_ms)
        .add(static_cast<int64_t>(o.declarations))
        .add(static_cast<int64_t>(o.voter_events))
        .add(static_cast<int64_t>(o.onset_events))
        .add(static_cast<int64_t>(o.overrun_onsets))
        .add(static_cast<int64_t>(o.exclusions))
        .add(static_cast<int64_t>(o.excluded_lane))
        .add(o.excluded_ms)
        .add(o.isolation_ms)
        .add(static_cast<int64_t>(o.healthy_excluded))
        .add(static_cast<int64_t>(o.nm_episodes))
        .add(static_cast<int64_t>(o.nm_edges))
        .add(static_cast<int64_t>(o.nm_frames))
        .add(o.nm_first_ms)
        .add_float(o.nm_lanes)
        .add(static_cast<int64_t>(o.wrong_output_frames))
        .add(static_cast<int64_t>(o.invariant_violations));
    for (double e : o.max_error) {
        r.add_double(e);
    }
    r.add(static_cast<int64_t>(o.normal_error_frames)).add(o.roll_infinite_normal);
    r.add(static_cast<int64_t>(o.held_frames)).add(o.normal_at_end).add(static_cast<int64_t>(o.nonfinite_voted_frames));
    return r.str();
}

std::string toolchain() {
    std::string out = "compiler ";
#if defined(__VERSION__)
    out += __VERSION__;
#else
    out += "unknown";
#endif
#if defined(_LIBCPP_VERSION)
    out += "; libc++ " + std::to_string(_LIBCPP_VERSION);
#elif defined(__GLIBCXX__)
    out += "; libstdc++ " + std::to_string(__GLIBCXX__);
#else
    out += "; standard library unknown";
#endif
#if defined(__OPTIMIZE__)
    out += "; optimized";
#else
    out += "; not optimized";
#endif
    return out;
}

std::string manifest_json(const ManifestInputs& in) {
    nlohmann::json m;
    m["document"] = "FTS-FI-001 fault-injection campaign";
    m["commit"] = in.commit;
    m["platform"] = in.platform;
    m["toolchain"] = toolchain();
    m["config_path"] = in.config_path;
    m["timing"] = {{"period_ms", in.timing.period_ms},          {"gps_interval_ms", in.timing.gps_interval_ms},
                   {"i2c_timeout_ms", in.timing.i2c_timeout_ms}, {"uart_timeout_ms", in.timing.uart_timeout_ms},
                   {"stuck_count", in.timing.stuck_count},       {"recovery_count", in.timing.recovery_count}};
    m["seed_layout"] = "run id = cell x 100000 + replicate; the schedule and the simulator's seed come from the kind "
                       "and the replicate, so the persistence counts of one lane kind share their faults and noise";
    m["predictions"] = {{"i2c_timeout_latency_ms", i2c_timeout_latency(in.timing)},
                        {"timeout_event_value_ms", timeout_event_value(i2c_timeout_latency(in.timing), in.timing)},
                        {"gps_event_value_ms", gps_event_value(in.timing)},
                        {"gps_fix_latency_ms", gps_fix_latency(in.timing)},
                        {"stuck_latency_ms", stuck_latency(1, in.timing)},
                        {"recovery_ms", recovery_time(0, in.timing)},
                        {"gps_recovery_min_ms", gps_recovery(in.timing.gps_interval_ms, in.timing)},
                        {"gps_recovery_max_ms", gps_recovery(in.timing.gps_interval_ms + in.timing.period_ms, in.timing)}};
    nlohmann::json cell_list = nlohmann::json::array();
    for (const CellRun& c : in.cells) {
        const Cell& cell = cells()[c.cell];
        const KindSpec& kind = kinds()[cell.kind];
        cell_list.push_back({{"cell", c.cell},
                         {"name", cell_name(c.cell)},
                         {"kind", kind.id},
                         {"title", kind.name},
                         {"failure_mode", kind.failure_mode},
                         {"class", class_name(kind.cls)},
                         {"persistence", cell.persistence},
                         {"ranges", kind.ranges},
                         {"replicates", c.replicates},
                         {"first_run_id", run_id(c.cell, 0)},
                         {"last_run_id", run_id(c.cell, c.replicates == 0 ? 0 : c.replicates - 1)},
                         {"isolation_latency_ms", isolation_latency(cell.persistence, in.timing)}});
    }
    m["cells"] = cell_list;
    m["runs"] = in.runs;
    m["as_predicted"] = in.as_predicted;
    m["as_written"] = in.as_written;
    m["wall_seconds"] = in.wall_seconds;
    return m.dump(2);
}
