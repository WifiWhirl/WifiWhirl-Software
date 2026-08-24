import type { ComponentChildren } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { saveConfig, type SaveResult } from '../api';
import { showRestart, showToast } from './feedback';
import { t } from '../i18n';

/**
 * @brief Framed content section used throughout configuration pages.
 */
export function Card({ title, id, className, children }: { title?: string; id?: string; className?: string; children: ComponentChildren }) {
  return (
    <section class={`card${className ? ' ' + className : ''}`} id={id}>
      {title && <div class="card-head"><h2>{title}</h2></div>}
      {children}
    </section>
  );
}

/**
 * @brief Standard label/value row for forms and status cards.
 */
export function Row({ label, htmlFor, children }: { label?: ComponentChildren; htmlFor?: string; children: ComponentChildren }) {
  return (
    <div class="row">
      {label != null && <label for={htmlFor}>{label}</label>}
      <div>{children}</div>
    </div>
  );
}

/**
 * @brief One step of the horizontal ss-timeline (dot + label + value).
 *
 * Wrap steps in `.ss-timeline` > `.ss-timeline-bar` + `.ss-timeline-steps`
 * (see SmartSchedule/Expert); `--ss-progress` drives the bar fill.
 */
export function TimelineStep({ label, value, active, done }: { label: string; value: string; active?: boolean; done?: boolean }) {
  return (
    <div class={`ss-timeline-step${active ? ' active' : ''}${done ? ' done' : ''}`}>
      <i />
      <span>{label}</span>
      <b>{value}</b>
    </div>
  );
}

/**
 * @brief Boolean switch with the app's visual switch styling.
 */
export function Toggle({ checked, onChange, id, disabled }: { checked: boolean; onChange: (v: boolean) => void; id?: string; disabled?: boolean }) {
  return (
    <label class="switch">
      <input
        id={id}
        type="checkbox"
        checked={checked}
        disabled={disabled}
        onChange={(e) => onChange((e.target as HTMLInputElement).checked)}
      />
      <span class="slider" />
    </label>
  );
}

/**
 * @brief Small segmented control for mutually exclusive options.
 * @tparam T String or number value type represented by the options.
 */
export function Segmented<T extends string | number>(props: {
  value: T;
  options: { value: T; label: string }[];
  onChange: (value: T) => void;
}) {
  return (
    <div class={`segmented${props.options.length > 2 ? ' many' : ''}`}>
      {props.options.map((option) => (
        <button
          key={String(option.value)}
          type="button"
          class={option.value === props.value ? 'active' : ''}
          aria-pressed={option.value === props.value}
          onClick={() => props.onChange(option.value)}
        >
          {option.label}
        </button>
      ))}
    </div>
  );
}

/**
 * @brief Touch-friendly slider that commits only on release.
 *
 * The displayed value updates while dragging, but onChange fires on release so
 * the firmware is not flooded with intermediate values.
 */
export function Slider(props: {
  value: number; min: number; max: number; step?: number; unit?: string;
  onChange: (v: number) => void;
}) {
  const { value, min, max, step = 1, unit, onChange } = props;
  const [v, setV] = useState(value);
  useEffect(() => { setV(value); }, [value]);
  return (
    <span class="range-control">
      <input
        type="range" min={min} max={max} step={step} value={v}
        onInput={(e) => setV(Number((e.target as HTMLInputElement).value))}
        onChange={(e) => onChange(Number((e.target as HTMLInputElement).value))}
      />
      <b>{v}{unit ? ` ${unit}` : ''}</b>
    </span>
  );
}

/**
 * @brief Plus/minus stepper for compact integer-style values.
 */
export function NumberStepper(props: {
  value: number; min?: number; max?: number; step?: number;
  onChange: (v: number) => void;
}) {
  const { value, min = 0, max = 100, step = 1, onChange } = props;
  const clamp = (v: number) => Math.min(max, Math.max(min, v));
  return (
    <div class="stepper">
      <button type="button" aria-label="−" onClick={() => onChange(clamp(value - step))}>−</button>
      <span class="val">{value}</span>
      <button type="button" aria-label="+" onClick={() => onChange(clamp(value + step))}>+</button>
    </div>
  );
}

/**
 * @brief Numeric input with adjacent increment and decrement buttons.
 */
export function NumberInput(props: {
  value: number | string;
  min?: number;
  max?: number;
  step?: number;
  unit?: string;
  id?: string;
  onInput: (v: string) => void;
}) {
  const { value, min, max, step = 1, unit, id, onInput } = props;
  const places = decimalPlaces(step);
  const current = Number(String(value).replace(',', '.'));
  const clamp = (v: number) => {
    const low = min ?? -Infinity;
    const high = max ?? Infinity;
    return Math.min(high, Math.max(low, v));
  };
  const format = (v: number) => {
    const rounded = places > 0 ? v.toFixed(places) : String(Math.round(v));
    return rounded.replace(/\.0+$/, '').replace(/(\.\d*?)0+$/, '$1');
  };
  const bump = (dir: -1 | 1) => {
    const base = Number.isFinite(current) ? current : (min ?? 0);
    onInput(format(clamp(base + dir * step)));
  };

  return (
    <div class="number-control">
      <button type="button" aria-label="−" onClick={() => bump(-1)}>−</button>
      <input
        id={id}
        type="number"
        value={value}
        min={min}
        max={max}
        step={step}
        inputMode={step < 1 ? 'decimal' : 'numeric'}
        onInput={(e) => onInput((e.target as HTMLInputElement).value)}
      />
      <button type="button" aria-label="+" onClick={() => bump(1)}>+</button>
      {unit && <span class="number-unit">{unit}</span>}
    </div>
  );
}

function decimalPlaces(step: number): number {
  const s = String(step);
  if (!s.includes('.')) return 0;
  return s.split('.')[1]?.length ?? 0;
}

/**
 * @brief Native dialog wrapper with app modal styling.
 *
 * onClose handles Esc, close buttons, and backdrop clicks. Omit onClose for a
 * non-dismissible dialog such as the login gate.
 */
export function Modal({ children, onClose, closing = false }: { children: ComponentChildren; onClose?: () => void; closing?: boolean }) {
  const ref = useRef<HTMLDialogElement>(null);
  useEffect(() => { if (ref.current && !ref.current.open) ref.current.showModal(); }, []);
  return (
    <dialog
      ref={ref}
      class={`modal-dialog${closing ? ' closing' : ''}`}
      onCancel={(e) => { e.preventDefault(); onClose?.(); }}
      onClick={(e) => { if (e.target === ref.current) onClose?.(); }}
    >
      <div class={`modal${closing ? ' closing' : ''}`}>{children}</div>
    </dialog>
  );
}

/**
 * @brief Button that POSTs a partial config and reports the result inline.
 */
export function SaveButton(props: {
  url: string;
  data: () => unknown;
  label?: string;
  className?: string;
  disabled?: boolean;
  title?: string;
  validate?: () => string | null | undefined;
  onResult?: (result: SaveResult) => void | Promise<void>;
  /** Route to return to when the save triggers a reboot (default: dashboard). */
  returnTo?: string;
}) {
  const [busy, setBusy] = useState(false);
  const [text, setText] = useState('');

  async function onClick() {
    const validationError = props.validate?.();
    if (validationError) {
      setText(t('common.error'));
      showToast(validationError, true);
      setTimeout(() => setText(''), 4000);
      return;
    }
    setBusy(true);
    setText(t('common.saving'));
    const r: SaveResult = await saveConfig(props.url, props.data());
    setBusy(false);
    if (r.kind === 'restart') { showRestart(r.reason, props.returnTo); return; }
    if (r.kind === 'saved') { setText(t('common.saved')); showToast(t('common.saved')); }
    else if (r.kind === 'nochange') setText(t('common.noChange'));
    else { setText(t('common.error')); showToast(t('common.error'), true); }
    await props.onResult?.(r);
    setTimeout(() => setText(''), 4000);
  }

  return (
    <button class={`btn ${props.className || ''}`} disabled={busy || props.disabled} title={props.title} onClick={onClick}>
      {text || props.label || t('common.save')}
    </button>
  );
}
