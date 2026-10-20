// The CI subset (REQ-TEST-006; FTS-TP-001 TC-035 step 5): every cell of the
// campaign at its fixed seeds, the lane cells at persistence 1 and 3, each
// run against its own prediction and the invariants. One parameterized case
// per cell, so ctest names the cell that departs. None of its assertions
// depends on the simulator's noise, so they hold on libc++ and libstdc++
// alike; a run that departs fails the build, and the fix is never a changed
// prediction without a finding in FTS-FI-001.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "campaign_run.h"
#include "fault_catalogue.h"
#include "prediction.h"
#include "run_outcome.h"

namespace {

std::vector<std::size_t> subset_cells() {
    std::vector<std::size_t> out;
    for (std::size_t c = 0; c < cells().size(); c++) {
        if (in_ci_subset(cells()[c])) {
            out.push_back(c);
        }
    }
    return out;
}

std::string test_name(const ::testing::TestParamInfo<std::size_t>& info) {
    std::string name = cell_name(info.param);
    std::replace(name.begin(), name.end(), '/', '_');
    return name;
}

class CellTest : public ::testing::TestWithParam<std::size_t> {};

}  // namespace

TEST_P(CellTest, AsPredicted) {
    const PipelineSettings settings{};
    const std::size_t cell = GetParam();
    for (uint32_t r = 0; r < kCiReplicates; r++) {
        const RunSchedule s = draw_schedule(run_id(cell, r), settings.timing);
        const Timeline timeline = run_schedule(s, settings);
        const RunOutcome outcome = classify(s, timeline, settings.timing);
        const Verdict verdict = judge(s, timeline, outcome, settings.timing);
        EXPECT_TRUE(verdict.as_predicted) << cell_name(cell) << ", seed " << s.run_id << ": " << verdict.mismatch;
    }
}

INSTANTIATE_TEST_SUITE_P(Subset, CellTest, ::testing::ValuesIn(subset_cells()), test_name);
