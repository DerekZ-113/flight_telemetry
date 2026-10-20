// The feed: one WebSocket to the bridge, and what the page knows from it
// (REQ-DISP-003, REQ-DISP-004, REQ-DISP-006).
//
// Staleness is judged from this hook's own clock at the moment a frame
// arrives, never from anything in the frame: timestamp_ms counts from the
// source's first frame on a clock the page does not share, and no frame
// carries an age or a sequence number. A ticker re-judges the age every
// tickMs, so a feed that stops is marked within one tick of the threshold,
// and the next frame clears the mark. A stream the publisher has ended is
// never stale: silence after an end is not a fault.
//
// Arrival time says that a frame came, not that it came on time, so the
// hook also measures the lag: its own elapsed time since the first frame
// of a session minus that frame's elapsed timestamp, two elapsed times from
// one shared event, which no pair of clocks with different epochs could
// give by subtraction. Constant while the pipeline keeps up, growing when
// frames queue anywhere between the binary and the page; the bridge's
// received_at_ms gives the same measure up to the bridge, so the two say on
// which side of it a queue is. The origin is re-taken at the first frame
// after a connection, after an ended stream, and after a timestamp that
// goes backwards (a restarted binary that never said ENDED).
//
// The socket constructor and the clock are injected with real defaults, the
// way the scheduler takes a Clock: the tests hand in a fake socket and a
// clock they advance by hand, so a second of staleness costs no time.
import { useEffect, useRef, useState } from "react";

import {
  finitePosition,
  parseMessage,
  sameIdentity,
  type BridgeMessage,
  type FrameMessage,
  type Position,
  type Session,
  type SocketFactory,
} from "./feed";

export type Connection = "connecting" | "open" | "closed";

export interface FeedOptions {
  socket?: SocketFactory; // default: a real WebSocket
  now?: () => number; // milliseconds on any monotonic clock; default performance.now
  tickMs?: number; // how often the age is re-judged
  reconnectMs?: number; // the wait before a new socket after the bridge drops
}

export interface FeedState {
  connection: Connection;
  opened: boolean; // whether a socket has ever opened (CONNECTING versus BRIDGE DISCONNECTED)
  session: Session | null; // the last session status, from hello or a session message
  staleMs: number; // from hello; the default until one arrives
  lastFrame: FrameMessage | null;
  lastArrivalMs: number | null; // this clock, when lastFrame arrived
  frameCount: number;
  rate: number; // frames whose arrival is within the trailing second
  ageMs: number | null; // now minus lastArrivalMs, re-judged on the ticker
  stale: boolean; // a frame exists, the stream has not ended, and the age is past the threshold
  ended: boolean; // the last session status says ENDED
  lagThresholdMs: number; // from hello (REQ-DISP-006); the default until one arrives
  trackMinSpeedMps: number; // from hello (REQ-DISP-002); the default until one arrives
  lagMs: number | null; // this clock's elapsed time since the lag origin minus the frame's, per frame
  bridgeLagMs: number | null; // the bridge's elapsed time since the origin minus the frame's, from received_at_ms
  lagExceeded: boolean; // the last frame's lag is past the threshold
  trail: Position[]; // one point per fresh fix since the origin, newest last, capped at TRAIL_MAX; cleared when the identity changes
}

export const DEFAULT_STALE_MS = 1000;
export const DEFAULT_LAG_MS = 500;
export const DEFAULT_TRACK_MIN_SPEED_MPS = 1.0;
export const TRAIL_MAX = 3600; // an hour of fixes at 1 Hz
const RATE_WINDOW_MS = 1000;

// The lag origin: three clocks read at one event, the first frame of a
// session, so that every later frame's elapsed times are comparable.
interface LagOrigin {
  arrivalMs: number;
  receivedAtMs: number;
  timestampMs: number;
}

// The hook's whole state; the origin is its own business and not the page's.
interface FeedInternal extends FeedState {
  lagOrigin: LagOrigin | null;
}

const defaultSocket: SocketFactory = (url) => new WebSocket(url);
const defaultNow = (): number => performance.now();

const initialState: FeedInternal = {
  connection: "connecting",
  opened: false,
  session: null,
  staleMs: DEFAULT_STALE_MS,
  lastFrame: null,
  lastArrivalMs: null,
  frameCount: 0,
  rate: 0,
  ageMs: null,
  stale: false,
  ended: false,
  lagThresholdMs: DEFAULT_LAG_MS,
  trackMinSpeedMps: DEFAULT_TRACK_MIN_SPEED_MPS,
  lagMs: null,
  bridgeLagMs: null,
  lagExceeded: false,
  trail: [],
  lagOrigin: null,
};

// Arrival times older than the window are dropped; what remains is the rate.
// The window is closed on its old side, so a frame that arrived exactly one
// second ago still counts: fifty frames at 20 ms judged on the grid read 50.
function pruneAndCount(arrivals: number[], nowMs: number): number {
  while (arrivals.length > 0 && arrivals[0] < nowMs - RATE_WINDOW_MS) {
    arrivals.shift();
  }
  return arrivals.length;
}

function judge(state: FeedInternal, nowMs: number, rate: number): FeedInternal {
  const ageMs = state.lastArrivalMs === null ? null : nowMs - state.lastArrivalMs;
  const stale = !state.ended && ageMs !== null && ageMs > state.staleMs;
  return { ...state, rate, ageMs, stale };
}

// The trail keeps the newest TRAIL_MAX fixes: an hour at the receiver's rate.
function appendCapped(trail: Position[], point: Position): Position[] {
  const kept = trail.length < TRAIL_MAX ? trail : trail.slice(trail.length - TRAIL_MAX + 1);
  return [...kept, point];
}

function applyFrame(state: FeedInternal, message: FrameMessage, nowMs: number): FeedInternal {
  const timestampMs = message.frame.timestamp_ms;
  const wentBackwards = state.lastFrame !== null && timestampMs < state.lastFrame.frame.timestamp_ms;
  // A new session: no origin yet (the first frame after a connection or
  // after ENDED), or a timestamp before its predecessor's. Its first frame
  // is the origin and the trail starts over.
  const origin: LagOrigin =
    state.lagOrigin === null || wentBackwards
      ? { arrivalMs: nowMs, receivedAtMs: message.received_at_ms, timestampMs }
      : state.lagOrigin;
  const newSession = origin !== state.lagOrigin;
  const frameElapsedMs = timestampMs - origin.timestampMs;
  const lagMs = nowMs - origin.arrivalMs - frameElapsedMs;
  const bridgeLagMs = message.received_at_ms - origin.receivedAtMs - frameElapsedMs;
  const previous = newSession ? [] : state.trail;
  // A fix whose position arrived as null (REQ-DISP-005) adds no point.
  const point = finitePosition(message.position);
  const trail = message.frame.gps_read_ok && point !== null ? appendCapped(previous, point) : previous;
  return {
    ...state,
    lastFrame: message,
    lastArrivalMs: nowMs,
    frameCount: state.frameCount + 1,
    lagOrigin: origin,
    lagMs,
    bridgeLagMs,
    lagExceeded: lagMs > state.lagThresholdMs,
    trail,
  };
}

function applyMessage(state: FeedInternal, message: BridgeMessage, nowMs: number, rate: number): FeedInternal {
  switch (message.type) {
    case "hello": {
      const session = message.session === null ? null : { ...message.session, identity: message.session.identity ?? null };
      return judge(
        {
          ...state,
          staleMs: message.stale_ms,
          lagThresholdMs: message.lag_ms,
          trackMinSpeedMps: message.track_min_speed_mps,
          session,
          ended: session?.state === "ENDED",
        },
        nowMs,
        rate,
      );
    }
    case "session": {
      const session: Session = {
        mode: message.mode,
        state: message.state,
        identity: message.identity ?? null,
        source_mode: message.source_mode,
      };
      const ended = session.state === "ENDED";
      // An end closes the lag origin: the next frame is a new session's
      // first. The trail stays drawn until that frame arrives. A new
      // aircraft is a new track (REQ-DISP-007): the status that names it
      // precedes the first frame carrying it, and the trail is cleared at
      // once, so no point of the previous aircraft joins the next one's.
      const identityChanged = !sameIdentity(state.session?.identity ?? null, session.identity);
      return judge(
        { ...state, session, ended, lagOrigin: ended ? null : state.lagOrigin, trail: identityChanged ? [] : state.trail },
        nowMs,
        rate,
      );
    }
    case "frame":
      return judge(applyFrame(state, message, nowMs), nowMs, rate);
  }
}

export function useTelemetryFeed(url: string, options: FeedOptions = {}): FeedState {
  const { socket: makeSocket = defaultSocket, now = defaultNow, tickMs = 100, reconnectMs = 1000 } = options;
  const [state, setState] = useState<FeedInternal>(initialState);
  // The arrival times behind the rate, kept outside React state and touched
  // only outside the updaters, which React may run twice.
  const arrivals = useRef<number[]>([]);

  useEffect(() => {
    let unmounted = false;
    let socket: ReturnType<SocketFactory> | null = null;
    let reconnect: ReturnType<typeof setTimeout> | null = null;

    const open = (): void => {
      const next = makeSocket(url);
      socket = next;
      // A new socket is a new session for the lag: its first frame is the origin.
      setState((s) => ({ ...s, connection: "connecting", lagOrigin: null }));
      next.onopen = () => {
        setState((s) => ({ ...s, connection: "open", opened: true }));
      };
      next.onmessage = (event) => {
        const message = parseMessage(String(event.data));
        if (message === null) {
          return; // not one of the bridge's: ignored, never thrown
        }
        const nowMs = now(); // the arrival time, this clock, before anything else
        if (message.type === "frame") {
          arrivals.current.push(nowMs);
        }
        const rate = pruneAndCount(arrivals.current, nowMs);
        setState((s) => applyMessage(s, message, nowMs, rate));
      };
      next.onclose = () => {
        setState((s) => ({ ...s, connection: "closed" }));
        if (!unmounted) {
          reconnect = setTimeout(open, reconnectMs);
        }
      };
    };

    open();
    const ticker = setInterval(() => {
      const nowMs = now();
      const rate = pruneAndCount(arrivals.current, nowMs);
      setState((s) => judge(s, nowMs, rate));
    }, tickMs);

    return () => {
      unmounted = true;
      clearInterval(ticker);
      if (reconnect !== null) {
        clearTimeout(reconnect);
      }
      socket?.close();
    };
  }, [url, makeSocket, now, tickMs, reconnectMs]);

  return state;
}
