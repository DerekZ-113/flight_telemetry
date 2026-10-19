#include "replay/recorded_injections.h"

#include "logging/log_reader.h"

std::vector<LaneFault> recorded_lane_faults(const std::vector<std::filesystem::path>& logs, std::string& error) {
    std::vector<LaneFault> faults;
    for (std::size_t i = 0; i < logs.size(); i++) {
        LogReader reader(logs[i]);
        // Each call on a line of its own and each decision on the next, the
        // style rule that keeps GCC's exception edges off a decision's line
        // (FTS-VR-001 Entry 11 §11.7, Entry 20 §20.7).
        const bool readable = reader.ok();
        if (!readable) {
            error = reader.error();
            return {};
        }
        LogRecord record;
        bool more = reader.next(record);
        while (more) {
            const bool injected = record.type == RecordType::FAULT_EVENT && record.event.channel == Channel::VOTER &&
                                  record.event.type == FaultType::LANE_FAULT_INJECTED;
            if (injected) {
                const float lane = record.event.value;
                const bool names_a_lane = lane == 1.0f || lane == 2.0f || lane == 3.0f;
                if (!names_a_lane) {
                    error = "a LANE_FAULT_INJECTED event in " + logs[i].string() + " names no lane from 1 to 3";
                    return {};
                }
                faults.push_back(demo_lane_fault(static_cast<int>(lane), record.event.timestamp_ms));
            }
            more = reader.next(record);
        }
    }
    return faults;
}
