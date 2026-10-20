// fault_campaign: the fault-injection campaign's command (REQ-TEST-005,
// REQ-TEST-006; FTS-FI-001, FTS-TP-001 TC-036). The decisions live in the
// tested units beside it; this reads the arguments and the configuration,
// loops over the cells and writes the record: runs.csv, manifest.json and
// the configuration's copy, config.yaml.
//
//   fault_campaign --out <dir> [--config <file>] [--commit <sha>] [--platform <text>]
//                  [--replicates <n> | --ci] [--cells <name>[,<name>...]]
//   fault_campaign --seed <run id> [--log <dir>] [--config <file>]

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "campaign_record.h"
#include "campaign_run.h"
#include "config/telemetry_config.h"
#include "fault_catalogue.h"
#include "logging/binary_logger.h"
#include "logging/log_format.h"
#include "prediction.h"
#include "run_outcome.h"
#include "run_schedule.h"

namespace {

struct Options {
    std::filesystem::path config = "config/telemetry_config.yaml";
    std::optional<std::filesystem::path> out;
    std::string commit = "unrecorded";
    std::string platform = "unrecorded";
    std::optional<uint32_t> replicates;
    bool ci = false;
    std::vector<std::string> cells;
    std::optional<uint32_t> seed;
    std::optional<std::filesystem::path> log;
    std::string error;
};

void usage() {
    std::cerr << "usage: fault_campaign --out <dir> [--config <file>] [--commit <sha>] [--platform <text>]\n"
                 "                      [--replicates <n> | --ci] [--cells <name>[,<name>...]]\n"
                 "       fault_campaign --seed <run id> [--log <dir>] [--config <file>]\n";
}

std::optional<uint32_t> whole(const std::string& text) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (text.empty() || *end != '\0' || value > 0xffffffffUL) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(value);
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i++) {
        const std::string flag = argv[i];
        const bool has_value = i + 1 < argc;
        if (flag == "--config" && has_value) {
            o.config = argv[++i];
        } else if (flag == "--out" && has_value) {
            o.out = argv[++i];
        } else if (flag == "--commit" && has_value) {
            o.commit = argv[++i];
        } else if (flag == "--platform" && has_value) {
            o.platform = argv[++i];
        } else if (flag == "--replicates" && has_value) {
            o.replicates = whole(argv[++i]);
            if (!o.replicates.has_value() || *o.replicates == 0 || *o.replicates > kReplicatesPerCell) {
                o.error = "--replicates needs a whole number from 1 to 100000";
            }
        } else if (flag == "--ci") {
            o.ci = true;
        } else if (flag == "--cells" && has_value) {
            std::stringstream list(argv[++i]);
            for (std::string name; std::getline(list, name, ',');) {
                o.cells.push_back(name);
            }
        } else if (flag == "--seed" && has_value) {
            o.seed = whole(argv[++i]);
            if (!o.seed.has_value() || *o.seed / kReplicatesPerCell >= cells().size()) {
                o.error = "--seed needs a run id: a cell's number times 100000 plus its replicate";
            }
        } else if (flag == "--log" && has_value) {
            o.log = argv[++i];
        } else {
            o.error = "unknown or incomplete argument: " + flag;
        }
        if (!o.error.empty()) {
            return o;
        }
    }
    if (!o.seed.has_value() && !o.out.has_value()) {
        o.error = "pass --out <dir> for a campaign, or --seed <run id> for one run";
    } else if (o.ci && o.replicates.has_value()) {
        o.error = "--ci runs the CI subset's fixed seeds; --replicates sets another count, so pass one of them";
    } else if (o.log.has_value() && !o.seed.has_value()) {
        o.error = "--log writes one run's binary log; pass --seed <run id> with it";
    }
    return o;
}

PipelineSettings settings_from(const TelemetryConfig& cfg) {
    PipelineSettings s;
    s.fault = cfg.fault;
    s.processing = cfg.processing;
    s.noise = cfg.simulation.noise;
    s.timing.period_ms = cfg.timing.period_ms;
    s.timing.gps_interval_ms = cfg.simulation.gps_interval_ms;
    s.timing.i2c_timeout_ms = cfg.fault.i2c_timeout_ms;
    s.timing.uart_timeout_ms = cfg.fault.uart_timeout_ms;
    s.timing.stuck_count = cfg.fault.stuck_count;
    s.timing.recovery_count = cfg.fault.recovery_count;
    return s;
}

int one_run(const Options& o, const PipelineSettings& settings) {
    const RunSchedule s = draw_schedule(*o.seed, settings.timing);
    std::optional<BinaryLogger> logger;
    if (o.log.has_value()) {
        std::filesystem::create_directories(*o.log);
        logger.emplace(*o.log, "run_" + std::to_string(*o.seed), uint64_t{1} << 30,
                       make_log_header(RunMode::SIMULATED, RunMode::SIMULATED));
    }
    const Timeline timeline = run_schedule(s, settings, logger.has_value() ? &logger.value() : nullptr);
    const RunOutcome outcome = classify(s, timeline, settings.timing);
    const Verdict verdict = judge(s, timeline, outcome, settings.timing);
    const Verdict written = judge_as_written(s, timeline, outcome, settings.timing);
    std::cout << "Run " << *o.seed << " (" << cell_name(s.cell) << "): " << describe(s) << "\n"
              << csv_header() << "\n"
              << csv_row(s, outcome, verdict, written) << "\n"
              << (verdict.as_predicted ? "as predicted" : "DEPARTS from its prediction: " + verdict.mismatch) << "\n"
              << (written.as_predicted ? "as written" : "departs from the prediction as written: " + written.mismatch)
              << "\n";
    if (logger.has_value()) {
        std::cout << "Logged to " << logger->current_path().string() << "\n";
    }
    return 0;
}

int campaign(const Options& o, const PipelineSettings& settings) {
    std::vector<CellRun> plan;
    for (std::size_t c = 0; c < cells().size(); c++) {
        const bool named = o.cells.empty() ||
                           std::find(o.cells.begin(), o.cells.end(), cell_name(c)) != o.cells.end();
        if (!named || (o.ci && !in_ci_subset(cells()[c]))) {
            continue;
        }
        const uint32_t replicates = o.ci ? kCiReplicates
                                         : o.replicates.value_or(static_cast<uint32_t>(kinds()[cells()[c].kind].full_runs));
        plan.push_back(CellRun{c, replicates});
    }
    if (plan.empty()) {
        std::cerr << "error: --cells names no cell of the catalogue\n";
        return 1;
    }
    std::filesystem::create_directories(*o.out);
    // The configuration's text beside the record, as a session keeps its
    // configuration beside its log (REQ-CFG-003): the report names its hash.
    std::error_code copy_error;
    std::filesystem::copy_file(o.config, *o.out / "config.yaml", std::filesystem::copy_options::overwrite_existing,
                               copy_error);
    if (copy_error) {
        std::cerr << "error: cannot copy " << o.config.string() << " beside the record: " << copy_error.message() << "\n";
        return 1;
    }
    std::ofstream csv(*o.out / "runs.csv");
    csv << csv_header() << "\n";

    ManifestInputs manifest;
    manifest.commit = o.commit;
    manifest.platform = o.platform;
    manifest.config_path = o.config.string();
    manifest.timing = settings.timing;
    manifest.cells = plan;
    std::vector<std::string> departures;
    const auto start = std::chrono::steady_clock::now();
    for (const CellRun& c : plan) {
        const auto cell_start = std::chrono::steady_clock::now();
        uint64_t predicted = 0;
        for (uint32_t r = 0; r < c.replicates; r++) {
            const RunSchedule s = draw_schedule(run_id(c.cell, r), settings.timing);
            const Timeline timeline = run_schedule(s, settings);
            const RunOutcome outcome = classify(s, timeline, settings.timing);
            const Verdict verdict = judge(s, timeline, outcome, settings.timing);
            const Verdict written = judge_as_written(s, timeline, outcome, settings.timing);
            csv << csv_row(s, outcome, verdict, written) << "\n";
            manifest.runs++;
            if (written.as_predicted) {
                manifest.as_written++;
            }
            if (verdict.as_predicted) {
                predicted++;
            } else if (departures.size() < 50) {
                departures.push_back(cell_name(c.cell) + " seed " + std::to_string(s.run_id) + ": " + verdict.mismatch);
            }
        }
        manifest.as_predicted += predicted;
        const double cell_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - cell_start).count();
        std::cout << cell_name(c.cell) << ": " << c.replicates << " runs, " << predicted << " as predicted (" << cell_s
                  << " s)" << std::endl;
    }
    manifest.wall_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::ofstream(*o.out / "manifest.json") << manifest_json(manifest) << "\n";
    std::cout << "Campaign: " << manifest.runs << " runs in " << manifest.wall_seconds << " s, " << manifest.as_predicted
              << " as predicted, " << manifest.runs - manifest.as_predicted << " departures, " << manifest.as_written
              << " as written; record in " << o.out->string() << std::endl;
    for (const std::string& d : departures) {
        std::cout << "  " << d << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Options o = parse(argc, argv);
    if (!o.error.empty()) {
        std::cerr << "error: " << o.error << "\n";
        usage();
        return 1;
    }
    const ConfigLoad load = load_config_file(o.config);
    if (!load.ok) {
        std::cerr << "error: configuration " << o.config.string() << ":\n";
        for (const std::string& problem : load.problems) {
            std::cerr << "  " << problem << "\n";
        }
        return 1;
    }
    const PipelineSettings settings = settings_from(load.config);
    return o.seed.has_value() ? one_run(o, settings) : campaign(o, settings);
}
