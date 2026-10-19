#include "fault_catalogue.h"

#include <algorithm>
#include <iterator>
#include <limits>

#include "drivers/mpu6050.h"

namespace {

// Every draw below consumes the engine in a fixed order: the onset first,
// then the kind's own parameters, then the duration. Reordering two calls
// changes every schedule that kind draws, which the fixed-seed test of the
// schedules would catch.

uint64_t at_frame(int64_t frame, const CampaignTiming& t) {
    return static_cast<uint64_t>(frame) * t.period_ms;
}

// A sensor fault's onset: uniform over the frames of [2 s, 12 s), ten whole
// GPS periods, so its place in the fix second is uniform.
uint64_t sensor_onset(ScheduleRng& rng, const CampaignTiming& t) {
    const auto first = static_cast<int64_t>(2000 / t.period_ms);
    const auto last = static_cast<int64_t>(12000 / t.period_ms) - 1;
    return at_frame(rng.between(first, last), t);
}

// A lane fault's onset: uniform over the frames of [2 s, 4 s).
uint64_t lane_onset_ms(ScheduleRng& rng, const CampaignTiming& t) {
    const auto first = static_cast<int64_t>(2000 / t.period_ms);
    const auto last = static_cast<int64_t>(4000 / t.period_ms) - 1;
    return at_frame(rng.between(first, last), t);
}

// A duration of whole frames from low_ms to high_ms, both included.
uint64_t duration_ms(ScheduleRng& rng, const CampaignTiming& t, uint64_t low_ms, uint64_t high_ms) {
    const auto low = static_cast<int64_t>((low_ms + t.period_ms - 1) / t.period_ms);
    const auto high = static_cast<int64_t>(high_ms / t.period_ms);
    return at_frame(rng.between(low, high), t);
}

float sign(ScheduleRng& rng) {
    return rng.coin() ? 1.0f : -1.0f;
}

RawField field_from(RawField first, uint32_t offset) {
    return static_cast<RawField>(static_cast<uint32_t>(first) + offset);
}

// --- the detected cells ---------------------------------------------------

void dropout(ScheduleRng& rng, const CampaignTiming& t, RunSchedule& s, std::vector<Channel> channels,
             uint64_t low_ms, uint64_t high_ms, uint64_t tail_ms) {
    s.sensor.mode = SensorMode::FAIL_READS;
    s.sensor.channels = std::move(channels);
    s.sensor.onset_ms = sensor_onset(rng, t);
    s.sensor.removal_ms = s.sensor.onset_ms + duration_ms(rng, t, low_ms, high_ms);
    s.end_ms = s.sensor.removal_ms + tail_ms;
}

void d01(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) { dropout(r, t, s, {Channel::BARO}, 1000, 5000, 1000); }
void d02(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) { dropout(r, t, s, {Channel::IMU}, 1000, 5000, 1000); }
void d03(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) { dropout(r, t, s, {Channel::GPS}, 3000, 8000, 6000); }
void d04(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    dropout(r, t, s, {Channel::BARO, Channel::IMU}, 1000, 5000, 1000);
}

void d05(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    s.sensor.mode = SensorMode::HOLD_CHANNEL;
    s.sensor.channels = {Channel::BARO};
    s.sensor.onset_ms = sensor_onset(r, t);
    s.sensor.removal_ms = s.sensor.onset_ms + duration_ms(r, t, 500, 5000);
    s.end_ms = s.sensor.removal_ms + 1000;
}

void field_hold(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s, RawField first) {
    s.sensor.mode = SensorMode::HOLD_FIELD;
    s.sensor.onset_ms = sensor_onset(r, t);
    s.sensor.field = field_from(first, r.below(3));
    s.sensor.removal_ms = s.sensor.onset_ms + duration_ms(r, t, 500, 5000);
    s.end_ms = s.sensor.removal_ms + 1000;
}

void d06(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) { field_hold(r, t, s, RawField::ACCEL_X); }
void d07(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) { field_hold(r, t, s, RawField::GYRO_X); }

void field_override(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s, RawField field, float value) {
    s.sensor.mode = SensorMode::OVERRIDE_FIELD;
    s.sensor.field = field;
    s.sensor.value = value;
    s.sensor.removal_ms = s.sensor.onset_ms + duration_ms(r, t, 500, 5000);
    s.end_ms = s.sensor.removal_ms + 1000;
}

void d08(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    s.sensor.onset_ms = sensor_onset(r, t);
    const bool low = r.coin();
    const double value = low ? r.uniform(100.0, 299.0) : r.uniform(1101.0, 1500.0);
    field_override(r, t, s, RawField::PRESSURE, static_cast<float>(value));
}

void d09(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    s.sensor.onset_ms = sensor_onset(r, t);
    const bool low = r.coin();
    const double value = low ? r.uniform(-80.0, -41.0) : r.uniform(86.0, 150.0);
    field_override(r, t, s, RawField::TEMPERATURE, static_cast<float>(value));
}

void d10(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    s.sensor.onset_ms = sensor_onset(r, t);
    const RawField axis = field_from(RawField::ACCEL_X, r.below(3));
    const float rail = r.coin() ? Mpu6050::kAccelPositiveRailMps2 : Mpu6050::kAccelNegativeRailMps2;
    field_override(r, t, s, axis, rail);
}

// --- below the thresholds -------------------------------------------------

void short_fault(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s, int64_t max_frames, uint64_t tail_ms) {
    s.sensor.removal_ms = s.sensor.onset_ms + at_frame(r.between(1, max_frames), t);
    s.end_ms = s.sensor.removal_ms + tail_ms;
}

void t01(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    s.sensor.mode = SensorMode::FAIL_READS;
    s.sensor.onset_ms = sensor_onset(r, t);
    s.sensor.channels = {r.coin() ? Channel::BARO : Channel::IMU};
    short_fault(r, t, s, static_cast<int64_t>(t.i2c_timeout_ms / t.period_ms), 1000);
}

void t02(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    s.sensor.mode = SensorMode::FAIL_READS;
    s.sensor.onset_ms = sensor_onset(r, t);
    s.sensor.channels = {Channel::GPS};
    short_fault(r, t, s, static_cast<int64_t>(t.gps_interval_ms / t.period_ms), 3000);
}

void t03(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    s.sensor.onset_ms = sensor_onset(r, t);
    const uint32_t target = r.below(7);   // the barometer, or one of the six IMU axes
    if (target == 0) {
        s.sensor.mode = SensorMode::HOLD_CHANNEL;
        s.sensor.channels = {Channel::BARO};
    } else {
        s.sensor.mode = SensorMode::HOLD_FIELD;
        s.sensor.field = field_from(RawField::ACCEL_X, target - 1);
    }
    short_fault(r, t, s, t.stuck_count - 2, 1000);
}

// --- no check covers ------------------------------------------------------

// A bias on for the rest of the run, which ends length_ms after its onset.
void bias(RunSchedule& s, RawField field, float step, float rate, uint64_t length_ms) {
    s.sensor.mode = SensorMode::BIAS_FIELD;
    s.sensor.field = field;
    s.sensor.value = step;
    s.sensor.rate_per_s = rate;
    s.sensor.removal_ms = kNever;
    s.end_ms = s.sensor.onset_ms + length_ms;
    s.twin = true;
}

void u01(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    s.sensor.onset_ms = sensor_onset(r, t);
    const float step = sign(r) * static_cast<float>(r.uniform(0.5, 5.0));
    bias(s, RawField::PRESSURE, step, 0.0f, 30000);
}

void u02(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    s.sensor.onset_ms = sensor_onset(r, t);
    const float rate = sign(r) * static_cast<float>(r.uniform(0.005, 0.05));
    bias(s, RawField::PRESSURE, 0.0f, rate, 60000);
}

void u03(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    s.sensor.onset_ms = sensor_onset(r, t);
    const RawField axis = r.coin() ? RawField::ACCEL_X : RawField::ACCEL_Y;
    const float step = sign(r) * static_cast<float>(r.uniform(0.05, 0.5));
    bias(s, axis, step, 0.0f, 20000);
}

void u04(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    s.sensor.onset_ms = sensor_onset(r, t);
    const float step = sign(r) * static_cast<float>(r.uniform(5.0, 50.0));
    bias(s, RawField::GPS_ALTITUDE, step, 0.0f, 30000);
}

void u05(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    s.sensor.mode = SensorMode::HOLD_CHANNEL;
    s.sensor.channels = {Channel::GPS};
    s.sensor.onset_ms = sensor_onset(r, t);
    s.sensor.removal_ms = s.sensor.onset_ms + 30000;
    s.end_ms = s.sensor.removal_ms + 3000;
    s.twin = true;
}

void one_frame_override(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s, RawField field, float value) {
    s.sensor.mode = SensorMode::OVERRIDE_FIELD;
    s.sensor.field = field;
    s.sensor.value = value;
    s.sensor.onset_ms = sensor_onset(r, t);
    s.sensor.removal_ms = s.sensor.onset_ms + t.period_ms;
    s.end_ms = s.sensor.onset_ms + 10000;
    s.twin = true;
}

void u06(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    one_frame_override(r, t, s, RawField::PRESSURE, std::numeric_limits<float>::quiet_NaN());
}

void u07(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    one_frame_override(r, t, s, RawField::GYRO_X, std::numeric_limits<float>::infinity());
}

void c00(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    (void)r;
    (void)t;
    s.end_ms = 60000;
}

// --- lane faults ----------------------------------------------------------

LaneFault lane_fault(int lane, LaneFaultKind kind, uint64_t onset, uint64_t end, LaneField field, uint8_t bit) {
    LaneFault f;
    f.lane = lane;
    f.kind = kind;
    f.onset_ms = onset;
    f.end_ms = end;
    f.field = field;
    f.bit = bit;
    return f;
}

LaneField output_field(ScheduleRng& r) {
    return static_cast<LaneField>(r.below(5));   // BARO_ALTITUDE to VERTICAL_SPEED
}

uint8_t bit_of(ScheduleRng& r) {
    return static_cast<uint8_t>(r.below(32));
}

int lane_of(ScheduleRng& r) {
    return static_cast<int>(r.between(1, 3));
}

// The other lane: one of the two that are not `lane`.
int other_lane(ScheduleRng& r, int lane) {
    return 1 + static_cast<int>((lane - 1 + r.between(1, 2)) % 3);
}

constexpr uint64_t kLaneTailMs = 3000;

void single(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s, LaneFaultKind kind, int cycles) {
    const uint64_t onset = lane_onset_ms(r, t);
    const int lane = lane_of(r);
    const LaneField field = output_field(r);
    const uint8_t bit = bit_of(r);
    const uint64_t end = cycles == 0 ? LaneFault::kPersistent : onset + at_frame(cycles, t);
    s.lane_faults.push_back(lane_fault(lane, kind, onset, end, field, bit));
    s.end_ms = onset + kLaneTailMs;
}

void v01(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) { single(r, t, s, LaneFaultKind::OUTPUT_BIT_FLIP, 0); }
void v02(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) { single(r, t, s, LaneFaultKind::OUTPUT_NAN, 0); }
void v03(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) { single(r, t, s, LaneFaultKind::OUTPUT_STALE, 0); }
void v04(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) { single(r, t, s, LaneFaultKind::OUTPUT_BIT_FLIP, 1); }
void v05(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) { single(r, t, s, LaneFaultKind::OUTPUT_NAN, 1); }
void v06(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) { single(r, t, s, LaneFaultKind::OUTPUT_STALE, 1); }

void v07(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    const uint64_t onset = lane_onset_ms(r, t);
    const int lane = lane_of(r);
    const LaneField field = output_field(r);
    const uint8_t bit = bit_of(r);
    s.length_cycles = static_cast<int>(r.between(2, 6));
    s.lane_faults.push_back(lane_fault(lane, LaneFaultKind::OUTPUT_BIT_FLIP, onset,
                                       onset + at_frame(s.length_cycles, t), field, bit));
    s.end_ms = onset + kLaneTailMs;
}

void v08(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    // The raw values the lane's filters read every cycle; accel z is left
    // out, since the filter reads it squared only (FTS-FI-001 Section 5.5).
    constexpr std::array<LaneField, 5> kInputs{LaneField::PRESSURE, LaneField::ACCEL_X, LaneField::ACCEL_Y,
                                               LaneField::GYRO_X, LaneField::GYRO_Y};
    const uint64_t onset = lane_onset_ms(r, t);
    const int lane = lane_of(r);
    const LaneField field = kInputs[r.below(5)];
    const uint8_t bit = bit_of(r);
    s.lane_faults.push_back(lane_fault(lane, LaneFaultKind::INPUT_BIT_FLIP, onset, onset + t.period_ms, field, bit));
    s.end_ms = onset + kLaneTailMs;
}

void v09(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    const uint64_t onset = lane_onset_ms(r, t);
    const int a = lane_of(r);
    const int b = other_lane(r, a);
    const LaneField field = output_field(r);
    const uint8_t bit = bit_of(r);
    s.lane_faults.push_back(lane_fault(a, LaneFaultKind::OUTPUT_BIT_FLIP, onset, LaneFault::kPersistent, field, bit));
    s.lane_faults.push_back(lane_fault(b, LaneFaultKind::OUTPUT_BIT_FLIP, onset, LaneFault::kPersistent, field, bit));
    s.end_ms = onset + kLaneTailMs;
}

void v10(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    const uint64_t onset = lane_onset_ms(r, t);
    const int a = lane_of(r);
    const int b = other_lane(r, a);
    const LaneField field = output_field(r);
    s.lane_faults.push_back(lane_fault(a, LaneFaultKind::OUTPUT_NAN, onset, LaneFault::kPersistent, field, 0));
    s.lane_faults.push_back(lane_fault(b, LaneFaultKind::OUTPUT_NAN, onset, LaneFault::kPersistent, field, 0));
    s.end_ms = onset + kLaneTailMs;
}

void v11(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    const uint64_t onset = lane_onset_ms(r, t);
    const int a = lane_of(r);
    const int b = other_lane(r, a);
    const LaneField field_a = output_field(r);
    const uint8_t bit_a = bit_of(r);
    const LaneField field_b = output_field(r);
    uint8_t bit_b = bit_of(r);
    if (field_b == field_a && bit_b == bit_a) {
        bit_b = static_cast<uint8_t>((bit_b + 1) % 32);   // a different flip, so the two wrong lanes disagree
    }
    s.gap_cycles = static_cast<int>(r.between(0, 10));
    s.lane_faults.push_back(lane_fault(a, LaneFaultKind::OUTPUT_BIT_FLIP, onset, LaneFault::kPersistent, field_a, bit_a));
    s.lane_faults.push_back(lane_fault(b, LaneFaultKind::OUTPUT_BIT_FLIP, onset + at_frame(s.gap_cycles, t),
                                       LaneFault::kPersistent, field_b, bit_b));
    s.end_ms = onset + kLaneTailMs;
}

void v12(ScheduleRng& r, const CampaignTiming& t, RunSchedule& s) {
    const uint64_t onset = lane_onset_ms(r, t);
    const int a = lane_of(r);
    const int b = other_lane(r, a);
    const LaneField field = output_field(r);
    const uint8_t bit = bit_of(r);
    s.gap_cycles = static_cast<int>(r.between(0, 10));
    s.lane_faults.push_back(lane_fault(a, LaneFaultKind::OUTPUT_BIT_FLIP, onset, LaneFault::kPersistent, field, bit));
    s.lane_faults.push_back(lane_fault(b, LaneFaultKind::OUTPUT_BIT_FLIP, onset + at_frame(s.gap_cycles, t),
                                       LaneFault::kPersistent, field, bit));
    s.end_ms = onset + kLaneTailMs;
}

}  // namespace

const std::vector<int>& swept_persistence() {
    static const std::vector<int> counts{1, 2, 3, 4, 5, 8};
    return counts;
}

const std::vector<KindSpec>& kinds() {
    // Append only: a kind's place is its number in every run id.
    static const std::vector<KindSpec> table{
        {"D01", "barometer dropout", "FAULT-001", CellClass::DETECTED, "every read fails, 1.0-5.0 s", 1000, d01},
        {"D02", "IMU dropout", "FAULT-002", CellClass::DETECTED, "every read fails, 1.0-5.0 s", 1000, d02},
        {"D03", "GPS dropout", "FAULT-003", CellClass::DETECTED, "every fix fails, 3.0-8.0 s", 1000, d03},
        {"D04", "I2C bus failure", "FAULT-004", CellClass::DETECTED, "barometer and IMU fail together, 1.0-5.0 s", 1000, d04},
        {"D05", "barometer stuck", "FAULT-005", CellClass::DETECTED, "the reading repeats, 0.5-5.0 s", 1000, d05},
        {"D06", "accelerometer axis stuck", "FAULT-006", CellClass::DETECTED, "x, y or z repeats, 0.5-5.0 s", 1000, d06},
        {"D07", "gyroscope axis stuck", "FAULT-007", CellClass::DETECTED, "x, y or z repeats, 0.5-5.0 s", 1000, d07},
        {"D08", "pressure out of range", "FAULT-008", CellClass::DETECTED, "100-299 or 1,101-1,500 hPa, 0.5-5.0 s", 1000, d08},
        {"D09", "temperature out of range", "FAULT-009", CellClass::DETECTED, "-80 to -41 or 86 to 150 C, 0.5-5.0 s", 1000, d09},
        {"D10", "accelerometer at a rail", "FAULT-010", CellClass::DETECTED, "one axis at either rail, 0.5-5.0 s", 1000, d10},
        {"T01", "I2C dropout below the timeout", "FAULT-001/002", CellClass::SUB_THRESHOLD, "1-25 frames", 1000, t01},
        {"T02", "GPS dropout of at most one fix", "FAULT-003", CellClass::SUB_THRESHOLD, "1-50 frames", 1000, t02},
        {"T03", "hold below the stuck count", "FAULT-005/006/007", CellClass::SUB_THRESHOLD, "1-8 frames", 1000, t03},
        {"U01", "pressure bias step", "FAULT-023", CellClass::UNCOVERED, "+-0.5-5.0 hPa, to the end", 300, u01},
        {"U02", "pressure drift", "FAULT-023", CellClass::UNCOVERED, "+-0.005-0.05 hPa/s for 60 s", 300, u02},
        {"U03", "accelerometer bias", "FAULT-023", CellClass::UNCOVERED, "+-0.05-0.5 m/s2 on x or y, to the end", 300, u03},
        {"U04", "GPS altitude bias", "FAULT-023", CellClass::UNCOVERED, "+-5-50 m, to the end", 300, u04},
        {"U05", "GPS fix frozen", "FAULT-023", CellClass::UNCOVERED, "the reading held, still fresh, 30 s", 300, u05},
        {"U06", "NaN pressure", "FAULT-024", CellClass::UNCOVERED, "one frame", 300, u06},
        {"U07", "infinite x rotation rate", "FAULT-024", CellClass::UNCOVERED, "one frame", 300, u07},
        {"C00", "clean", "none", CellClass::CLEAN, "60 s runs", 10000, c00},
        {"V01", "lane value, persistent", "FAULT-020", CellClass::LANE, "one bit of one computed value, every cycle", 1000, v01},
        {"V02", "lane NaN, persistent", "FAULT-020", CellClass::LANE, "one computed value NaN, every cycle", 1000, v02},
        {"V03", "lane overrun, persistent", "FAULT-022", CellClass::LANE, "the previous output, every cycle", 1000, v03},
        {"V04", "lane value, one cycle", "FAULT-019", CellClass::LANE, "as V01 for one cycle", 1000, v04},
        {"V05", "lane NaN, one cycle", "FAULT-019", CellClass::LANE, "as V02 for one cycle", 1000, v05},
        {"V06", "lane overrun, one cycle", "FAULT-022", CellClass::LANE, "as V03 for one cycle", 1000, v06},
        {"V07", "lane value, L cycles", "FAULT-019/020", CellClass::LANE, "as V01 for 2-6 cycles", 1000, v07},
        {"V08", "lane input upset", "FAULT-020", CellClass::LANE, "one bit of a raw input, one cycle", 1000, v08},
        {"V09", "two lanes, identical flip", "FAULT-021", CellClass::LANE, "two lanes, the same flip, every cycle", 1000, v09},
        {"V10", "two lanes, identical NaN", "FAULT-021", CellClass::LANE, "two lanes, the same NaN, every cycle", 1000, v10},
        {"V11", "two lanes, different flips", "FAULT-021", CellClass::LANE, "the second 0-10 cycles after the first", 1000, v11},
        {"V12", "two lanes, identical flip, delayed", "FAULT-021", CellClass::LANE, "the second 0-10 cycles after the first", 1000, v12},
    };
    return table;
}

const std::vector<Cell>& cells() {
    static const std::vector<Cell> table = [] {
        std::vector<Cell> out;
        for (std::size_t k = 0; k < kinds().size(); k++) {
            if (kinds()[k].cls == CellClass::LANE) {
                const std::vector<int>& counts = swept_persistence();
                std::transform(counts.begin(), counts.end(), std::back_inserter(out), [k](int p) { return Cell{k, p}; });
            } else {
                out.push_back(Cell{k, Voter::kLanePersistence});
            }
        }
        return out;
    }();
    return table;
}

bool in_ci_subset(const Cell& cell) {
    if (kinds()[cell.kind].cls != CellClass::LANE) {
        return true;
    }
    return cell.persistence == 1 || cell.persistence == Voter::kLanePersistence;
}

std::string cell_name(std::size_t cell) {
    const Cell& c = cells()[cell];
    std::string name = kinds()[c.kind].id;
    if (kinds()[c.kind].cls == CellClass::LANE) {
        name += "/P" + std::to_string(c.persistence);
    }
    return name;
}

uint32_t run_id(std::size_t cell, uint32_t replicate) {
    return static_cast<uint32_t>(cell) * kReplicatesPerCell + replicate;
}

RunSchedule draw_schedule(uint32_t id, const CampaignTiming& timing) {
    RunSchedule s;
    s.run_id = id;
    s.cell = id / kReplicatesPerCell;
    s.replicate = id % kReplicatesPerCell;
    const Cell& cell = cells().at(s.cell);
    s.kind = cell.kind;
    s.persistence = cell.persistence;
    // The kind and the replicate, never the count: the counts of one lane
    // kind run over the same faults and the same noise.
    s.sim_seed = static_cast<uint32_t>(cell.kind) * kReplicatesPerCell + s.replicate;
    ScheduleRng rng(s.sim_seed, 0x5eedu);
    kinds()[cell.kind].draw(rng, timing, s);
    return s;
}
