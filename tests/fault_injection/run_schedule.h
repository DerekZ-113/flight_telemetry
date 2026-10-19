#pragma once

#include <cstdint>
#include <vector>

#include "drivers/fault_injecting_source.h"
#include "processing/fault_detector.h"
#include "processing/lane_fault_injector.h"
#include "processing/voter.h"

// One campaign run, as drawn from its seed (REQ-TEST-005, FTS-DD-001
// Section 14): which fault, on what, from when to when, at what count.

// The configuration's numbers the draws and the predictions use: the frame
// period, the GPS fix period and the detector's thresholds. The CLI takes
// them from the configuration file, the tests from the struct defaults,
// which equal it (ConfigTest.ShippedFileEqualsDefaults).
struct CampaignTiming {
    uint64_t period_ms = 20;
    uint64_t gps_interval_ms = 1000;
    uint64_t i2c_timeout_ms = 500;
    uint64_t uart_timeout_ms = 2000;
    int stuck_count = 10;
    int recovery_count = 5;
};

// How a sensor fault is put in, through FaultInjectingSource.
enum class SensorMode : uint8_t {
    NONE,             // nothing: a clean or a lane-fault run
    FAIL_READS,       // every read of the channels fails, fields held
    HOLD_CHANNEL,     // the channels' readings repeat the last good one
    HOLD_FIELD,       // one field repeats its value from the frame before
    OVERRIDE_FIELD,   // one field replaced by a constant (NaN and infinity included)
    BIAS_FIELD,       // one field offset by a step plus a rate times the time since onset
};

constexpr uint64_t kNever = UINT64_MAX;

struct SensorFault {
    SensorMode mode = SensorMode::NONE;
    std::vector<Channel> channels;        // FAIL_READS and HOLD_CHANNEL; D04 names two
    RawField field = RawField::PRESSURE;  // the field modes
    float value = 0.0f;                   // OVERRIDE_FIELD's constant, BIAS_FIELD's step
    float rate_per_s = 0.0f;              // BIAS_FIELD's drift
    uint64_t onset_ms = kNever;           // the frame it is switched on
    uint64_t removal_ms = kNever;         // the frame it is switched off; kNever: to the end
};

struct RunSchedule {
    uint32_t run_id = 0;                  // the seed of record: cell x 100,000 + replicate
    std::size_t cell = 0;
    std::size_t kind = 0;
    uint32_t replicate = 0;
    uint32_t sim_seed = 0;                // the simulator's: the kind's and the replicate's, not the count's
    int persistence = Voter::kLanePersistence;
    uint64_t end_ms = 0;                  // frames at 0, period, ..., up to end_ms excluded
    SensorFault sensor;
    std::vector<LaneFault> lane_faults;
    bool twin = false;                    // an uncovered cell: its clean twin is the reference
    int length_cycles = 0;                // V07: the upset's length
    int gap_cycles = 0;                   // V11, V12: the second fault's delay
};

// The channel a raw field belongs to.
Channel channel_of(RawField field);

// The channels a sensor fault touches: its own list, or its field's.
std::vector<Channel> channels_touched(const SensorFault& fault);

// The lowest-numbered lane no fault alters, the campaign's healthy
// reference; 0 when every lane is altered, which no cell does.
int reference_lane(const RunSchedule& schedule);

// The earliest onset of a fault on `lane`, or kNever when none alters it.
uint64_t lane_onset(const RunSchedule& schedule, int lane);
