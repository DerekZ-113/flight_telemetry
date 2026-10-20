// Tests for src/drivers/selection_log (REQ-SENS-009). Test card TC-029
// step 11: the header line and one row per change, in a temporary file;
// an unwritable path reports not ok and records nothing.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "drivers/selection_log.h"
#include "scratch_dir.h"

namespace fs = std::filesystem;

namespace {

std::string read_all(const fs::path& path) {
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

SwitchRecord record(uint64_t timestamp_ms, size_t poll, std::optional<AircraftIdentity> previous,
                    std::optional<AircraftIdentity> current, SwitchReason reason, double distance_m) {
    SwitchRecord r;
    r.timestamp_ms = timestamp_ms;
    r.poll = poll;
    r.change.previous = std::move(previous);
    r.change.current = std::move(current);
    r.change.reason = reason;
    r.change.distance_m = distance_m;
    return r;
}

}  // namespace

// The header, then one row per change: an initial selection, a landing
// replaced by an aircraft without a callsign, and a drop with nothing in
// its place; the distance to one decimal.
TEST(SelectionLogTest, WritesTheHeaderAndOneRowPerChange) {
    const ScratchDir scratch("selection_log");
    const fs::path path = scratch.path() / "telemetry_adsb.csv";
    {
        SelectionLog log(path);
        ASSERT_TRUE(log.ok());
        log.record(record(100, 1, std::nullopt, AircraftIdentity{"000001", "TEST001"}, SwitchReason::INITIAL, 5123.44));
        log.record(record(5000, 3, AircraftIdentity{"000001", "TEST001"}, AircraftIdentity{"000002", ""},
                          SwitchReason::LANDED, 8900.0));
        log.record(record(9000, 5, AircraftIdentity{"000002", ""}, std::nullopt, SwitchReason::LEFT, 0.0));
    }
    EXPECT_EQ(read_all(path),
              "timestamp_ms,poll,previous_icao24,previous_callsign,icao24,callsign,reason,distance_m\n"
              "100,1,,,000001,TEST001,initial,5123.4\n"
              "5000,3,000001,TEST001,000002,,landed,8900.0\n"
              "9000,5,000002,,,,left,0.0\n");
}

TEST(SelectionLogTest, UnwritablePathReportsNotOk) {
    SelectionLog log(fs::temp_directory_path() / "ftlg_no_such_directory" / "adsb.csv");
    EXPECT_FALSE(log.ok());
    log.record(record(1, 1, std::nullopt, std::nullopt, SwitchReason::LEFT, 0.0));   // harmless
}
