#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "processing/lane_fault_injector.h"

// The lane faults a log records (REQ-RED-007): one demonstration fault for
// every LANE_FAULT_INJECTED event in the files, in the order they hold
// them, at the event's lane and timestamp. A replay re-applies them, so it
// reproduces the recorded run record for record.
//
// It reads the files with a reader of its own, before the replay source
// opens them: the source skips event records and reads a frame ahead
// (log_replay_source.h), so a marker it streamed past would reach the
// pipeline a frame late. A file the reader refuses, or an injection that
// names no lane, sets `error` and returns nothing, and the replay refuses
// to start, as it does for a header it cannot read.
std::vector<LaneFault> recorded_lane_faults(const std::vector<std::filesystem::path>& logs, std::string& error);
