// The track indicator (REQ-DISP-001, REQ-DISP-002; TC-023 steps 5 and 7,
// TC-025 step 7): the GPS course over ground under a lubber line, labelled
// TRK and never HDG; withheld with dashes and no rose below the speed
// threshold; dashes, or the TRK flag, by the channel's status.
import { cleanup, render } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import type { InstrumentState } from "./instrumentState";
import { TrackIndicator } from "./TrackIndicator";

afterEach(cleanup);

function draw(trackDeg: number, shown: boolean, state: InstrumentState) {
  return render(
    <svg>
      <TrackIndicator trackDeg={trackDeg} shown={shown} state={state} />
    </svg>,
  ).container;
}

describe("the track indicator", () => {
  it("turns the rose so the track sits under the lubber line and reads TRK, never HDG", () => {
    const c = draw(90, true, { show: "value" });
    expect(c.querySelector('[data-testid="rose"]')?.getAttribute("transform")).toBe("rotate(-90 400 640)");
    expect(c.querySelector('[data-testid="trk-readout"]')?.textContent).toBe("TRK 090°");
    expect(c.querySelector('[data-testid="lubber"]')).not.toBeNull();
    expect(c.textContent).not.toMatch(/HDG|heading/i);
    expect(c.querySelector('[data-testid="trk-flag"]')).toBeNull();
    cleanup();
    expect(draw(87.4, true, { show: "value" }).querySelector('[data-testid="trk-readout"]')?.textContent).toBe("TRK 087°");
  });

  it("withholds the track below the speed threshold: dashes and no rose, no flag", () => {
    const c = draw(90, false, { show: "value" });
    expect(c.querySelector('[data-testid="trk-readout"]')?.textContent).toBe("TRK ---");
    expect(c.querySelector('[data-testid="rose"]')).toBeNull();
    expect(c.querySelector('[data-testid="trk-flag"]')).toBeNull();
  });

  it("shows dashes and no rose while the channel has no data", () => {
    const c = draw(90, true, { show: "dashes" });
    expect(c.querySelector('[data-testid="trk-readout"]')?.textContent).toBe("TRK ---");
    expect(c.querySelector('[data-testid="rose"]')).toBeNull();
  });

  it("removes the track under the TRK flag", () => {
    for (const cause of ["fail", "stale", "ended"] as const) {
      const c = draw(90, true, { show: "flag", cause });
      expect(c.querySelector('[data-testid="rose"]')).toBeNull();
      expect(c.querySelector('[data-testid="trk-readout"]')).toBeNull();
      expect(c.textContent).not.toContain("090");
      expect(c.querySelector('[data-testid="trk-flag"]')?.textContent).toBe("TRK");
      expect(c.querySelector('[data-testid="trk-flag"] rect')?.getAttribute("fill")).toBe(
        cause === "ended" ? "#5a6570" : "#d0342c",
      );
      cleanup();
    }
  });
});
