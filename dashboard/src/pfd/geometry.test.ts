// The instruments' geometry as pure functions (REQ-DISP-002; TC-024 steps 1
// to 6): the horizon's transform and its signs, the ladder, the roll
// pointer, the altitude tape's ticks, the vertical speed pointer pinned at
// its ends. Every number is a viewBox unit from FTS-DS-001 §4.
import { describe, expect, it } from "vitest";

import {
  horizonTransform,
  pitchLadder,
  plotPoints,
  plotScale,
  rollPointerTransform,
  roseTicks,
  roseTransform,
  scaleBarLabel,
  tapeReadout,
  tapeTicks,
  trackReadout,
  vsPointerY,
  vsReadout,
} from "./geometry";

describe("the attitude indicator's geometry", () => {
  it("moves the horizon down for nose up and up for nose down (step 1)", () => {
    expect(horizonTransform(10, 0)).toBe("rotate(0 400 260) translate(0 40)");
    expect(horizonTransform(-10, 0)).toBe("rotate(0 400 260) translate(0 -40)");
  });

  it("rotates the horizon counterclockwise for a right roll (step 2)", () => {
    expect(horizonTransform(0, 30)).toBe("rotate(-30 400 260) translate(0 0)");
    expect(horizonTransform(0, -30)).toBe("rotate(30 400 260) translate(0 0)");
  });

  it("translates first, along the ball's own vertical, then rotates (step 3)", () => {
    expect(horizonTransform(10, 30)).toBe("rotate(-30 400 260) translate(0 40)");
  });

  it("places the ladder's lines and rotates the roll pointer with the ball (step 4)", () => {
    const ladder = pitchLadder();
    const at = (deg: number) => ladder.find((line) => line.deg === deg);
    expect(at(10)?.y).toBe(220);
    expect(at(20)?.y).toBe(180);
    expect(at(30)?.y).toBe(140);
    expect(at(-10)?.y).toBe(300);
    expect(at(-30)?.y).toBe(380);
    expect(at(10)?.long).toBe(true);
    expect(at(10)?.label).toBe("10");
    expect(at(5)?.long).toBe(false);
    expect(at(5)?.label).toBeUndefined();
    expect(ladder.map((line) => line.deg)).toEqual([-30, -25, -20, -15, -10, -5, 5, 10, 15, 20, 25, 30]);
    expect(rollPointerTransform(30)).toBe("rotate(-30 400 260)");
    expect(rollPointerTransform(-45)).toBe("rotate(45 400 260)");
  });
});

describe("the tapes' geometry", () => {
  it("places the altitude tape's ticks around the value, labelled every hundred feet (step 5)", () => {
    const ticks = tapeTicks(400);
    const at = (ft: number) => ticks.find((tick) => tick.ft === ft);
    expect(at(400)?.y).toBe(260);
    expect(at(300)?.y).toBeCloseTo(300, 6);
    expect(at(500)?.y).toBeCloseTo(220, 6);
    expect(ticks).toHaveLength(41);
    expect(ticks[0].ft).toBe(0);
    expect(ticks[40].ft).toBe(800);
    for (const tick of ticks) {
      expect(tick.y).toBeGreaterThanOrEqual(100);
      expect(tick.y).toBeLessThanOrEqual(420);
    }
    expect(at(400)?.label).toBe("400");
    expect(at(420)?.label).toBeUndefined();
    expect(at(0)?.label).toBe("0");
    expect(tapeTicks(-30).find((tick) => tick.ft === -100)?.label).toBe("−100");
    expect(tapeReadout(400.26)).toBe("400");
    expect(tapeReadout(6561.7)).toBe("6,560");
  });

  it("places the vertical speed pointer, pinned at the ends, with the readout unclamped (step 6)", () => {
    expect(260 - vsPointerY(590.55)).toBeCloseTo(35.4, 1);
    expect(vsPointerY(-393.7) - 260).toBeCloseTo(23.6, 1);
    expect(vsPointerY(2952.8)).toBe(140);
    expect(vsPointerY(-5000)).toBe(380);
    expect(vsPointerY(0)).toBe(260);
    expect(vsReadout(590.55)).toBe("+600");
    expect(vsReadout(-393.7)).toBe("−400");
    expect(vsReadout(2952.8)).toBe("+2,950");
    expect(vsReadout(0)).toBe("0");
  });
});

describe("the track indicator's geometry", () => {
  it("turns the rose so the track sits under the lubber line, labelled every thirty degrees", () => {
    expect(roseTransform(87)).toBe("rotate(-87 400 640)");
    expect(roseTransform(0)).toBe("rotate(0 400 640)");
    expect(roseTicks()).toHaveLength(36);
    expect(roseTicks().filter((tick) => tick.label !== undefined).map((tick) => tick.label)).toEqual([
      "N", "3", "6", "E", "12", "15", "S", "21", "24", "W", "30", "33",
    ]);
    expect(trackReadout(87.4)).toBe("087°");
    expect(trackReadout(359.6)).toBe("000°");
    expect(trackReadout(270)).toBe("270°");
  });
});

describe("the ground track plot's geometry", () => {
  it("scales to the trail with a 1-2-5 grid step and a scale bar in metres or kilometres", () => {
    const small = plotScale([
      { east_m: 0, north_m: 0 },
      { east_m: 1, north_m: 0 },
      { east_m: 1, north_m: 1 },
    ]);
    expect(small.halfExtentM).toBe(10);
    expect(small.pxPerM).toBe(15);
    expect(small.stepM).toBe(2);
    expect(scaleBarLabel(small.stepM)).toBe("2 m");
    expect(plotPoints([{ east_m: 0, north_m: 0 }, { east_m: 1, north_m: 0 }, { east_m: 1, north_m: 1 }], small)).toBe(
      "150,150 165,150 165,135",
    );
    const far = plotScale([{ east_m: 0, north_m: 0 }, { east_m: 3000, north_m: 0 }]);
    expect(far.halfExtentM).toBeCloseTo(3600, 6);
    expect(far.stepM).toBe(1000);
    expect(scaleBarLabel(far.stepM)).toBe("1 km");
    expect(scaleBarLabel(200)).toBe("200 m");
    expect(scaleBarLabel(5000)).toBe("5 km");
    expect(plotScale([]).halfExtentM).toBe(10);
    expect(plotScale([{ east_m: -40, north_m: 10 }]).halfExtentM).toBeCloseTo(48, 6);
  });
});
