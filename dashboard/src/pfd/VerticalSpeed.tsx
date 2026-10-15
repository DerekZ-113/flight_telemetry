// The vertical speed indicator (REQ-DISP-002, FTS-DS-001 §4.3): feet per
// minute from the fused vertical speed, a pointer on a linear scale pinned
// at ±2,000 while the readout, rounded to 50, stays unclamped. Dashes and no
// pointer while there is no value; the VS flag when the value is withdrawn.
import type { JSX } from "react";

import { Flag } from "./Flag";
import { COLORS, VSI, vsPointerY, vsReadout } from "./geometry";
import type { InstrumentState } from "./instrumentState";
import { mpsToFpm } from "./units";

const MARKS_FPM = [-2000, -1000, -500, 0, 500, 1000, 2000];

export function VerticalSpeed({
  verticalSpeedMps,
  state,
}: {
  verticalSpeedMps: number;
  state: InstrumentState;
}): JSX.Element {
  const { x, width, top, bottom, cy } = VSI;
  const fpm = mpsToFpm(verticalSpeedMps);
  return (
    <g className="vsi" data-testid="vsi">
      <text x={x + width / 2} y={top - 12} textAnchor="middle" fontSize={13} fill={COLORS.dim}>
        VS fpm
      </text>
      <rect x={x} y={top} width={width} height={bottom - top} fill={COLORS.panel} />
      {MARKS_FPM.map((mark) => (
        <g key={mark}>
          <line x1={x} y1={vsPointerY(mark)} x2={x + 8} y2={vsPointerY(mark)} stroke="#ffffff" strokeWidth={1.5} />
          <text x={x + 12} y={vsPointerY(mark)} dominantBaseline="middle" fontSize={11} fill="#ffffff">
            {Math.abs(mark) / 100}
          </text>
        </g>
      ))}
      {state.show === "value" ? (
        <line
          data-testid="vs-pointer"
          x1={x + 8}
          y1={vsPointerY(fpm)}
          x2={x + width}
          y2={vsPointerY(fpm)}
          stroke={COLORS.symbol}
          strokeWidth={4}
        />
      ) : null}
      {state.show !== "flag" ? (
        <text
          data-testid="vs-readout"
          x={x + width / 2}
          y={bottom + 22}
          textAnchor="middle"
          fontSize={16}
          fontWeight={700}
          fill="#ffffff"
        >
          {state.show === "value" ? vsReadout(fpm) : "---"}
        </text>
      ) : null}
      {state.show === "flag" && state.cause !== undefined ? (
        <Flag name="VS" cause={state.cause} x={x + width / 2} y={cy} testId="vs-flag" />
      ) : null}
    </g>
  );
}
