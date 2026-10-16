// The vertical speed indicator (REQ-DISP-001, REQ-DISP-002; TC-023 step 5,
// TC-024 step 6): feet per minute from the fused vertical speed, the
// pointer pinned at ±2,000 with the readout unclamped and rounded to 50;
// dashes, or the VS flag, in place of the value.
import { cleanup, render } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import type { InstrumentState } from "./instrumentState";
import { VerticalSpeed } from "./VerticalSpeed";

afterEach(cleanup);

function draw(verticalSpeedMps: number, state: InstrumentState) {
  return render(
    <svg>
      <VerticalSpeed verticalSpeedMps={verticalSpeedMps} state={state} />
    </svg>,
  ).container;
}

function pointerY(c: HTMLElement): number {
  return Number(c.querySelector('[data-testid="vs-pointer"]')?.getAttribute("y1"));
}

describe("the vertical speed indicator", () => {
  it("shows the vertical speed in feet per minute with the pointer and the readout", () => {
    let c = draw(3, { show: "value" });
    expect(c.querySelector('[data-testid="vs-readout"]')?.textContent).toBe("+600");
    expect(260 - pointerY(c)).toBeCloseTo(35.4, 1);
    expect(c.textContent).toContain("fpm");
    cleanup();
    c = draw(-2, { show: "value" });
    expect(c.querySelector('[data-testid="vs-readout"]')?.textContent).toBe("−400");
    expect(pointerY(c) - 260).toBeCloseTo(23.6, 1);
    cleanup();
    c = draw(15, { show: "value" });
    expect(c.querySelector('[data-testid="vs-readout"]')?.textContent).toBe("+2,950");
    expect(pointerY(c)).toBe(140);
  });

  it("shows dashes and no pointer while there is no value", () => {
    const c = draw(3, { show: "dashes" });
    expect(c.querySelector('[data-testid="vs-readout"]')?.textContent).toBe("---");
    expect(c.querySelector('[data-testid="vs-pointer"]')).toBeNull();
    expect(c.querySelector('[data-testid="vs-flag"]')).toBeNull();
  });

  it("removes the value under the VS flag", () => {
    for (const cause of ["fail", "stale", "ended"] as const) {
      const c = draw(3, { show: "flag", cause });
      expect(c.querySelector('[data-testid="vs-pointer"]')).toBeNull();
      expect(c.querySelector('[data-testid="vs-readout"]')).toBeNull();
      expect(c.textContent).not.toContain("600");
      expect(c.querySelector('[data-testid="vs-flag"]')?.textContent).toBe("VS");
      expect(c.querySelector('[data-testid="vs-flag"] rect')?.getAttribute("fill")).toBe(
        cause === "ended" ? "#5a6570" : "#d0342c",
      );
      cleanup();
    }
  });
});
