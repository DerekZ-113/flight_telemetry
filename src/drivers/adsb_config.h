#pragma once

#include <cstdint>

// The ADS-B source's tunables (REQ-SENS-008, REQ-SENS-009, REQ-FAULT-001):
// the optional `adsb` block of the configuration file (REQ-CFG-001). The
// block is the one a file may omit: an ADS-B run requires it and main
// refuses --adsb without it by name, no other run reads it, and a
// configuration recorded before the block existed replays as recorded
// (design.md section 12). Present, every key inside it is required like
// any other. The defaults equal the shipped file's values, the unit tests'
// baseline (ConfigTest.ShippedFileEqualsDefaults holds them equal), never
// the binary's fallback. The service's endpoints are constants of the
// client and the credentials come from a file outside the repository
// (REQ-CFG-004): neither is a key.
//
// The box and the reference point are public places, the SFO approach
// and the airport, never a private location: the file is public and is
// recorded beside every log (REQ-CFG-003).

// The bounding box the service is asked for, WGS-84 degrees. The research
// note's box: SFO, the Bay, the 28L/28R final from about 15 nm out; 0.30
// by 0.45 degrees, 0.135 square degrees, one credit per poll.
struct AdsbBox {
    double lat_min = 37.45;
    double lat_max = 37.75;
    double lon_min = -122.55;
    double lon_max = -122.10;
};

// The point the selection measures distance from: KSFO's published
// coordinates. The nearest descending airborne aircraft to it is the one
// on final.
struct AdsbReference {
    double latitude = 37.6213;
    double longitude = -122.3790;
};

struct AdsbConfig {
    AdsbBox box;
    AdsbReference reference;

    // How often the service is polled. 5 s is the service's own time
    // resolution for a registered account; one credit per poll for this
    // box, 720 an hour against 4,000 a day. 1,000 to 3,600,000 ms.
    uint64_t poll_interval_ms = 5000;

    // A candidate replaces the kept aircraft only when it is nearer the
    // reference point by more than this, so two parallel arrivals do not
    // alternate on the display (REQ-SENS-009). At least 0.
    double switch_margin_m = 2000.0;

    // The geometric altitude (the vector's own datum, WGS-84 HAE, the
    // field the frame presents as gps_altitude_m) above which an aircraft
    // is not a candidate and the kept aircraft is dropped (REQ-SENS-009;
    // FTS-FM-001 FAULT-015). Ground distance cannot tell an overflight
    // from an approach: an arrival descending through 3,500 m 2.4 km from
    // the field was nearer than the aircraft on short final and displaced
    // it for 36 s (FTS-VR-001 Entry 18 §18.5). Height can: inside the box
    // every aircraft on an approach to this field is below the 3° glide
    // path's height at the box's far corner, 31.1 km from the reference
    // point, 1,629 m above the field, which sits 23 m below the
    // ellipsoid; rounded up to 1,650 m, so nothing on the approach inside
    // the box is excluded (a test pins the derivation to the shipped
    // box). At least 0.
    double ceiling_m = 1650.0;

    // Responses without the kept aircraft before it counts as gone: a
    // receiver's coverage flickers, and a failed poll is not a response.
    // At least 1.
    int gone_after_polls = 2;

    // The GPS channel's communication timeout in this mode (REQ-FAULT-001,
    // FTS-FM-001 FAULT-003), which main hands to the detector in place of
    // fault.uart_timeout_ms: the channel's delivery period is the poll
    // interval here, so this must exceed it (the loader refuses otherwise).
    // Four polls as shipped.
    uint64_t timeout_ms = 20000;
};
