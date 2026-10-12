#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "drivers/i2c_bus.h"

// A bus with no wires. Every address has a 256-byte register file, and a
// transaction is a copy to or from it. Driver tests load the bytes a chip
// would hold (a datasheet's worked example, a dump from the real chip) and
// check what the driver makes of them; the live-source test puts two chips
// on one fake, as the real bus has.
//
// Failure injection is per register, so a test can fail one transaction
// inside a sequence: the calibration burst after a good chip-ID read, or
// the second of three configuration writes. Every branch in a driver's
// init() is reachable that way.
//
// A device that was never loaded answers with zeros, which is what a dead
// or absent chip looks like through a chip-ID read; a refused transaction
// (fail_reads_at) is what a NACK looks like.
class FakeI2cBus : public I2cBus {
public:
    struct Transaction {
        uint8_t address;
        uint8_t reg;
        bool write;      // true: write_register; false: read_registers
        uint8_t value;   // the byte written; 0 for a read
        size_t length;   // bytes requested by a read; 1 for a write
    };

    bool write_register(uint8_t address, uint8_t reg, uint8_t value) override;
    bool read_registers(uint8_t address, uint8_t reg, uint8_t* buffer, size_t length) override;

    // The register file of one device, created all-zero on first use.
    std::array<uint8_t, 256>& registers(uint8_t address);

    // Load consecutive registers from `reg` on.
    void load(uint8_t address, uint8_t reg, const std::vector<uint8_t>& bytes);

    // Fail every read that starts at `reg`, or every write to it, on that
    // device. `fail` false lifts it again.
    void fail_reads_at(uint8_t address, uint8_t reg, bool fail = true);
    void fail_writes_at(uint8_t address, uint8_t reg, bool fail = true);

    // Fail only a write of exactly `value` to `reg`, so one write in a
    // sequence that touches the same register twice can fail alone.
    void fail_write_of(uint8_t address, uint8_t reg, uint8_t value, bool fail = true);

    // Every transaction attempted, in order, successful or not.
    const std::vector<Transaction>& transactions() const;
    void clear_transactions();

private:
    std::map<uint8_t, std::array<uint8_t, 256>> devices_;
    std::set<std::pair<uint8_t, uint8_t>> failing_reads_;
    std::set<std::pair<uint8_t, uint8_t>> failing_writes_;
    std::set<std::tuple<uint8_t, uint8_t, uint8_t>> failing_write_values_;
    std::vector<Transaction> transactions_;
};
