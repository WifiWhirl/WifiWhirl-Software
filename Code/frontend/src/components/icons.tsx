import type { ComponentType, JSX } from 'preact';

import PiArrowClockwise from '@preact-icons/pi/PiArrowClockwise';
import PiArrowsHorizontal from '@preact-icons/pi/PiArrowsHorizontal';
import PiArrowsVertical from '@preact-icons/pi/PiArrowsVertical';
import PiBinary from '@preact-icons/pi/PiBinary';
import PiBroadcast from '@preact-icons/pi/PiBroadcast';
import PiBrowser from '@preact-icons/pi/PiBrowser';
import PiCalendarCheck from '@preact-icons/pi/PiCalendarCheck';
import PiCheck from '@preact-icons/pi/PiCheck';
import PiCloud from '@preact-icons/pi/PiCloud';
import PiCpu from '@preact-icons/pi/PiCpu';
import PiDotsSixVertical from '@preact-icons/pi/PiDotsSixVertical';
import PiDrop from '@preact-icons/pi/PiDrop';
import PiFan from '@preact-icons/pi/PiFan';
import PiFlame from '@preact-icons/pi/PiFlame';
import PiHardDrives from '@preact-icons/pi/PiHardDrives';
import PiHouse from '@preact-icons/pi/PiHouse';
import PiInfo from '@preact-icons/pi/PiInfo';
import PiLightning from '@preact-icons/pi/PiLightning';
import PiList from '@preact-icons/pi/PiList';
import PiLockKey from '@preact-icons/pi/PiLockKey';
import PiPencilSimple from '@preact-icons/pi/PiPencilSimple';
import PiPower from '@preact-icons/pi/PiPower';
import PiMagnifyingGlass from '@preact-icons/pi/PiMagnifyingGlass';
import PiSignOut from '@preact-icons/pi/PiSignOut';
import PiTrash from '@preact-icons/pi/PiTrash';
import PiMoon from '@preact-icons/pi/PiMoon';
import PiSquaresFour from '@preact-icons/pi/PiSquaresFour';
import PiSun from '@preact-icons/pi/PiSun';
import PiWaves from '@preact-icons/pi/PiWaves';
import PiWifiHigh from '@preact-icons/pi/PiWifiHigh';
import PiWifiLow from '@preact-icons/pi/PiWifiLow';
import PiWifiMedium from '@preact-icons/pi/PiWifiMedium';
import PiWifiNone from '@preact-icons/pi/PiWifiNone';
import PiWifiSlash from '@preact-icons/pi/PiWifiSlash';
import PiWind from '@preact-icons/pi/PiWind';

type PiIcon = ComponentType<{ size?: number; class?: string; color?: string; title?: string }>;

/**
 * @brief Names for the Phosphor icons used by navigation, controls, and status.
 */
export type IconName =
  | 'dashboard' | 'automation' | 'smartschedule' | 'config' | 'web' | 'mqtt'
  | 'wifi' | 'wifiHigh' | 'wifiMedium' | 'wifiLow' | 'wifiNone' | 'wifiSlash'
  | 'device' | 'hardware' | 'cloud' | 'info' | 'restart' | 'logout'
  | 'home' | 'menu' | 'moon' | 'sun'
  | 'power' | 'lock' | 'airjet' | 'heater' | 'pump' | 'hydrojet' | 'binary'
  | 'edit' | 'done' | 'grip' | 'resize' | 'resizeV' | 'search' | 'trash';

const icons: Record<IconName, PiIcon> = {
  dashboard: PiSquaresFour,
  automation: PiLightning,
  smartschedule: PiCalendarCheck,
  config: PiDrop,
  web: PiBrowser,
  mqtt: PiBroadcast,
  wifi: PiWifiHigh,
  wifiHigh: PiWifiHigh,
  wifiMedium: PiWifiMedium,
  wifiLow: PiWifiLow,
  wifiNone: PiWifiNone,
  wifiSlash: PiWifiSlash,
  device: PiCpu,
  hardware: PiHardDrives,
  cloud: PiCloud,
  info: PiInfo,
  restart: PiArrowClockwise,
  logout: PiSignOut,
  home: PiHouse,
  menu: PiList,
  moon: PiMoon,
  sun: PiSun,
  power: PiPower,
  lock: PiLockKey,
  airjet: PiWind,
  heater: PiFlame,
  pump: PiFan,
  hydrojet: PiWaves,
  binary: PiBinary,
  edit: PiPencilSimple,
  done: PiCheck,
  grip: PiDotsSixVertical,
  resize: PiArrowsHorizontal,
  resizeV: PiArrowsVertical,
  search: PiMagnifyingGlass,
  trash: PiTrash,
};

/**
 * @brief Map a 0-4 signal bar value to the closest Phosphor Wi-Fi glyph.
 */
export function wifiSignalIconName(bars: number): IconName {
  if (bars >= 4) return 'wifiHigh';
  if (bars >= 3) return 'wifiMedium';
  if (bars >= 1) return 'wifiLow';
  return 'wifiNone';
}

/**
 * @brief Render one of the app's Phosphor icons from @preact-icons/pi.
 */
export function Icon({ name, size = 24, class: cls }: {
  name: IconName; size?: number; stroke?: number; class?: string;
}): JSX.Element {
  const Glyph = icons[name];
  return <Glyph size={size} class={cls ? `ph-icon ${cls}` : 'ph-icon'} />;
}
