#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "drivers/serial_port.h"

// A port with no wire: read() serves queued chunks in order, one chunk
// per call, so a test decides exactly how a byte stream is cut across
// reads (a sentence split in two, two epochs in one read, a flood). The
// counterpart of FakeI2cBus for the GPS driver (TC-018).
class FakeSerialPort : public SerialPort {
public:
    struct Read {
        size_t requested = 0;
        std::optional<size_t> returned;
    };

    std::optional<size_t> read(uint8_t* buffer, size_t length) override;

    // Queue bytes for successive reads. A chunk shorter than the buffer is
    // delivered whole; a chunk longer than the buffer is delivered across
    // consecutive reads; an empty queue reads 0.
    void queue(const std::string& text);
    void queue_bytes(const std::vector<uint8_t>& bytes);

    // Nothing waiting at this point of the queue: the read that reaches it
    // returns 0 once, which ends a poll, and later reads serve what was
    // queued after it. A receiver between two epochs looks like this.
    void queue_pause();

    // An error at this point of the queue: the read that reaches it fails
    // once, with last_errno() set, and the reads after it serve the chunks
    // queued later. fail_every_read() fails every read until lifted.
    void queue_error(int errno_value = EIO);
    void fail_every_read(bool fail = true);

    int last_errno() const;
    bool empty() const;

    // Every read() attempted, in order, so a test can assert the chunk
    // size and the per-poll cap.
    const std::vector<Read>& reads() const;

private:
    struct Chunk {
        std::vector<uint8_t> bytes;
        int error = 0;   // nonzero: an error entry, no bytes
    };
    std::deque<Chunk> queue_;
    size_t offset_ = 0;   // into the front chunk
    bool failing_ = false;
    int last_errno_ = 0;
    std::vector<Read> reads_;
};
