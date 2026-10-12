#include "fake_i2c_bus.h"

#include <algorithm>

bool FakeI2cBus::write_register(uint8_t address, uint8_t reg, uint8_t value) {
    transactions_.push_back(Transaction{address, reg, true, value, 1});
    if (failing_writes_.count({address, reg}) != 0 ||
        failing_write_values_.count(std::make_tuple(address, reg, value)) != 0) {
        return false;
    }
    registers(address)[reg] = value;
    return true;
}

bool FakeI2cBus::read_registers(uint8_t address, uint8_t reg, uint8_t* buffer, size_t length) {
    transactions_.push_back(Transaction{address, reg, false, 0, length});
    if (failing_reads_.count({address, reg}) != 0) {
        return false;
    }
    // A burst past the end of the register file is a test bug, not a chip
    // behavior worth modelling: refuse it rather than wrap.
    if (static_cast<size_t>(reg) + length > 256) {
        return false;
    }
    const std::array<uint8_t, 256>& file = registers(address);
    std::copy_n(file.begin() + reg, length, buffer);
    return true;
}

std::array<uint8_t, 256>& FakeI2cBus::registers(uint8_t address) {
    // operator[] value-initializes a missing entry: all zeros.
    return devices_[address];
}

void FakeI2cBus::load(uint8_t address, uint8_t reg, const std::vector<uint8_t>& bytes) {
    std::array<uint8_t, 256>& file = registers(address);
    std::copy(bytes.begin(), bytes.end(), file.begin() + reg);
}

void FakeI2cBus::fail_reads_at(uint8_t address, uint8_t reg, bool fail) {
    if (fail) {
        failing_reads_.insert({address, reg});
    } else {
        failing_reads_.erase({address, reg});
    }
}

void FakeI2cBus::fail_writes_at(uint8_t address, uint8_t reg, bool fail) {
    if (fail) {
        failing_writes_.insert({address, reg});
    } else {
        failing_writes_.erase({address, reg});
    }
}

void FakeI2cBus::fail_write_of(uint8_t address, uint8_t reg, uint8_t value, bool fail) {
    if (fail) {
        failing_write_values_.insert(std::make_tuple(address, reg, value));
    } else {
        failing_write_values_.erase(std::make_tuple(address, reg, value));
    }
}

const std::vector<FakeI2cBus::Transaction>& FakeI2cBus::transactions() const {
    return transactions_;
}

void FakeI2cBus::clear_transactions() {
    transactions_.clear();
}
