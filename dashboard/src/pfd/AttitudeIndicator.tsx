// The attitude indicator (REQ-DISP-002, FTS-DS-001 §4.1): the aircraft
// symbol is fixed to the case and the world moves behind it. The horizon
// group carries the sky, the ground, the horizon line and the pitch ladder,
// shifted along the ball's own vertical by the pitch and turned about the
// centre by the roll (geometry.ts: horizonTransform); a pointer turns with
// the ball against a roll scale fixed to the case. Under a flag the ball is
// a blank disc with the symbol and the ATT flag; with nothing to show it is
// the blank disc alone, since dashes have no place on a ball.
import type { JSX } from "react";

import { Flag } from "./Flag";
import { ATTITUDE, COLORS, horizonTransform, pitchLadder, rollPointerTransform } from "./geometry";
import type { InstrumentState } from "./instrumentState";

const ROLL_SCALE_DEG = [-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60];
const HALF = 400; // the ball's rectangles reach past the clip under any pitch and roll

export function AttitudeIndicator({
  pitchDeg,
  rollDeg,
  state,
}: {
  pitchDeg: number;
  rollDeg: number;
  state: InstrumentState;
}): JSX.Element {
  const { cx, cy, r } = ATTITUDE;
  const showing = state.show === "value";
  return (
    <g className="attitude" data-testid="attitude">
      <defs>
        <clipPath id="attitude-clip">
          <circle cx={cx} cy={cy} r={r} />
        </clipPath>
      </defs>
      {showing ? (
        <g clipPath="url(#attitude-clip)">
          <g data-testid="horizon" transform={horizonTransform(pitchDeg, rollDeg)}>
            <rect x={cx - HALF} y={cy - 600} width={2 * HALF} height={600} fill={COLORS.sky} />
            <rect x={cx - HALF} y={cy} width={2 * HALF} height={600} fill={COLORS.ground} />
            <line x1={cx - HALF} y1={cy} x2={cx + HALF} y2={cy} stroke="#ffffff" strokeWidth={2} />
            {pitchLadder().map((line) => {
              const half = line.long ? 60 : 30;
              return (
                <g key={line.deg}>
                  <line
                    data-deg={line.deg}
                    x1={cx - half}
                    y1={line.y}
                    x2={cx + half}
                    y2={line.y}
                    stroke="#ffffff"
                    strokeWidth={line.long ? 2 : 1}
                  />
                  {line.label !== undefined ? (
                    <>
                      <text x={cx - half - 8} y={line.y} textAnchor="end" dominantBaseline="middle" fontSize={14} fill="#ffffff">
                        {line.label}
                      </text>
                      <text x={cx + half + 8} y={line.y} dominantBaseline="middle" fontSize={14} fill="#ffffff">
                        {line.label}
                      </text>
                    </>
                  ) : null}
                </g>
              );
            })}
          </g>
        </g>
      ) : (
        <circle data-testid="attitude-blank" cx={cx} cy={cy} r={r} fill={COLORS.panel} />
      )}
      <circle cx={cx} cy={cy} r={r} fill="none" stroke="#000000" strokeWidth={3} />
      {showing ? (
        <>
          <g className="roll-scale">
            {ROLL_SCALE_DEG.map((deg) => (
              <line
                key={deg}
                transform={`rotate(${-deg} ${cx} ${cy})`}
                x1={cx}
                y1={cy - r}
                x2={cx}
                y2={cy - r + (deg % 30 === 0 ? 16 : 10)}
                stroke="#ffffff"
                strokeWidth={2}
              />
            ))}
          </g>
          <g data-testid="roll-pointer" transform={rollPointerTransform(rollDeg)}>
            <polygon points={`${cx},${cy - r + 18} ${cx - 8},${cy - r + 34} ${cx + 8},${cy - r + 34}`} fill="#ffffff" />
          </g>
        </>
      ) : null}
      <g data-testid="aircraft-symbol">
        <circle cx={cx} cy={cy} r={4} fill={COLORS.symbol} />
        <rect x={cx - 110} y={cy - 3} width={70} height={6} fill={COLORS.symbol} />
        <rect x={cx + 40} y={cy - 3} width={70} height={6} fill={COLORS.symbol} />
        <rect x={cx - 40} y={cy - 3} width={6} height={14} fill={COLORS.symbol} />
        <rect x={cx + 34} y={cy - 3} width={6} height={14} fill={COLORS.symbol} />
      </g>
      {state.show === "flag" && state.cause !== undefined ? (
        <Flag name="ATT" cause={state.cause} x={cx} y={cy} testId="att-flag" />
      ) : null}
    </g>
  );
}
