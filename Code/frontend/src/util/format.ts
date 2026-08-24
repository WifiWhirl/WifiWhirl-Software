// Locale-aware time/number formatting. Reads signals so callers re-render on
// language or currency changes when they format inside render.
import { activeLocale, t } from '../i18n';
import { currencySymbol } from '../currency';

/**
 * @brief Sentinel used to treat very old timestamps as "never".
 */
export const TWENTY_YEARS_SEC = 20 * 365 * 24 * 3600;

/**
 * @brief Format an age in seconds as localized relative text.
 * @param seconds Seconds elapsed since the event.
 * @returns Localized relative time such as "2 minutes ago".
 */
export function getTimeSinceText(seconds: number): string {
  if (seconds > TWENTY_YEARS_SEC) return t('time.never');
  if (seconds < 60) return t('time.justNow');
  const rtf = new Intl.RelativeTimeFormat(activeLocale.value, { numeric: 'auto' });
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return rtf.format(-minutes, 'minute');
  const hours = Math.floor(seconds / 3600);
  if (hours < 24) return rtf.format(-hours, 'hour');
  return rtf.format(-Math.floor(seconds / 86400), 'day');
}

/**
 * @brief Format time-to-ready including backend readiness sentinels.
 * @param t2rHours Firmware time-to-ready value in hours.
 * @returns Localized display string.
 */
export function t2rText(t2rHours: number): string {
  const val = t2rHours * 3600;
  if (val === -7200) return `00:00:00 (${t('ready.ready')})`;
  if (val === -3600) return t('ready.notComputable');
  return `${dhms(val)} (${t2rHours <= 0 ? t('ready.ready') : t('ready.notReady')})`;
}

/**
 * @brief Format the time-to-ready duration only.
 * @param t2rHours Firmware time-to-ready value in hours.
 * @returns Clock-style duration string.
 */
export function t2rDuration(t2rHours: number): string {
  if (t2rHours === -2) return dhms(0);
  if (t2rHours === -1) return dhms(999 * 3600);
  return dhms(t2rHours * 3600);
}

/**
 * @brief Format seconds as a clock-style duration.
 * @param val Duration in seconds.
 * @returns Text like "2 Tage 03:04:05".
 */
export function dhms(val: number): string {
  const days = Math.floor(val / 86400);
  const hours = Math.floor((val % 86400) / 3600);
  const minutes = Math.floor((val % 3600) / 60);
  const seconds = Math.floor(val % 60);
  const p = (n: number) => String(n).padStart(2, '0');
  const d = days >= 1 ? `${days} ${t(days === 1 ? 'time.day' : 'time.days')} ` : '';
  return `${d}${p(hours)}:${p(minutes)}:${p(seconds)}`;
}

/**
 * @brief Format seconds as a compact words-style duration.
 * @param seconds Duration in seconds.
 * @returns Text like "2 days, 3 h, 5 min".
 */
export function durLong(seconds: number): string {
  if (seconds < 0) return t('time.past');
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const parts: string[] = [];
  if (days > 0) parts.push(`${days} ${t(days === 1 ? 'time.day' : 'time.days')}`);
  if (hours > 0) parts.push(`${hours} ${t('time.hoursShort')}`);
  if (minutes > 0 || parts.length === 0) parts.push(`${minutes} ${t('time.minutesShort')}`);
  return parts.join(', ');
}

/**
 * @brief Format a decimal using the active locale.
 * @param n Number to format.
 * @param digits Fixed fraction digit count.
 * @returns Locale-aware decimal text.
 */
export function comma(n: number, digits = 2): string {
  return new Intl.NumberFormat(activeLocale.value, {
    minimumFractionDigits: digits, maximumFractionDigits: digits,
  }).format(n);
}

/**
 * @brief Format an optional number using the active locale.
 * @param v Number to format.
 * @param digits Optional fixed fraction digit count.
 * @returns Locale-aware text, or "--" when undefined.
 */
export function dec(v?: number, digits?: number): string {
  if (v === undefined) return '--';
  return new Intl.NumberFormat(activeLocale.value,
    digits !== undefined ? { minimumFractionDigits: digits, maximumFractionDigits: digits } : {}
  ).format(v);
}

/**
 * @brief Format a money amount with the configured display currency.
 * @param v Number to format.
 * @param digits Fixed fraction digit count.
 * @returns Locale-aware amount with currency symbol.
 */
export function money(v: number, digits = 2): string {
  return `${comma(v, digits)} ${currencySymbol()}`;
}
