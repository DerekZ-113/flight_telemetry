// The feed hook's state machine (REQ-DISP-003, REQ-DISP-004, REQ-DISP-006;
// TC-022 steps 9, 11, 12 and 13 and TC-025 step 5 at the hook level):
// staleness from the hook's own clock, an ended stream never stale, the rate
// over the trailing second, the thresholds from hello, the reconnect after
// the backoff, the lag from elapsed times on each clock and its origins, the
// trail of fresh fixes. A fake socket and an injected clock stand in for the
// bridge and for time, so nothing waits.
import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { FakeSocket, fakeSocketFactory } from "./fakeSocket";
import { frame, hello, session, PERIOD_MS, STALE_MS, TEST001, TEST002 } from "./testMessages";
import { useTelemetryFeed } from "./useTelemetryFeed";

let clock = 0;
const now = () => clock;
const options = { socket: fakeSocketFactory, now, tickMs: 100, reconnectMs: 1000 };
const URL = "ws://127.0.0.1:8765";

function advance(ms: number): void {
  act(() => {
    clock += ms;
    vi.advanceTimersByTime(ms);
  });
}

function deliver(message: unknown): void {
  act(() => FakeSocket.latest().deliver(message));
}

function openWithHello(initial = hello()): void {
  act(() => FakeSocket.latest().open());
  deliver(initial);
}

beforeEach(() => {
  vi.useFakeTimers();
  clock = 0;
  FakeSocket.reset();
});

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

describe("useTelemetryFeed", () => {
  it("starts connecting with no session, no frame and the default threshold", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    expect(result.current.connection).toBe("connecting");
    expect(result.current.session).toBeNull();
    expect(result.current.lastFrame).toBeNull();
    expect(result.current.stale).toBe(false);
    expect(result.current.ended).toBe(false);
    expect(FakeSocket.latest().url).toBe(URL);
  });

  it("takes the threshold and the session, its source mode with it, from hello and from a status", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello(hello({ mode: "REPLAY", state: "RUNNING", identity: null, source_mode: "ADSB" }, 2500));
    expect(result.current.connection).toBe("open");
    expect(result.current.staleMs).toBe(2500);
    expect(result.current.session).toEqual({ mode: "REPLAY", state: "RUNNING", identity: null, source_mode: "ADSB" });
    deliver(session("REPLAY", "RUNNING", null, "SIMULATED"));
    expect(result.current.session).toEqual({ mode: "REPLAY", state: "RUNNING", identity: null, source_mode: "SIMULATED" });
  });

  it("marks the feed stale from its own clock and clears it on the next frame (step 11)", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello();
    deliver(frame(0));
    expect(result.current.stale).toBe(false);
    expect(result.current.ageMs).toBe(0);
    advance(STALE_MS);
    expect(result.current.stale).toBe(false); // at the threshold, not past it
    advance(100);
    expect(result.current.stale).toBe(true);
    expect(result.current.ageMs).toBe(STALE_MS + 100);
    expect(result.current.rate).toBe(0);
    deliver(frame(1));
    expect(result.current.stale).toBe(false);
    expect(result.current.ageMs).toBe(0);
  });

  it("never marks an ended stream stale (step 12)", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello();
    deliver(frame(0));
    deliver(session("REPLAY", "ENDED"));
    expect(result.current.ended).toBe(true);
    advance(5 * STALE_MS);
    expect(result.current.stale).toBe(false);
    expect(result.current.ended).toBe(true);
    // A new run after the end is a running stream again, judged as before.
    deliver(session("REPLAY", "RUNNING"));
    expect(result.current.ended).toBe(false);
    advance(STALE_MS + 100);
    expect(result.current.stale).toBe(true);
  });

  it("counts the rate over the trailing second", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello();
    for (let i = 0; i < 50; i++) {
      deliver(frame(i));
      advance(PERIOD_MS);
    }
    expect(result.current.frameCount).toBe(50);
    expect(result.current.rate).toBe(50);
    advance(500);
    expect(result.current.rate).toBe(25);
    advance(600);
    expect(result.current.rate).toBe(0);
  });

  it("reports a closed socket and reconnects after the backoff (step 13)", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello();
    expect(FakeSocket.instances).toHaveLength(1);
    act(() => FakeSocket.latest().closeFromServer());
    expect(result.current.connection).toBe("closed");
    advance(999);
    expect(FakeSocket.instances).toHaveLength(1);
    advance(1);
    expect(FakeSocket.instances).toHaveLength(2);
    expect(result.current.connection).toBe("connecting");
    openWithHello();
    expect(result.current.connection).toBe("open");
  });

  it("closes its socket and stops its timers when unmounted", () => {
    const { unmount } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello();
    unmount();
    expect(FakeSocket.latest().closed).toBe(true);
    act(() => FakeSocket.latest().closeFromServer());
    advance(2000);
    expect(FakeSocket.instances).toHaveLength(1); // no reconnect after unmount
  });

  it("ignores a message that is not one of the bridge's", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello();
    deliver("not json");
    deliver("null");
    deliver("42");
    deliver({ type: "something-else" });
    expect(result.current.lastFrame).toBeNull();
    expect(result.current.frameCount).toBe(0);
  });

  // --- TC-022 step 18: a value that is not a finite number arrives as null (REQ-DISP-005) ------

  it("keeps a frame whose value arrived as null, and refuses the same text with a bare NaN", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello();
    const degraded = frame(1, {}, { pressure_hpa: null });
    degraded.status.baro = "DEGRADED";
    const sent = JSON.stringify(degraded);
    const bare = sent.replace('"pressure_hpa":null', '"pressure_hpa":NaN'); // what Python's default encoder wrote
    expect(bare).toContain("NaN");
    deliver(bare);
    expect(result.current.frameCount).toBe(0); // JSON.parse refuses it, and the frame is gone with it
    deliver(sent);
    expect(result.current.frameCount).toBe(1);
    expect(result.current.lastFrame?.frame.pressure_hpa).toBeNull();
    expect(result.current.lastFrame?.status.baro).toBe("DEGRADED");
  });

  it("adds no point to the trail for a fresh fix whose position arrived as null", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello();
    deliver({ ...frame(0), position: { east_m: 0, north_m: 0 } });
    deliver({ ...frame(50), position: { east_m: null, north_m: null } }); // a fix that was not a number
    deliver({ ...frame(100), position: { east_m: 2, north_m: 1 } });
    expect(result.current.trail).toEqual([
      { east_m: 0, north_m: 0 },
      { east_m: 2, north_m: 1 },
    ]);
  });

  // --- TC-025 step 5: the lag, its origins, the thresholds, the trail (REQ-DISP-006) ---------

  it("takes the lag and track thresholds from hello", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    expect(result.current.lagThresholdMs).toBe(500);
    expect(result.current.trackMinSpeedMps).toBe(1.0);
    openWithHello(hello(null, STALE_MS, { lag_ms: 250, track_min_speed_mps: 2.5 }));
    expect(result.current.lagThresholdMs).toBe(250);
    expect(result.current.trackMinSpeedMps).toBe(2.5);
  });

  it("measures the lag from elapsed times on each clock, never by subtracting one clock from the other", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello();
    expect(result.current.lagMs).toBeNull();
    expect(result.current.bridgeLagMs).toBeNull();
    deliver(frame(0)); // t=0, received_at 0, arriving at 0: the origin
    expect(result.current.lagMs).toBe(0);
    expect(result.current.bridgeLagMs).toBe(0);
    advance(PERIOD_MS);
    deliver(frame(1)); // t=20, received_at 20, arriving at 20: on time
    expect(result.current.lagMs).toBe(0);
    expect(result.current.bridgeLagMs).toBe(0);
    advance(380); // the clock at 400
    deliver({ ...frame(2), received_at_ms: 300 }); // t=40, held 260 ms in the bridge and 100 ms in the socket
    expect(result.current.lagMs).toBe(360);
    expect(result.current.bridgeLagMs).toBe(260);
    expect(result.current.lagExceeded).toBe(false); // 360 is under the shipped 500
    deliver(hello(null, STALE_MS, { lag_ms: 250 }));
    advance(PERIOD_MS);
    deliver({ ...frame(3), received_at_ms: 320 }); // t=60 at 420: the same lag, now past 250
    expect(result.current.lagMs).toBe(360);
    expect(result.current.lagExceeded).toBe(true);
    expect(result.current.lastFrame?.frame.fused_altitude_m).toBe(12.5); // the metres the message carried, untouched
  });

  it("re-takes the lag origin after a reconnect, after ENDED and after a backwards timestamp", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello();
    deliver(frame(0));
    advance(100);
    deliver(frame(1)); // t=20 at 100: 80 ms of lag against the first frame
    expect(result.current.lagMs).toBe(80);

    act(() => FakeSocket.latest().closeFromServer());
    advance(1000);
    openWithHello();
    advance(50); // the clock at 1150
    deliver(frame(2)); // t=40: a new origin, so no lag however long the gap
    expect(result.current.lagMs).toBe(0);

    deliver(session("REPLAY", "ENDED"));
    advance(100); // the clock at 1250
    deliver(frame(3)); // t=60: a frame after an end is a new session
    expect(result.current.lagMs).toBe(0);
    advance(PERIOD_MS);
    deliver(frame(4)); // t=80 at 1270: on time against the new origin
    expect(result.current.lagMs).toBe(0);

    advance(PERIOD_MS); // the clock at 1290
    deliver(frame(1)); // t=20 after t=80: a restarted binary
    expect(result.current.lagMs).toBe(0);
  });

  it("keeps a trail of fresh fixes, capped, and clears it with the origin", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello();
    for (let i = 0; i <= 100; i++) {
      deliver({ ...frame(i), position: { east_m: i, north_m: 0 } }); // fresh on 0, 50 and 100 only
    }
    expect(result.current.trail).toEqual([
      { east_m: 0, north_m: 0 },
      { east_m: 50, north_m: 0 },
      { east_m: 100, north_m: 0 },
    ]);
    deliver({ ...frame(150), position: null }); // fresh, but the bridge has no origin: nothing to add
    expect(result.current.trail).toHaveLength(3);
    for (let i = 0; i < 3700; i++) {
      deliver({ ...frame(200 + i, {}, { gps_read_ok: true }), position: { east_m: 200 + i, north_m: 1 } });
    }
    expect(result.current.trail).toHaveLength(3600);
    expect(result.current.trail[3599]).toEqual({ east_m: 200 + 3699, north_m: 1 });
    deliver(session("REPLAY", "ENDED"));
    expect(result.current.trail).toHaveLength(3600); // kept until a new session's first frame
    deliver({ ...frame(0), position: { east_m: 0, north_m: 0 } });
    expect(result.current.trail).toEqual([{ east_m: 0, north_m: 0 }]);
  });
  // --- TC-030 step 5: the trail and the identity (REQ-DISP-007) ------------------------------

  it("clears the trail when the identity changes and keeps it when a status repeats it", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello(hello({ mode: "ADSB", state: "RUNNING", identity: TEST001, source_mode: "ADSB" }));
    expect(result.current.session?.identity).toEqual(TEST001);
    for (let i = 0; i <= 50; i++) {
      deliver({ ...frame(i), position: { east_m: i, north_m: 0 } }); // fresh on 0 and 50
    }
    expect(result.current.trail).toHaveLength(2);
    deliver(session("ADSB", "RUNNING", TEST001)); // the same aircraft again: kept
    expect(result.current.trail).toHaveLength(2);
    deliver(session("ADSB", "RUNNING", TEST002)); // a new aircraft: a new track, before its first frame
    expect(result.current.trail).toEqual([]);
    expect(result.current.session?.identity).toEqual(TEST002);
    deliver({ ...frame(100), position: { east_m: 0, north_m: 0 } });
    expect(result.current.trail).toEqual([{ east_m: 0, north_m: 0 }]);
    deliver(session("ADSB", "RUNNING", null)); // nothing selected is a change too
    expect(result.current.trail).toEqual([]);
    expect(result.current.session?.identity).toBeNull();
  });
});
