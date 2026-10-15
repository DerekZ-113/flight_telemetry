// The ground track plot (REQ-DISP-002, REQ-DISP-005, FTS-DS-001 §6): the
// trail of fixes relative to the first one, in metres north and east, on a
// plain grid with a scale bar, north up. The bridge sends displacements and
// never a coordinate, and this plot draws nothing but them: no tiles, no
// image, so it reveals a shape and never a place. The marker is a triangle
// turned to the track while the track is shown, a dot below the speed
// threshold; under a flag the trail stays, since it is history, and the POS
// flag takes the marker's place.
import { useMemo, type JSX } from "react";

import type { Position } from "../feed";
import { Flag } from "./Flag";
import { COLORS, PLOT, plotMarkerTransform, plotPoint, plotPoints, plotScale, scaleBarLabel } from "./geometry";
import type { InstrumentState } from "./instrumentState";

export function GroundTrackPlot({
  trail,
  position,
  trackDeg,
  trackShown,
  state,
}: {
  trail: Position[];
  position: Position | null;
  trackDeg: number;
  trackShown: boolean;
  state: InstrumentState;
}): JSX.Element {
  // The trail is a new array only on a fresh fix, so the points string is
  // rebuilt once a second and not fifty times.
  const scale = useMemo(() => plotScale(trail), [trail]);
  const points = useMemo(() => plotPoints(trail, scale), [trail, scale]);
  const stepPx = scale.stepM * scale.pxPerM;
  const gridOffsets: number[] = [];
  for (let d = stepPx; d < PLOT.half; d += stepPx) {
    gridOffsets.push(d);
  }
  const marker = position !== null && state.show === "value" ? plotPoint(position, scale) : null;
  const { half, size } = PLOT;

  return (
    <svg className="plot" viewBox={`0 0 ${size} ${size}`} role="img" aria-label="ground track" data-testid="ground-track">
      <rect width={size} height={size} fill={COLORS.background} />
      <g className="grid" stroke="#243040" strokeWidth={1}>
        <line x1={half} y1={0} x2={half} y2={size} />
        <line x1={0} y1={half} x2={size} y2={half} />
        {gridOffsets.map((d) => (
          <g key={d}>
            <line x1={half + d} y1={0} x2={half + d} y2={size} />
            <line x1={half - d} y1={0} x2={half - d} y2={size} />
            <line x1={0} y1={half + d} x2={size} y2={half + d} />
            <line x1={0} y1={half - d} x2={size} y2={half - d} />
          </g>
        ))}
      </g>
      <text x={8} y={16} fontSize={11} fill={COLORS.dim}>
        GROUND TRACK · relative to first fix
      </text>
      <text x={half} y={34} textAnchor="middle" fontSize={12} fontWeight={700} fill={COLORS.text}>
        N
      </text>
      <line x1={half} y1={38} x2={half} y2={52} stroke={COLORS.text} strokeWidth={2} />
      {trail.length > 0 ? (
        <polyline data-testid="plot-trail" points={points} fill="none" stroke={COLORS.normal} strokeWidth={2} />
      ) : null}
      {marker !== null && position !== null ? (
        trackShown ? (
          <polygon
            data-testid="plot-marker"
            transform={plotMarkerTransform(position, scale, trackDeg)}
            points="0,-9 6,7 -6,7"
            fill={COLORS.symbol}
          />
        ) : (
          <circle data-testid="plot-dot" cx={marker.x} cy={marker.y} r={5} fill={COLORS.symbol} />
        )
      ) : null}
      {trail.length === 0 && position === null ? (
        <text data-testid="plot-nofix" x={half} y={half} textAnchor="middle" fontSize={14} fill={COLORS.dim}>
          no fix
        </text>
      ) : null}
      {state.show === "flag" && state.cause !== undefined ? (
        <Flag name="POS" cause={state.cause} x={half} y={half} testId="pos-flag" />
      ) : null}
      <line x1={10} y1={size - 10} x2={10 + stepPx} y2={size - 10} stroke={COLORS.text} strokeWidth={3} />
      <text data-testid="plot-scale" x={10} y={size - 16} fontSize={11} fill={COLORS.text}>
        {scaleBarLabel(scale.stepM)}
      </text>
    </svg>
  );
}
