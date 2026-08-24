import type { JSX, RefObject } from 'preact';
import { useEffect, useLayoutEffect, useRef } from 'preact/hooks';
import Sortable from 'sortablejs';
import { Icon } from './icons';
import { Toggle } from './ui';
import { layout, updateWidgets, clampW, snapH, type WidgetId } from '../util/dashboardLayout';
import { t } from '../i18n';

interface Props {
  edit: boolean;
  title: (id: WidgetId) => string;
  render: (id: WidgetId) => JSX.Element | null;
  chrome?: (id: WidgetId) => JSX.Element | null;
}

/**
 * @brief Reflow grid of dashboard widgets with drag-to-reorder and resize while editing.
 *
 * Widget order/width/enabled come from the localStorage-backed `layout` signal,
 * which is the single source of truth. SortableJS mutates the DOM on drop, so
 * onEnd reverts that mutation and lets Preact re-render from the updated signal.
 */
export function DashboardGrid({ edit, title, render, chrome }: Props) {
  const ref = useRef<HTMLDivElement>(null);
  const widgets = layout.value.widgets;

  // Masonry: give each widget a row span tall enough for its content (+gap), so
  // dense packing slots short widgets beside tall ones instead of leaving gaps.
  useLayoutEffect(() => {
    const grid = ref.current;
    if (!grid) return;
    const measure = () => {
      const track = parseFloat(getComputedStyle(grid).getPropertyValue('--dash-track')) || 8;
      const gap = parseFloat(getComputedStyle(grid).columnGap) || 0;
      for (const el of Array.from(grid.children) as HTMLElement[]) {
        // Sum in-flow children (chrome bar + body); skip absolute resize handles.
        // Measuring children, not the widget itself, avoids the grid-track ↔ height feedback loop.
        let h = 0;
        for (const c of Array.from(el.children) as HTMLElement[]) {
          const cs = getComputedStyle(c);
          if (cs.position === 'absolute' || cs.position === 'fixed') continue;
          h += c.getBoundingClientRect().height + parseFloat(cs.marginTop) + parseFloat(cs.marginBottom);
        }
        el.style.setProperty('--span', String(Math.max(1, Math.ceil((h + gap) / track))));
      }
    };
    const ro = new ResizeObserver(measure);
    ro.observe(grid); // width changes reflow content heights
    for (const el of Array.from(grid.children))
      for (const child of Array.from(el.children)) ro.observe(child);
    measure();
    return () => ro.disconnect();
  }, [widgets, edit]);

  useEffect(() => {
    if (!edit || !ref.current) return;
    const sortable = Sortable.create(ref.current, {
      handle: '.dash-grip',
      animation: 150,
      ghostClass: 'sortable-ghost',
      dragClass: 'sortable-drag',
      onEnd: (e) => {
        const { oldIndex, newIndex } = e;
        if (oldIndex == null || newIndex == null || oldIndex === newIndex) return;
        // Revert Sortable's DOM move so the DOM matches Preact's vdom again.
        const parent = e.from;
        parent.removeChild(e.item);
        parent.insertBefore(e.item, parent.children[oldIndex] ?? null);
        updateWidgets((ws) => { const [m] = ws.splice(oldIndex, 1); ws.splice(newIndex, 0, m); return ws; });
      },
    });
    return () => sortable.destroy();
  }, [edit]);

  return (
    <div ref={ref} class={`dashboard-layout${edit ? ' editing' : ''}`}>
      {widgets.map((w) => {
        const body = render(w.id);
        if (!edit && (!w.enabled || body == null)) return null;
        const showBody = w.enabled && body != null;
        return (
          <div key={w.id} class={`dash-widget${w.h ? ' sized' : ''}${edit && !w.enabled ? ' disabled' : ''}`} style={`--w:${w.w}${w.h ? `;--h:${w.h}px` : ''}`} data-id={w.id}>
            {edit && (
              <div class="dash-chrome">
                <span class="dash-grip" aria-label={t('dash.drag')}><Icon name="grip" size={18} /></span>
                <span class="dash-title">{title(w.id)}</span>
                {chrome && <span class="dash-chrome-extra">{chrome(w.id)}</span>}
                <Toggle checked={w.enabled} onChange={(on) => updateWidgets((ws) => ws.map((x) => (x.id === w.id ? { ...x, enabled: on } : x)))} />
              </div>
            )}
            {showBody ? body : (edit ? <div class="dash-placeholder">{title(w.id)}</div> : null)}
            {edit && w.enabled && <>
              <ResizeHandle id={w.id} axis="w" gridRef={ref} />
              <ResizeHandle id={w.id} axis="h" gridRef={ref} />
            </>}
          </div>
        );
      })}
    </div>
  );
}

/**
 * @brief Edge handle that resizes a widget while dragging.
 *
 * axis "w" snaps the column span (right edge); axis "h" sets a free pixel height
 * (bottom edge). Uses pointer events (mouse + touch); touch-action:none in CSS
 * keeps a touch drag from scrolling. Updates live, persists on release.
 */
function ResizeHandle({ id, axis, gridRef }: { id: WidgetId; axis: 'w' | 'h'; gridRef: RefObject<HTMLDivElement> }) {
  function onDown(e: JSX.TargetedPointerEvent<HTMLSpanElement>) {
    e.preventDefault();
    const handle = e.currentTarget as HTMLElement;
    const grid = gridRef.current;
    const widget = handle.closest('.dash-widget') as HTMLElement | null;
    if (!grid || !widget) return;
    handle.setPointerCapture?.(e.pointerId);
    const current = layout.value.widgets.find((w) => w.id === id);
    widget.dataset.w = String(current?.w ?? 12);
    widget.dataset.h = String(current?.h ?? 0);
    const rect = widget.getBoundingClientRect();
    const cardRect = (widget.querySelector('.card') as HTMLElement | null)?.getBoundingClientRect() ?? rect;
    const col = grid.getBoundingClientRect().width / 12;
    const resizingClass = axis === 'w' ? 'dash-resizing-w' : 'dash-resizing-h';
    document.documentElement.classList.add(resizingClass);

    const move = (ev: PointerEvent) => {
      if (axis === 'w') {
        const span = clampW(Math.round((ev.clientX - rect.left) / col));
        widget.style.setProperty('--w', String(span));
        widget.dataset.w = String(span);
      } else {
        const h = snapH(ev.clientY - cardRect.top, id);
        widget.classList.add('sized');
        widget.style.setProperty('--h', `${h}px`);
        widget.dataset.h = String(h);
      }
    };
    const up = (ev: PointerEvent) => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('pointercancel', up);
      document.documentElement.classList.remove(resizingClass);
      handle.releasePointerCapture?.(ev.pointerId);
      if (axis === 'w') {
        const span = Number(widget.dataset.w) || 12;
        updateWidgets((ws) => ws.map((x) => (x.id === id ? { ...x, w: span } : x)));
      } else {
        const h = Number(widget.dataset.h) || 0;
        updateWidgets((ws) => ws.map((x) => (x.id === id ? { ...x, h } : x)));
      }
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    window.addEventListener('pointercancel', up);
  }
  return (
    <span class={`dash-resize dash-resize-${axis}`} onPointerDown={onDown}
      aria-label={t(axis === 'w' ? 'dash.resize' : 'dash.resizeH')} />
  );
}
