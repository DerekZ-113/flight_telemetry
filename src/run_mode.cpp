#include "run_mode.h"

const char* mode_name(RunMode mode) {
    switch (mode) {
        case RunMode::UNKNOWN:   return "UNKNOWN";
        case RunMode::LIVE:      return "LIVE";
        case RunMode::SIMULATED: return "SIMULATED";
        case RunMode::REPLAY:    return "REPLAY";
        case RunMode::ADSB:      return "ADSB";
    }
    // A byte outside the enumeration: a corrupt header, never main, which
    // chooses the mode. Reachable from a file, so a test reaches it too.
    return "UNKNOWN";
}
