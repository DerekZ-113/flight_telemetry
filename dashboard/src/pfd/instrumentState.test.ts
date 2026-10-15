// The one rule that decides what an instrument shows (REQ-DISP-001; TC-023
// steps 1 to 4): the value, dashes, or a flag, from the fields' verdicts and
// the feed's state, with the precedence FTS-DS-001 §2.1 and §5 state.
import { describe, expect, it } from "vitest";

import { feedCause, instrumentState } from "./instrumentState";
import { runningFeed } from "../testMessages";

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
