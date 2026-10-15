// One word for the feed's state (REQ-DISP-003, REQ-DISP-004), in order of
// what matters most: a bridge that is gone, a stream that has ended (never
// stale, however old the last frame), a feed with nothing yet, a feed that
// went quiet, a feed running. A module of its own so App.tsx exports only a
// component: React's Fast Refresh then hot-swaps the view on an edit and
// keeps the feed's state, instead of reloading the page and losing it.
import type { FeedState } from "./useTelemetryFeed";

export interface Badge {
  kind: "connecting" | "disconnected" | "waiting" | "running" | "stale" | "ended";
  text: string;
  detail: string;
}

export function badgeFor(feed: FeedState): Badge {
  const lastFrame = feed.lastFrame === null ? "no frame received" : `last frame t=${feed.lastFrame.frame.timestamp_ms} ms`;
  if (feed.connection !== "open") {
    return feed.opened
      ? { kind: "disconnected", text: "BRIDGE DISCONNECTED", detail: "reconnecting" }
      : { kind: "connecting", text: "CONNECTING", detail: "" };
  }
  if (feed.ended) {
    return { kind: "ended", text: "ENDED", detail: lastFrame };
  }
  if (feed.lastFrame === null) {
    return { kind: "waiting", text: "WAITING", detail: "no frame yet" };
  }
  if (feed.stale) {
    return { kind: "stale", text: "STALE", detail: `last frame ${((feed.ageMs ?? 0) / 1000).toFixed(1)} s ago` };
  }
  return { kind: "running", text: "RUNNING", detail: lastFrame };
}
