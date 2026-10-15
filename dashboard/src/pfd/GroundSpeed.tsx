// The ground speed readout (REQ-DISP-002, FTS-DS-001 §4.4): knots from the
// GPS speed over ground, labelled GS because that is what a GPS measures;
// there is no pitot, so nothing here is airspeed. The field changes once
// per fix and is held between, which is a reading at the receiver's own
// rate, not a stale one; a hold past the timeout is the detector's
// DEGRADED and shows as the flag.
import type { JSX } from "react";

import { Flag } from "./Flag";
import { COLORS } from "./geometry";
import type { InstrumentState } from "./instrumentState";
import { mpsToKnots } from "./units";

const BOX = { x: 40, y: 236, width: 110, height: 48 } as const;

export function GroundSpeed({
  groundSpeedMps,
  state,
}: {
  groundSpeedMps: number;
  state: InstrumentState;
}): JSX.Element {
  const cx = BOX.x + BOX.width / 2;
  const cy = BOX.y + BOX.height / 2;
  const knots = Math.round(mpsToKnots(groundSpeedMps));
  return (
    <g className="ground-speed" data-testid="ground-speed">
      <text x={cx} y={BOX.y - 12} textAnchor="middle" fontSize={13} fill={COLORS.dim}>
        GS
      </text>
      <rect
        x={BOX.x}
        y={BOX.y}
        width={BOX.width}
        height={BOX.height}
        fill={COLORS.panel}
        stroke={state.show === "value" ? "#ffffff" : "none"}
        strokeWidth={1.5}
      />
      {state.show !== "flag" ? (
        <>
          <text
            data-testid="gs-readout"
            x={BOX.x + 80}
            y={cy}
            textAnchor="end"
            dominantBaseline="middle"
            fontSize={22}
            fontWeight={700}
            fill="#ffffff"
          >
            {state.show === "value" ? String(knots) : "---"}
          </text>
          <text x={BOX.x + 86} y={cy} dominantBaseline="middle" fontSize={12} fill={COLORS.dim}>
            kt
          </text>
        </>
      ) : null}
      {state.show === "flag" && state.cause !== undefined ? (
        <Flag name="GS" cause={state.cause} x={cx} y={cy} testId="gs-flag" />
      ) : null}
    </g>
  );
}
