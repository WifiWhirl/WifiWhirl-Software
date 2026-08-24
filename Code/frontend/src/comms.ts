import { signal } from '@preact/signals';
import { useEffect } from 'preact/hooks';
import { getJson, needsLogin, postJson } from './api';

// --- inbound message shapes (firmware contract) ---------------------------
/**
 * @brief Firmware OTHER payload with model, connectivity, and build metadata.
 */
export interface OtherMsg {
  CONTENT: 'OTHER';
  MQTT: number;
  FW: string;
  BUILDENV?: string;
  /** Newer firmware version found by the daily check; "" / absent when up to date. */
  NEWFW?: string;
  RSSI: number;
  HASJETS: boolean;
  WEATHER: boolean;
  CLOUD?: string;
}

/**
 * @brief Firmware STATES payload with current pump/display state.
 */
export interface StatesMsg {
  CONTENT: 'STATES';
  TMP: number; TGT: number;
  PWR: boolean; LCK: boolean; AIR: boolean; UNT: boolean; FLT: boolean;
  HJT: boolean; RED: boolean; GRN: boolean;
  // heat-loss calibration running (functions locked firmware-side)
  HLCAL?: boolean;
  CH1: number; CH2: number; CH3: number;
  BRT: number; AMB: number;
  // brightness boost (optional - only on builds with the feature)
  BRTBASE?: number; BRTOVR?: boolean;
}

/**
 * @brief Firmware TIMES payload with counters, timers, and sensor history.
 */
export interface TimesMsg {
  CONTENT: 'TIMES';
  TIME: number;
  CLTIME: number; CLINT: number;
  FTIME: number; FINT: number;
  FCTIME: number; FCINT: number;
  WCTIME: number; WCINT: number;
  PHTIME: number; PHINT: number; PHVAL: number;
  CLVTIME: number; CLVAL: number;
  HEATINGTIME: number; UPTIME: number; AIRTIME: number; PUMPTIME: number; JETTIME: number;
  COST: number; T2R: number;
  // optional, only present on some builds / when enabled
  WATT?: number; KWHD?: number; KWH?: number; COSTD?: number;
  CYATIME?: number; CYAVAL?: number; ALKTIME?: number; ALKVAL?: number;
}

/**
 * @brief Union of all payloads returned by /getpolldata/.
 */
export type InboundMessage = OtherMsg | StatesMsg | TimesMsg;

// command name -> firmware CMD id. Mirrors enum Commands in
// lib/BWC_unified/enums.h (authoritative - do not renumber).
const cmdMap: Record<string, number> = {
  setTarget: 0, setTargetSelector: 0, toggleUnit: 1, toggleBubbles: 2,
  toggleHeater: 3, togglePump: 4, restartEsp: 6, resetTotals: 8,
  resetTimerChlorine: 9, resetTimerFilter: 10, toggleHydroJets: 11,
  setBrightness: 12, setBrightnessSelector: 12, setBeep: 13,
  setAmbientF: 14, setAmbient: 15, setAmbientSelector: 15, setAmbientC: 15,
  resetDaily: 16, toggleGodmode: 17, setFullpower: 18, printText: 19,
  setReady: 20, setR: 21, togglePWR: 22, toggleLCK: 23,
  resetTimerCleanFilter: 24, resetTimerWaterChange: 25, setEnableButtons: 26,
  setPhValue: 27, setClValue: 28, setCyaValue: 29, setAlkValue: 30,
};

// --- reactive state -------------------------------------------------------
/** @brief True when the last active live poll completed successfully. */
export const commsOk = signal(false); // comms healthy (last poll succeeded)

/** @brief True while at least one mounted view has requested live polling. */
export const pollingActive = signal(false);

/** @brief Most recent OTHER payload received from the firmware. */
export const lastOther = signal<OtherMsg | null>(null);

/** @brief Most recent STATES payload received from the firmware. */
export const lastStates = signal<StatesMsg | null>(null);

/** @brief Most recent TIMES payload received from the firmware. */
export const lastTimes = signal<TimesMsg | null>(null);

// Transport: HTTP polling only. The async WebSocket was removed because the
// ESP8266 field hardware runs too low on heap to send WS frames without blocking
// (soft-WDT reboots); /getpolldata is the single stable transport.
const POLL_MS = 1000;

let pollTimer: number | undefined;
let pollInFlight = false;
let pollUsers = 0;

/**
 * @brief Dispatch a live payload into the matching reactive cache.
 * @param obj Parsed firmware payload.
 */
function dispatch(obj: InboundMessage | null): void {
  if (!obj || typeof obj !== 'object') return;
  if (obj.CONTENT === 'OTHER') lastOther.value = obj;
  else if (obj.CONTENT === 'STATES') lastStates.value = obj;
  else if (obj.CONTENT === 'TIMES') lastTimes.value = obj;
}

// --- HTTP polling transport -----------------------------------------------
// GET /getpolldata/ returns [STATES, TIMES, OTHER] - the same objects the
// WebSocket pushes - and POST /sendcommand/ takes the same command JSON.
/**
 * @brief Poll /getpolldata/ once and update live state signals.
 */
async function pollOnce(): Promise<void> {
  if (pollInFlight) return;
  if (needsLogin.value) {
    stopPolling();
    return;
  }
  if (document.hidden) return; // skip while tab is backgrounded
  pollInFlight = true;
  try {
    const arr = await getJson<InboundMessage[]>('/getpolldata/');
    if (Array.isArray(arr)) arr.forEach(dispatch);
    commsOk.value = true;
  } catch {
    commsOk.value = false;
    if (needsLogin.value) stopPolling();
  } finally {
    pollInFlight = false;
  }
}

function startPolling(): void {
  if (needsLogin.value) return;
  if (pollTimer) return;
  pollingActive.value = true;
  pollOnce();
  pollTimer = window.setInterval(pollOnce, POLL_MS);
}

function stopPolling(): void {
  if (!pollTimer) return;
  window.clearInterval(pollTimer);
  pollTimer = undefined;
  pollingActive.value = false;
}

// Poll immediately when the tab regains focus so it doesn't feel stale.
document.addEventListener('visibilitychange', () => {
  if (!document.hidden && pollingActive.value && !needsLogin.value) pollOnce();
});

// --- public API -----------------------------------------------------------
function acquirePolling(): void {
  pollUsers++;
  startPolling();
}

function releasePolling(): void {
  if (pollUsers > 0) pollUsers--;
  if (pollUsers === 0) stopPolling();
}

/**
 * @brief Acquire live polling for the lifetime of the calling component.
 *
 * Pages that need live state call this hook while mounted. Polling stops when
 * the last caller unmounts, which avoids background traffic on static pages.
 */
export function useCommsPolling(): void {
  useEffect(() => {
    acquirePolling();
    return releasePolling;
  }, []);
}

/**
 * @brief Refresh live status once without starting the dashboard poll loop.
 *
 * Static subpages use this to keep top-bar MQTT/Wi-Fi and firmware version
 * metadata fresh while avoiding continuous background traffic.
 */
export function refreshCommsOnce(): Promise<void> {
  return pollOnce();
}

/**
 * @brief Send a firmware command through the HTTP command endpoint.
 * @param cmd Friendly command name from cmdMap.
 * @param value Optional command value.
 * @param extra Optional scheduling/text fields for queued commands.
 */
export function sendCommand(
  cmd: string,
  value: number | boolean | string = 0,
  extra?: Partial<{ XTIME: number; INTERVAL: number; TXT: string }>
): void {
  if (typeof cmdMap[cmd] === 'undefined') return;
  const payload: Record<string, unknown> = {
    CMD: cmdMap[cmd], VALUE: value,
    XTIME: Math.floor(Date.now() / 1000), INTERVAL: 0, TXT: '',
    FORCE: true, // live dashboard commands bypass safety checks (matches index.js)
  };
  Object.assign(payload, extra || {});
  // fire the command, then refresh state right away for snappy feedback
  postJson('/sendcommand/', payload).then(() => {
    if (pollingActive.value) pollOnce();
  }).catch(() => {});
}

/**
 * @brief Convert RSSI dBm to a 1-4 bar display value.
 * @param rssi Signal strength in dBm.
 * @returns Bar count for the Wi-Fi indicator.
 */
export function rssiBars(rssi: number): number {
  return rssi <= -80 ? 1 : rssi <= -70 ? 2 : rssi <= -67 ? 3 : 4;
}
