#pragma once

#include <filesystem>
#include <fstream>

#include "drivers/adsb_source.h"

// Every change of the selected aircraft as CSV beside the log
// (REQ-SENS-009), one line per change:
//   timestamp_ms,poll,previous_icao24,previous_callsign,icao24,callsign,reason,distance_m
//
// The jitter CSV's shape, for the jitter CSV's reason: a switch is an
// event of the run, not of the flight, and the telemetry log must replay
// byte-identically, so the record goes beside the log, named by the run's
// prefix (`telemetry_adsb.csv`). The timestamp is that of the first frame
// which carried the new aircraft, so the file says which aircraft every
// frame of a recorded run showed, which the log itself cannot (design.md
// section 12). An absent identity is two empty fields: a dropped aircraft
// with nothing in its place has its reason and nothing after it. Each row
// is flushed as it is written, so the file is current during a run.
class SelectionLog {
public:
    explicit SelectionLog(const std::filesystem::path& file);

    bool ok() const { return ok_; }
    void record(const SwitchRecord& change);

private:
    std::ofstream out_;
    bool ok_ = false;
};
