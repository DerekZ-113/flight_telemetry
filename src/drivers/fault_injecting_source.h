#pragma once

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
// Minimal on purpose. Three injection modes cover the fault model's three
// detection strategies. Scripted schedules and randomized sweeps come with
// the Sprint 2 harness.
// The wrapped source's sequence number passes through untouched (a fault
// is in a channel, not in the stream, REQ-LOG-008), and so does what the
// frame states of itself (NEW_VEHICLE); a held or failed channel keeps its
// whole reading, the presence bits of its optional fields included.
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
    void fail_reads(Channel channel, bool enable);

    // Stuck sensor: read_ok = true, fields repeat the last good value.
    void hold_values(Channel channel, bool enable);

    // Out-of-range: replace pressure with this value on every frame.
    void override_pressure(float hpa);
    void clear_pressure_override();

private:
    // static: copies between two frames and reads nothing from this
    // object, so it needs no `this`.
    static void copy_channel(Channel channel, const TelemetryFrame& from, TelemetryFrame& to);

    std::unique_ptr<DataSource> inner_;
    TelemetryFrame last_good_{};
    bool have_last_good_ = false;
    bool fail_baro_ = false;
    bool fail_imu_ = false;
    bool fail_gps_ = false;
    bool hold_baro_ = false;
    bool hold_imu_ = false;
    bool hold_gps_ = false;
    std::optional<float> pressure_override_;
};
