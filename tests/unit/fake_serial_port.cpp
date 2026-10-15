#include "fake_serial_port.h"

#include <algorithm>

std::optional<size_t> FakeSerialPort::read(uint8_t* buffer, size_t length) {
    Read record{};
    record.requested = length;
    std::optional<size_t> result;
    if (failing_) {
        last_errno_ = EIO;
    } else if (queue_.empty()) {
        result = 0;
    } else if (queue_.front().error != 0) {
        last_errno_ = queue_.front().error;
        queue_.pop_front();
        offset_ = 0;
    } else {
        const Chunk& chunk = queue_.front();
        const size_t remaining = chunk.bytes.size() - offset_;
        const size_t count = std::min(length, remaining);
        std::copy_n(chunk.bytes.begin() + static_cast<std::ptrdiff_t>(offset_), count, buffer);
        offset_ += count;
        if (offset_ == chunk.bytes.size()) {
            queue_.pop_front();
            offset_ = 0;
        }
        result = count;
    }
    record.returned = result;
    reads_.push_back(record);
    return result;
}

void FakeSerialPort::queue(const std::string& text) {
    queue_bytes(std::vector<uint8_t>(text.begin(), text.end()));
}

void FakeSerialPort::queue_bytes(const std::vector<uint8_t>& bytes) {
    Chunk chunk{};
    chunk.bytes = bytes;
    queue_.push_back(chunk);
}

void FakeSerialPort::queue_pause() {
    queue_bytes({});
}

void FakeSerialPort::queue_error(int errno_value) {
    Chunk chunk{};
    chunk.error = errno_value;
    queue_.push_back(chunk);
}

void FakeSerialPort::fail_every_read(bool fail) {
    failing_ = fail;
}

int FakeSerialPort::last_errno() const {
    return last_errno_;
}

bool FakeSerialPort::empty() const {
    return queue_.empty();
}

const std::vector<FakeSerialPort::Read>& FakeSerialPort::reads() const {
    return reads_;
}
