#include "drivers/adsb_source.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace {

AdsbReading reading_of(const AircraftIdentity& identity, const opensky_json::AircraftState& state,
                       int64_t server_time) {
    // Every optional is present: the selector keeps a state only when it
    // is usable, which is exactly these six fields.
    AdsbReading reading;
    reading.identity = identity;
    reading.latitude = *state.latitude;
    reading.longitude = *state.longitude;
    reading.gps_altitude_m = *state.geo_altitude_m;
    reading.ground_speed_mps = *state.velocity_mps;
    reading.track_deg = *state.true_track_deg;
    reading.time_position = *state.time_position;
    reading.server_time = server_time;
    return reading;
}

}  // namespace

AdsbSource::AdsbSource(std::unique_ptr<HttpClient> http, Clock& clock, const AdsbConfig& config,
                       const opensky_json::Credentials& credentials, const std::string& credentials_path,
                       const std::string& origin_override)
    // Declaration order: the client takes its reference to *http_, the
    // worker its step over everything above it.
    : config_(config),
      clock_(clock),
      http_(std::move(http)),
      client_(*http_, clock, credentials, config.box, origin_override),
      selector_(config.reference, config.switch_margin_m, config.gone_after_polls),
      credentials_path_(credentials_path),
      worker_([this] { return poll_once(); })
{
    counters_.age_min_s = std::numeric_limits<int64_t>::max();
    counters_.age_max_s = std::numeric_limits<int64_t>::min();
}

AdsbSource::~AdsbSource() {
    stop();
}

bool AdsbSource::start() {
    // On the calling thread, before the worker exists: a refused token is
    // known before the loop, and the first frame can carry a position.
    PollReport first = poll();
    const bool refused = first.status == FetchStatus::TOKEN_REFUSED;
    const long http_status = first.http_status;
    const std::chrono::milliseconds delay = first.next_delay;
    publish(std::move(first));
    if (refused) {
        // The status and the path, never the body (REQ-CFG-004, FAULT-013).
        error_ = "the token endpoint refused the credentials in " + credentials_path_ + " (HTTP " +
                 std::to_string(http_status) + ")";
        return false;
    }
    worker_.start(delay);
    return true;
}

const std::string& AdsbSource::error() const {
    return error_;
}

void AdsbSource::stop() {
    worker_.stop();
}

bool AdsbSource::polling() const {
    return worker_.running();
}

std::chrono::milliseconds AdsbSource::poll_once() {
    PollReport report = poll();
    const std::chrono::milliseconds delay = report.next_delay;
    publish(std::move(report));
    return delay;
}

PollReport AdsbSource::poll() {
    PollReport report;
    const FetchResult result = client_.fetch_states();
    report.client = client_.counters();
    report.poll = report.client.polls;
    report.status = result.status;
    report.http_status = result.http_status;
    report.next_delay = std::chrono::milliseconds(config_.poll_interval_ms);
    // The service's own back-pressure (FAULT-014): wait what it names, the
    // interval when it names nothing.
    const bool told_to_wait =
        result.status == FetchStatus::RATE_LIMITED && result.retry_after_s.has_value() && *result.retry_after_s > 0;
    if (told_to_wait) {
        report.next_delay = std::chrono::seconds(*result.retry_after_s);
    }
    if (result.status == FetchStatus::OK) {
        // Only a response reaches the selector: a failed poll says nothing
        // about the sky and must not count as a miss (FAULT-015).
        const opensky_json::StatesDocument& document = result.document;
        report.response = true;
        report.aircraft = document.states.size();
        report.candidates = static_cast<size_t>(
            std::count_if(document.states.begin(), document.states.end(), &AircraftSelector::candidate));
        std::optional<SelectionChange> change = selector_.update(document);
        if (change.has_value()) {
            report.changes.push_back(SwitchRecord{0, report.poll, std::move(*change)});
        }
        if (selector_.selected_state().has_value()) {
            report.selected = reading_of(*selector_.selected(), *selector_.selected_state(), document.time);
        }
    }
    report.identity = selector_.selected();
    return report;
}

void AdsbSource::publish(PollReport report) {
    std::lock_guard<std::mutex> lock(slot_mutex_);
    if (pending_.has_value()) {
        // The loop has not taken the last report: this one supersedes it
        // (latest wins), carrying what must not be lost, the switches in
        // order and a vector this poll did not replace.
        overwritten_++;
        report.changes.insert(report.changes.begin(), pending_->changes.begin(), pending_->changes.end());
        if (!report.selected.has_value()) {
            report.selected = std::move(pending_->selected);
        }
    }
    pending_ = std::move(report);
}

std::optional<PollReport> AdsbSource::take() {
    std::lock_guard<std::mutex> lock(slot_mutex_);
    counters_.overwritten = overwritten_;
    std::optional<PollReport> report = std::move(pending_);
    pending_.reset();
    return report;
}

TelemetryFrame AdsbSource::read_frame() {
    // Value-initialized: the logger writes the struct's bytes
    // (verification_results.md Entries 4 and 5).
    TelemetryFrame frame{};

    // Read time, as the live source stamps it: milliseconds since the
    // first frame, rounded to the nearest millisecond.
    const uint64_t now_ns = clock_.now_ns();
    if (!first_read_ns_.has_value()) {
        first_read_ns_ = now_ns;
    }
    frame.timestamp_ms = (now_ns - *first_read_ns_ + 500'000) / 1'000'000;
    frame.sequence = next_sequence();   // REQ-LOG-008: the frames' count, not the polls'

    bool fresh = false;
    std::optional<PollReport> report = take();
    if (report.has_value()) {
        counters_.reports++;
        counters_.client = report->client;
        if (report->response) {
            counters_.aircraft = report->aircraft;
            counters_.candidates = report->candidates;
        }
        for (SwitchRecord& record : report->changes) {
            // The timestamp of the first frame that carries the new
            // aircraft: this one.
            record.timestamp_ms = frame.timestamp_ms;
            switches_.push_back(std::move(record));
            counters_.changes++;
        }
        if (report->selected.has_value()) {
            const AdsbReading& vector = *report->selected;
            // Fresh once: a newly selected aircraft, whatever its position
            // time, or the kept aircraft at a later position time. The
            // same vector polled again is the same measurement.
            const bool newly_selected = !identity_.has_value() || *identity_ != vector.identity;
            const bool later = held_.has_value() && held_->identity == vector.identity &&
                               vector.time_position > held_->time_position;
            if (newly_selected || later) {
                held_ = vector;
                fresh = true;
                counters_.readings++;
                const int64_t age_s = vector.server_time - vector.time_position;
                counters_.age_min_s = std::min(counters_.age_min_s, age_s);
                counters_.age_max_s = std::max(counters_.age_max_s, age_s);
                counters_.age_sum_s += static_cast<double>(age_s);
            }
        }
        identity_ = report->identity;
    }

    // The held vector on every frame; fresh on the one it arrived.
    if (held_.has_value()) {
        frame.latitude = held_->latitude;
        frame.longitude = held_->longitude;
        frame.gps_altitude_m = held_->gps_altitude_m;
        frame.ground_speed_mps = held_->ground_speed_mps;
        frame.course_deg = held_->track_deg;
    }
    frame.gps_read_ok = fresh;

    // The channels this source does not provide (REQ-SENS-011,
    // REQ-FAULT-007): their fields the zeros of the value-initialized
    // frame, never fresh, NOT_AVAILABLE for the detector to leave alone.
    // The GPS channel is the detector's to judge.
    frame.baro_status = ChannelStatus::NOT_AVAILABLE;
    frame.imu_status = ChannelStatus::NOT_AVAILABLE;
    frame.gps_status = ChannelStatus::NOMINAL;
    return frame;
}

const std::optional<AircraftIdentity>& AdsbSource::identity() const {
    return identity_;
}

const std::vector<SwitchRecord>& AdsbSource::switches() const {
    return switches_;
}

const AdsbCounters& AdsbSource::counters() const {
    return counters_;
}

const std::optional<AdsbReading>& AdsbSource::last_reading() const {
    return held_;
}
