// The page (REQ-DISP-003, REQ-DISP-004, REQ-PROC-007; TC-022 steps 8 to 14;
// TC-023 and TC-025 for the panel above the diagnostic view): no mode until a
// status arrives and the simulator never LIVE, each validity rendered by
// name, the feed marked stale from arrival time and recovering, an ended
// stream shown as ended, a lost bridge reported, no coordinate anywhere.
// The header bar and the panel's strip both carry the mode and the state, so
// the bar's words are read inside the bar.
import { act, cleanup, render, screen, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { App } from "./App";
import { FakeSocket, fakeSocketFactory } from "./fakeSocket";
import { frame, hello, session, PERIOD_MS, STALE_MS, TEST001 } from "./testMessages";

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

function renderPage() {
  return render(<App url="ws://127.0.0.1:8765" options={options} />);
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

function bar() {
  return within(screen.getByTestId("bar"));
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
    expect(bar().getByText("MODE UNKNOWN")).toBeTruthy();
    expect(bar().getByText("CONNECTING")).toBeTruthy();
    openWithHello();
    expect(bar().getByText("MODE UNKNOWN")).toBeTruthy();
    expect(bar().getByText("WAITING")).toBeTruthy();
    expect(valueOf("Fused altitude (m)")).toBe("---");
  });

  it("labels SIM and REPLAY as themselves and never as LIVE (step 9)", () => {
    renderPage();
    openWithHello();
    deliver(session("SIMULATED"));
    expect(bar().getByText("SIM")).toBeTruthy();
    expect(screen.queryByText(/LIVE/)).toBeNull();
    deliver(session("REPLAY", "RUNNING", null, "SIMULATED"));
    expect(bar().getByText("REPLAY")).toBeTruthy(); // a replayed simulator run has no identity to lose (REQ-DISP-007)
    expect(screen.queryByText(/LIVE/)).toBeNull();
    deliver(session("LIVE"));
    expect(bar().getByText("LIVE")).toBeTruthy();
  });

  it("knows the mode from hello when it joins mid-run (step 9)", () => {
    renderPage();
    openWithHello(hello({ mode: "SIMULATED", state: "RUNNING", identity: null, source_mode: "SIMULATED" }));
    expect(bar().getByText("SIM")).toBeTruthy();
    expect(screen.queryAllByText("MODE UNKNOWN")).toHaveLength(0);
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
    expect(bar().getByText("RUNNING")).toBeTruthy();
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
    expect(bar().getByText("STALE")).toBeTruthy();
    expect(screen.getByTestId("age").textContent).toBe("last frame 1.1 s ago");
    expect(screen.getByTestId("rate").textContent).toBe("0");
    for (const label of ["Baro altitude (m)", "Pitch (deg)", "Roll (deg)", "Fused altitude (m)", "Vertical speed (m/s)"]) {
      expect(row(label).classList.contains("stale")).toBe(true);
    }
    expect(valueOf("Fused altitude (m)")).toBe("12.50"); // the last number stays legible under the mark
    deliver(frame(1));
    expect(bar().queryByText("STALE")).toBeNull();
    expect(bar().getByText("RUNNING")).toBeTruthy();
    expect(row("Fused altitude (m)").classList.contains("stale")).toBe(false);
  });

  it("shows an ended stream as ended, never stale (step 12)", () => {
    renderPage();
    openWithHello();
    deliver(frame(3));
    deliver(session("REPLAY", "ENDED"));
    expect(bar().getByText("ENDED")).toBeTruthy();
    expect(screen.getByTestId("last-frame").textContent).toBe("t=60 ms");
    advance(5 * STALE_MS);
    expect(bar().getByText("ENDED")).toBeTruthy();
    expect(screen.queryAllByText(/STALE/)).toHaveLength(0);
  });

  it("reports a lost bridge and reconnects after the backoff (step 13)", () => {
    renderPage();
    openWithHello();
    act(() => FakeSocket.latest().closeFromServer());
    expect(bar().getByText("BRIDGE DISCONNECTED")).toBeTruthy();
    advance(1000);
    expect(FakeSocket.instances).toHaveLength(2);
    openWithHello();
    expect(screen.queryAllByText("BRIDGE DISCONNECTED")).toHaveLength(0);
    expect(bar().getByText("WAITING")).toBeTruthy();
  });

  it("never renders a coordinate (step 14)", () => {
    renderPage();
    openWithHello(hello({ mode: "LIVE", state: "RUNNING", identity: null, source_mode: "LIVE" }));
    deliver(frame(7));
    const text = document.body.textContent ?? "";
    expect(text).not.toMatch(/latitude|longitude/i);
    expect(text).not.toMatch(/\d{2}\.\d{4,}/); // no decimal-degree-looking number anywhere
  });

  // --- TC-023 and TC-025: the panel above the diagnostic view, the lag beside the counters ------

  it("renders the instrument panel above the diagnostic view", () => {
    const { container } = renderPage();
    const main = container.querySelector("main");
    const panel = screen.getByTestId("pfd");
    const table = container.querySelector("table.fields");
    expect(main).not.toBeNull();
    expect(table).not.toBeNull();
    const order = Array.from(main!.querySelectorAll("*"));
    expect(order.indexOf(panel)).toBeLessThan(order.indexOf(table!));
    expect(bar().getByText("MODE UNKNOWN")).toBeTruthy();
    expect(screen.getByTestId("pfd-mode").textContent).toBe("MODE UNKNOWN");
  });

  it("draws the ground track plot beside the panel from the bridge's positions", () => {
    renderPage();
    openWithHello();
    expect(screen.getByTestId("ground-track")).toBeTruthy();
    expect(screen.getByTestId("plot-nofix").textContent).toBe("no fix");
    deliver({ ...frame(0), position: { east_m: 0, north_m: 0 } });
    deliver({ ...frame(50, {}, { ground_speed_mps: 3, course_deg: 90 }), position: { east_m: 1, north_m: 0 } });
    expect(screen.getByTestId("plot-trail").getAttribute("points")).toBe("150,150 165,150");
    expect(screen.getByTestId("plot-marker").getAttribute("transform")).toBe("translate(165 150) rotate(90)");
    expect(screen.getByTestId("trk-readout").textContent).toBe("TRK 090°");
    expect(screen.getByTestId("gs-readout").textContent).toBe("6");
  });

  it("shows the lag and the bridge's share of it beside the counters", () => {
    renderPage();
    openWithHello();
    expect(screen.getByTestId("lag").textContent).toBe("---");
    deliver(frame(0));
    advance(400);
    deliver(frame(1)); // t=20, received_at 20, arriving 400 ms after the first: the queue is past the bridge
    expect(screen.getByTestId("lag").textContent).toBe("380 ms");
    expect(screen.getByTestId("bridge-lag").textContent).toBe("bridge 0 ms");
  });
  it("names the aircraft in the bar and attributes the traffic data whenever the source mode is ADS-B, live or replayed, and nowhere else (TC-030 steps 4 and 7)", () => {
    renderPage();
    openWithHello(hello({ mode: "ADSB", state: "RUNNING", identity: TEST001, source_mode: "ADSB" }));
    expect(bar().getByText("ADS-B \u00b7 TEST001")).toBeTruthy();
    expect(screen.getByTestId("attribution").textContent).toBe("Traffic data: The OpenSky Network (opensky-network.org)");
    expect(screen.queryByText(/^LIVE/)).toBeNull();
    // A replayed ADS-B recording shows the network's data as much as a live run does (TC-031 step 16).
    deliver(session("REPLAY", "RUNNING", null, "ADSB"));
    expect(bar().getByText("REPLAY \u00b7 ID UNAVAILABLE")).toBeTruthy();
    expect(screen.getByTestId("attribution")).toBeTruthy();
    deliver(session("REPLAY", "RUNNING", null, "SIMULATED"));
    expect(bar().getByText("REPLAY")).toBeTruthy();
    expect(screen.queryByTestId("attribution")).toBeNull();
    deliver(session("SIMULATED"));
    expect(bar().getByText("SIM")).toBeTruthy();
    expect(screen.queryByTestId("attribution")).toBeNull();
  });
});
