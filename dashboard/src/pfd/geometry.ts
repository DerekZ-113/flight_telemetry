// The panel's geometry (FTS-DS-001 §3): one SVG of 800 by 520 viewBox units,
// scaled to its container, so every coordinate here and in the tests is a
// viewBox unit and never a pixel of a window. The instruments' own geometry
// (the horizon transform, the tape's ticks, the rose) joins this file with
// them.

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
