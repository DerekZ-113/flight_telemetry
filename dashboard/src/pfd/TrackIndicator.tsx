// The track indicator (REQ-DISP-002, FTS-DS-001 §4.5): the GPS course over
// ground (the frame's course_deg) on the top arc of a compass rose, turned
// so the current track sits under a lubber line fixed to the case, with the
// readout above it. It is labelled TRK and never HDG: there is no
// magnetometer, and a course says where the aircraft moves, not where its
// nose points. Below the speed threshold the track is withheld, dashes and
// no rose, with no flag, because a stationary receiver's course is noise
// and nothing has failed.
import type { JSX } from "react";

import { Flag } from "./Flag";
import { COLORS, TRACK, roseTicks, roseTransform, trackReadout } from "./geometry";
import type { InstrumentState } from "./instrumentState";

export function TrackIndicator({
  trackDeg,
  shown,
  state,
}: {
  trackDeg: number;
  shown: boolean;
  state: InstrumentState;
}): JSX.Element {
  const { cx, cy, r, top, bottom, lubberY, readoutY } = TRACK;
  const showing = state.show === "value" && shown;
  const readout = state.show === "flag" ? null : showing ? `TRK ${trackReadout(trackDeg)}` : "TRK ---";
  return (
    <g className="track" data-testid="track">
      <defs>
        <clipPath id="track-clip">
          <rect x={cx - 200} y={top} width={400} height={bottom - top} />
        </clipPath>
      </defs>
      {showing ? (
        <g clipPath="url(#track-clip)">
          <g data-testid="rose" transform={roseTransform(trackDeg)}>
            <circle cx={cx} cy={cy} r={r} fill="none" stroke="#ffffff" strokeWidth={2} />
            {roseTicks().map((tick) => (
              <g key={tick.deg} transform={`rotate(${tick.deg} ${cx} ${cy})`}>
                <line
                  x1={cx}
                  y1={cy - r}
                  x2={cx}
                  y2={cy - r + (tick.label !== undefined ? 14 : 8)}
                  stroke="#ffffff"
                  strokeWidth={tick.label !== undefined ? 2 : 1}
                />
                {tick.label !== undefined ? (
                  <text x={cx} y={cy - r + 30} textAnchor="middle" fontSize={14} fill="#ffffff">
                    {tick.label}
                  </text>
                ) : null}
              </g>
            ))}
          </g>
        </g>
      ) : null}
      <polygon
        data-testid="lubber"
        points={`${cx},${lubberY} ${cx - 7},${lubberY - 12} ${cx + 7},${lubberY - 12}`}
        fill={COLORS.symbol}
      />
      {readout !== null ? (
        <g>
          <rect x={cx - 52} y={readoutY - 12} width={104} height={24} fill="#000000" stroke="#ffffff" strokeWidth={1.5} />
          <text
            data-testid="trk-readout"
            x={cx}
            y={readoutY}
            textAnchor="middle"
            dominantBaseline="middle"
            fontSize={16}
            fontWeight={700}
            fill="#ffffff"
          >
            {readout}
          </text>
        </g>
      ) : null}
      {state.show === "flag" && state.cause !== undefined ? (
        <Flag name="TRK" cause={state.cause} x={cx} y={490} testId="trk-flag" />
      ) : null}
    </g>
  );
}
