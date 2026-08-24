import { signal } from '@preact/signals';

/**
 * @brief Indicates that a guarded firmware endpoint rejected the current user.
 *
 * AuthGate observes this signal and replaces the app shell with the login view.
 * When global auth is disabled the firmware returns 200 and this remains false.
 */
export const needsLogin = signal(false);
export const authSessionActive = signal(false);

/**
 * @brief First-run onboarding gate. True until the firmware reports the Setup
 * Assistant has been completed (setupComplete). App renders the wizard while set.
 */
export const setupNeeded = signal(false);

/**
 * @brief True while the device is running its SoftAP captive setup portal, so
 * the wizard opens on its WiFi step.
 */
export const apMode = signal(false);

/**
 * @brief Expert mode gate. When true the Expert (wattage) page/nav item is shown.
 */
export const expertMode = signal(false);

/**
 * @brief Cloud availability gate. True only when the firmware has a provisioned
 * PSK; the whole cloud page/nav item stays hidden otherwise (open-source units).
 * null until /auth/status answers, so a #/cloud deep link isn't bounced away
 * before the flag is known (e.g. the reload after enabling the cloud).
 */
export const cloudAvailable = signal<boolean | null>(null);

interface AuthStatus {
  enabled?: boolean;
  authed?: boolean;
  apMode?: boolean;
  setupComplete?: boolean;
  expertMode?: boolean;
  cloudAvailable?: boolean;
}

export interface LoginResult {
  ok: boolean;
  retryAfter?: number;
}

/**
 * @brief Thrown for a non-OK HTTP response. A dropped connection rejects with a
 *        plain TypeError instead, which lets callers tell the two apart.
 */
export class HttpError extends Error {
  constructor(public status: number) { super(`HTTP ${status}`); }
}

async function request(path: string, init?: RequestInit): Promise<Response> {
  const res = await fetch(path, init);
  if (res.status === 401) { needsLogin.value = true; throw new HttpError(401); }
  if (!res.ok) throw new HttpError(res.status);
  return res;
}

/**
 * @brief Fetch and parse a JSON response from a guarded firmware endpoint.
 * @tparam T Expected JSON shape.
 * @param path Firmware endpoint path.
 * @returns Parsed JSON payload.
 */
export async function getJson<T>(path: string): Promise<T> {
  return (await request(path)).json();
}

/**
 * @brief Fetch a text response from a guarded firmware endpoint.
 * @param path Firmware endpoint path.
 * @returns Response body as text.
 */
export async function getText(path: string): Promise<string> {
  return (await request(path)).text();
}

/**
 * @brief POST JSON to a guarded firmware endpoint and parse the response.
 * @tparam T Expected response type.
 * @param path Firmware endpoint path.
 * @param body Optional request body; omitted for body-less POSTs.
 * @returns Parsed JSON when possible, otherwise the raw text response.
 */
export async function postJson<T = unknown>(path: string, body?: unknown): Promise<T> {
  const res = await request(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const text = await res.text();
  try { return JSON.parse(text) as T; } catch { return text as unknown as T; }
}

/**
 * @brief POST JSON and return the raw Response while still honoring the login gate.
 *
 * Useful for endpoints where non-OK bodies carry localized refusal codes.
 */
export async function postJsonResponse(path: string, body?: unknown): Promise<Response> {
  const res = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  if (res.status === 401) { needsLogin.value = true; throw new HttpError(401); }
  return res;
}

/**
 * @brief POST JSON to a guarded endpoint and return the response as a Blob.
 *
 * Uses request() so an expired session opens the login gate instead of
 * surfacing a generic error (e.g. command queue download).
 */
export async function postBlob(path: string, body?: unknown): Promise<Blob> {
  const res = await request(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  return res.blob();
}

/**
 * @brief POST a raw string body to a guarded endpoint via request().
 *
 * Returns the response text; 401 routes through the login gate.
 */
export async function postRaw(path: string, body: string): Promise<string> {
  const res = await request(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body,
  });
  return res.text();
}

/**
 * @brief Normalized result of saving a partial config object.
 *
 * Mirrors the legacy data/function.js sendPartialConfig behavior while hiding
 * firmware response details from page components.
 */
export type SaveResult =
  | { kind: 'saved' }
  | { kind: 'nochange' }
  | { kind: 'restart'; reason?: string }
  | { kind: 'error' };

/**
 * @brief Save a partial configuration payload and classify the firmware reply.
 * @param url Firmware save endpoint.
 * @param data Partial config object to POST.
 * @returns Save status used by buttons and toast/restart flows.
 */
export async function saveConfig(url: string, data: unknown): Promise<SaveResult> {
  try {
    const r = await postJson<{ restart?: boolean; reason?: string; saved?: boolean } | string>(url, data);
    if (r && typeof r === 'object') {
      if (r.restart === true) return { kind: 'restart', reason: r.reason };
      if (r.saved === true) return { kind: 'saved' };
      if (r.saved === false || r.restart === false) return { kind: 'nochange' };
      return { kind: 'saved' };
    }
    return { kind: 'saved' };
  } catch {
    return { kind: 'error' };
  }
}

/**
 * @brief Authenticate against the firmware login endpoint.
 * @param user Login username.
 * @param pwd Login password.
 * @param keep Whether to request a persistent 30-day session.
 * @returns Login result, including retry delay when throttled.
 */
export async function login(user: string, pwd: string, keep = false): Promise<LoginResult> {
  const res = await fetch('/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ user, pwd, keep: keep ? '1' : '0' }).toString(),
  });
  if (res.ok) {
    try {
      const j = await res.json();
      const ok = j?.ok !== false;
      if (ok) {
        needsLogin.value = false;
        authSessionActive.value = true;
      }
      return { ok };
    } catch {
      needsLogin.value = false;
      authSessionActive.value = true;
      return { ok: true }; // non-JSON 200 still means success
    }
  }
  if (res.status === 429) {
    try {
      const j = await res.json();
      return { ok: false, retryAfter: Number(j?.retryAfter) || undefined };
    } catch {
      return { ok: false, retryAfter: Number(res.headers.get('Retry-After')) || undefined };
    }
  }
  return { ok: false };
}

/**
 * @brief Refresh non-sensitive firmware auth state.
 *
 * The session cookie is HttpOnly, so the SPA asks the firmware whether global
 * auth is enabled and whether this request carried a valid session.
 */
export async function refreshAuthStatus(): Promise<void> {
  try {
    const res = await fetch('/auth/status');
    if (!res.ok) return;
    const status = await res.json() as AuthStatus;
    authSessionActive.value = !!status.enabled && !!status.authed;
    needsLogin.value = !!status.enabled && !status.authed;
    apMode.value = !!status.apMode;
    // Only firmware that reports the flag (=== false) triggers onboarding; older
    // firmware omits it and the wizard stays hidden. Only show it while global
    // auth is off (a fresh device) so it never pre-empts the login gate.
    setupNeeded.value = status.setupComplete === false && !status.enabled;
    expertMode.value = !!status.expertMode;
    cloudAvailable.value = !!status.cloudAvailable;
  } catch {
    /* keep the current UI state when the module is unreachable */
  }
}

/**
 * @brief End the current firmware session if one exists.
 */
export async function logout(): Promise<void> {
  try { await fetch('/logout', { method: 'POST' }); } catch { /* ignore */ }
  authSessionActive.value = false;
}
