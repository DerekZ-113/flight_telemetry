// The altitude tape (REQ-DISP-001, REQ-DISP-002; TC-023 step 5, TC-024
// step 5): feet from the fused altitude's metres, ticks around the value,
// the readout to ten feet; dashes, or the ALT flag, in place of the value.
import { cleanup, render } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import { AltitudeTape } from "./AltitudeTape";
import type { InstrumentState } from "./instrumentState";

afterEach(cleanup);

function draw(altitudeM: number, state: InstrumentState) {
  return render(
    <svg>
      <AltitudeTape altitudeM={altitudeM} state={state} />
    </svg>,
  ).container;
}

describe("the altitude tape", () => {
  it("shows the fused altitude in feet, ticks around it and the readout to ten feet", () => {
    const c = draw(121.92, { show: "value" });
    expect(c.querySelector('[data-testid="alt-readout"]')?.textContent).toBe("400");
    expect(c.querySelector('text[data-label="400"]')?.getAttribute("y")).toBe("260");
    expect(Number(c.querySelector('text[data-label="300"]')?.getAttribute("y"))).toBeCloseTo(300, 6);
    expect(Number(c.querySelector('text[data-label="500"]')?.getAttribute("y"))).toBeCloseTo(220, 6);
    expect(c.querySelectorAll('[data-testid="altitude-tape"] line[data-ft]')).toHaveLength(41);
    expect(c.querySelector('[data-testid="alt-flag"]')).toBeNull();
    expect(c.textContent).toContain("ft");
    cleanup();
    expect(draw(2000, { show: "value" }).querySelector('[data-testid="alt-readout"]')?.textContent).toBe("6,560");
  });

  it("shows dashes and no ticks while there is no value", () => {
    for (const state of [{ show: "dashes" }, { show: "dashes", pipeline: true }] as const) {
      const c = draw(121.92, state);
      expect(c.querySelector('[data-testid="alt-readout"]')?.textContent).toBe("---");
      expect(c.querySelectorAll('[data-testid="altitude-tape"] line[data-ft]')).toHaveLength(0);
      expect(c.querySelector('[data-testid="alt-flag"]')).toBeNull();
      cleanup();
    }
  });

  it("removes the value under the ALT flag, red or grey by the cause", () => {
    for (const cause of ["fail", "stale", "lag", "link", "ended"] as const) {
      const c = draw(121.92, { show: "flag", cause });
      expect(c.querySelectorAll('[data-testid="altitude-tape"] line[data-ft]')).toHaveLength(0);
      expect(c.querySelector('[data-testid="alt-readout"]')).toBeNull();
      expect(c.textContent).not.toContain("400");
      const flag = c.querySelector('[data-testid="alt-flag"]');
      expect(flag?.textContent).toBe("ALT");
      expect(flag?.querySelector("rect")?.getAttribute("fill")).toBe(cause === "ended" ? "#5a6570" : "#d0342c");
      expect(c.innerHTML).not.toMatch(/line-through|opacity/);
      cleanup();
    }
  });
  it("names the datum the panel gives it, ft unless told otherwise (REQ-DISP-007)", () => {
    expect(draw(0, { show: "value" }).querySelector('[data-testid="alt-unit"]')?.textContent).toBe("ALT ft");
    cleanup();
    const hae = render(
      <svg>
        <AltitudeTape altitudeM={0} state={{ show: "value" }} unit="ft HAE" />
      </svg>,
    ).container;
    expect(hae.querySelector('[data-testid="alt-unit"]')?.textContent).toBe("ALT ft HAE");
  });
});
