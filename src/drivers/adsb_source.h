#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "data_source.h"
#include "drivers/adsb_config.h"
#include "drivers/aircraft_selector.h"
#include "drivers/http_client.h"
#include "drivers/opensky_client.h"
#include "drivers/opensky_json.h"
#include "timing/clock.h"
#include "timing/periodic_worker.h"

// The fifth DataSource, and the first whose sensor is another vehicle
// (REQ-SENS-008, REQ-SENS-010, REQ-SENS-011; the ADS-B mode of
// REQ-LOG-004): one aircraft from the OpenSky Network's states endpoint,
// presented as the frame's GPS channel. Six raw fields take a state vector
// directly, in the service's SI units: latitude, longitude, the geometric
// altitude (the aircraft's GNSS height above the WGS-84 ellipsoid, a
// different datum from the bench receiver's altitude above mean sea
// level, design.md section 12) as gps_altitude_m, the ground speed, and
// the track over ground as course_deg, the receiver's course over ground
// (REQ-SENS-005). The broadcast carries no pressure and no attitude,
// so the barometer and the IMU are channels this source does not provide:
// their fields zero, never fresh, and NOT_AVAILABLE on every frame
// (REQ-FAULT-007), the one status a source asserts. A pressure derived
// from the reported altitude would be a fabrication posing as a
// measurement. The pressure altitude and the vertical rate the vector
// carries are raw fields of their own since format version 3, each with a
// presence bit (REQ-SENS-008): a vector may lack either, and zero is a
// rate. Nothing consumes them yet; the vertical rate is Phase B's third
// measurement.
//
// Sample-and-hold (REQ-SENS-010). The service answers every few seconds
// and the loop runs at 50 Hz, so one vector serves many frames, and it is
// a measurement once: gps_read_ok is true on the one cycle a new vector (a
// later position time, or a newly selected aircraft) is first handed over
// and false on every other, the held fields unchanged. That is the shape
// REQ-PROC-006 was written for: the Kalman filter updates once per
// reading and predicts between, its uncertainty growing while nothing
// arrives, whereas the same vector applied again would tell the filter
// that two independent sensors agreed. The timestamp is the Clock seam's
// read time since the first frame, as the live source stamps it; the
// position's own epoch is the server's and is kept for the age on the
// exit line. The first frame carrying a newly selected aircraft is also
// flagged NEW_VEHICLE (REQ-PROC-008), so the processor begins the new
// aircraft from that reading instead of reading the step between two
// aircraft's altitudes as a climb.
//
// The thread (design.md section 10). The HTTP request takes hundreds of
// milliseconds, so it runs on PeriodicWorker: poll_once() fetches, selects
// (AircraftSelector, REQ-SENS-009) and publishes one whole PollReport into
// a slot under a mutex, latest wins, an untaken report replaced and
// counted with its changes and its vector carried forward; read_frame()
// takes the slot in one lock and one move, merges the counters, records
// each switch with the frame's timestamp and copies the held vector into
// the GPS fields, so the loop holds a lock for a move and never waits on
// the network. The client's and the selector's state belong to the poll
// thread; the counters, the switch records and the identity belong to the
// loop thread, so identity(), switches() and counters() are plain reads on
// the thread that calls read_frame(), and stop() (a join) makes them final
// for the exit line. start() runs the first poll on the calling thread
// before any thread exists, so a refused token refuses the run by name and
// the first frame can already carry a position. The worker is declared
// last: destroyed first, it is joined while everything poll_once() touches
// is still alive.
//
// No credential, token, header or body reaches a message, a counter or a
// frame (REQ-CFG-004): a refusal names the status and the file's path.

// One state vector of the selected aircraft: the raw fields a reading
// fills, the two the vector may lack as optionals, and the times that say
// how old it is.
struct AdsbReading {
    AircraftIdentity identity;
    double latitude = 0.0;
    double longitude = 0.0;
    float gps_altitude_m = 0.0f;      // the geometric altitude: GNSS height above the WGS-84 ellipsoid
    float ground_speed_mps = 0.0f;
    float track_deg = 0.0f;           // the track over ground: the frame's course_deg (REQ-SENS-005)
    std::optional<float> vertical_rate_mps;     // positive climbing; the frame's gps_vertical_rate_mps (REQ-SENS-008)
    std::optional<float> pressure_altitude_m;   // at 1013.25 hPa; the frame's gps_pressure_altitude_m (REQ-SENS-008)
    int64_t time_position = 0;        // the position's own epoch, the server's unix seconds
    int64_t server_time = 0;          // the response's time; the difference is the position's age
};

// A change of the selected aircraft as the frames saw it: the change, the
// poll whose response caused it, and the timestamp of the first frame that
// carried the new aircraft (with none, the first frame after the drop).
struct SwitchRecord {
    uint64_t timestamp_ms = 0;
    size_t poll = 0;
    SelectionChange change;
};

// What one poll hands the loop: the whole outcome in one object, so the
// slot holds one thing and the loop takes it in one move.
struct PollReport {
    size_t poll = 0;                                   // the poll's ordinal, 1 for the first
    FetchStatus status = FetchStatus::TRANSPORT_ERROR;
    long http_status = 0;                              // the last status seen, 0 without one
    bool response = false;                             // a states document arrived: the census below is this response's
    size_t aircraft = 0;                               // rows in the response
    size_t candidates = 0;                             // of them, candidates
    std::optional<AdsbReading> selected;               // the kept aircraft's vector, when it was in the response and usable
    std::optional<AircraftIdentity> identity;          // the selection after this poll
    std::vector<SwitchRecord> changes;                 // in order, each with the poll that caused it, its timestamp the loop's to stamp; those of a replaced untaken report first
    OpenSkyCounters client;                            // the client's counters, copied
    std::chrono::milliseconds next_delay{0};           // the interval, or a 429's Retry-After
};

// The loop thread's view, for the exit line. The ages are of the delivered
// readings, server time minus position time, min and max seeded from the
// limits so the first reading sets both (jitter_log.h).
struct AdsbCounters {
    OpenSkyCounters client;    // as of the last report taken
    size_t reports = 0;        // reports taken by the loop
    size_t overwritten = 0;    // reports replaced in the slot before the loop took them
    size_t readings = 0;       // vectors delivered fresh to a frame
    size_t changes = 0;        // switch records
    size_t aircraft = 0;       // rows in the last states response
    size_t candidates = 0;     // of them, candidates
    int64_t age_min_s = 0;
    int64_t age_max_s = 0;
    double age_sum_s = 0.0;    // the mean is age_sum_s / readings
};

class AdsbSource : public DataSource {
public:
    // Takes ownership of `http`; `clock` must outlive this object.
    // `credentials_path` names the file the credentials came from, for a
    // refusal that must name the path and never the contents;
    // `origin_override` replaces the service's origin for a test server
    // (opensky_client.h).
    AdsbSource(std::unique_ptr<HttpClient> http, Clock& clock, const AdsbConfig& config,
               const opensky_json::Credentials& credentials, const std::string& credentials_path,
               const std::string& origin_override = "");
    ~AdsbSource() override;

    // The first poll on the calling thread, then the thread. False only
    // when the token endpoint refused the credentials, error() naming the
    // status and the path; an unreachable service is counted and tried
    // again at the interval, and the run starts with no reading yet.
    bool start();
    const std::string& error() const;
    // Joins the poll thread; after it the counters are final. Harmless
    // twice and before start().
    void stop();
    bool polling() const;   // the thread is running

    // One poll on the calling thread: the fetch, the selection, the report
    // into the slot. Returns the delay before the next poll. Public so the
    // tests drive it synchronously with the fake client; the worker's step.
    std::chrono::milliseconds poll_once();

    TelemetryFrame read_frame() override;

    // The loop thread's: read on the thread that calls read_frame(), or
    // after stop().
    const std::optional<AircraftIdentity>& identity() const;
    const std::vector<SwitchRecord>& switches() const;
    const AdsbCounters& counters() const;
    const std::optional<AdsbReading>& last_reading() const;   // the last vector delivered fresh

private:
    PollReport poll();
    void publish(PollReport report);
    std::optional<PollReport> take();

    AdsbConfig config_;
    Clock& clock_;
    std::unique_ptr<HttpClient> http_;
    OpenSkyClient client_;
    AircraftSelector selector_;
    std::string credentials_path_;
    std::string error_;

    // The slot: written by the poll thread, read by the loop, latest wins.
    std::mutex slot_mutex_;
    std::optional<PollReport> pending_;
    size_t overwritten_ = 0;

    // The loop's own: never touched by the poll thread.
    std::optional<uint64_t> first_read_ns_;
    std::optional<AdsbReading> held_;
    std::optional<AircraftIdentity> identity_;
    std::vector<SwitchRecord> switches_;
    AdsbCounters counters_;

    // Declared last: destroyed first, so the thread is joined while
    // everything poll_once() touches is still alive.
    PeriodicWorker worker_;
};
