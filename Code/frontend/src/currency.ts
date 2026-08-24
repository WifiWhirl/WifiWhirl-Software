import { signal } from '@preact/signals';
import { activeLocale } from './i18n';

export type CurrencyCode = 'EUR' | 'PLN' | 'GBP';

export const currencies: { code: CurrencyCode; label: string; symbol: string }[] = [
  { code: 'EUR', label: 'Euro', symbol: '€' },
  { code: 'PLN', label: 'Polski złoty', symbol: 'zł' },
  { code: 'GBP', label: 'British pound', symbol: '£' },
];

function isCurrencyCode(v: string | null): v is CurrencyCode {
  return v === 'EUR' || v === 'PLN' || v === 'GBP';
}

function defaultCurrency(): CurrencyCode {
  return activeLocale.value === 'pl' ? 'PLN' : 'EUR';
}

const savedCurrency = localStorage.getItem('currency');
export const currency = signal<CurrencyCode>(isCurrencyCode(savedCurrency) ? savedCurrency : defaultCurrency());

export function setCurrency(v: string): void {
  if (!isCurrencyCode(v)) return;
  currency.value = v;
  localStorage.setItem('currency', v);
}

export function currencySymbol(code = currency.value): string {
  return currencies.find((c) => c.code === code)?.symbol ?? '€';
}
