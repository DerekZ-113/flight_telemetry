#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "drivers/nmea.h"
#include "drivers/serial_port.h"

// One fix, as the live source takes it: what the GGA and the RMC of one
// epoch said (REQ-SENS-005). Named for the protocol, not the chip: the
// board is sold as a NEO-6M and described as a u-blox 7, and nothing here
// depends on which it is.
struct GpsFix {
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    float altitude_m = 0.0f;         // above mean sea level
    float ground_speed_mps = 0.0f;
    float course_deg = 0.0f;         // over ground, degrees true
    int fix_quality = 0;             // 1 GPS, 2 differential, 6 estimated; never 0 in a fix
    int satellites = 0;
    std::string utc_time;            // the epoch's "hhmmss.ss": the receiver's clock, not the frame's
};

// What the driver saw, printed at exit. The detector sees only whether a
// fix arrived; these counters are the operator's view of a receiver that
// talks without a fix, is wired at the wrong baud rate (bad checksums), or
// is not there at all (nothing).
struct GpsCounters {
    size_t bytes = 0;
    size_t sentences = 0;          // complete lines that passed the checksum
    size_t used = 0;               // of those, GGA and RMC
    size_t bad_checksum = 0;
    size_t malformed = 0;          // a GGA or RMC the parser refused
    size_t overlong = 0;           // a line past kMaxLineBytes with no newline
    size_t resyncs = 0;            // runs of bytes dropped to find the next "$"
    size_t incomplete_epochs = 0;  // a GGA or RMC replaced before its partner came
    size_t no_fix_epochs = 0;      // pairs the receiver itself reported invalid
    size_t fixes = 0;              // delivered by poll()
    size_t fixes_skipped = 0;      // completed in a poll that then completed a later one
    size_t read_errors = 0;
};

// The GPS driver: bytes in from a SerialPort, at most one fix out per
// poll. It frames the byte stream into sentences (a sentence starts at
// "$" and ends at a newline, whatever the port cut it into), checks each
// checksum, parses GGA and RMC, and pairs the two that carry the same UTC
// time into an epoch. The epoch is a fix only if the receiver says so:
// GGA quality at least 1 with a position, RMC status A. Everything else
// is counted and dropped, so gps_read_ok can mean exactly "a valid fix
// completed this cycle" (FTS-FM-001 FAULT-003).
//
// Called once per 50 Hz cycle. It reads everything waiting, in chunks,
// up to a cap: at 9600 baud a cycle carries nineteen bytes and an epoch
// takes about half a second to arrive; over USB an epoch lands as one
// burst. The cap keeps a receiver at the wrong rate, or a burst after a
// stall, from stretching the cycle (design.md section 10). The three
// sizes are facts about NMEA and the cycle, not tuning, so they are
// constants and not keys.
class NmeaGps {
public:
    static constexpr size_t kMaxLineBytes = 128;      // NMEA 0183's longest sentence is 82
    static constexpr size_t kChunkBytes = 256;
    static constexpr size_t kMaxBytesPerPoll = 4096;

    explicit NmeaGps(SerialPort& port);

    // The latest fix completed during this call, if any. A fix is
    // delivered once; when two complete in one call the earlier is
    // counted as skipped, because the newest is the one that is current.
    std::optional<GpsFix> poll();

    const GpsCounters& counters() const;

private:
    void feed(uint8_t byte);
    void complete_line();
    void take_gga(const nmea::Gga& gga);
    void take_rmc(const nmea::Rmc& rmc);
    void try_complete_epoch();

    SerialPort& port_;
    std::string line_;
    bool in_sentence_ = false;   // between a "$" and its newline
    bool discarding_ = false;    // dropping bytes outside a sentence, counted once per run
    std::optional<nmea::Gga> pending_gga_;
    std::optional<nmea::Rmc> pending_rmc_;
    std::optional<GpsFix> latest_;
    GpsCounters counters_{};
};
