#include "drivers/nmea_gps.h"

#include <algorithm>
#include <vector>

NmeaGps::NmeaGps(SerialPort& port) : port_(port) {
    line_.reserve(kMaxLineBytes + 1);
}

std::optional<GpsFix> NmeaGps::poll() {
    latest_.reset();
    size_t total = 0;
    uint8_t chunk[kChunkBytes];
    while (total < kMaxBytesPerPoll) {
        const size_t want = std::min(kChunkBytes, kMaxBytesPerPoll - total);
        const std::optional<size_t> got = port_.read(chunk, want);
        if (!got.has_value()) {
            counters_.read_errors++;
            break;
        }
        if (*got == 0) {
            break;
        }
        counters_.bytes += *got;
        total += *got;
        for (size_t i = 0; i < *got; ++i) {
            const uint8_t byte = chunk[i];
            feed(byte);
        }
    }
    if (latest_.has_value()) {
        counters_.fixes++;
    }
    return latest_;
}

const GpsCounters& NmeaGps::counters() const {
    return counters_;
}

void NmeaGps::feed(uint8_t byte) {
    const char c = static_cast<char>(byte);
    if (c == '$') {
        // A sentence starts here whatever was in progress: a partial line
        // is noise, not a sentence, and the stream resyncs on the "$".
        if (in_sentence_) {
            counters_.resyncs++;
        }
        line_.assign(1, '$');
        in_sentence_ = true;
        discarding_ = false;
        return;
    }
    if (!in_sentence_) {
        // Garbage between sentences, or the tail of a sentence the flush
        // at open cut in half: one count per run, until the next "$".
        if (!discarding_) {
            counters_.resyncs++;
            discarding_ = true;
        }
        return;
    }
    if (c == '\n') {
        complete_line();
        return;
    }
    line_.push_back(c);
    if (line_.size() > kMaxLineBytes) {
        // No sentence is this long. A receiver at the wrong baud rate
        // produces one; so would a stuck line. Drop it and wait for a "$".
        counters_.overlong++;
        line_.clear();
        in_sentence_ = false;
        discarding_ = true;
    }
}

void NmeaGps::complete_line() {
    // line_ holds at least the "$" that opened it.
    const bool carriage_return = line_.back() == '\r';
    if (carriage_return) {
        line_.pop_back();
    }
    const bool valid = nmea::checksum_ok(line_);
    if (valid) {
        counters_.sentences++;
        // checksum_ok proved a "$" first and a "*" after it, so fields()
        // holds at least the address.
        const std::vector<std::string> f = nmea::fields(line_);
        const nmea::Formatter kind = nmea::formatter(f[0]);
        if (kind == nmea::Formatter::GGA) {
            counters_.used++;
            const std::optional<nmea::Gga> gga = nmea::parse_gga(f);
            if (gga.has_value()) {
                take_gga(*gga);
            } else {
                counters_.malformed++;
            }
        } else if (kind == nmea::Formatter::RMC) {
            counters_.used++;
            const std::optional<nmea::Rmc> rmc = nmea::parse_rmc(f);
            if (rmc.has_value()) {
                take_rmc(*rmc);
            } else {
                counters_.malformed++;
            }
        }
    } else {
        counters_.bad_checksum++;
    }
    line_.clear();
    in_sentence_ = false;
    discarding_ = false;
}

// A GGA and an RMC belong to one epoch when their UTC time fields are the
// same text. A sentence of a newer epoch replaces a pending one of an older
// epoch, and a sentence that arrives twice replaces its first copy; each
// replacement is an epoch that never completed.
void NmeaGps::take_gga(const nmea::Gga& gga) {
    bool stale_rmc = false;
    if (pending_rmc_.has_value()) {
        stale_rmc = pending_rmc_->utc_time != gga.utc_time;
    }
    if (stale_rmc) {
        pending_rmc_.reset();
        counters_.incomplete_epochs++;
    }
    if (pending_gga_.has_value()) {
        counters_.incomplete_epochs++;
    }
    pending_gga_ = gga;
    try_complete_epoch();
}

void NmeaGps::take_rmc(const nmea::Rmc& rmc) {
    bool stale_gga = false;
    if (pending_gga_.has_value()) {
        stale_gga = pending_gga_->utc_time != rmc.utc_time;
    }
    if (stale_gga) {
        pending_gga_.reset();
        counters_.incomplete_epochs++;
    }
    if (pending_rmc_.has_value()) {
        counters_.incomplete_epochs++;
    }
    pending_rmc_ = rmc;
    try_complete_epoch();
}

void NmeaGps::try_complete_epoch() {
    const bool both = pending_gga_.has_value() && pending_rmc_.has_value();
    if (!both) {
        return;
    }
    const nmea::Gga& gga = *pending_gga_;
    const nmea::Rmc& rmc = *pending_rmc_;
    // The receiver's own verdict, both halves of it. The position comes
    // from the GGA, which also carries the altitude the Kalman filter
    // takes; speed and course from the RMC. A GGA with quality 1 or more
    // always has a position: parse_gga refuses one without.
    const bool fix = gga.fix_quality >= 1 && rmc.valid;
    if (fix) {
        if (latest_.has_value()) {
            counters_.fixes_skipped++;
        }
        GpsFix out{};
        out.latitude_deg = gga.latitude_deg;
        out.longitude_deg = gga.longitude_deg;
        out.altitude_m = gga.altitude_m;
        out.ground_speed_mps = rmc.speed_mps;
        out.course_deg = rmc.course_deg;
        out.fix_quality = gga.fix_quality;
        out.satellites = gga.satellites;
        out.utc_time = gga.utc_time;
        latest_ = out;
    } else {
        counters_.no_fix_epochs++;
    }
    pending_gga_.reset();
    pending_rmc_.reset();
}
