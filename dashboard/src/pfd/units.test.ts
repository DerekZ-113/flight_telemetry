// Display units convert at the display only, by exact constants
// (REQ-DISP-002; TC-024 step 7), and the readouts round to the steps
// FTS-DS-001 §2.5 states.
import { describe, expect, it } from "vitest";

import {
  FEET_PER_METER,
  FPM_PER_MPS,
  KNOTS_PER_MPS,
  formatSigned,
  formatThousands,
  metersToFeet,
  mpsToFpm,
  mpsToKnots,
  roundTo,
} from "./units";

describe("units", () => {
  it("converts a metre and a metre per second by the exact constants", () => {
    expect(FEET_PER_METER).toBe(1 / 0.3048);
    expect(FPM_PER_MPS).toBe(60 / 0.3048);
    expect(KNOTS_PER_MPS).toBe(3600 / 1852);
    expect(metersToFeet(1)).toBeCloseTo(3.28084, 5);
    expect(mpsToFpm(1)).toBeCloseTo(196.85039, 5);
    expect(mpsToKnots(1)).toBeCloseTo(1.94384, 5);
    expect(metersToFeet(0.3048)).toBe(1);
    expect(metersToFeet(121.92)).toBeCloseTo(400, 6);
  });

  it("rounds the readouts to their steps and formats them", () => {
    expect(roundTo(6561.7, 10)).toBe(6560);
    expect(roundTo(400.26, 10)).toBe(400);
    expect(roundTo(590.55, 50)).toBe(600);
    expect(roundTo(-393.7, 50)).toBe(-400);
    expect(formatThousands(6560)).toBe("6,560");
    expect(formatThousands(400)).toBe("400");
    expect(formatThousands(-20)).toBe("−20");
    expect(formatSigned(600)).toBe("+600");
    expect(formatSigned(-400)).toBe("−400");
    expect(formatSigned(0)).toBe("0");
    expect(formatSigned(2950)).toBe("+2,950");
  });
});
