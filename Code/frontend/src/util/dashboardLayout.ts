import { signal } from '@preact/signals';
import { postJson } from '../api';

/** Stable identifiers for the top-level dashboard widgets. */
export type WidgetId =
  | 'display' | 'thermostat' | 'controls' | 'buttons'
  | 'water' | 'timer' | 'totals' | 'energy';

export interface WidgetCfg { id: WidgetId; enabled: boolean; w: number; h: number; } // w = span on a 12-col grid; h = px height (0 = fit content)
export interface DashLayout { v: 1; widgets: WidgetCfg[]; showCya: boolean; showAlk: boolean; }

const KEY = 'ww.dashLayout';
/** Allowed widget widths (12-col grid): small / medium / full. */
export const WIDTHS = [4, 6, 12] as const;
/**
 * Height is snapped to a row grid - the vertical analogue of the 12 columns.
 * MIN_ROWS sets the floor so a widget can't be dragged down to almost nothing.
 */
export const ROW_H = 48;
export const MIN_ROWS = 4;
export const MAX_ROWS = 20;
export const MIN_H = MIN_ROWS * ROW_H;
export const MAX_H = MAX_ROWS * ROW_H;

/**
 * Per-widget floor overrides. The LED display is naturally ~150px tall - below
 * the general 4-row floor - so with the shared minimum a resized display could
 * never be dragged back down to anywhere near its own size.
 */
const MIN_ROWS_BY_ID: Partial<Record<WidgetId, number>> = { display: 3 };

function minRows(id?: WidgetId): number {
  return (id && MIN_ROWS_BY_ID[id]) || MIN_ROWS;
}

/** Snap a dragged pixel height to the row grid, clamped to the row bounds. */
export function snapH(px: number, id?: WidgetId): number {
  const rows = Math.min(MAX_ROWS, Math.max(minRows(id), Math.round(px / ROW_H)));
  return rows * ROW_H;
}
/** Normalize a stored height: keep 0 as auto, otherwise snap to the row grid. */
function normH(v: unknown, id?: WidgetId): number {
  const n = Number(v);
  return Number.isFinite(n) && n > 0 ? snapH(n, id) : 0;
}
/** Canonical widget order, also the source of truth for which widgets exist. */
export const ORDER: WidgetId[] = ['display', 'thermostat', 'controls', 'buttons', 'water', 'timer', 'totals', 'energy'];

/** Balanced two-column defaults for tablet/desktop; phone CSS stacks them full-width. */
const DEFAULT_WIDGETS: WidgetCfg[] = [
  { id: 'display', enabled: true, w: 6, h: 0 },
  { id: 'thermostat', enabled: true, w: 6, h: 0 },
  { id: 'controls', enabled: true, w: 6, h: 0 },
  { id: 'buttons', enabled: true, w: 6, h: 0 },
  { id: 'water', enabled: true, w: 6, h: 0 },
  { id: 'timer', enabled: true, w: 6, h: 0 },
  { id: 'totals', enabled: true, w: 6, h: 0 },
  { id: 'energy', enabled: true, w: 6, h: 0 },
];

/** Snap an arbitrary width to the nearest allowed span. */
export function clampW(w: unknown): number {
  const n = Number(w);
  if (!Number.isFinite(n)) return 12;
  return WIDTHS.reduce((a, b) => (Math.abs(b - n) < Math.abs(a - n) ? b : a));
}

function defaultLayout(): DashLayout {
  return { v: 1, widgets: DEFAULT_WIDGETS.map((w) => ({ ...w })), showCya: false, showAlk: false };
}

function isLegacyFullWidthDefault(widgets: WidgetCfg[]): boolean {
  return widgets.length === DEFAULT_WIDGETS.length
    && widgets.every((w, i) => w.id === DEFAULT_WIDGETS[i].id && w.w === 12 && w.h === 0);
}

function normalize(raw: unknown): DashLayout {
  if (!raw || typeof raw !== 'object' || !Array.isArray((raw as DashLayout).widgets)) return defaultLayout();
  const r = raw as DashLayout;
  const seen = new Set<WidgetId>();
  const widgets: WidgetCfg[] = [];
  for (const w of r.widgets) {
    if (!w || !ORDER.includes(w.id) || seen.has(w.id)) continue; // drop unknown / duplicate
    seen.add(w.id);
    widgets.push({ id: w.id, enabled: w.enabled !== false, w: clampW(w.w), h: normH(w.h, w.id) });
  }
  for (const def of DEFAULT_WIDGETS) if (!seen.has(def.id)) widgets.push({ ...def }); // append new widgets
  const normalizedWidgets = isLegacyFullWidthDefault(widgets)
    ? widgets.map((w, i) => ({ ...w, w: DEFAULT_WIDGETS[i].w }))
    : widgets;
  return { v: 1, widgets: normalizedWidgets, showCya: !!r.showCya, showAlk: !!r.showAlk };
}

function load(): DashLayout {
  try { return normalize(JSON.parse(localStorage.getItem(KEY) || 'null')); }
  catch { return defaultLayout(); }
}

/** Reactive dashboard layout, persisted to localStorage. */
export const layout = signal<DashLayout>(load());

/** Whether the dashboard is in edit mode. Driven by the topbar toggle. */
export const editMode = signal(false);

export function saveLayout(next: DashLayout): void {
  layout.value = next;
  try { localStorage.setItem(KEY, JSON.stringify(next)); } catch { /* private mode / quota: keep in-memory */ }
}

/** Apply a transform to the widgets array and persist. */
export function updateWidgets(fn: (w: WidgetCfg[]) => WidgetCfg[]): void {
  saveLayout({ ...layout.value, widgets: fn(layout.value.widgets.map((x) => ({ ...x }))) });
}

export function resetLayout(): void {
  try { localStorage.removeItem(KEY); } catch { /* ignore */ }
  saveLayout(defaultLayout());
}

/**
 * One-time seed from the device's old section flags so existing users keep their
 * hidden sections. Runs only when no local layout exists yet; on fetch failure it
 * persists nothing, so the next load retries.
 */
export async function migrateFromDevice(): Promise<void> {
  if (localStorage.getItem(KEY)) return;
  // The thermostat is deliberately not seeded: its legacy flag (SST) ships as
  // false, so migrating from it hid the dial on every existing device. It now
  // always starts on and can be switched off in edit mode like any other widget.
  const map: Partial<Record<WidgetId, string>> = {
    display: 'SSD', controls: 'SSC', buttons: 'SSB',
    water: 'SSWQ', timer: 'SSTIM', totals: 'SSTOT', energy: 'SSEN',
  };
  try {
    const cfg = await postJson<Record<string, boolean>>('/getwebconfig/');
    const next = defaultLayout();
    for (const w of next.widgets) {
      const key = map[w.id];
      if (key && key in cfg) w.enabled = !!cfg[key];
    }
    next.showCya = !!cfg.SWQCYA;
    next.showAlk = !!cfg.SWQALK;
    saveLayout(next);
  } catch { /* keep defaults, do not persist → retry next load */ }
}
