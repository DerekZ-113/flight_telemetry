// What an instrument shows (REQ-DISP-001, FTS-DS-001 §2.1 and §5): the
// value, dashes, or a flag naming the instrument, decided by one function
// that every instrument calls. The feed's state comes first, because a
// stale, lagging, ended or disconnected feed withdraws every value whatever
// the fields say; then the most severe verdict among the instrument's
// fields: a failure warning (or a DEGRADED channel) is a flag, "not judged"
// is dashes under the PIPELINE annunciator, a channel the source does not
// provide (NOT_AVAILABLE, REQ-FAULT-007) is the no-source flag, "no data" is
// dashes, NORMAL (or NOMINAL) is the value. A removed value is never struck
// through on an instrument: a struck-through altitude is still an altitude
// a pilot can read, so the number goes and a flag takes its place.
import { badgeFor } from "../badge";
import { VOTER_BITS } from "../feed";
import type { StatusName, ValidityName } from "../feed";
import type { FeedState } from "../useTelemetryFeed";

export type FieldVerdict = ValidityName | StatusName;

// Why a value was withdrawn: the feed's state, a field's own failure, or a
// channel the source does not provide (nothing failed: the flag is an
// outline, and the strip names no cause).
export type FeedCause = "link" | "ended" | "stale" | "lag";
export type FlagCause = FeedCause | "fail" | "unavailable";

export interface InstrumentState {
  show: "value" | "dashes" | "flag";
  cause?: FlagCause;
  pipeline?: true;
}

// The feed-level cause that removes every instrument, in order of
// precedence (FTS-DS-001 §5), or null while the feed is good. Nothing is
// withdrawn before a frame has arrived: dashes need no flag.
export function feedCause(feed: FeedState): FeedCause | null {
  if (feed.lastFrame === null) {
    return null;
  }
  if (feed.connection !== "open") {
    return "link";
  }
  if (feed.ended) {
    return "ended";
  }
  if (feed.stale) {
    return "stale";
  }
  if (feed.lagExceeded) {
    return "lag";
  }
  return null;
}

// Worst first: a failure outranks "not judged", which outranks "no source",
// which outranks "no data" (FTS-DS-001 §2.1).
const SEVERITY: Record<FieldVerdict, number> = {
  FAILURE_WARNING: 3,
  DEGRADED: 3,
  NOT_JUDGED: 2,
  NOT_AVAILABLE: 1.5,
  NO_COMPUTED_DATA: 1,
  NO_DATA: 1,
  NORMAL: 0,
  NOMINAL: 0,
};

export function instrumentState(verdicts: FieldVerdict[], feed: FeedState): InstrumentState {
  if (feed.lastFrame === null || verdicts.length === 0) {
    return { show: "dashes" };
  }
  const cause = feedCause(feed);
  if (cause !== null) {
    return { show: "flag", cause };
  }
  const worst = verdicts.reduce((a, b) => (SEVERITY[b] > SEVERITY[a] ? b : a));
  switch (worst) {
    case "FAILURE_WARNING":
    case "DEGRADED":
      return { show: "flag", cause: "fail" };
    case "NOT_JUDGED":
      return { show: "dashes", pipeline: true };
    case "NOT_AVAILABLE":
      return { show: "flag", cause: "unavailable" };
    case "NO_COMPUTED_DATA":
    case "NO_DATA":
      return { show: "dashes" };
    case "NORMAL":
    case "NOMINAL":
      return { show: "value" };
  }
}

// The track is shown only while the GPS channel is NOMINAL and the ground
// speed is at or above the threshold hello named (REQ-DISP-002): a
// stationary receiver's course is noise, so below the threshold the track
// indicator reads dashes and draws no rose, with no flag, since nothing
// failed.
export function trackShown(feed: FeedState): boolean {
  const frame = feed.lastFrame;
  return frame !== null && frame.status.gps === "NOMINAL" && frame.frame.ground_speed_mps >= feed.trackMinSpeedMps;
}

// Which of the three lanes are in the vote, from the voter's status
// (REQ-RED-003): a lane is out when its FAULTED bit is set.
export function lanesIn(voterStatus: number): boolean[] {
  return [VOTER_BITS.LANE_1_FAULTED, VOTER_BITS.LANE_2_FAULTED, VOTER_BITS.LANE_3_FAULTED].map(
    (bit) => (voterStatus & bit) === 0,
  );
}

// What the voter panel shows (REQ-DISP-008, FTS-DS-001 §4.7): the feed's
// cause first, as for every instrument, the lanes removed under the VOTE
// flag; then the voter's status. Zero means no voter ran (a frame from a
// build before the voter): dashes, and no pipeline annunciator, since zero
// is a defined state and not an unjudged one. Otherwise the three lanes, an
// excluded one marked, and under NO_MAJORITY the red VOTE flag below them:
// the lanes stay, because which lanes are in is the voter's latched state,
// not a voted value.
export interface VoterState {
  lanes: boolean[] | null; // the cells, true for a lane in the vote; null when none are drawn
  flag: FlagCause | null; // the VOTE flag's cause, or no flag
  dashes: boolean; // nothing to show: no frame yet, or no voter ran
}

export function voterState(feed: FeedState): VoterState {
  const frame = feed.lastFrame;
  if (frame === null) {
    return { lanes: null, flag: null, dashes: true };
  }
  const cause = feedCause(feed);
  if (cause !== null) {
    return { lanes: null, flag: cause, dashes: false };
  }
  const bits = frame.frame.voter_status;
  if (bits === 0) {
    return { lanes: null, flag: null, dashes: true };
  }
  const noMajority = (bits & VOTER_BITS.NO_MAJORITY) !== 0;
  return { lanes: lanesIn(bits), flag: noMajority ? "fail" : null, dashes: false };
}

// The strip's word for the feed, with its cause's measure: the badge's
// text, except that a running feed past the lag threshold reads LAG with
// the lag and a stale one reads STALE with the age (FTS-DS-001 §5).
export function feedStateText(feed: FeedState): string {
  const badge = badgeFor(feed);
  if (badge.kind === "running" && feed.lagExceeded) {
    return `LAG ${seconds(feed.lagMs ?? 0)}`;
  }
  if (badge.kind === "stale") {
    return `STALE ${seconds(feed.ageMs ?? 0)}`;
  }
  return badge.text;
}

export function lagLabel(lagMs: number | null): string {
  return lagMs === null ? "lag ---" : `lag ${Math.round(lagMs)} ms`;
}

function seconds(ms: number): string {
  return `${(ms / 1000).toFixed(1)} s`;
}
