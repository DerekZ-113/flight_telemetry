#include "timing/jitter_log.h"

void JitterStats::add(const CycleTiming& t) {
    cycles++;
    if (t.jitter_ns < min_ns) min_ns = t.jitter_ns;
    if (t.jitter_ns > max_ns) max_ns = t.jitter_ns;
    sum_ns += static_cast<double>(t.jitter_ns);
    if (t.missed_cycles > 0) {
        overruns++;
        missed_cycles += t.missed_cycles;
    }
}

double JitterStats::mean_ns() const {
    return cycles == 0 ? 0.0 : sum_ns / static_cast<double>(cycles);
}

JitterLog::JitterLog(const std::filesystem::path& file)
    : out_(file, std::ios::trunc)
{
    ok_ = out_.good();
    if (ok_) {
        out_ << "cycle,scheduled_ns,actual_ns,jitter_ns,missed_cycles\n";
    }
}

void JitterLog::record(const CycleTiming& t) {
    stats_.add(t);
    if (!ok_) {
        return;
    }
    out_ << t.cycle << ',' << t.scheduled_ns << ',' << t.actual_ns << ','
         << t.jitter_ns << ',' << t.missed_cycles << '\n';
}
