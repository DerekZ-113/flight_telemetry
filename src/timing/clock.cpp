#include "timing/clock.h"

#include <cerrno>
#include <ctime>

namespace {

constexpr uint64_t kNsPerSecond = 1'000'000'000ull;

timespec to_timespec(uint64_t ns) {
    timespec ts;
    ts.tv_sec = static_cast<time_t>(ns / kNsPerSecond);
    ts.tv_nsec = static_cast<long>(ns % kNsPerSecond);
    return ts;
}

}  // namespace

uint64_t MonotonicClock::now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * kNsPerSecond + static_cast<uint64_t>(ts.tv_nsec);
}

void MonotonicClock::sleep_until_ns(uint64_t target_ns) {
    // The only platform split in the codebase. Everything outside this
    // function is identical on both platforms.
#if defined(__linux__)
    // Absolute sleep. A signal can interrupt it early (EINTR); the target
    // is absolute, so the same call is simply re-issued. A relative sleep
    // would have to recompute the remainder first.
    const timespec target = to_timespec(target_ns);
    int rc;
    do {
        rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, nullptr);
    } while (rc == EINTR);   // LCOV_EXCL_BR_LINE: signal delivery is not reproducible in a unit test
#else
    // Fallback: relative sleeps toward an absolute target. Re-reading the
    // clock each pass makes an early wake (signal, short sleep) harmless.
    for (;;) {
        const uint64_t now = now_ns();
        if (now >= target_ns) {
            return;
        }
        const timespec remaining = to_timespec(target_ns - now);
        nanosleep(&remaining, nullptr);
    }
#endif
}
