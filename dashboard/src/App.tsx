// The page (part 1 of the Primary Flight Display): frames arriving. The
// mode label and the feed's state (REQ-DISP-003, REQ-DISP-004), the rate,
// the last frame, and each computed field with its validity by name beside
// the number (REQ-PROC-007: a display tells a value from its absence by the
// flag, never by the number). No instrument yet: part 2 builds those on
// this feed. No position, by construction: the bridge sends none.
import type { JSX } from "react";

import { badgeFor } from "./badge";
import { CHANNELS, COMPUTED_FIELDS, modeLabel, type Channel, type ComputedField, type ValidityName } from "./feed";
import { useTelemetryFeed, type FeedOptions } from "./useTelemetryFeed";

const FIELD_LABELS: Record<ComputedField, string> = {
  baro_altitude_m: "Baro altitude (m)",
  pitch_deg: "Pitch (deg)",
  roll_deg: "Roll (deg)",
  fused_altitude_m: "Fused altitude (m)",
  vertical_speed_mps: "Vertical speed (m/s)",
};

const CHANNEL_LABELS: Record<Channel, string> = { baro: "BARO", imu: "IMU", gps: "GPS" };

// What the cell shows for a value under each verdict: the number while the
// value is NORMAL or flagged, dashes while no value exists or none was judged.
function renderValue(value: number | null, validity: ValidityName | null): string {
  if (value === null || validity === null) {
    return "---";
  }
  switch (validity) {
    case "NORMAL":
    case "FAILURE_WARNING":
      return value.toFixed(2);
    case "NO_COMPUTED_DATA":
    case "NOT_JUDGED":
      return "---";
  }
}

export function App({ url, options }: { url: string; options?: FeedOptions }): JSX.Element {
  const feed = useTelemetryFeed(url, options);
  const badge = badgeFor(feed);
  const stale = feed.stale ? " stale" : "";

  return (
    <main className="pfd">
      <header className="bar">
        <span className="mode" data-mode={feed.session?.mode ?? "UNKNOWN"}>
          {modeLabel(feed.session)}
        </span>
        <span className={`badge badge-${badge.kind}`} data-badge={badge.kind}>
          {badge.text}
        </span>
        <span className="detail" data-testid={badge.kind === "stale" ? "age" : "detail"}>
          {badge.detail}
        </span>
      </header>

      <section className="counters">
        <div>
          <span className="label">rate</span>
          <span className="number" data-testid="rate">
            {feed.rate}
          </span>
          <span className="unit">frames/s</span>
        </div>
        <div>
          <span className="label">frames</span>
          <span className="number" data-testid="frames">
            {feed.frameCount}
          </span>
        </div>
        <div>
          <span className="label">last frame</span>
          <span className="number" data-testid="last-frame">
            {feed.lastFrame === null ? "---" : `t=${feed.lastFrame.frame.timestamp_ms} ms`}
          </span>
        </div>
      </section>

      <table className={`fields${stale}`}>
        <tbody>
          {COMPUTED_FIELDS.map((field) => {
            const validity = feed.lastFrame?.validity[field] ?? null;
            const value = feed.lastFrame?.frame[field] ?? null;
            return (
              <tr
                key={field}
                data-field={field}
                data-validity={validity ?? "NONE"}
                className={`validity-${validity ?? "NONE"}${stale}`}
              >
                <th scope="row">{FIELD_LABELS[field]}</th>
                <td className="value">{renderValue(value, validity)}</td>
                <td className="verdict">
                  <span className="name">{validity ?? "---"}</span>
                  {validity === "NOT_JUDGED" ? <span className="annunciator">PIPELINE</span> : null}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>

      <section className="channels">
        {CHANNELS.map((channel) => {
          const status = feed.lastFrame?.status[channel] ?? null;
          const fresh = feed.lastFrame?.frame[`${channel}_read_ok`] ?? null;
          return (
            <div key={channel} className={`channel status-${status ?? "NONE"}${stale}`} data-testid={`status-${channel}`}>
              <span className="name">{CHANNEL_LABELS[channel]}</span>
              <span className="status">{status ?? "---"}</span>
              <span className="fresh">{fresh === null ? "" : fresh ? "fresh" : "held"}</span>
            </div>
          );
        })}
      </section>
    </main>
  );
}
