#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>

// The project's one thread (REQ-SENS-008; design.md section 10): a step
// that runs, waits the delay it returned, and runs again until told to
// stop. Written for the ADS-B source's poll, whose HTTP request takes
// hundreds of milliseconds against a 20 ms cycle, and generic because
// nothing in it is about HTTP.
//
// One mutex and one condition variable, whose only predicate is the stop.
// The wait is wait_for with that predicate, so a worker parked on an
// hour's delay (a 429's Retry-After) leaves in microseconds when stop()
// sets the flag and notifies, and a spurious wakeup goes back to sleep.
// The step runs with no lock held: the lock protects the flag and the
// count, never the step's work, so the owner's own lock (the ADS-B
// source's slot) is taken inside the step without this one. stop() joins,
// so when it returns no step is running and none will; the destructor
// calls it, so the owner cannot outlive the thread. Declared last in its
// owner, the worker is destroyed first, joined while everything the step
// touches is still alive (the declaration-order rule of
// live_sensor_source.h). Not copyable: a thread has one owner.
//
// The delays are wall-clock durations (std::chrono; the steady clock under
// wait_for), because the service's cadence is the wall's; the frame's
// timestamp and the token's expiry use the Clock seam, which a test can
// drive. The thread's own tests run on the real clock, in milliseconds.
class PeriodicWorker {
public:
    // Returns the delay before the next step.
    using Step = std::function<std::chrono::milliseconds()>;

    explicit PeriodicWorker(Step step);
    ~PeriodicWorker();
    PeriodicWorker(const PeriodicWorker&) = delete;
    PeriodicWorker& operator=(const PeriodicWorker&) = delete;

    // Starts the thread; the first step runs after `first_delay`, never
    // before. A second start while running is ignored.
    void start(std::chrono::milliseconds first_delay);
    // Stops the thread and joins it: no step runs after it returns.
    // Harmless before start() and when called twice.
    void stop();

    bool running() const;
    size_t steps() const;   // steps completed so far

private:
    void run(std::chrono::milliseconds first_delay);

    Step step_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_requested_ = false;
    bool running_ = false;
    size_t steps_ = 0;
    std::thread thread_;
};
