// The page (REQ-DISP-003, REQ-DISP-004, REQ-PROC-007; TC-022 steps 8 to 14):
// no mode until a status arrives and the simulator never LIVE, each validity
// rendered by name, the feed marked stale from arrival time and recovering,
// an ended stream shown as ended, a lost bridge reported, no coordinate
// anywhere.
import { act, cleanup, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { App } from "./App";
import { FakeSocket, fakeSocketFactory } from "./fakeSocket";
import { frame, hello, session, PERIOD_MS, STALE_MS } from "./testMessages";

let clock = 0;
const now = () => clock;
const options = { socket: fakeSocketFactory, now, tickMs: 100, reconnectMs: 1000 };

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

function renderPage(): void {
  render(<App url="ws://127.0.0.1:8765" options={options} />);
}

function row(label: string): HTMLTableRowElement {
  const cell = screen.getByText(label);
  const tr = cell.closest("tr");
  if (tr === null) {
    throw new Error(`${label} is not in a table row`);
  }
  return tr as HTMLTableRowElement;
}

function valueOf(label: string): string {
  return row(label).querySelector(".value")?.textContent ?? "";
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

describe("the page", () => {
  it("shows no mode and waits before any message (step 8)", () => {
    renderPage();
    expect(screen.getByText("MODE UNKNOWN")).toBeTruthy();
    expect(screen.getByText("CONNECTING")).toBeTruthy();
    openWithHello();
    expect(screen.getByText("MODE UNKNOWN")).toBeTruthy();
    expect(screen.getByText("WAITING")).toBeTruthy();
    expect(valueOf("Fused altitude (m)")).toBe("---");
  });

  it("labels SIM and REPLAY as themselves and never as LIVE (step 9)", () => {
    renderPage();
    openWithHello();
    deliver(session("SIMULATED"));
    expect(screen.getByText("SIM")).toBeTruthy();
    expect(screen.queryByText(/LIVE/)).toBeNull();
    deliver(session("REPLAY"));
    expect(screen.getByText("REPLAY")).toBeTruthy();
    expect(screen.queryByText(/LIVE/)).toBeNull();
    deliver(session("LIVE"));
    expect(screen.getByText("LIVE")).toBeTruthy();
  });

  it("knows the mode from hello when it joins mid-run (step 9)", () => {
    renderPage();
    openWithHello(hello({ mode: "SIMULATED", state: "RUNNING" }));
    expect(screen.getByText("SIM")).toBeTruthy();
    expect(screen.queryByText("MODE UNKNOWN")).toBeNull();
  });

  it("renders each validity value by name, distinguishable by text and class (step 10)", () => {
    renderPage();
    openWithHello();
    deliver(
      frame(0, {
        baro_altitude_m: "NORMAL",
        pitch_deg: "FAILURE_WARNING",
        roll_deg: "NO_COMPUTED_DATA",
        fused_altitude_m: "NOT_JUDGED",
        vertical_speed_mps: "NORMAL",
      }),
    );
    const baro = row("Baro altitude (m)");
    expect(baro.dataset.validity).toBe("NORMAL");
    expect(baro.classList.contains("validity-NORMAL")).toBe(true);
    expect(valueOf("Baro altitude (m)")).toBe("0.00");
    expect(baro.textContent).toContain("NORMAL");

    const pitch = row("Pitch (deg)");
    expect(pitch.dataset.validity).toBe("FAILURE_WARNING");
    expect(pitch.classList.contains("validity-FAILURE_WARNING")).toBe(true);
    expect(valueOf("Pitch (deg)")).toBe("1.50"); // the value stays, flagged
    expect(pitch.textContent).toContain("FAILURE_WARNING");

    const roll = row("Roll (deg)");
    expect(roll.dataset.validity).toBe("NO_COMPUTED_DATA");
    expect(valueOf("Roll (deg)")).toBe("---");
    expect(roll.textContent).toContain("NO_COMPUTED_DATA");
    expect(roll.querySelector(".annunciator")).toBeNull();

    const fused = row("Fused altitude (m)");
    expect(fused.dataset.validity).toBe("NOT_JUDGED");
    expect(valueOf("Fused altitude (m)")).toBe("---");
    expect(fused.textContent).toContain("NOT_JUDGED");
    expect(fused.querySelector(".annunciator")?.textContent).toBe("PIPELINE");

    expect(valueOf("Vertical speed (m/s)")).toBe("0.25");
  });

  it("counts the rate and shows the last frame (step 10)", () => {
    renderPage();
    openWithHello();
    for (let i = 0; i < 50; i++) {
      deliver(frame(i));
      advance(PERIOD_MS);
    }
    expect(screen.getByText("RUNNING")).toBeTruthy();
    expect(screen.getByTestId("rate").textContent).toBe("50");
    expect(screen.getByTestId("frames").textContent).toBe("50");
    expect(screen.getByTestId("last-frame").textContent).toBe("t=980 ms");
    expect(screen.getByTestId("status-baro").textContent).toContain("NOMINAL");
    expect(screen.getByTestId("status-gps").textContent).toContain("held"); // fresh on frame 0 only
  });

  it("marks the feed stale from its own clock and recovers on the next frame (step 11)", () => {
    renderPage();
    openWithHello();
    deliver(frame(0));
    advance(STALE_MS + 100);
    expect(screen.getByText("STALE")).toBeTruthy();
    expect(screen.getByTestId("age").textContent).toBe("last frame 1.1 s ago");
    expect(screen.getByTestId("rate").textContent).toBe("0");
    for (const label of ["Baro altitude (m)", "Pitch (deg)", "Roll (deg)", "Fused altitude (m)", "Vertical speed (m/s)"]) {
      expect(row(label).classList.contains("stale")).toBe(true);
    }
    expect(valueOf("Fused altitude (m)")).toBe("12.50"); // the last number stays legible under the mark
    deliver(frame(1));
    expect(screen.queryByText("STALE")).toBeNull();
    expect(screen.getByText("RUNNING")).toBeTruthy();
    expect(row("Fused altitude (m)").classList.contains("stale")).toBe(false);
  });

  it("shows an ended stream as ended, never stale (step 12)", () => {
    renderPage();
    openWithHello();
    deliver(frame(3));
    deliver(session("REPLAY", "ENDED"));
    expect(screen.getByText("ENDED")).toBeTruthy();
    expect(screen.getByTestId("last-frame").textContent).toBe("t=60 ms");
    advance(5 * STALE_MS);
    expect(screen.getByText("ENDED")).toBeTruthy();
    expect(screen.queryByText("STALE")).toBeNull();
  });

  it("reports a lost bridge and reconnects after the backoff (step 13)", () => {
    renderPage();
    openWithHello();
    act(() => FakeSocket.latest().closeFromServer());
    expect(screen.getByText("BRIDGE DISCONNECTED")).toBeTruthy();
    advance(1000);
    expect(FakeSocket.instances).toHaveLength(2);
    openWithHello();
    expect(screen.queryByText("BRIDGE DISCONNECTED")).toBeNull();
    expect(screen.getByText("WAITING")).toBeTruthy();
  });

  it("never renders a coordinate (step 14)", () => {
    renderPage();
    openWithHello(hello({ mode: "LIVE", state: "RUNNING" }));
    deliver(frame(7));
    const text = document.body.textContent ?? "";
    expect(text).not.toMatch(/latitude|longitude/i);
    expect(text).not.toMatch(/\d{2}\.\d{4,}/); // no decimal-degree-looking number anywhere
  });
});
