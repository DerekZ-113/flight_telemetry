// The instrument panel's strip (REQ-DISP-001, REQ-DISP-004, REQ-DISP-006;
// TC-023 step 6): the mode label inside the panel, never LIVE for the
// simulator or a replay; the feed's state with its cause; PIPELINE only
// with a NOT_JUDGED field; the rate and the lag. The panel is rendered
// from a feed state built by hand, so nothing here waits on a socket.
import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import { Pfd } from "./Pfd";
import { frame, runningFeed, TEST001 } from "../testMessages";

afterEach(cleanup);

describe("the no-source flag (REQ-FAULT-007)", () => {
  it("flags the attitude indicator for an IMU the source does not provide, and keeps dashes for a DEGRADED one before any seed", () => {
    const absent = frame(0, { pitch_deg: "NO_COMPUTED_DATA", roll_deg: "NO_COMPUTED_DATA" });
    absent.status.imu = "NOT_AVAILABLE";
    absent.status.gps = "NOT_AVAILABLE";
    const { rerender, container } = render(<Pfd feed={runningFeed({ lastFrame: absent })} />);
    expect(container.querySelector('[data-testid="att-flag"]')?.getAttribute("data-cause")).toBe("unavailable");
    expect(container.querySelector('[data-testid="gs-flag"]')?.getAttribute("data-cause")).toBe("unavailable");
    expect(container.querySelector('[data-testid="trk-flag"]')?.getAttribute("data-cause")).toBe("unavailable");
    // No cause in the strip: nothing failed.
    expect(screen.getByTestId("pfd-state").textContent).toBe("RUNNING");

    const degraded = frame(0, { pitch_deg: "NO_COMPUTED_DATA", roll_deg: "NO_COMPUTED_DATA" });
    degraded.status.imu = "DEGRADED";
    rerender(<Pfd feed={runningFeed({ lastFrame: degraded })} />);
    expect(container.querySelector('[data-testid="att-flag"]')).toBeNull();
  });
});

describe("the panel's strip", () => {
  it("shows no mode before a status, boxes SIM and REPLAY, and never shows the simulator as LIVE", () => {
    const { rerender, container } = render(<Pfd feed={runningFeed({ session: null })} />);
    let mode = screen.getByTestId("pfd-mode");
    expect(mode.textContent).toBe("MODE UNKNOWN");
    expect(mode.dataset.mode).toBe("UNKNOWN");
    expect(mode.dataset.boxed).toBe("true");

    rerender(<Pfd feed={runningFeed({ session: { mode: "SIMULATED", state: "RUNNING", identity: null } })} />);
    mode = screen.getByTestId("pfd-mode");
    expect(mode.textContent).toBe("SIM");
    expect(mode.dataset.boxed).toBe("true");
    expect(container.textContent).not.toMatch(/LIVE/);

    rerender(<Pfd feed={runningFeed({ session: { mode: "REPLAY", state: "RUNNING", identity: null } })} />);
    mode = screen.getByTestId("pfd-mode");
    expect(mode.textContent).toBe("REPLAY \u00b7 ID UNAVAILABLE"); // a replay carries no identity (REQ-DISP-007)
    expect(mode.dataset.boxed).toBe("true");
    expect(container.textContent).not.toMatch(/LIVE/);

    rerender(<Pfd feed={runningFeed({ session: { mode: "LIVE", state: "RUNNING", identity: null } })} />);
    mode = screen.getByTestId("pfd-mode");
    expect(mode.textContent).toBe("LIVE");
    expect(mode.dataset.boxed).toBe("false");
  });

  it("names the feed's state and its cause", () => {
    const cases: Array<[Parameters<typeof runningFeed>[0], string, string]> = [
      [{ lastFrame: null, connection: "connecting", opened: false }, "CONNECTING", "none"],
      [{ lastFrame: null }, "WAITING", "none"],
      [{}, "RUNNING", "none"],
      [{ stale: true, ageMs: 1234 }, "STALE 1.2 s", "stale"],
      [{ ended: true }, "ENDED", "ended"],
      [{ lagExceeded: true, lagMs: 812 }, "LAG 0.8 s", "lag"],
      [{ connection: "closed" }, "BRIDGE DISCONNECTED", "link"],
      [{ connection: "closed", ended: true, stale: true, lagExceeded: true }, "BRIDGE DISCONNECTED", "link"],
    ];
    for (const [overrides, text, cause] of cases) {
      const { unmount } = render(<Pfd feed={runningFeed(overrides)} />);
      const state = screen.getByTestId("pfd-state");
      expect(state.textContent).toBe(text);
      expect(state.dataset.cause).toBe(cause);
      unmount();
    }
  });

  it("lights PIPELINE only while a field is NOT_JUDGED", () => {
    const { rerender } = render(<Pfd feed={runningFeed()} />);
    expect(screen.queryByTestId("pfd-pipeline")).toBeNull();
    rerender(<Pfd feed={runningFeed({ lastFrame: frame(0, { fused_altitude_m: "NOT_JUDGED" }) })} />);
    expect(screen.getByTestId("pfd-pipeline").textContent).toBe("PIPELINE");
  });

  it("labels the instruments by what they measure, never heading or airspeed (TC-023 step 7)", () => {
    const { container } = render(<Pfd feed={runningFeed()} />);
    expect(container.textContent).toContain("TRK");
    expect(container.textContent).toContain("GS");
    expect(container.textContent).not.toMatch(/HDG|heading|airspeed|IAS/i);
  });

  it("shows the rate and the lag", () => {
    const { rerender } = render(<Pfd feed={runningFeed({ rate: 50, lagMs: 12.4 })} />);
    expect(screen.getByTestId("pfd-rate").textContent).toBe("50 fps · lag 12 ms");
    rerender(<Pfd feed={runningFeed({ rate: 0, lagMs: null, lastFrame: null })} />);
    expect(screen.getByTestId("pfd-rate").textContent).toBe("0 fps · lag ---");
  });
});

// TC-030 steps 4 and 6 (REQ-DISP-007, REQ-DISP-004).
describe("the identity after the label", () => {
  it("shows the aircraft after ADS-B, plain and never LIVE, dashes with none, ID UNAVAILABLE after REPLAY, nothing after LIVE and SIM", () => {
    const { rerender, container } = render(
      <Pfd feed={runningFeed({ session: { mode: "ADSB", state: "RUNNING", identity: TEST001 } })} />,
    );
    let mode = screen.getByTestId("pfd-mode");
    expect(mode.textContent).toBe("ADS-B \u00b7 TEST001");
    expect(mode.dataset.mode).toBe("ADSB");
    expect(mode.dataset.boxed).toBe("false");
    expect(container.textContent).not.toMatch(/LIVE/);

    const nameless = { icao24: "a1b2c3", callsign: "" };
    rerender(<Pfd feed={runningFeed({ session: { mode: "ADSB", state: "RUNNING", identity: nameless } })} />);
    expect(screen.getByTestId("pfd-mode").textContent).toBe("ADS-B \u00b7 a1b2c3");
    rerender(<Pfd feed={runningFeed({ session: { mode: "ADSB", state: "RUNNING", identity: null } })} />);
    expect(screen.getByTestId("pfd-mode").textContent).toBe("ADS-B \u00b7 ---");

    rerender(<Pfd feed={runningFeed({ session: { mode: "REPLAY", state: "RUNNING", identity: null } })} />);
    mode = screen.getByTestId("pfd-mode");
    expect(mode.textContent).toBe("REPLAY \u00b7 ID UNAVAILABLE");
    expect(mode.dataset.boxed).toBe("true");
    rerender(<Pfd feed={runningFeed({ session: { mode: "LIVE", state: "RUNNING", identity: null } })} />);
    expect(screen.getByTestId("pfd-mode").textContent).toBe("LIVE");
    rerender(<Pfd feed={runningFeed()} />);
    expect(screen.getByTestId("pfd-mode").textContent).toBe("SIM");
  });

  it("labels the altitude tape ft HAE in ADS-B mode and ft otherwise", () => {
    const { rerender, container } = render(
      <Pfd feed={runningFeed({ session: { mode: "ADSB", state: "RUNNING", identity: TEST001 } })} />,
    );
    expect(container.querySelector('[data-testid="alt-unit"]')?.textContent).toBe("ALT ft HAE");
    rerender(<Pfd feed={runningFeed()} />);
    expect(container.querySelector('[data-testid="alt-unit"]')?.textContent).toBe("ALT ft");
    rerender(<Pfd feed={runningFeed({ session: { mode: "REPLAY", state: "RUNNING", identity: null } })} />);
    expect(container.querySelector('[data-testid="alt-unit"]')?.textContent).toBe("ALT ft");
  });
});
