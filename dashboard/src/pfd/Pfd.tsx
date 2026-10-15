// The Primary Flight Display's panel (REQ-DISP-001, REQ-DISP-002,
// REQ-DISP-004, REQ-DISP-006; FTS-DS-001 §3 and §5): one SVG with the strip
// at its top (the mode label inside the panel, so no crop can lose it; the
// feed's state with its cause; PIPELINE when a field was never judged; the
// rate and the lag) and the instruments below it, each deciding what it
// shows through instrumentState. Rendered from the feed's state alone, so
// the tests build that state by hand.
import type { JSX } from "react";

import { COMPUTED_FIELDS, modeLabel } from "../feed";
import type { FeedState } from "../useTelemetryFeed";
import { COLORS, PANEL, STRIP } from "./geometry";
import { feedCause, feedStateText, lagLabel } from "./instrumentState";

function stateColor(cause: ReturnType<typeof feedCause>, feed: FeedState): string {
  switch (cause) {
    case "link":
    case "stale":
    case "lag":
      return COLORS.red;
    case "ended":
      return COLORS.dim;
    case "fail":
    case null:
      return feed.lastFrame === null ? COLORS.dim : COLORS.normal;
  }
}

export function Pfd({ feed }: { feed: FeedState }): JSX.Element {
  const cause = feedCause(feed);
  const label = modeLabel(feed.session);
  const boxed = feed.session?.mode !== "LIVE"; // SIM, REPLAY and MODE UNKNOWN are boxed in amber
  const pipeline =
    feed.lastFrame !== null && COMPUTED_FIELDS.some((field) => feed.lastFrame?.validity[field] === "NOT_JUDGED");
  const labelWidth = label.length * 11 + 16;

  return (
    <svg
      className="panel"
      viewBox={`0 0 ${PANEL.width} ${PANEL.height}`}
      role="img"
      aria-label="primary flight display"
      data-testid="pfd"
    >
      <rect width={PANEL.width} height={PANEL.height} fill={COLORS.background} />
      <g className="strip">
        {boxed ? (
          <rect x={8} y={6} width={labelWidth} height={STRIP.height - 12} rx={3} fill="none" stroke={COLORS.amber} strokeWidth={2} />
        ) : null}
        <text
          data-testid="pfd-mode"
          data-mode={feed.session?.mode ?? "UNKNOWN"}
          data-boxed={boxed ? "true" : "false"}
          x={16}
          y={STRIP.baseline}
          fontSize={18}
          fontWeight={700}
          fill={boxed ? COLORS.amber : COLORS.text}
        >
          {label}
        </text>
        <text
          data-testid="pfd-state"
          data-cause={cause ?? "none"}
          x={PANEL.width / 2}
          y={STRIP.baseline}
          textAnchor="middle"
          fontSize={18}
          fontWeight={700}
          fill={stateColor(cause, feed)}
        >
          {feedStateText(feed)}
        </text>
        {pipeline ? (
          <text data-testid="pfd-pipeline" x={560} y={STRIP.baseline} fontSize={14} fontWeight={700} fill={COLORS.amber}>
            PIPELINE
          </text>
        ) : null}
        <text data-testid="pfd-rate" x={PANEL.width - 16} y={STRIP.baseline} textAnchor="end" fontSize={14} fill={COLORS.dim}>
          {`${feed.rate} fps · ${lagLabel(feed.lagMs)}`}
        </text>
      </g>
    </svg>
  );
}
