// The ground track plot (REQ-DISP-002, REQ-DISP-005; TC-025 step 6): the
// trail of fixes relative to the first one on a plain grid with a scale bar,
// north up, the marker a triangle on the track or a dot below the speed
// threshold; never an image, never a coordinate; under a flag the trail
// stays and the POS flag replaces the marker.
import { cleanup, render } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import type { Position } from "../feed";
import { GroundTrackPlot } from "./GroundTrackPlot";
import type { InstrumentState } from "./instrumentState";

afterEach(cleanup);

const THREE: Position[] = [
  { east_m: 0, north_m: 0 },
  { east_m: 1, north_m: 0 },
  { east_m: 1, north_m: 1 },
];

function draw(
  trail: Position[],
  position: Position | null,
  trackDeg: number,
  trackShown: boolean,
  state: InstrumentState,
) {
  return render(
    <GroundTrackPlot trail={trail} position={position} trackDeg={trackDeg} trackShown={trackShown} state={state} />,
  ).container;
}

describe("the ground track plot", () => {
  it("draws the trail scaled into the viewBox, north up, with a scale bar and a marker on the track", () => {
    const c = draw(THREE, THREE[2], 45, true, { show: "value" });
    expect(c.querySelector('[data-testid="plot-trail"]')?.getAttribute("points")).toBe("150,150 165,150 165,135");
    const marker = c.querySelector('[data-testid="plot-marker"]');
    expect(marker?.getAttribute("transform")).toBe("translate(165 135) rotate(45)");
    expect(c.querySelector('[data-testid="plot-dot"]')).toBeNull();
    expect(c.querySelector('[data-testid="plot-scale"]')?.textContent).toBe("2 m");
    expect(c.querySelectorAll("image, img")).toHaveLength(0);
    expect(c.textContent).not.toMatch(/latitude|longitude|\d{2}\.\d{4,}/i);
    expect(c.textContent).toContain("relative to first fix");
    expect(c.querySelector('[data-testid="pos-flag"]')).toBeNull();
  });

  it("shows a dot instead of the marker below the speed threshold", () => {
    const c = draw(THREE, THREE[2], 45, false, { show: "value" });
    expect(c.querySelector('[data-testid="plot-marker"]')).toBeNull();
    const dot = c.querySelector('[data-testid="plot-dot"]');
    expect(dot?.getAttribute("cx")).toBe("165");
    expect(dot?.getAttribute("cy")).toBe("135");
  });

  it("labels the scale bar in kilometres for a long trail", () => {
    const far: Position[] = [
      { east_m: 0, north_m: 0 },
      { east_m: 3000, north_m: 0 },
    ];
    const c = draw(far, far[1], 90, true, { show: "value" });
    expect(c.querySelector('[data-testid="plot-scale"]')?.textContent).toBe("1 km");
  });

  it("keeps the trail and replaces the marker with the POS flag under a flag", () => {
    for (const cause of ["fail", "stale", "ended"] as const) {
      const c = draw(THREE, THREE[2], 45, true, { show: "flag", cause });
      expect(c.querySelector('[data-testid="plot-trail"]')?.getAttribute("points")).toBe("150,150 165,150 165,135");
      expect(c.querySelector('[data-testid="plot-marker"]')).toBeNull();
      expect(c.querySelector('[data-testid="plot-dot"]')).toBeNull();
      expect(c.querySelector('[data-testid="pos-flag"]')?.textContent).toBe("POS");
      expect(c.querySelector('[data-testid="pos-flag"] rect')?.getAttribute("fill")).toBe(
        cause === "ended" ? "#5a6570" : "#d0342c",
      );
      cleanup();
    }
  });

  it("draws an empty grid and says so before any fix", () => {
    const c = draw([], null, 0, false, { show: "dashes" });
    expect(c.querySelector('[data-testid="plot-trail"]')).toBeNull();
    expect(c.querySelector('[data-testid="plot-marker"]')).toBeNull();
    expect(c.querySelector('[data-testid="plot-dot"]')).toBeNull();
    expect(c.querySelector('[data-testid="plot-nofix"]')?.textContent).toBe("no fix");
    expect(c.querySelector('[data-testid="plot-scale"]')?.textContent).toBe("2 m");
  });
});
