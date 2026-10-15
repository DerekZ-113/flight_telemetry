// The panel's geometry (FTS-DS-001 §3 and §4): one SVG of 800 by 520
// viewBox units, scaled to its container, so every coordinate here and in
// the tests is a viewBox unit and never a pixel of a window. The
// instruments' geometry lives here as pure functions, each with its own
// test, so a component only places what these compute.
import type { Position } from "../feed";
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

// --- the track indicator (FTS-DS-001 §4.5) --------------------------------

// A compass rose whose centre sits below the panel: only its top arc shows,
// in the band between top and bottom, under a lubber line fixed to the case.
export const TRACK = { cx: 400, cy: 640, r: 180, top: 448, bottom: 520, lubberY: 462, readoutY: 434 } as const;

// The rose turns the other way from the track, so the tick at the current
// track comes up under the lubber line.
export function roseTransform(trackDeg: number): string {
  return `rotate(${num(-trackDeg)} ${TRACK.cx} ${TRACK.cy})`;
}

export interface RoseTick {
  deg: number;
  label?: string;
}

const ROSE_LABELS: Record<number, string> = {
  0: "N",
  30: "3",
  60: "6",
  90: "E",
  120: "12",
  150: "15",
  180: "S",
  210: "21",
  240: "24",
  270: "W",
  300: "30",
  330: "33",
};

// A tick every 10°, a label every 30°, as a cockpit's rose is lettered.
export function roseTicks(): RoseTick[] {
  const ticks: RoseTick[] = [];
  for (let deg = 0; deg < 360; deg += 10) {
    const tick: RoseTick = { deg };
    const label = ROSE_LABELS[deg];
    if (label !== undefined) {
      tick.label = label;
    }
    ticks.push(tick);
  }
  return ticks;
}

// Three digits, 000 to 359: a course of 360 is north.
export function trackReadout(trackDeg: number): string {
  const whole = ((Math.round(trackDeg) % 360) + 360) % 360;
  return `${String(whole).padStart(3, "0")}°`;
}

// --- the ground track plot (FTS-DS-001 §6) -------------------------------

export const PLOT = { size: 300, half: 150, minHalfExtentM: 10, margin: 1.2 } as const;

export interface PlotScale {
  halfExtentM: number; // the metres from the origin to the plot's edge
  pxPerM: number;
  stepM: number; // the grid step and the scale bar's length
}

// The 1-2-5 value nearest to a length: 2.5 m gives 2, 900 m gives 1,000.
function niceStep(rawM: number): number {
  const base = Math.pow(10, Math.floor(Math.log10(rawM)));
  return [1, 2, 5, 10]
    .map((candidate) => candidate * base)
    .reduce((best, candidate) => (Math.abs(candidate - rawM) < Math.abs(best - rawM) ? candidate : best));
}

// The plot is centred on the origin and scaled to the trail's farthest
// point with a fifth of margin, never closer than 10 m to the edge, with a
// grid step of about a quarter of the half-extent.
export function plotScale(trail: Position[]): PlotScale {
  const largest = trail.reduce((most, p) => Math.max(most, Math.abs(p.east_m), Math.abs(p.north_m)), 0);
  const halfExtentM = Math.max(PLOT.minHalfExtentM, PLOT.margin * largest);
  return { halfExtentM, pxPerM: PLOT.half / halfExtentM, stepM: niceStep(halfExtentM / 4) };
}

// North up: east is to the right, north is up the page (smaller y).
export function plotPoint(p: Position, scale: PlotScale): { x: number; y: number } {
  return { x: PLOT.half + p.east_m * scale.pxPerM, y: PLOT.half - p.north_m * scale.pxPerM };
}

export function plotPoints(trail: Position[], scale: PlotScale): string {
  return trail
    .map((p) => {
      const { x, y } = plotPoint(p, scale);
      return `${num(x)},${num(y)}`;
    })
    .join(" ");
}

// The marker is drawn pointing up (north) and turned clockwise to the track.
export function plotMarkerTransform(p: Position, scale: PlotScale, trackDeg: number): string {
  const { x, y } = plotPoint(p, scale);
  return `translate(${num(x)} ${num(y)}) rotate(${num(trackDeg)})`;
}

export function scaleBarLabel(stepM: number): string {
  return stepM >= 1000 ? `${formatThousands(stepM / 1000)} km` : `${formatThousands(stepM)} m`;
}
