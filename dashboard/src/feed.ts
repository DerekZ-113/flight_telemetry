// The bridge's JSON contract (pipeline/bridge.py), typed. This file mirrors
// three Python definitions and must change with them:
//
//   pipeline/frame_format.py  FRAME_FIELDS less latitude and longitude  ->  FrameFields
//   pipeline/frame_format.py  VALIDITY_NAMES, STATUS_NAMES, MODE_NAMES, STATE_NAMES  ->  the name unions
//   pipeline/bridge.py        frame_message, session_message, hello     ->  the message types
//
// A typed record is the cheapest check that the page and the bridge agree
// on the names: a field the bridge renames fails here at build time, not in
// a cell that quietly reads undefined.

export type ValidityName = "NOT_JUDGED" | "NO_COMPUTED_DATA" | "FAILURE_WARNING" | "NORMAL";
export type StatusName = "NOMINAL" | "DEGRADED" | "NO_DATA";
export type RunMode = "LIVE" | "SIMULATED" | "REPLAY";
export type StreamState = "RUNNING" | "ENDED";

export const COMPUTED_FIELDS = [
  "baro_altitude_m",
  "pitch_deg",
  "roll_deg",
  "fused_altitude_m",
  "vertical_speed_mps",
] as const;
export type ComputedField = (typeof COMPUTED_FIELDS)[number];

export const CHANNELS = ["baro", "imu", "gps"] as const;
export type Channel = (typeof CHANNELS)[number];

// Every field of the 104-byte frame but the two the bridge withholds
// (REQ-DISP-005): no latitude, no longitude, so the page has no slot that
// could ever show one.
export interface FrameFields {
  timestamp_ms: number;
  pressure_hpa: number;
  temperature_c: number;
  baro_altitude_m: number;
  accel_x: number;
  accel_y: number;
  accel_z: number;
  gyro_x: number;
  gyro_y: number;
  gyro_z: number;
  pitch_deg: number;
  roll_deg: number;
  reserved_a: number;
  gps_altitude_m: number;
  ground_speed_mps: number;
  heading_deg: number;
  fused_altitude_m: number;
  vertical_speed_mps: number;
  baro_status: number;
  imu_status: number;
  gps_status: number;
  baro_read_ok: boolean;
  imu_read_ok: boolean;
  gps_read_ok: boolean;
  baro_altitude_validity: number;
  pitch_validity: number;
  roll_validity: number;
  fused_altitude_validity: number;
  vertical_speed_validity: number;
  reserved_b: number;
}

export interface FrameMessage {
  type: "frame";
  received_at_ms: number;
  frame: FrameFields;
  validity: Record<ComputedField, ValidityName>;
  status: Record<Channel, StatusName>;
}

export interface Session {
  mode: RunMode;
  state: StreamState;
}

export interface SessionMessage extends Session {
  type: "session";
  received_at_ms: number;
}

export interface HelloMessage {
  type: "hello";
  stale_ms: number;
  period_ms: number;
  session: Session | null;
  frames_received: number;
  gaps: number;
  out_of_order: number;
}

export type BridgeMessage = FrameMessage | SessionMessage | HelloMessage;

const MESSAGE_TYPES = new Set(["frame", "session", "hello"]);

// A message off the socket, or null for anything that is not one of the
// three: a page never throws on a stray message.
export function parseMessage(text: string): BridgeMessage | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    return null;
  }
  if (typeof parsed !== "object" || parsed === null) {
    return null;
  }
  const type = (parsed as { type?: unknown }).type;
  if (typeof type !== "string" || !MESSAGE_TYPES.has(type)) {
    return null;
  }
  return parsed as BridgeMessage;
}

// The subset of WebSocket the feed uses, so a test can hand in a fake
// (fakeSocket.ts) the way the C++ tests hand the scheduler a fake clock.
export type SocketLike = Pick<WebSocket, "onopen" | "onmessage" | "onclose" | "close">;
export type SocketFactory = (url: string) => SocketLike;

// The label the page shows for a run mode (REQ-DISP-004): no mode until a
// session status has arrived, and the simulator is never LIVE.
export function modeLabel(session: Session | null): string {
  if (session === null) {
    return "MODE UNKNOWN";
  }
  switch (session.mode) {
    case "LIVE":
      return "LIVE";
    case "SIMULATED":
      return "SIM";
    case "REPLAY":
      return "REPLAY";
  }
}
