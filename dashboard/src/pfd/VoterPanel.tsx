// The voter panel (REQ-DISP-008, FTS-DS-001 §4.7): which of the three
// processing lanes are in the vote, a lane the voter excluded filled amber,
// a caution, since the output is still good and the system runs on the two
// lanes left; NO_MAJORITY as the red flag VOTE below the cells, a failure of
// the vote itself; dashes when no voter ran. Under a feed cause the cells go
// and the flag carries the cause, as on every instrument. A transient
// disagreement is not shown: no bit of the frame carries it.
import type { JSX } from "react";

import { Flag } from "./Flag";
import { COLORS, VOTER } from "./geometry";
import type { VoterState } from "./instrumentState";

export function VoterPanel({ state }: { state: VoterState }): JSX.Element {
  const cy = VOTER.cellY + VOTER.cellHeight / 2;
  return (
    <g className="voter-panel" data-testid="voter-panel">
      <text x={VOTER.flagX} y={VOTER.captionY} textAnchor="middle" fontSize={13} fill={COLORS.dim}>
        VOTE
      </text>
      {state.dashes ? (
        <text
          data-testid="voter-dashes"
          x={VOTER.flagX}
          y={cy}
          textAnchor="middle"
          dominantBaseline="middle"
          fontSize={22}
          fontWeight={700}
          fill="#ffffff"
        >
          ---
        </text>
      ) : null}
      {state.lanes !== null
        ? state.lanes.map((inVote, index) => (
            <g key={index} data-testid={`voter-lane-${index + 1}`} data-in={inVote ? "true" : "false"}>
              <rect
                x={VOTER.cellX[index]}
                y={VOTER.cellY}
                width={VOTER.cellWidth}
                height={VOTER.cellHeight}
                rx={3}
                fill={inVote ? "none" : COLORS.amber}
                stroke={inVote ? "#ffffff" : COLORS.amber}
                strokeWidth={1.5}
              />
              <text
                x={VOTER.cellX[index] + VOTER.cellWidth / 2}
                y={cy}
                textAnchor="middle"
                dominantBaseline="middle"
                fontSize={16}
                fontWeight={700}
                fill={inVote ? "#ffffff" : COLORS.background}
              >
                {index + 1}
              </text>
            </g>
          ))
        : null}
      {state.flag !== null ? (
        <Flag name="VOTE" cause={state.flag} x={VOTER.flagX} y={VOTER.flagY} testId="vote-flag" />
      ) : null}
    </g>
  );
}
