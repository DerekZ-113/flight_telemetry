#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "prediction.h"
#include "run_outcome.h"
#include "run_schedule.h"

// The campaign's record (REQ-TEST-006, FTS-FI-001 Section 3): one CSV row
// per run, its columns in a fixed order that the report generator reads by
// name, and the manifest that says what was run, on what, with what. The
// rows are enough to rebuild every statistic of the report; the seeds are
// enough to rebuild every run.

std::string csv_header();
// `gate` is judge()'s verdict, the CI gate's; `written` is judge_as_written()'s,
// FTS-FI-001 Section 5 as written, which the report counts against.
std::string csv_row(const RunSchedule& schedule, const RunOutcome& outcome, const Verdict& gate,
                    const Verdict& written);

struct CellRun {
    std::size_t cell = 0;
    uint32_t replicates = 0;   // replicates 0 to replicates - 1
};

struct ManifestInputs {
    std::string commit;
    std::string platform;
    std::string config_path;
    CampaignTiming timing;
    std::vector<CellRun> cells;
    uint64_t runs = 0;
    uint64_t as_predicted = 0;
    uint64_t as_written = 0;
    double wall_seconds = 0.0;
};

std::string manifest_json(const ManifestInputs& inputs);

// The compiler, the standard library and whether the build is optimized:
// what a run's noise and speed depend on.
std::string toolchain();
