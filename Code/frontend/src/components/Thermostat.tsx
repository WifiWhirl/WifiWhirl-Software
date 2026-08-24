import { useEffect, useRef, useState } from 'preact/hooks';
import { t } from '../i18n';

// Circular thermostat dial for the target temperature. Drag (or use the arrow
// keys) around the open-bottom gauge to set the value; a tick marks where the
// current water temperature sits so the gap to the target is visible at a glance.
// The dial tracks a local value so it follows the finger immediately, and
// reconciles with the device value (prop) whenever the user isn't dragging.
const START = 225; // lower-left
const SWEEP = 270; // open 90° at the bottom
const TICKS = 24;  // small index marks ringed inside the track
const THUMB_R = 10; // visible dot
const HIT_R = 20;   // draggable area around the dot (viewBox units)

function polar(cx: number, cy: number, r: number, deg: number): [number, number] {
  const rad = ((deg - 90) * Math.PI) / 180;
  return [cx + r * Math.cos(rad), cy + r * Math.sin(rad)];
}

function arc(cx: number, cy: number, r: number, a0: number, a1: number): string {
  const [x0, y0] = polar(cx, cy, r, a0);
  const [x1, y1] = polar(cx, cy, r, a1);
  const large = a1 - a0 > 180 ? 1 : 0;
  return `M ${x0} ${y0} A ${r} ${r} 0 ${large} 1 ${x1} ${y1}`;
}

/**
 * @brief Circular target-temperature control with current-temperature marker.
 *
 * The dial tracks a local value while dragging and commits to the firmware only
 * on release or keyboard step completion.
 */
export function Thermostat(props: {
  value: number;
  current: number;
  min: number;
  max: number;
  unit: string;
  heating?: boolean;
  onChange: (v: number) => void;
}) {
  const { value, current, min, max, unit, heating, onChange } = props;
  const ref = useRef<SVGSVGElement>(null);
  const dragging = useRef(false);
  const latest = useRef(value);
  const [val, setVal] = useState(value);
  // follow the device value unless the user is actively dragging
  useEffect(() => { if (!dragging.current) { setVal(value); latest.current = value; } }, [value]);

  const C = 110, R = 90;
  const clamp = (v: number) => Math.min(max, Math.max(min, v));
  const frac = (v: number) => (clamp(v) - min) / (max - min);
  const angle = (v: number) => START + frac(v) * SWEEP;

  // update the on-screen value only; the device is told on release (commit)
  function set(v: number) {
    const nv = clamp(v);
    latest.current = nv;
    setVal(nv);
  }
  function commit() {
    if (latest.current !== value) { navigator.vibrate?.(10); onChange(latest.current); }
  }

  function valueFromPoint(clientX: number, clientY: number) {
    const svg = ref.current;
    if (!svg) return;
    const rect = svg.getBoundingClientRect();
    const px = ((clientX - rect.left) / rect.width) * (C * 2) - C;
    const py = ((clientY - rect.top) / rect.height) * (C * 2) - C;
    const deg = (Math.atan2(py, px) * 180) / Math.PI + 90; // 0 = top, clockwise
    let p = (((deg - START) % 360) + 360) % 360;
    if (p > SWEEP) p = p > SWEEP + 45 ? 0 : SWEEP; // snap the bottom gap to nearest end
    set(Math.round(min + (p / SWEEP) * (max - min)));
  }

  // Only a press on the thumb starts a drag, and it does not move the value: the
  // dial used to be one big hit area, so any tap near the ring jumped the target
  // to that spot. The value now follows the finger from the first move onwards.
  function onPointerDown(e: PointerEvent) {
    dragging.current = true;
    (e.currentTarget as SVGElement).setPointerCapture(e.pointerId);
  }
  function onPointerMove(e: PointerEvent) {
    if (!dragging.current) return;
    valueFromPoint(e.clientX, e.clientY);
  }
  function onPointerUp() {
    if (!dragging.current) return;
    dragging.current = false;
    commit();
  }
  function onKeyDown(e: KeyboardEvent) {
    if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { set(latest.current + 1); commit(); e.preventDefault(); }
    if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { set(latest.current - 1); commit(); e.preventDefault(); }
  }

  const [tx, ty] = polar(C, C, R, angle(val));        // target thumb
  const [cx0, cy0] = polar(C, C, R, angle(current));  // current-temp tick (inner)
  const [cx1, cy1] = polar(C, C, R - 16, angle(current));
  const [minLx, minLy] = polar(C, C, R + 22, START);            // min label, lower-left
  const [maxLx, maxLy] = polar(C, C, R + 22, START + SWEEP);    // max label, lower-right

  return (
    <div class={`thermostat${heating ? ' heating' : ''}`}>
      <svg
        ref={ref}
        viewBox={`0 0 ${C * 2} ${C * 2}`}
        class="thermostat-dial"
        role="slider"
        // lowercase: SVG attribute names are case-sensitive, so "tabIndex" was
        // rendered verbatim and left the dial unfocusable (arrow keys did nothing)
        tabindex={0}
        aria-valuenow={val}
        aria-valuemin={min}
        aria-valuemax={max}
        aria-valuetext={`${val}${unit}`}
        aria-label={t('dashboard.targetTemp')}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerUp}
        onLostPointerCapture={onPointerUp}
        onKeyDown={onKeyDown}
      >
        <defs>
          <linearGradient id="th-grad" x1="0" y1="1" x2="1" y2="0">
            <stop class="th-grad-cool" offset="0" />
            <stop class="th-grad-warm" offset="1" />
          </linearGradient>
        </defs>
        <path class="th-track" d={arc(C, C, R, START, START + SWEEP)} />
        {Array.from({ length: TICKS + 1 }, (_, i) => {
          const a = START + (i / TICKS) * SWEEP;
          const [x0, y0] = polar(C, C, R - 18, a);
          const [x1, y1] = polar(C, C, R - 24, a);
          return <line key={i} class="th-tick" x1={x0} y1={y0} x2={x1} y2={y1} />;
        })}
        <path class="th-glow" d={arc(C, C, R, START, angle(val))} />
        <path class="th-fill" d={arc(C, C, R, START, angle(val))} />
        <line class="th-current" x1={cx0} y1={cy0} x2={cx1} y2={cy1} />
        <circle class="th-thumb" cx={tx} cy={ty} r={THUMB_R} />
        <text class="th-minmax" x={minLx} y={minLy} text-anchor="middle">{min}°</text>
        <text class="th-minmax" x={maxLx} y={maxLy} text-anchor="middle">{max}°</text>
        {/* Grab area, last so it sits on top of the arcs and labels it overlaps.
            HIT_R is in viewBox units (dial = 220 wide): ~55px across on a 304px
            dial, a comfortable touch target that stays off the rest of the ring. */}
        <circle class="th-hit" cx={tx} cy={ty} r={HIT_R} onPointerDown={onPointerDown} />
      </svg>
      <div class="thermostat-center">
        <span class="th-value">{val}<span class="th-unit">{unit}</span></span>
        <span class="th-now">{current}{unit}</span>
      </div>
    </div>
  );
}
