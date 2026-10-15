// The flag that replaces a withdrawn value (REQ-DISP-001, FTS-DS-001 §2.1):
// a box with the instrument's name over the instrument's centre, red for
// every cause but an ended stream, whose box is grey because nothing failed.
// The strip names the cause; the flag names what is missing, as a cockpit's
// ATT, ALT and VS flags do.
import type { JSX } from "react";

import { COLORS } from "./geometry";
import type { FlagCause } from "./instrumentState";

export function Flag({
  name,
  cause,
  x,
  y,
  testId,
}: {
  name: string;
  cause: FlagCause;
  x: number;
  y: number;
  testId: string;
}): JSX.Element {
  const fill = cause === "ended" ? COLORS.ended : COLORS.red;
  return (
    <g data-testid={testId} data-cause={cause} className={`flag flag-${cause}`}>
      <rect x={x - 45} y={y - 16} width={90} height={32} rx={4} fill={fill} />
      <text x={x} y={y + 7} textAnchor="middle" fontSize={20} fontWeight={700} fill="#ffffff">
        {name}
      </text>
    </g>
  );
}
