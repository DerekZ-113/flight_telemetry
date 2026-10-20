#include "schedule_rng.h"

ScheduleRng::ScheduleRng(uint32_t a, uint32_t b) {
    std::seed_seq seq{a, b};
    engine_.seed(seq);
}

ScheduleRng::ScheduleRng(const std::mt19937& engine)
    : engine_(engine)
{
}

uint32_t ScheduleRng::next() {
    return static_cast<uint32_t>(engine_());
}

uint32_t ScheduleRng::below(uint32_t n) {
    // (2^32 - n) mod n, computed in 32 bits: the count of words at the
    // bottom of the range that would make the low residues more likely.
    const uint32_t threshold = static_cast<uint32_t>(0u - n) % n;
    uint32_t word = next();
    while (word < threshold) {
        word = next();
    }
    return word % n;
}

int64_t ScheduleRng::between(int64_t low, int64_t high) {
    const uint32_t span = static_cast<uint32_t>(high - low + 1);
    return low + static_cast<int64_t>(below(span));
}

double ScheduleRng::uniform(double low, double high) {
    const double fraction = static_cast<double>(next()) / 4294967296.0;
    return low + (high - low) * fraction;
}

bool ScheduleRng::coin() {
    return (next() & 1u) != 0;
}
