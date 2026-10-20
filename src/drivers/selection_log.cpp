#include "drivers/selection_log.h"

#include <iomanip>
#include <optional>
#include <ostream>

namespace {

void write_identity(std::ostream& out, const std::optional<AircraftIdentity>& identity) {
    if (identity.has_value()) {
        out << identity->icao24 << ',' << identity->callsign;
    } else {
        out << ',';
    }
}

}  // namespace

SelectionLog::SelectionLog(const std::filesystem::path& file)
    : out_(file, std::ios::trunc)
{
    ok_ = out_.good();
    if (ok_) {
        out_ << "timestamp_ms,poll,previous_icao24,previous_callsign,icao24,callsign,reason,distance_m\n";
    }
}

void SelectionLog::record(const SwitchRecord& change) {
    if (!ok_) {
        return;
    }
    out_ << change.timestamp_ms << ',' << change.poll << ',';
    write_identity(out_, change.change.previous);
    out_ << ',';
    write_identity(out_, change.change.current);
    out_ << ',' << switch_reason_name(change.change.reason) << ','
         << std::fixed << std::setprecision(1) << change.change.distance_m << std::endl;
}
