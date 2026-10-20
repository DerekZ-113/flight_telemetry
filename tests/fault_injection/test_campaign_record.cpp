// Tests for tests/fault_injection/campaign_record.cpp (REQ-TEST-006): the
// CSV row the report generator reads by column name, and the manifest that
// records what was run. Test card TC-035 step 8's C++ half.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "campaign_record.h"
#include "campaign_run.h"
#include "fault_catalogue.h"
#include "prediction.h"
#include "run_outcome.h"

namespace {

// The fields of one CSV line: a quoted field may hold commas and doubled
// quotes.
std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out(1);
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); i++) {
        const char c = line[i];
        if (quoted && c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
            out.back() += '"';
            i++;
        } else if (c == '"') {
            quoted = !quoted;
        } else if (c == ',' && !quoted) {
            out.emplace_back();
        } else {
            out.back() += c;
        }
    }
    return out;
}

std::size_t cell_named(const std::string& name) {
    for (std::size_t c = 0; c < cells().size(); c++) {
        if (cell_name(c) == name) {
            return c;
        }
    }
    return 0;
}

}  // namespace

// Every row has the header's columns, whatever the cell, and a mismatch
// holding a comma stays one field.
TEST(CampaignRecordTest, EveryRowHasTheHeadersColumns) {
    const PipelineSettings settings{};
    const std::vector<std::string> header = fields(csv_header());
    const std::size_t columns = header.size();
    EXPECT_GT(columns, 60u);
    std::size_t mismatch = 0;
    while (mismatch < columns && header[mismatch] != "mismatch") {
        mismatch++;
    }
    ASSERT_LT(mismatch, columns);
    for (const char* name : {"D04", "T03", "U07", "C00", "V12/P3"}) {
        const RunSchedule s = draw_schedule(run_id(cell_named(name), 1), settings.timing);
        const Timeline tl = run_schedule(s, settings);
        const RunOutcome o = classify(s, tl, settings.timing);
        Verdict departing;
        departing.as_predicted = false;
        departing.mismatch = "BARO latency: 480, predicted 500";
        const std::vector<std::string> row = fields(csv_row(s, o, departing, departing));
        ASSERT_EQ(row.size(), columns) << name;
        EXPECT_EQ(row[0], std::to_string(s.run_id));
        EXPECT_EQ(row[1], name);
        EXPECT_EQ(row[mismatch], "BARO latency: 480, predicted 500") << name;
    }
}

TEST(CampaignRecordTest, TheManifestRecordsTheCommitThePlatformAndTheCells) {
    ManifestInputs in;
    in.commit = "e23fc87";
    in.platform = "test platform";
    in.cells = {CellRun{0, 1000}, CellRun{cell_named("V01/P3"), 1000}};
    in.runs = 2000;
    in.as_predicted = 1999;
    in.as_written = 1998;
    const std::string json = manifest_json(in);
    for (const std::string expected : {"\"commit\": \"e23fc87\"", "\"platform\": \"test platform\"", "\"name\": \"D01\"",
                                       "\"name\": \"V01/P3\"", "\"first_run_id\": 2300000",
                                       "\"last_run_id\": 2300999", "\"isolation_latency_ms\": 40",
                                       "\"as_written\": 1998", "\"i2c_timeout_latency_ms\": 500"}) {
        EXPECT_NE(json.find(expected), std::string::npos) << expected;
    }
    EXPECT_NE(json.find("toolchain"), std::string::npos);
}
