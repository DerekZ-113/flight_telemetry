// The ground speed readout (REQ-DISP-001, REQ-DISP-002; TC-023 steps 5
// and 7): knots from the GPS speed over ground, labelled GS and never
// airspeed; dashes, or the GS flag, in place of the value.
import { cleanup, render } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import { GroundSpeed } from "./GroundSpeed";
import type { InstrumentState } from "./instrumentState";

afterEach(cleanup);

function draw(groundSpeedMps: number, state: InstrumentState) {
  return render(
    <svg>
      <GroundSpeed groundSpeedMps={groundSpeedMps} state={state} />
    </svg>,
  ).container;
}

describe("the ground speed readout", () => {
  it("shows the GPS speed over ground in knots, labelled GS", () => {
    const c = draw(30, { show: "value" });
    expect(c.querySelector('[data-testid="gs-readout"]')?.textContent).toBe("58");
    expect(c.textContent).toContain("GS");
    expect(c.textContent).toContain("kt");
    expect(c.textContent).not.toMatch(/airspeed|IAS|TAS/i);
    expect(c.querySelector('[data-testid="gs-flag"]')).toBeNull();
    cleanup();
    expect(draw(0.3, { show: "value" }).querySelector('[data-testid="gs-readout"]')?.textContent).toBe("1");
  });

  it("shows dashes while there is no value", () => {
    const c = draw(30, { show: "dashes" });
    expect(c.querySelector('[data-testid="gs-readout"]')?.textContent).toBe("---");
    expect(c.querySelector('[data-testid="gs-flag"]')).toBeNull();
  });

  it("removes the value under the GS flag", () => {
    for (const cause of ["fail", "stale", "ended", "unavailable"] as const) {
      const c = draw(30, { show: "flag", cause });
      expect(c.querySelector('[data-testid="gs-readout"]')).toBeNull();
      expect(c.textContent).not.toContain("58");
      expect(c.querySelector('[data-testid="gs-flag"]')?.textContent).toBe("GS");
      const box = c.querySelector('[data-testid="gs-flag"] rect');
      // Red for a failure, grey for an end, and for a channel the source does not provide an
      // outline on no fill: nothing failed (REQ-DISP-001, FTS-DS-001 §2.1).
      expect(box?.getAttribute("fill")).toBe(cause === "ended" ? "#5a6570" : cause === "unavailable" ? "none" : "#d0342c");
      expect(box?.getAttribute("stroke")).toBe(cause === "unavailable" ? "#e8edf2" : "none");
      cleanup();
    }
  });
});
