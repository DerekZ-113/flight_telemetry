// The attitude indicator (REQ-DISP-001, REQ-DISP-002; TC-023 step 5, TC-024
// steps 1 to 4): the horizon moves, the symbol stays; a flag replaces the
// ball, dashes have no place on a ball, and nothing is ever struck through.
import { cleanup, render } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import { AttitudeIndicator } from "./AttitudeIndicator";
import type { InstrumentState } from "./instrumentState";

afterEach(cleanup);

function draw(pitchDeg: number, rollDeg: number, state: InstrumentState) {
  return render(
    <svg>
      <AttitudeIndicator pitchDeg={pitchDeg} rollDeg={rollDeg} state={state} />
    </svg>,
  ).container;
}

describe("the attitude indicator", () => {
  it("moves the horizon and the roll pointer with the attitude and keeps the symbol fixed", () => {
    const c = draw(10, 30, { show: "value" });
    expect(c.querySelector('[data-testid="horizon"]')?.getAttribute("transform")).toBe(
      "rotate(-30 400 260) translate(0 40)",
    );
    expect(c.querySelector('[data-testid="roll-pointer"]')?.getAttribute("transform")).toBe("rotate(-30 400 260)");
    expect(c.querySelector('[data-testid="aircraft-symbol"]')).not.toBeNull();
    expect(c.querySelector('[data-testid="aircraft-symbol"]')?.getAttribute("transform")).toBeNull();
    const line = c.querySelector('[data-testid="horizon"] line[data-deg="10"]');
    expect(line?.getAttribute("y1")).toBe("220");
    expect(c.querySelector('[data-testid="att-flag"]')).toBeNull();
    expect(c.querySelector('[data-testid="attitude-blank"]')).toBeNull();
  });

  it("replaces the ball with a flag naming the instrument, red or grey by the cause", () => {
    for (const cause of ["fail", "stale", "lag", "link"] as const) {
      const c = draw(10, 30, { show: "flag", cause });
      expect(c.querySelector('[data-testid="horizon"]')).toBeNull();
      expect(c.querySelector('[data-testid="roll-pointer"]')).toBeNull();
      expect(c.querySelector('[data-testid="attitude-blank"]')).not.toBeNull();
      const flag = c.querySelector('[data-testid="att-flag"]');
      expect(flag?.textContent).toBe("ATT");
      expect(flag?.getAttribute("data-cause")).toBe(cause);
      expect(flag?.querySelector("rect")?.getAttribute("fill")).toBe("#d0342c");
      expect(c.querySelector('[data-testid="aircraft-symbol"]')).not.toBeNull();
      cleanup();
    }
    const c = draw(10, 30, { show: "flag", cause: "ended" });
    expect(c.querySelector('[data-testid="att-flag"] rect')?.getAttribute("fill")).toBe("#5a6570");
    expect(c.querySelector('[data-testid="att-flag"]')?.textContent).toBe("ATT");
  });

  it("shows a blank ball and no flag while there is nothing to show", () => {
    for (const state of [{ show: "dashes" }, { show: "dashes", pipeline: true }] as const) {
      const c = draw(10, 30, state);
      expect(c.querySelector('[data-testid="horizon"]')).toBeNull();
      expect(c.querySelector('[data-testid="attitude-blank"]')).not.toBeNull();
      expect(c.querySelector('[data-testid="att-flag"]')).toBeNull();
      expect(c.textContent).not.toMatch(/\d/); // no number anywhere on a blank ball
      cleanup();
    }
  });

  it("never strikes a value through or dims it", () => {
    const c = draw(10, 30, { show: "flag", cause: "stale" });
    expect(c.querySelector(".stale")).toBeNull();
    expect(c.innerHTML).not.toMatch(/line-through|opacity/);
  });
});
