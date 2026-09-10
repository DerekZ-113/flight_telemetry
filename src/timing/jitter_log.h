#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>

#include "timing/fixed_rate_scheduler.h"

// Running summary of cycle timing (REQ-TIME-002, REQ-TIME-003).
// min and max are seeded from numeric_limits so the first sample sets
// both; no sentinel values to special-case.
struct JitterStats {
    uint64_t cycles = 0;
    int64_t min_ns = std::numeric_limits<int64_t>::max();
    int64_t max_ns = std::numeric_limits<int64_t>::min();
    double sum_ns = 0.0;
    uint64_t overruns = 0;        // cycles that had to skip at least one period
    uint64_t missed_cycles = 0;   // total periods skipped

    void add(const CycleTiming& t);
    double mean_ns() const;       // 0 when no cycles
};

// Per-cycle jitter as CSV (REQ-TIME-002), one line per cycle:
//   cycle,scheduled_ns,actual_ns,jitter_ns,missed_cycles
//
// A separate file, not a record in the binary telemetry log. Jitter
// measures this machine on this run; the telemetry log is the record of
// the flight and must replay byte-identically. Mixing them would make two
// identical flights produce different logs. CSV also loads straight into
// numpy for the characterization run on the Pi.
class JitterLog {
public:
    explicit JitterLog(const std::filesystem::path& file);

    bool ok() const { return ok_; }
    void record(const CycleTiming& t);
    const JitterStats& stats() const { return stats_; }

private:
    std::ofstream out_;
    JitterStats stats_;
    bool ok_ = false;
};
