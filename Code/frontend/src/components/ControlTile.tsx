import { commsOk } from '../comms';
import { Icon, type IconName } from './icons';

/**
 * @brief Large touch-friendly toggle tile for spa pump functions.
 *
 * Filled/glowing when on, quiet when off. Use tone="heat" for the heater tile
 * while heating (red) and tone="ok" once the target temperature is reached.
 */
export function ControlTile(props: {
  icon: IconName;
  label: string;
  on: boolean;
  tone?: 'heat' | 'ok';
  stateOn: string;
  stateOff: string;
  onClick: () => void;
}) {
  return (
    <button
      type="button"
      aria-pressed={props.on}
      disabled={!commsOk.value}
      class={`tile${props.on ? ' on' : ''}${props.tone ? ` ${props.tone}` : ''}`}
      onClick={() => { navigator.vibrate?.(15); props.onClick(); }}
    >
      <span class="tile-ic"><Icon name={props.icon} size={26} /></span>
      <span class="tile-lbl">{props.label}</span>
      <span class="tile-state">{props.on ? props.stateOn : props.stateOff}</span>
    </button>
  );
}
