#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "run_schedule.h"
#include "schedule_rng.h"

// The campaign's cells (REQ-TEST-005, FTS-FI-001 Section 4.2): every fault
// kind with its ranges and its draw, in one fixed order that only grows,
// so a cell's number, and with it every run id, never changes.

// What the campaign expects of a kind (FTS-FI-001 Section 5).
enum class CellClass : uint8_t {
    DETECTED,        // D: a fault a check is built for
    SUB_THRESHOLD,   // T: a fault below a threshold, never declared
    UNCOVERED,       // U: a fault no check covers; its consequence is measured
    CLEAN,           // C: nothing injected
    LANE,            // V: a lane fault, at each persistence count
};

struct KindSpec {
    const char* id;              // "D01"
    const char* name;
    const char* failure_mode;    // FTS-FM-001's entry
    CellClass cls;
    const char* ranges;          // as FTS-FI-001 Section 4.2 states them
    int full_runs;               // runs per cell in the full campaign
    void (*draw)(ScheduleRng& rng, const CampaignTiming& timing, RunSchedule& out);
};

struct Cell {
    std::size_t kind;
    int persistence;
};

// A run id is cell x kReplicatesPerCell + replicate.
constexpr uint32_t kReplicatesPerCell = 100000;

// The CI subset (REQ-TEST-006): this many replicates of every cell, the
// lane cells at the counts in_ci_subset() keeps.
constexpr uint32_t kCiReplicates = 10;

// The persistence counts a lane kind is swept over; the other kinds run at
// the shipped count.
const std::vector<int>& swept_persistence();

const std::vector<KindSpec>& kinds();
const std::vector<Cell>& cells();

bool in_ci_subset(const Cell& cell);

// "D01", or for a lane cell "V01/P3".
std::string cell_name(std::size_t cell);

uint32_t run_id(std::size_t cell, uint32_t replicate);

// The run a seed names: its cell and replicate decoded, then its schedule
// drawn from the kind and the replicate alone, so the persistence counts of
// one lane kind share their faults and their noise. The id must name a
// cell (cell_count() above it is refused by the CLI before it gets here).
RunSchedule draw_schedule(uint32_t run_id, const CampaignTiming& timing);
