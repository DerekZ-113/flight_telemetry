// The feed: one WebSocket to the bridge, and what the page knows from it
// (REQ-DISP-003, REQ-DISP-004).
//
// Staleness is judged from this hook's own clock at the moment a frame
// arrives, never from anything in the frame: timestamp_ms counts from the
// source's first frame on a clock the page does not share, and no frame
// carries an age or a sequence number. A ticker re-judges the age every
// tickMs, so a feed that stops is marked within one tick of the threshold,
// and the next frame clears the mark. A stream the publisher has ended is
// never stale: silence after an end is not a fault.
//
// The socket constructor and the clock are injected with real defaults, the
// way the scheduler takes a Clock: the tests hand in a fake socket and a
// clock they advance by hand, so a second of staleness costs no time.
import { useEffect, useRef, useState } from "react";

import { parseMessage, type BridgeMessage, type FrameMessage, type Session, type SocketFactory } from "./feed";

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
}

export const DEFAULT_STALE_MS = 1000;
const RATE_WINDOW_MS = 1000;

const defaultSocket: SocketFactory = (url) => new WebSocket(url);
const defaultNow = (): number => performance.now();

const initialState: FeedState = {
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

function judge(state: FeedState, nowMs: number, rate: number): FeedState {
  const ageMs = state.lastArrivalMs === null ? null : nowMs - state.lastArrivalMs;
  const stale = !state.ended && ageMs !== null && ageMs > state.staleMs;
  return { ...state, rate, ageMs, stale };
}

function applyMessage(state: FeedState, message: BridgeMessage, nowMs: number, rate: number): FeedState {
  switch (message.type) {
    case "hello": {
      const session = message.session;
      return judge({ ...state, staleMs: message.stale_ms, session, ended: session?.state === "ENDED" }, nowMs, rate);
    }
    case "session": {
      const session: Session = { mode: message.mode, state: message.state };
      return judge({ ...state, session, ended: session.state === "ENDED" }, nowMs, rate);
    }
    case "frame":
      return judge(
        { ...state, lastFrame: message, lastArrivalMs: nowMs, frameCount: state.frameCount + 1 },
        nowMs,
        rate,
      );
  }
}

export function useTelemetryFeed(url: string, options: FeedOptions = {}): FeedState {
  const { socket: makeSocket = defaultSocket, now = defaultNow, tickMs = 100, reconnectMs = 1000 } = options;
  const [state, setState] = useState<FeedState>(initialState);
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
      setState((s) => ({ ...s, connection: "connecting" }));
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
