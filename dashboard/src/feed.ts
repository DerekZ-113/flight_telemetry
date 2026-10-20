// The bridge's JSON contract (pipeline/bridge.py), typed. This file mirrors
// three Python definitions and must change with them:
//
//   pipeline/frame_format.py  FRAME_FIELDS less latitude and longitude  ->  FrameFields
//   pipeline/frame_format.py  VALIDITY_NAMES, STATUS_NAMES, MODE_NAMES, STATE_NAMES  ->  the name unions
//   pipeline/frame_format.py  VOTER_STATUS_NAMES                         ->  VOTER_BITS
//   pipeline/bridge.py        frame_message, session_message, hello     ->  the message types
//   pipeline/bridge.py        wire_text: null for a non-finite value     ->  WireNumber, WirePosition
//
// A typed record is the cheapest check that the page and the bridge agree
// on the names: a field the bridge renames fails here at build time, not in
// a cell that quietly reads undefined.

export type ValidityName = "NOT_JUDGED" | "NO_COMPUTED_DATA" | "FAILURE_WARNING" | "NORMAL";
export type StatusName = "NOMINAL" | "DEGRADED" | "NO_DATA" | "NOT_AVAILABLE";
export type RunMode = "LIVE" | "SIMULATED" | "REPLAY" | "ADSB";
// How the frames were produced (REQ-LOG-007): the run's own mode, except in a
// replay, which names its recording's; UNKNOWN for a log older than format
// version 3, whose source nobody recorded.
export type SourceMode = RunMode | "UNKNOWN";
export type StreamState = "RUNNING" | "ENDED";

export const COMPUTED_FIELDS = [
  "baro_altitude_m",
  "pitch_deg",
  "roll_deg",
  "fused_altitude_m",
  "vertical_speed_mps",
] as const;
export type ComputedField = (typeof COMPUTED_FIELDS)[number];

// The voter's status bits (REQ-RED-002 to REQ-RED-004; src/telemetry_frame.h,
// VoterStatus), the twin of VOTER_STATUS_NAMES: the bridge forwards the
// number by name in every frame, and the page decodes it here, once.
export const VOTER_BITS = {
  VOTED: 0x0001,
  LANE_1_FAULTED: 0x0002,
  LANE_2_FAULTED: 0x0004,
  LANE_3_FAULTED: 0x0008,
  NO_MAJORITY: 0x0010,
} as const;

export const CHANNELS = ["baro", "imu", "gps"] as const;
export type Channel = (typeof CHANNELS)[number];

// A number as the bridge sends one. JSON has no NaN and no infinity, so a
// value that is not a finite number arrives as null (REQ-DISP-005), with the
// channel status or the validity beside it saying why. Every float of the
// frame and of the position is typed with it, so the compiler finds each
// place a null must be handled; the counters, statuses, bits and flags are
// integers and never null.
export type WireNumber = number | null;

// Every field of the 120-byte frame (format version 3) but the two the bridge withholds
// (REQ-DISP-005): no latitude, no longitude, so the page has no slot that
// could ever show one.
export interface FrameFields {
  timestamp_ms: number;
  pressure_hpa: WireNumber;
  temperature_c: WireNumber;
  baro_altitude_m: WireNumber;
  accel_x: WireNumber;
  accel_y: WireNumber;
  accel_z: WireNumber;
  gyro_x: WireNumber;
  gyro_y: WireNumber;
  gyro_z: WireNumber;
  pitch_deg: WireNumber;
  roll_deg: WireNumber;
  sequence: number; // the source's count of its frames, from 0 (REQ-LOG-008)
  gps_altitude_m: WireNumber;
  ground_speed_mps: WireNumber;
  course_deg: WireNumber; // the course over ground, degrees true (heading_deg until format version 3)
  fused_altitude_m: WireNumber;
  vertical_speed_mps: WireNumber;
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
  flags: number; // FrameFlag bits: NEW_VEHICLE, VERTICAL_RATE, PRESSURE_ALTITUDE, FIX_QUALITY
  gps_vertical_rate_mps: WireNumber; // present only under the VERTICAL_RATE bit (REQ-SENS-008)
  gps_pressure_altitude_m: WireNumber; // present only under the PRESSURE_ALTITUDE bit (REQ-SENS-008)
  gps_fix_quality: number; // present only under the FIX_QUALITY bit (REQ-SENS-005)
  gps_satellites: number;
  voter_status: number; // VoterStatus bits (VOTER_BITS); zero: no voter ran
  reserved_d: number;
}

// A position relative to the first fix the bridge saw, in metres
// (REQ-DISP-005): a displacement reveals a shape, not a place.
export interface Position {
  east_m: number;
  north_m: number;
}

// The position as it arrives: null before the bridge has an origin, and a
// coordinate null when the fix it was computed from was not a number.
export interface WirePosition {
  east_m: WireNumber;
  north_m: WireNumber;
}

// The position, when both of its coordinates are numbers; null otherwise.
export function finitePosition(position: WirePosition | null): Position | null {
  if (position === null || position.east_m === null || position.north_m === null) {
    return null;
  }
  return { east_m: position.east_m, north_m: position.north_m };
}

export interface FrameMessage {
  type: "frame";
  received_at_ms: number;
  frame: FrameFields;
  validity: Record<ComputedField, ValidityName>;
  status: Record<Channel, StatusName>;
  position: WirePosition | null;
}

// The aircraft an ADS-B run presents (REQ-DISP-007): its ICAO address as
// six hex digits and its callsign, which may be empty. Null when nothing is
// selected, in every other mode, and when the bridge hides it.
export interface Identity {
  icao24: string;
  callsign: string;
}

export interface Session {
  mode: RunMode;
  state: StreamState;
  identity: Identity | null;
  source_mode: SourceMode; // since status version 3
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
  track_min_speed_mps: number; // the ground speed under which the track is withheld (REQ-DISP-002)
  lag_ms: number; // the lag past which the instruments are removed (REQ-DISP-006)
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
    case "ADSB":
      return "ADS-B";
  }
}

// Whether two identities name the same aircraft; two nulls do.
export function sameIdentity(a: Identity | null, b: Identity | null): boolean {
  if (a === null || b === null) {
    return a === b;
  }
  return a.icao24 === b.icao24 && a.callsign === b.callsign;
}

// Whether the frames came from the ADS-B source, live or replayed
// (REQ-LOG-007): the tape's datum, a replay's identity statement and the
// data license's attribution follow the data, not the run (REQ-DISP-007).
export function adsbSource(session: Session | null): boolean {
  return session?.source_mode === "ADSB";
}

// What follows the mode label (REQ-DISP-007, FTS-DS-001 §5): in ADS-B mode
// the aircraft the instruments show, its callsign, or its address when no
// callsign is reported, or dashes when none is selected (or hidden); in a
// REPLAY of an ADS-B recording a statement that the identity is
// unavailable, because a replay carries none and a blank would read as
// "none selected"; nothing after any other replay, which never had an
// identity to lose, nor after LIVE and SIM, which are own-ship.
export function identityLabel(session: Session | null): string | null {
  if (session === null) {
    return null;
  }
  switch (session.mode) {
    case "ADSB":
      if (session.identity === null) {
        return "---";
      }
      return session.identity.callsign !== "" ? session.identity.callsign : session.identity.icao24;
    case "REPLAY":
      return adsbSource(session) ? "ID UNAVAILABLE" : null;
    case "LIVE":
    case "SIMULATED":
      return null;
  }
}

// The strip's text: the label, and the identity after a middle dot.
export function stripLabel(session: Session | null): string {
  const identity = identityLabel(session);
  return identity === null ? modeLabel(session) : `${modeLabel(session)} \u00b7 ${identity}`;
}
