// Messages the tests deliver, built the way the bridge builds them, and a
// feed state built by hand for the instruments' tests.
import type {
  ComputedField,
  FrameMessage,
  HelloMessage,
  Identity,
  Session,
  SessionMessage,
  SourceMode,
  ValidityName,
} from "./feed";
import type { FeedState } from "./useTelemetryFeed";

export const PERIOD_MS = 20;
export const STALE_MS = 1000;
export const LAG_MS = 500;
export const TRACK_MIN_SPEED_MPS = 1.0;

const VALIDITY_BYTES: Record<ValidityName, number> = {
  NOT_JUDGED: 0,
  NO_COMPUTED_DATA: 1,
  FAILURE_WARNING: 2,
  NORMAL: 3,
};

export function hello(session: Session | null = null, staleMs = STALE_MS, extra: Partial<HelloMessage> = {}): HelloMessage {
  return {
    type: "hello",
    stale_ms: staleMs,
    period_ms: PERIOD_MS,
    session,
    frames_received: 0,
    gaps: 0,
    out_of_order: 0,
    track_min_speed_mps: TRACK_MIN_SPEED_MPS,
    lag_ms: LAG_MS,
    ...extra,
  };
}

// A status as the bridge forwards one: the source mode is the run's own
// unless given, and a replay's is UNKNOWN unless given, as for a log older
// than format version 3 (REQ-LOG-007).
export function session(
  mode: Session["mode"],
  state: Session["state"] = "RUNNING",
  identity: Identity | null = null,
  sourceMode: SourceMode = mode === "REPLAY" ? "UNKNOWN" : mode,
): SessionMessage {
  return { type: "session", mode, state, identity, source_mode: sourceMode, received_at_ms: 0 };
}

// The two synthetic aircraft the tests show (nothing from the service).
export const TEST001: Identity = { icao24: "a1b2c3", callsign: "TEST001" };
export const TEST002: Identity = { icao24: "b2c3d4", callsign: "TEST002" };

// A processed frame on the 20 ms grid: every validity NORMAL and every
// channel NOMINAL and fresh unless overridden, no coordinates anywhere.
export function frame(
  index: number,
  validity: Partial<Record<ComputedField, ValidityName>> = {},
  values: Partial<FrameMessage["frame"]> = {},
): FrameMessage {
  const names: Record<ComputedField, ValidityName> = {
    baro_altitude_m: "NORMAL",
    pitch_deg: "NORMAL",
    roll_deg: "NORMAL",
    fused_altitude_m: "NORMAL",
    vertical_speed_mps: "NORMAL",
    ...validity,
  };
  return {
    type: "frame",
    received_at_ms: index * PERIOD_MS,
    frame: {
      timestamp_ms: index * PERIOD_MS,
      pressure_hpa: 1013.25,
      temperature_c: 20,
      baro_altitude_m: 0,
      accel_x: 0,
      accel_y: 0,
      accel_z: 9.81,
      gyro_x: 0,
      gyro_y: 0,
      gyro_z: 0,
      pitch_deg: 1.5,
      roll_deg: -2.25,
      sequence: index,
      gps_altitude_m: 3,
      ground_speed_mps: 0,
      course_deg: 0,
      fused_altitude_m: 12.5,
      vertical_speed_mps: 0.25,
      baro_status: 0,
      imu_status: 0,
      gps_status: 0,
      baro_read_ok: true,
      imu_read_ok: true,
      gps_read_ok: index % 50 === 0,
      baro_altitude_validity: VALIDITY_BYTES[names.baro_altitude_m],
      pitch_validity: VALIDITY_BYTES[names.pitch_deg],
      roll_validity: VALIDITY_BYTES[names.roll_deg],
      fused_altitude_validity: VALIDITY_BYTES[names.fused_altitude_m],
      vertical_speed_validity: VALIDITY_BYTES[names.vertical_speed_mps],
      flags: 0,
      gps_vertical_rate_mps: 0,
      gps_pressure_altitude_m: 0,
      gps_fix_quality: 0,
      gps_satellites: 0,
      voter_status: 0,
      reserved_d: 0,
      ...values,
    },
    validity: names,
    status: { baro: "NOMINAL", imu: "NOMINAL", gps: "NOMINAL" },
    position: null,
  };
}

// A feed in the middle of a run: open, SIM, one NORMAL frame just arrived,
// fifty a second, no lag, nothing stale or ended; overrides pick the case.
export function runningFeed(overrides: Partial<FeedState> = {}): FeedState {
  return {
    connection: "open",
    opened: true,
    session: { mode: "SIMULATED", state: "RUNNING", identity: null, source_mode: "SIMULATED" },
    staleMs: STALE_MS,
    lastFrame: frame(0),
    lastArrivalMs: 0,
    frameCount: 1,
    rate: 50,
    ageMs: 0,
    stale: false,
    ended: false,
    lagThresholdMs: LAG_MS,
    trackMinSpeedMps: TRACK_MIN_SPEED_MPS,
    lagMs: 0,
    bridgeLagMs: 0,
    lagExceeded: false,
    trail: [],
    ...overrides,
  };
}
