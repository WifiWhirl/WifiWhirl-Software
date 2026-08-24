// POSIX TZ strings per IANA zone (ported from data/config.html). Used by the
// SPA config page so TIMEZONE (POSIX) + TIMEZONE_NAME (IANA) stay consistent.
/** @brief Default IANA timezone name used when no firmware value matches. */
export const DEFAULT_TZ_NAME = 'Europe/Berlin';

/** @brief Default POSIX TZ string sent to the firmware. */
export const DEFAULT_TZ = 'CET-1CEST,M3.5.0,M10.5.0/3';

/** @brief Supported IANA timezone names mapped to firmware POSIX TZ strings. */
export const TIMEZONES: Record<string, string> = {
  'UTC': 'UTC0',
  'Europe/Amsterdam': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Andorra': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Astrakhan': 'UTC-4',
  'Europe/Athens': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Belgrade': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Berlin': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Bratislava': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Brussels': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Bucharest': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Budapest': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Busingen': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Chisinau': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Copenhagen': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Dublin': 'GMT0IST,M3.5.0/1,M10.5.0',
  'Europe/Gibraltar': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Guernsey': 'GMT0BST,M3.5.0/1,M10.5.0',
  'Europe/Helsinki': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Isle_of_Man': 'GMT0BST,M3.5.0/1,M10.5.0',
  'Europe/Istanbul': 'TRT-3',
  'Europe/Jersey': 'GMT0BST,M3.5.0/1,M10.5.0',
  'Europe/Kaliningrad': 'EET-2',
  'Europe/Kiev': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Kirov': 'MSK-3',
  'Europe/Kyiv': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Lisbon': 'WET0WEST,M3.5.0/1,M10.5.0',
  'Europe/Ljubljana': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/London': 'GMT0BST,M3.5.0/1,M10.5.0',
  'Europe/Luxembourg': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Madrid': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Malta': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Mariehamn': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Minsk': 'MSK-3',
  'Europe/Monaco': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Moscow': 'MSK-3',
  'Europe/Oslo': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Paris': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Podgorica': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Prague': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Riga': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Rome': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Samara': 'UTC-4',
  'Europe/San_Marino': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Sarajevo': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Saratov': 'UTC-4',
  'Europe/Simferopol': 'MSK-3',
  'Europe/Skopje': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Sofia': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Stockholm': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Tallinn': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Tirane': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Ulyanovsk': 'UTC-4',
  'Europe/Vaduz': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Vatican': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Vienna': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Vilnius': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Volgograd': 'MSK-3',
  'Europe/Warsaw': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Zagreb': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Zurich': 'CET-1CEST,M3.5.0,M10.5.0/3',
};

/**
 * @brief Find the display timezone name for a POSIX TZ string.
 * @param posix POSIX TZ string from firmware config.
 * @returns Matching IANA name, or DEFAULT_TZ_NAME.
 */
export function nameForPosix(posix: string): string {
  for (const z in TIMEZONES) if (TIMEZONES[z] === posix) return z;
  return DEFAULT_TZ_NAME;
}
