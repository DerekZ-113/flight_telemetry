// The panel's geometry (FTS-DS-001 §3 and §4): one SVG of 800 by 520
// viewBox units, scaled to its container, so every coordinate here and in
// the tests is a viewBox unit and never a pixel of a window. The
// instruments' geometry lives here as pure functions, each with its own
// test, so a component only places what these compute.
import { formatSigned, formatThousands, roundTo } from "./units";

export const PANEL = { width: 800, height: 520 } as const;

export const STRIP = { height: 36, baseline: 25 } as const;

// The cockpit's colours (FTS-DS-001 §2.2): red warns, amber cautions, white
// informs; the ended box is grey because nothing failed.
export const COLORS = {
  background: "#0f1318",
  panel: "#1e2530",
  text: "#e8edf2",
  dim: "#8a94a0",
  normal: "#37c871",
  amber: "#f0a830",
  red: "#d0342c",
  ended: "#5a6570",
  sky: "#2a6fd6",
  ground: "#8a5a2b",
  symbol: "#ffcc33",
} as const;

// --- the attitude indicator (FTS-DS-001 §4.1) -------------------------------

export const ATTITUDE = { cx: 400, cy: 260, r: 160, pxPerDeg: 4 } as const;

// An SVG number: no trailing zeros, and never "-0".
function num(value: number): string {
  return String(Number(value.toFixed(3)));
}

// The horizon group's transform. SVG applies the rightmost transform first:
// the translation moves the horizon along the ball's own vertical, 4 px per
// degree, down (positive y) for nose up; then the rotation turns the ball
// about the centre, and since SVG's positive rotation is clockwise, a right
// roll (positive) turns the horizon counterclockwise, as the real one
// appears to a pilot whose right wing drops.
export function horizonTransform(pitchDeg: number, rollDeg: number): string {
  return `rotate(${num(-rollDeg)} ${ATTITUDE.cx} ${ATTITUDE.cy}) translate(0 ${num(pitchDeg * ATTITUDE.pxPerDeg)})`;
}

export interface LadderLine {
  deg: number;
  y: number; // inside the horizon group: above the horizon line for positive pitch
  long: boolean;
  label?: string;
}

// Long lines with labels every 10°, short ones in between, nothing at 0 (the
// horizon line itself), ±30° at most: more would crowd the ball.
export function pitchLadder(): LadderLine[] {
  const lines: LadderLine[] = [];
  for (let deg = -30; deg <= 30; deg += 5) {
    if (deg === 0) {
      continue;
    }
    const long = deg % 10 === 0;
    const line: LadderLine = { deg, y: ATTITUDE.cy - deg * ATTITUDE.pxPerDeg, long };
    if (long) {
      line.label = String(Math.abs(deg));
    }
    lines.push(line);
  }
  return lines;
}

// The roll pointer turns with the ball (a sky pointer) against a scale fixed
// to the case.
export function rollPointerTransform(rollDeg: number): string {
  return `rotate(${num(-rollDeg)} ${ATTITUDE.cx} ${ATTITUDE.cy})`;
}

// --- the altitude tape (FTS-DS-001 §4.2) -----------------------------------

export const ALTITUDE_TAPE = {
  x: 600,
  width: 90,
  top: 100,
  bottom: 420,
  cy: 260,
  pxPerFt: 0.4,
  tickFt: 20,
  labelFt: 100,
  spanFt: 400,
} as const;

export interface TapeTick {
  ft: number;
  y: number;
  label?: string;
}

// The ticks within ±400 ft of the value, each at its own y (the tape is not
// one translated group, so a tick is never drawn outside the window), with
// a label on every hundred feet.
export function tapeTicks(valueFt: number): TapeTick[] {
  const { tickFt, labelFt, spanFt, cy, pxPerFt, top, bottom } = ALTITUDE_TAPE;
  const first = Math.ceil((valueFt - spanFt) / tickFt) * tickFt;
  const last = Math.floor((valueFt + spanFt) / tickFt) * tickFt;
  const ticks: TapeTick[] = [];
  for (let ft = first; ft <= last; ft += tickFt) {
    const y = cy + (valueFt - ft) * pxPerFt;
    if (y < top || y > bottom) {
      continue;
    }
    const tick: TapeTick = { ft, y };
    if (ft % labelFt === 0) {
      tick.label = formatThousands(ft);
    }
    ticks.push(tick);
  }
  return ticks;
}

export function tapeReadout(valueFt: number): string {
  return formatThousands(roundTo(valueFt, 10));
}

// --- the vertical speed indicator (FTS-DS-001 §4.3) -----------------------

export const VSI = { x: 710, width: 50, top: 100, bottom: 420, cy: 260, pxPerFpm: 0.06, maxFpm: 2000 } as const;

// Linear, ±2,000 ft/min over ±120 px, the pointer pinned at the ends while
// the readout stays unclamped.
export function vsPointerY(fpm: number): number {
  const pinned = Math.max(-VSI.maxFpm, Math.min(VSI.maxFpm, fpm));
  return VSI.cy - pinned * VSI.pxPerFpm;
}

export function vsReadout(fpm: number): string {
  return formatSigned(roundTo(fpm, 50));
}
