// The voter panel (REQ-DISP-008; TC-032 step 10, TC-033 step 8): the three
// lanes in the vote, an excluded one amber; the red VOTE flag under
// NO_MAJORITY with the lanes kept; dashes when no voter ran; a feed cause's
// flag in place of the lanes.
import { cleanup, render } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import { frame, runningFeed } from "../testMessages";
import { voterState } from "./instrumentState";
import type { VoterState } from "./instrumentState";
import { VoterPanel } from "./VoterPanel";

afterEach(cleanup);

function draw(state: VoterState) {
  return render(
    <svg>
      <VoterPanel state={state} />
    </svg>,
  ).container;
}

function withStatus(voterStatus: number): VoterState {
  return voterState(runningFeed({ lastFrame: frame(0, {}, { voter_status: voterStatus }) }));
}

function cell(c: HTMLElement, lane: number) {
  return c.querySelector(`[data-testid="voter-lane-${lane}"]`);
}

describe("the voter panel", () => {
  it("shows three white lane cells, 1 2 3, and no flag while all three vote", () => {
    const c = draw(withStatus(0x0001));
    for (const lane of [1, 2, 3]) {
      expect(cell(c, lane)?.getAttribute("data-in")).toBe("true");
      expect(cell(c, lane)?.querySelector("rect")?.getAttribute("fill")).toBe("none");
      expect(cell(c, lane)?.querySelector("rect")?.getAttribute("stroke")).toBe("#ffffff");
      expect(cell(c, lane)?.textContent).toBe(String(lane));
    }
    expect(c.querySelector('[data-testid="vote-flag"]')).toBeNull();
    expect(c.querySelector('[data-testid="voter-dashes"]')).toBeNull();
    expect(c.textContent).toContain("VOTE");
  });

  it("fills an excluded lane's cell amber, a caution, with no flag (TC-032 step 10)", () => {
    for (const [status, lane] of [
      [0x0003, 1],
      [0x0005, 2],
      [0x0009, 3],
    ] as const) {
      const c = draw(withStatus(status));
      for (const other of [1, 2, 3]) {
        const excluded = other === lane;
        expect(cell(c, other)?.getAttribute("data-in")).toBe(excluded ? "false" : "true");
        expect(cell(c, other)?.querySelector("rect")?.getAttribute("fill")).toBe(excluded ? "#f0a830" : "none");
      }
      expect(c.querySelector('[data-testid="vote-flag"]')).toBeNull();
      cleanup();
    }
  });

  it("raises the red VOTE flag under no majority and keeps the lanes (TC-033 step 8)", () => {
    const three = draw(withStatus(0x0011));
    const flag = three.querySelector('[data-testid="vote-flag"]');
    expect(flag?.textContent).toBe("VOTE");
    expect(flag?.getAttribute("data-cause")).toBe("fail");
    expect(flag?.querySelector("rect")?.getAttribute("fill")).toBe("#d0342c");
    expect([1, 2, 3].map((lane) => cell(three, lane)?.getAttribute("data-in"))).toEqual(["true", "true", "true"]);
    cleanup();
    const two = draw(withStatus(0x0015));
    expect(two.querySelector('[data-testid="vote-flag"]')?.getAttribute("data-cause")).toBe("fail");
    expect([1, 2, 3].map((lane) => cell(two, lane)?.getAttribute("data-in"))).toEqual(["true", "false", "true"]);
  });

  it("shows dashes and no flag when no voter ran, and before any frame", () => {
    for (const state of [withStatus(0), voterState(runningFeed({ lastFrame: null }))]) {
      const c = draw(state);
      expect(c.querySelector('[data-testid="voter-dashes"]')?.textContent).toBe("---");
      expect(c.querySelector('[data-testid="voter-lane-1"]')).toBeNull();
      expect(c.querySelector('[data-testid="vote-flag"]')).toBeNull();
      cleanup();
    }
  });

  it("removes the lanes under the VOTE flag with the feed's cause, red, or grey when the stream ended", () => {
    const causes = [
      ["stale", { stale: true, ageMs: 1500 }],
      ["ended", { ended: true }],
      ["lag", { lagExceeded: true, lagMs: 800 }],
      ["link", { connection: "closed" as const }],
    ] as const;
    for (const voterStatus of [0x0001, 0x0015]) {
      for (const [cause, overrides] of causes) {
        const c = draw(voterState(runningFeed({ ...overrides, lastFrame: frame(0, {}, { voter_status: voterStatus }) })));
        const flag = c.querySelector('[data-testid="vote-flag"]');
        expect(flag?.getAttribute("data-cause")).toBe(cause);
        expect(flag?.querySelector("rect")?.getAttribute("fill")).toBe(cause === "ended" ? "#5a6570" : "#d0342c");
        expect(c.querySelector('[data-testid="voter-lane-1"]')).toBeNull();
        expect(c.querySelector('[data-testid="voter-dashes"]')).toBeNull();
        cleanup();
      }
    }
  });
});
