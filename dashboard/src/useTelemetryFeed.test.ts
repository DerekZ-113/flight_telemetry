// The feed hook's state machine (REQ-DISP-003, REQ-DISP-004; TC-022 steps
// 9, 11, 12 and 13 at the hook level): staleness from the hook's own clock,
// an ended stream never stale, the rate over the trailing second, the
// threshold from hello, the reconnect after the backoff. A fake socket and
// an injected clock stand in for the bridge and for time, so nothing waits.
import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { FakeSocket, fakeSocketFactory } from "./fakeSocket";
import { frame, hello, session, PERIOD_MS, STALE_MS } from "./testMessages";
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

  it("takes the threshold and the session from hello", () => {
    const { result } = renderHook(() => useTelemetryFeed(URL, options));
    openWithHello(hello({ mode: "REPLAY", state: "RUNNING" }, 2500));
    expect(result.current.connection).toBe("open");
    expect(result.current.staleMs).toBe(2500);
    expect(result.current.session).toEqual({ mode: "REPLAY", state: "RUNNING" });
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
});
