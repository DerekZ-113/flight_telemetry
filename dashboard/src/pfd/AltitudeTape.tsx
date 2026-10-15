// The altitude tape (REQ-DISP-002, FTS-DS-001 §4.2): feet from the fused
// altitude's metres (units.ts), ticks every 20 ft around the value, labels
// every 100, the readout to 10 ft on the centre line. Dashes in the readout
// and no ticks while there is no value; the ALT flag over the tape's centre
// when the value is withdrawn.
import type { JSX } from "react";

import { Flag } from "./Flag";
import { ALTITUDE_TAPE, COLORS, tapeReadout, tapeTicks } from "./geometry";
import type { InstrumentState } from "./instrumentState";
import { metersToFeet } from "./units";

export function AltitudeTape({ altitudeM, state }: { altitudeM: number; state: InstrumentState }): JSX.Element {
  const { x, width, top, bottom, cy } = ALTITUDE_TAPE;
  const feet = metersToFeet(altitudeM);
  const ticks = state.show === "value" ? tapeTicks(feet) : [];
  const readout = state.show === "value" ? tapeReadout(feet) : state.show === "dashes" ? "---" : null;
  return (
    <g className="altitude-tape" data-testid="altitude-tape">
      <text x={x + width / 2} y={top - 12} textAnchor="middle" fontSize={13} fill={COLORS.dim}>
        ALT ft
      </text>
      <rect x={x} y={top} width={width} height={bottom - top} fill={COLORS.panel} />
      {ticks.map((tick) => (
        <g key={tick.ft}>
          <line
            data-ft={tick.ft}
            x1={x}
            y1={tick.y}
            x2={x + (tick.label !== undefined ? 16 : 10)}
            y2={tick.y}
            stroke="#ffffff"
            strokeWidth={tick.label !== undefined ? 2 : 1}
          />
          {tick.label !== undefined ? (
            <text data-label={tick.label} x={x + 22} y={tick.y} dominantBaseline="middle" fontSize={14} fill="#ffffff">
              {tick.label}
            </text>
          ) : null}
        </g>
      ))}
      {readout !== null ? (
        <g>
          <rect x={x - 6} y={cy - 16} width={width + 12} height={32} fill="#000000" stroke="#ffffff" strokeWidth={1.5} />
          <text
            data-testid="alt-readout"
            x={x + width}
            y={cy}
            textAnchor="end"
            dominantBaseline="middle"
            fontSize={20}
            fontWeight={700}
            fill="#ffffff"
          >
            {readout}
          </text>
        </g>
      ) : null}
      {state.show === "flag" && state.cause !== undefined ? (
        <Flag name="ALT" cause={state.cause} x={x + width / 2} y={cy} testId="alt-flag" />
      ) : null}
    </g>
  );
}
