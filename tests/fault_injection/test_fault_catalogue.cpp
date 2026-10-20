// Tests for tests/fault_injection/fault_catalogue.cpp (REQ-TEST-005): the
// cells, the run ids and the draws. Test card TC-035 step 2.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "drivers/mpu6050.h"
#include "fault_catalogue.h"

namespace {

std::size_t cell_named(const std::string& name) {
    for (std::size_t c = 0; c < cells().size(); c++) {
        if (cell_name(c) == name) {
            return c;
        }
    }
    ADD_FAILURE() << "no cell " << name;
    return 0;
}

const CampaignTiming kTiming{};

}  // namespace

// 21 sensor and clean cells, then twelve lane kinds at six counts each:
// 93, in a fixed order; the CI subset keeps 45 of them.
TEST(CatalogueTest, NinetyThreeCellsInTheirOrder) {
    EXPECT_EQ(kinds().size(), 33u);
    ASSERT_EQ(cells().size(), 93u);
    EXPECT_EQ(cell_name(0), "D01");
    EXPECT_EQ(cell_name(12), "T03");
    EXPECT_EQ(cell_name(19), "U07");
    EXPECT_EQ(cell_name(20), "C00");
    EXPECT_EQ(cell_name(21), "V01/P1");
    EXPECT_EQ(cell_name(23), "V01/P3");
    EXPECT_EQ(cell_name(26), "V01/P8");
    EXPECT_EQ(cell_name(92), "V12/P8");
    EXPECT_EQ(std::count_if(cells().begin(), cells().end(), in_ci_subset), 45);
}

TEST(CatalogueTest, RunIdRoundTrips) {
    for (std::size_t c = 0; c < cells().size(); c++) {
        for (uint32_t r : {0u, 1u, 99999u}) {
            const RunSchedule s = draw_schedule(run_id(c, r), kTiming);
            ASSERT_EQ(s.cell, c);
            ASSERT_EQ(s.replicate, r);
            ASSERT_EQ(s.kind, cells()[c].kind);
            ASSERT_EQ(s.persistence, cells()[c].persistence);
            ASSERT_EQ(s.run_id, run_id(c, r));
        }
    }
}

// A seed is a run: drawn twice it is the same schedule, field for field,
// and the next replicate is another.
TEST(CatalogueTest, SameSeedSameScheduleAndTheNextReplicateDiffers) {
    for (std::size_t c = 0; c < cells().size(); c++) {
        const uint32_t id = run_id(c, 7);
        EXPECT_EQ(describe(draw_schedule(id, kTiming)), describe(draw_schedule(id, kTiming))) << cell_name(c);
        if (kinds()[cells()[c].kind].cls != CellClass::CLEAN) {
            EXPECT_NE(describe(draw_schedule(id, kTiming)), describe(draw_schedule(id + 1, kTiming))) << cell_name(c);
        }
    }
}

// Common random numbers: the counts of one lane kind share the replicate's
// faults and noise, and differ in the count alone.
TEST(CatalogueTest, TheCountsOfOneLaneKindShareTheirFaults) {
    const RunSchedule p1 = draw_schedule(run_id(cell_named("V11/P1"), 42), kTiming);
    const RunSchedule p8 = draw_schedule(run_id(cell_named("V11/P8"), 42), kTiming);
    EXPECT_EQ(p1.sim_seed, p8.sim_seed);
    EXPECT_EQ(p1.persistence, 1);
    EXPECT_EQ(p8.persistence, 8);
    RunSchedule p8_as_p1 = p8;
    p8_as_p1.persistence = 1;
    p8_as_p1.cell = p1.cell;
    p8_as_p1.run_id = p1.run_id;
    EXPECT_EQ(describe(p8_as_p1), describe(p1));
}

// Every draw inside the range FTS-FI-001 Section 4.2 states, over 1,000
// replicates of every cell.
TEST(CatalogueTest, DrawsStayInTheirRanges) {
    for (std::size_t c = 0; c < cells().size(); c++) {
        const std::string id = kinds()[cells()[c].kind].id;
        const CellClass cls = kinds()[cells()[c].kind].cls;
        for (uint32_t r = 0; r < 1000; r++) {
            const RunSchedule s = draw_schedule(run_id(c, r), kTiming);
            const SensorFault& f = s.sensor;
            SCOPED_TRACE(cell_name(c) + " replicate " + std::to_string(r));
            ASSERT_EQ(s.end_ms % kTiming.period_ms, 0u);
            if (cls == CellClass::LANE) {
                ASSERT_FALSE(s.lane_faults.empty());
                for (const LaneFault& lf : s.lane_faults) {
                    ASSERT_GE(lf.lane, 1);
                    ASSERT_LE(lf.lane, 3);
                    ASSERT_LT(lf.bit, 32);
                    ASSERT_EQ(lf.onset_ms % kTiming.period_ms, 0u);
                }
                const uint64_t onset = s.lane_faults.front().onset_ms;
                ASSERT_GE(onset, 2000u);
                ASSERT_LT(onset, 4000u);
                ASSERT_EQ(s.end_ms, onset + 3000);
                if (s.lane_faults.size() == 2) {
                    ASSERT_NE(s.lane_faults[0].lane, s.lane_faults[1].lane);
                }
                if (id == "V07") {
                    ASSERT_GE(s.length_cycles, 2);
                    ASSERT_LE(s.length_cycles, 6);
                }
                if (id == "V11" || id == "V12") {
                    ASSERT_GE(s.gap_cycles, 0);
                    ASSERT_LE(s.gap_cycles, 10);
                    ASSERT_EQ(s.lane_faults[1].onset_ms, onset + 20u * static_cast<uint64_t>(s.gap_cycles));
                }
                if (id == "V11") {
                    const bool differ = s.lane_faults[0].field != s.lane_faults[1].field ||
                                        s.lane_faults[0].bit != s.lane_faults[1].bit;
                    ASSERT_TRUE(differ);
                }
                continue;
            }
            if (cls == CellClass::CLEAN) {
                ASSERT_EQ(f.mode, SensorMode::NONE);
                ASSERT_EQ(s.end_ms, 60000u);
                continue;
            }
            ASSERT_GE(f.onset_ms, 2000u);
            ASSERT_LT(f.onset_ms, 12000u);
            ASSERT_EQ(f.onset_ms % kTiming.period_ms, 0u);
            const uint64_t length = f.removal_ms == kNever ? 0 : f.removal_ms - f.onset_ms;
            if (id == "D01" || id == "D02" || id == "D04") {
                ASSERT_GE(length, 1000u);
                ASSERT_LE(length, 5000u);
            } else if (id == "D03") {
                ASSERT_GE(length, 3000u);
                ASSERT_LE(length, 8000u);
            } else if (id[0] == 'D') {
                ASSERT_GE(length, 500u);
                ASSERT_LE(length, 5000u);
            }
            if (id == "D08") {
                ASSERT_TRUE((f.value >= 100.0f && f.value <= 299.0f) || (f.value >= 1101.0f && f.value <= 1500.0f));
            }
            if (id == "D09") {
                ASSERT_TRUE((f.value >= -80.0f && f.value <= -41.0f) || (f.value >= 86.0f && f.value <= 150.0f));
            }
            if (id == "D10") {
                ASSERT_TRUE(f.value == Mpu6050::kAccelPositiveRailMps2 || f.value == Mpu6050::kAccelNegativeRailMps2);
            }
            if (id == "T01") {
                ASSERT_GE(length, 20u);
                ASSERT_LE(length, 500u);
            }
            if (id == "T02") {
                ASSERT_GE(length, 20u);
                ASSERT_LE(length, 1000u);
            }
            if (id == "T03") {
                ASSERT_GE(length, 20u);
                ASSERT_LE(length, 160u);
            }
            if (id == "U01") {
                ASSERT_GE(std::fabs(f.value), 0.5f);
                ASSERT_LE(std::fabs(f.value), 5.0f);
            }
            if (id == "U02") {
                ASSERT_GE(std::fabs(f.rate_per_s), 0.005f);
                ASSERT_LE(std::fabs(f.rate_per_s), 0.05f);
                ASSERT_EQ(s.end_ms, f.onset_ms + 60000);
            }
            if (id == "U03") {
                ASSERT_GE(std::fabs(f.value), 0.05f);
                ASSERT_LE(std::fabs(f.value), 0.5f);
            }
            if (id == "U04") {
                ASSERT_GE(std::fabs(f.value), 5.0f);
                ASSERT_LE(std::fabs(f.value), 50.0f);
            }
            if (id == "U06") {
                ASSERT_TRUE(std::isnan(f.value));
                ASSERT_EQ(length, 20u);
            }
            if (id == "U07") {
                ASSERT_TRUE(std::isinf(f.value));
                ASSERT_EQ(length, 20u);
            }
            if (cls == CellClass::UNCOVERED) {
                ASSERT_TRUE(s.twin);
            }
        }
    }
}

// The cross-platform claim, tested where it can fail: three seeds' schedules
// as literals. The draws read only the standard-fixed engine and the
// harness's arithmetic, so CI on libstdc++ must print the same lines as
// this Mac on libc++.
TEST(CatalogueTest, AFixedSeedDrawsTheSameScheduleOnEveryPlatform) {
    EXPECT_EQ(describe(draw_schedule(run_id(cell_named("D03"), 0), kTiming)),
              "cell 2 rep 0 sim 200000 P 3 end 15780 | sensor 1 ch 2 field 0 value 0 rate 0 on 4720 off 9780 | twin 0 L 0 "
              "g 0 |");
    EXPECT_EQ(describe(draw_schedule(run_id(cell_named("U01"), 0), kTiming)),
              "cell 13 rep 0 sim 1300000 P 3 end 38260 | sensor 5 ch field 0 value -3.82286 rate 0 on 8260 off "
              "18446744073709551615 | twin 1 L 0 g 0 |");
    EXPECT_EQ(describe(draw_schedule(run_id(cell_named("V11/P3"), 0), kTiming)),
              "cell 83 rep 0 sim 3100000 P 3 end 5280 | sensor 0 ch field 0 value 0 rate 0 on 18446744073709551615 off "
              "18446744073709551615 | twin 0 L 0 g 3 | lane 3 kind 0 on 2280 end 18446744073709551615 field 1 bit 24 lane 1 "
              "kind 0 on 2340 end 18446744073709551615 field 3 bit 30");
}
