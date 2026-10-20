#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "data_source.h"
#include "processing/fault_detector.h"   // Channel enum

// A DataSource that wraps another DataSource and corrupts its frames on
// the way out (FTS-TP-001 section 2.3: faults are injected at the
// DataSource boundary).
//
// The pipeline sees a DataSource and nothing else, so it cannot tell an
// injected fault from a real one. That is the point: the same detector,
// processor, and tests run against a wire being pulled and against this
// class, and the only difference is which source the factory built.
//
// Minimal on purpose. Three channel modes cover the fault model's three
// detection strategies; the field modes below (2026-10-18) let the
// fault-injection campaign hold, replace or offset one raw field, for the
// faults a single axis can have and the faults no check covers
// (REQ-TEST-005). Schedules and randomized sweeps live in the campaign
// (tests/fault_injection/), which switches the modes on and off at the
// frames it drew. Nothing here draws a random number, so a run and its
// clean twin read the same simulated values.
// The wrapped source's sequence number passes through untouched (a fault
// is in a channel, not in the stream, REQ-LOG-008), and so does what the
// frame states of itself (NEW_VEHICLE); a held or failed channel keeps its
// whole reading, the presence bits of its optional fields included.
// One raw float a field mode alters: the barometer's two, the IMU's six,
// the GPS altitude. The position is a pair of doubles, held whole by
// hold_values(Channel::GPS).
enum class RawField : uint8_t {
    PRESSURE, TEMPERATURE,
    ACCEL_X, ACCEL_Y, ACCEL_Z,
    GYRO_X, GYRO_Y, GYRO_Z,
    GPS_ALTITUDE,
};
constexpr std::size_t kRawFieldCount = 9;

class FaultInjectingSource : public DataSource {
public:
    // Takes ownership of `inner`. The caller passes std::move(ptr) and is
    // left holding null; this object deletes the inner source when it is
    // itself destroyed.
    explicit FaultInjectingSource(std::unique_ptr<DataSource> inner);

    TelemetryFrame read_frame() override;

    // The inner source knows whether a next frame exists (REQ-LOG-006).
    // The forward goes to the inner source, another object, never to the
    // base: cppcheck 2.13 (CI's package) reads a same-named call as a call
    // to the base and reports a useless override, which 2.21 does not
    // (FTS-VR-001 Entry 16 §16.7). Suppressed by name so the release gate
    // cannot trip on it.
    // cppcheck-suppress uselessOverride
    bool has_next() override;

    // Communication failure: read_ok = false, fields held at the last
    // good value, as a driver that got no reply would leave them.
    // Channel::VOTER names the voter's events, not a sensor, so it is
    // accepted and does nothing here and in hold_values: the cost of one
    // enumeration serving both kinds of event (design.md section 13).
    void fail_reads(Channel channel, bool enable);

    // Stuck sensor: read_ok = true, fields repeat the last good value.
    void hold_values(Channel channel, bool enable);

    // Out-of-range: replace pressure with this value on every frame. The
    // field override of RawField::PRESSURE, so either clear clears it.
    void override_pressure(float hpa);
    void clear_pressure_override();

    // One field stuck: it repeats the value it had on the frame before the
    // hold began, while the rest of its channel and its read flag pass
    // through: a single stuck axis, which hold_values cannot express
    // (FAULT-006, FAULT-007). Enabling a hold already on keeps its value.
    void hold_field(RawField field, bool enable);

    // One field replaced by a constant on every frame until cleared: an
    // out-of-range temperature or acceleration, a NaN or an infinity
    // (FAULT-008 to FAULT-010, FAULT-024). Applied last, so it wins over a
    // hold or a bias of the same field.
    void override_field(RawField field, float value);
    void clear_override(RawField field);

    // One field offset by offset + rate x (t - t0), t0 the first frame the
    // bias applies to, in seconds: a bias step (rate 0) or a drift
    // (offset 0) that keeps every reading varying (FAULT-023).
    void bias_field(RawField field, float offset, float rate_per_s);
    void clear_bias(RawField field);

private:
    // static: copies between two frames and reads nothing from this
    // object, so it needs no `this`.
    static void copy_channel(Channel channel, const TelemetryFrame& from, TelemetryFrame& to);

    // The holds, biases and overrides, field by field, after the channel
    // modes: what a sensor's reading becomes, whatever its channel did.
    void apply_field_modes(TelemetryFrame& frame);

    struct FieldHold {
        bool enabled = false;
        bool captured = false;   // the held value has been taken
        float value = 0.0f;
    };
    struct FieldBias {
        bool enabled = false;
        bool started = false;    // its first frame has set start_ms
        uint64_t start_ms = 0;
        float offset = 0.0f;
        float rate_per_s = 0.0f;
    };

    std::unique_ptr<DataSource> inner_;
    TelemetryFrame last_good_{};
    bool have_last_good_ = false;
    bool fail_baro_ = false;
    bool fail_imu_ = false;
    bool fail_gps_ = false;
    bool hold_baro_ = false;
    bool hold_imu_ = false;
    bool hold_gps_ = false;
    std::array<FieldHold, kRawFieldCount> holds_{};
    std::array<FieldBias, kRawFieldCount> biases_{};
    std::array<std::optional<float>, kRawFieldCount> overrides_{};
    TelemetryFrame previous_{};   // the last frame handed out: what a new hold repeats
    bool have_previous_ = false;
};
