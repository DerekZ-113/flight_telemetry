// Display units (REQ-DISP-002, FTS-DS-001 §2.4 and §2.5). The frame is SI
// and stays SI in the feed's state; the instruments convert here, and
// nowhere else, with the exact definitions: a foot is 0.3048 m, a nautical
// mile 1,852 m. A pilot reads feet, feet per minute and knots; the record
// keeps metres so that the log, the pipeline, the Python twin and every
// byte-identity claim share one unit system.

export const FEET_PER_METER = 1 / 0.3048;
export const FPM_PER_MPS = 60 / 0.3048;
export const KNOTS_PER_MPS = 3600 / 1852;

export function metersToFeet(meters: number): number {
  return meters / 0.3048;
}

export function mpsToFpm(metersPerSecond: number): number {
  return (metersPerSecond * 60) / 0.3048;
}

export function mpsToKnots(metersPerSecond: number): number {
  return (metersPerSecond * 3600) / 1852;
}

// The readouts round to a step (10 ft, 50 ft/min); the tapes move on the
// unrounded value.
export function roundTo(value: number, step: number): number {
  return Math.round(value / step) * step;
}

// A typographic minus, so a negative readout cannot be mistaken for a dash.
const MINUS = "−";

export function formatThousands(value: number): string {
  const digits = Math.abs(value).toLocaleString("en-US", { maximumFractionDigits: 0 });
  return value < 0 ? MINUS + digits : digits;
}

// Vertical speed carries its sign: "+600", "−400", "0".
export function formatSigned(value: number): string {
  return value > 0 ? "+" + formatThousands(value) : formatThousands(value);
}
