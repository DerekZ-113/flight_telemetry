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
import { AltitudeTape } from "./AltitudeTape";
import { AttitudeIndicator } from "./AttitudeIndicator";
import { COLORS, PANEL, STRIP } from "./geometry";
import { GroundSpeed } from "./GroundSpeed";
import { feedCause, feedStateText, instrumentState, lagLabel, trackShown } from "./instrumentState";
import type { FieldVerdict } from "./instrumentState";
import { TrackIndicator } from "./TrackIndicator";
import { VerticalSpeed } from "./VerticalSpeed";

function stateColor(cause: ReturnType<typeof feedCause>, feed: FeedState): string {
  switch (cause) {
    case "link":
    case "stale":
    case "lag":
      return COLORS.red;
    case "ended":
      return COLORS.dim;
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
  // Each instrument's state from its own fields (REQ-DISP-001): the attitude
  // from both angles, the tape and the vertical speed from the fused fields.
  // The attitude also takes the IMU channel's availability, and only that
  // (REQ-FAULT-007, FTS-DS-001 §4.1): an IMU the source does not provide
  // shows the no-source flag, while a DEGRADED IMU before the first seed
  // keeps the dashes its validity says.
  const frame = feed.lastFrame;
  const attitudeVerdicts: FieldVerdict[] = frame === null ? [] : [frame.validity.pitch_deg, frame.validity.roll_deg];
  if (frame !== null && frame.status.imu === "NOT_AVAILABLE") {
    attitudeVerdicts.push("NOT_AVAILABLE");
  }
  const attitude = instrumentState(attitudeVerdicts, feed);
  const altitude = instrumentState(frame === null ? [] : [frame.validity.fused_altitude_m], feed);
  const verticalSpeed = instrumentState(frame === null ? [] : [frame.validity.vertical_speed_mps], feed);
  // The raw instruments follow the GPS channel's status (REQ-DISP-002); the
  // track is withheld below the speed threshold besides.
  const gps = instrumentState(frame === null ? [] : [frame.status.gps], feed);
  const track = trackShown(feed);

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
      <AttitudeIndicator pitchDeg={frame?.frame.pitch_deg ?? 0} rollDeg={frame?.frame.roll_deg ?? 0} state={attitude} />
      <AltitudeTape altitudeM={frame?.frame.fused_altitude_m ?? 0} state={altitude} />
      <VerticalSpeed verticalSpeedMps={frame?.frame.vertical_speed_mps ?? 0} state={verticalSpeed} />
      <GroundSpeed groundSpeedMps={frame?.frame.ground_speed_mps ?? 0} state={gps} />
      <TrackIndicator trackDeg={frame?.frame.heading_deg ?? 0} shown={track} state={gps} />
    </svg>
  );
}
