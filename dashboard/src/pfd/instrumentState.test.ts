// The one rule that decides what an instrument shows (REQ-DISP-001; TC-023
// steps 1 to 4): the value, dashes, or a flag, from the fields' verdicts and
// the feed's state, with the precedence FTS-DS-001 §2.1 and §5 state.
import { describe, expect, it } from "vitest";

import { feedCause, instrumentState, lanesIn, voterState } from "./instrumentState";
import { frame, runningFeed } from "../testMessages";

describe("instrumentState", () => {
  it("follows a computed field's validity (step 1)", () => {
    const feed = runningFeed();
    expect(instrumentState(["NORMAL"], feed)).toEqual({ show: "value" });
    expect(instrumentState(["NO_COMPUTED_DATA"], feed)).toEqual({ show: "dashes" });
    expect(instrumentState(["FAILURE_WARNING"], feed)).toEqual({ show: "flag", cause: "fail" });
    expect(instrumentState(["NOT_JUDGED"], feed)).toEqual({ show: "dashes", pipeline: true });
  });

  it("follows a raw instrument's channel status (step 2)", () => {
    const feed = runningFeed();
    expect(instrumentState(["NOMINAL"], feed)).toEqual({ show: "value" });
    expect(instrumentState(["NO_DATA"], feed)).toEqual({ show: "dashes" });
    expect(instrumentState(["DEGRADED"], feed)).toEqual({ show: "flag", cause: "fail" });
    // A channel the source does not provide (REQ-FAULT-007): the no-source flag, not the failure flag.
    expect(instrumentState(["NOT_AVAILABLE"], feed)).toEqual({ show: "flag", cause: "unavailable" });
  });

  it("ranks the no-source flag below a failure and a pipeline fault and above dashes (step 2)", () => {
    const feed = runningFeed();
    // The attitude indicator beside an IMU the source does not provide: its validities say "never
    // existed", the channel says "never will"; the channel's word wins over dashes.
    expect(instrumentState(["NO_COMPUTED_DATA", "NO_COMPUTED_DATA", "NOT_AVAILABLE"], feed)).toEqual({
      show: "flag",
      cause: "unavailable",
    });
    expect(instrumentState(["NOT_JUDGED", "NOT_AVAILABLE"], feed)).toEqual({ show: "dashes", pipeline: true });
    expect(instrumentState(["FAILURE_WARNING", "NOT_AVAILABLE"], feed)).toEqual({ show: "flag", cause: "fail" });
    expect(instrumentState(["NOT_AVAILABLE"], runningFeed({ stale: true }))).toEqual({ show: "flag", cause: "stale" });
  });

  it("shows the most severe of several fields, failure warning first (step 3)", () => {
    const feed = runningFeed();
    expect(instrumentState(["NORMAL", "FAILURE_WARNING"], feed)).toEqual({ show: "flag", cause: "fail" });
    expect(instrumentState(["NORMAL", "NOT_JUDGED"], feed)).toEqual({ show: "dashes", pipeline: true });
    expect(instrumentState(["NOT_JUDGED", "NO_COMPUTED_DATA"], feed)).toEqual({ show: "dashes", pipeline: true });
    expect(instrumentState(["FAILURE_WARNING", "NOT_JUDGED"], feed)).toEqual({ show: "flag", cause: "fail" });
    expect(instrumentState(["NORMAL", "NORMAL"], feed)).toEqual({ show: "value" });
  });

  it("removes every instrument under the feed's cause, in order of precedence (step 4)", () => {
    expect(feedCause(runningFeed())).toBeNull();
    expect(instrumentState(["NORMAL"], runningFeed({ stale: true }))).toEqual({ show: "flag", cause: "stale" });
    expect(instrumentState(["NORMAL"], runningFeed({ ended: true }))).toEqual({ show: "flag", cause: "ended" });
    expect(instrumentState(["NORMAL"], runningFeed({ lagExceeded: true }))).toEqual({ show: "flag", cause: "lag" });
    expect(instrumentState(["NORMAL"], runningFeed({ connection: "closed" }))).toEqual({ show: "flag", cause: "link" });
    // Several at once: the bridge gone outranks an end, an end outranks stale, stale outranks lag.
    const everything = runningFeed({ connection: "closed", ended: true, stale: true, lagExceeded: true });
    expect(feedCause(everything)).toBe("link");
    expect(feedCause(runningFeed({ ended: true, stale: true, lagExceeded: true }))).toBe("ended");
    expect(feedCause(runningFeed({ stale: true, lagExceeded: true }))).toBe("stale");
    // A feed-level cause removes a value whatever the field's own verdict says.
    expect(instrumentState(["NO_COMPUTED_DATA"], runningFeed({ stale: true }))).toEqual({ show: "flag", cause: "stale" });
  });

  it("shows dashes while no frame has arrived, whatever the feed says", () => {
    for (const feed of [
      runningFeed({ lastFrame: null }),
      runningFeed({ lastFrame: null, connection: "connecting", opened: false }),
      runningFeed({ lastFrame: null, connection: "closed" }),
      runningFeed({ lastFrame: null, ended: true }),
    ]) {
      expect(feedCause(feed)).toBeNull();
      expect(instrumentState(["NORMAL"], feed)).toEqual({ show: "dashes" });
    }
    expect(instrumentState([], runningFeed())).toEqual({ show: "dashes" });
  });
});

// The voter panel's rule (REQ-DISP-008; TC-032 step 10, TC-033 step 8): the
// feed's cause first, then the voter's status as format version 3 defines
// its bits.
describe("voterState", () => {
  it("reads which lanes are in the vote from the FAULTED bits", () => {
    expect(lanesIn(0x0001)).toEqual([true, true, true]);
    expect(lanesIn(0x0003)).toEqual([false, true, true]);
    expect(lanesIn(0x0005)).toEqual([true, false, true]);
    expect(lanesIn(0x0009)).toEqual([true, true, false]);
    expect(lanesIn(0x0015)).toEqual([true, false, true]);
  });

  it("shows the lanes while voting, the fail flag with the lanes under no majority, dashes for zero", () => {
    const at = (bits: number) => voterState(runningFeed({ lastFrame: frame(0, {}, { voter_status: bits }) }));
    expect(at(0x0001)).toEqual({ lanes: [true, true, true], flag: null, dashes: false });
    expect(at(0x0009)).toEqual({ lanes: [true, true, false], flag: null, dashes: false });
    expect(at(0x0011)).toEqual({ lanes: [true, true, true], flag: "fail", dashes: false });
    expect(at(0x0015)).toEqual({ lanes: [true, false, true], flag: "fail", dashes: false });
    expect(at(0)).toEqual({ lanes: null, flag: null, dashes: true });
  });

  it("puts the feed's cause before the voter's status, and dashes before any frame", () => {
    const lastFrame = frame(0, {}, { voter_status: 0x0001 });
    expect(voterState(runningFeed({ lastFrame, stale: true }))).toEqual({ lanes: null, flag: "stale", dashes: false });
    expect(voterState(runningFeed({ lastFrame, ended: true }))).toEqual({ lanes: null, flag: "ended", dashes: false });
    expect(voterState(runningFeed({ lastFrame: null }))).toEqual({ lanes: null, flag: null, dashes: true });
  });
});
