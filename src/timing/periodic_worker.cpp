#include "timing/periodic_worker.h"

#include <utility>

PeriodicWorker::PeriodicWorker(Step step) : step_(std::move(step)) {}

PeriodicWorker::~PeriodicWorker() {
    stop();
}

void PeriodicWorker::start(std::chrono::milliseconds first_delay) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return;
    }
    stop_requested_ = false;
    running_ = true;
    // The thread blocks on mutex_ until this function returns and releases
    // it, so its first wait begins after start() is complete.
    thread_ = std::thread(&PeriodicWorker::run, this, first_delay);
}

void PeriodicWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        stop_requested_ = true;
    }
    // Notified after the lock is released, so the waiter wakes into a free
    // mutex; joined without the lock, since the thread needs it to leave.
    wake_.notify_all();
    thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
}

bool PeriodicWorker::running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

size_t PeriodicWorker::steps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return steps_;
}

void PeriodicWorker::run(std::chrono::milliseconds delay) {
    std::unique_lock<std::mutex> lock(mutex_);
    // wait_for with a predicate returns false when the delay elapsed with
    // no stop requested (run the step) and true the moment stop() set the
    // flag; a spurious wakeup re-checks the predicate and keeps waiting.
    while (!wake_.wait_for(lock, delay, [this] { return stop_requested_; })) {  // LCOV_EXCL_EXCEPTION_BR_LINE: exception-unwind edges only; the decision itself is still counted
        lock.unlock();
        delay = step_();
        lock.lock();
        steps_++;
    }
}
