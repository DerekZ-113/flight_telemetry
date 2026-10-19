#pragma once

#include <cstdint>
#include <string>

#include "campaign_run.h"
#include "run_outcome.h"
#include "run_schedule.h"

// The predictions of FTS-FI-001 Section 5 (REQ-TEST-006), written before
// the campaign ran, as formulas of the configuration, and the per-run
// judgment the CI subset asserts and the report counts. Each formula reads
// the detector's rule off its code: the comment beside it names the line.

// fault_detector.cpp:42-48: declared when the time since the last good
// read is more than the timeout; the last good read is one frame before
// onset, so floor(timeout / period) frames after onset: 500 ms.
int64_t i2c_timeout_latency(const CampaignTiming& t);

// The event's value, the time since the last good read: latency + period.
int64_t timeout_event_value(int64_t latency_ms, const CampaignTiming& t);

// The GPS timeout, on the operator's clock: declared at the last fix before
// onset plus (floor(timeout / period) + 1) frames, 2,020 ms.
int64_t gps_declared_at(uint64_t onset_ms, const CampaignTiming& t);
int64_t gps_event_value(const CampaignTiming& t);   // 2,020
int64_t gps_fix_latency(const CampaignTiming& t);   // from the first suppressed fix: 1,020

// RECOVERY on the fifth fix at or after removal (fault_detector.cpp:196-205).
int64_t gps_recovery(uint64_t removal_ms, const CampaignTiming& t);

// fault_detector.cpp:12-26: stuck on the stuck_count-th identical value;
// `prior_run` is the identical run the reading had reached on the frame
// before onset, 1 unless it happened to repeat: 160 ms at 1.
int64_t stuck_latency(int prior_run, const CampaignTiming& t);

// recovery_count valid frames in a row, after `repeats` frames from removal
// that still equal a held value: 80 ms at none.
int64_t recovery_time(int repeats, const CampaignTiming& t);

// voter.cpp:200-208: excluded on the persistence-th miss: (P - 1) frames.
int64_t isolation_latency(int persistence, const CampaignTiming& t);

// The identical run the held field had reached on the frame before onset,
// and the frames from removal that still equal the held value: the two
// coincidences a hold's prediction reads from the run's own frames.
int prior_run(const RunSchedule& schedule, const Timeline& timeline, const CampaignTiming& t);
int repeats_at_removal(const RunSchedule& schedule, const Timeline& timeline, const CampaignTiming& t);

struct Verdict {
    bool as_predicted = true;
    std::string mismatch;   // the first departure, named
};

Verdict judge(const RunSchedule& schedule, const Timeline& timeline, const RunOutcome& outcome,
              const CampaignTiming& t);
