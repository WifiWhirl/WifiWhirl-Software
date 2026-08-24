import { signal } from '@preact/signals';
import de from './locales/de.json';
import en from './locales/en.json';
import fr from './locales/fr.json';
import nl from './locales/nl.json';
import pl from './locales/pl.json';

type Dict = Record<string, string>;
const dicts: Record<string, Dict> = { de, en, fr, nl, pl };
type LocaleCode = keyof typeof dicts;
type LocalePreference = LocaleCode | 'auto';
const LOCALE_STORAGE_KEY = 'localePreference';

export const languages: { code: LocalePreference; label: string; beta?: boolean }[] = [
  { code: 'auto', label: 'Auto' },
  { code: 'de', label: 'Deutsch' },
  { code: 'en', label: 'English' },
  { code: 'fr', label: 'Français', beta: true },
  { code: 'nl', label: 'Nederlands', beta: true },
  { code: 'pl', label: 'Polski', beta: true },
];

function isLocaleCode(l: string): l is LocaleCode {
  return l in dicts;
}

function isLocalePreference(l: string): l is LocalePreference {
  return l === 'auto' || isLocaleCode(l);
}

function browserLocale(): LocaleCode {
  for (const lang of navigator.languages ?? [navigator.language]) {
    const code = lang.slice(0, 2).toLowerCase();
    if (isLocaleCode(code)) return code;
  }
  return 'en';
}

function resolveLocale(preference: LocalePreference): LocaleCode {
  return preference === 'auto' ? browserLocale() : preference;
}

function initialLocale(): LocalePreference {
  const saved = localStorage.getItem(LOCALE_STORAGE_KEY);
  if (saved && isLocalePreference(saved)) return saved;
  return 'auto';
}

function applyLocale(): void {
  activeLocale.value = resolveLocale(locale.value);
  document.documentElement.lang = activeLocale.value;
}

/**
 * @brief Currently selected UI locale preference.
 */
export const locale = signal(initialLocale());

/**
 * @brief Resolved UI locale after applying "auto".
 */
export const activeLocale = signal(resolveLocale(locale.value));

applyLocale();

window.addEventListener('languagechange', () => {
  if (locale.value === 'auto') applyLocale();
});

/**
 * @brief Persist and apply a new UI locale.
 * @param l Locale code, or "auto" for browser language detection.
 */
export function setLocale(l: string): void {
  if (!isLocalePreference(l)) return;
  locale.value = l;
  localStorage.setItem(LOCALE_STORAGE_KEY, l);
  applyLocale();
}

/**
 * @brief Translate a locale key and interpolate optional variables.
 * @param key Locale dictionary key.
 * @param vars Replacement values for {name}-style placeholders.
 * @returns Localized text, or the key when no translation exists.
 */
export function t(key: string, vars?: Record<string, string | number>): string {
  let s = dicts[activeLocale.value]?.[key] ?? dicts.en[key] ?? dicts.de[key] ?? key;
  if (vars) for (const k in vars) s = s.replaceAll(`{${k}}`, String(vars[k]));
  return s;
}
