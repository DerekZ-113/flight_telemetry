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
    for (const cause of ["fail", "stale", "ended"] as const) {
      const c = draw(30, { show: "flag", cause });
      expect(c.querySelector('[data-testid="gs-readout"]')).toBeNull();
      expect(c.textContent).not.toContain("58");
      expect(c.querySelector('[data-testid="gs-flag"]')?.textContent).toBe("GS");
      expect(c.querySelector('[data-testid="gs-flag"] rect')?.getAttribute("fill")).toBe(
        cause === "ended" ? "#5a6570" : "#d0342c",
      );
      cleanup();
    }
  });
});
