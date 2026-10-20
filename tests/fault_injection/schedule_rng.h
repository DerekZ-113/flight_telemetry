#pragma once

#include <cstdint>
#include <random>

// The campaign's own random draws (REQ-TEST-005, FTS-DD-001 Section 14).
//
// std::mt19937 and std::seed_seq are fixed by the C++ standard, word for
// word; std::uniform_int_distribution and its kin are not, so libc++ and
// libstdc++ turn the same engine into different numbers. Every draw here
// is the harness's own arithmetic on the engine's 32-bit words, so one
// seed gives one schedule on every standard library. (The simulator's
// noise still comes from std::normal_distribution, which is why a run's
// noise is the platform's: FTS-DD-001 Section 8.)
class ScheduleRng {
public:
    // The engine seeded through std::seed_seq{a, b}: two words, so a seed
    // and a stream tag give independent sequences.
    ScheduleRng(uint32_t a, uint32_t b);

    // A given engine, for the test that checks the arithmetic against the
    // standard's fixed sequence.
    explicit ScheduleRng(const std::mt19937& engine);

    uint32_t next();

    // Uniform over [0, n), n at least 1: words below 2^32 mod n are
    // rejected so every value is equally likely, then taken modulo n.
    uint32_t below(uint32_t n);

    // Uniform over the integers low to high, both included.
    int64_t between(int64_t low, int64_t high);

    // Uniform over [low, high): the word as a fraction of 2^32.
    double uniform(double low, double high);

    bool coin();

private:
    std::mt19937 engine_;
};
