/**
 * @brief Compact recreation of the spa pump LED screen.
 *
 * Brightness mirrors the device: red intensity scales with BRT (0-8), matching
 * the legacy frontend formula red = 143 + 16 * BRT, clamped to 255.
 */
export function LedDisplay(props: {
  main: string;
  unit?: string;
  brt?: number;
}) {
  const main = props.main || '--';
  const brt = Math.max(0, Math.min(8, props.brt ?? 8));
  const red = Math.min(255, 143 + 16 * brt);
  const color = `rgb(${red}, ${Math.round(red * 0.1)}, ${Math.round(red * 0.08)})`;
  const glow = brt / 8;
  const style = `color:${color};text-shadow:0 0 ${2 + glow * 6}px rgba(${red},0,0,${0.25 + glow * 0.45})`;

  return (
    <div class="display-panel">
      <div class="display-screen" aria-label={`${main}${props.unit || ''}`}>
        <span key={main} class="display-value flash" style={style}>{main}</span>
        {props.unit && <span class="display-unit" style={style}>{props.unit}</span>}
      </div>
    </div>
  );
}
