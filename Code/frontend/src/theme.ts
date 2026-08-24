import { signal } from '@preact/signals';

// Preserve the legacy key/value so users keep their setting across the migration
// (old darkmode.js stored "On"/"Off" under "darkModeStatus").
const KEY = 'darkModeStatus';

/**
 * @brief Current dark-theme state.
 */
export const dark = signal(localStorage.getItem(KEY) === 'On');

function apply(on: boolean): void {
  document.documentElement.setAttribute('data-theme', on ? 'dark' : 'light');
  // keep the old class too: function.js status pages read .darkmode
  document.documentElement.classList.toggle('darkmode', on);
  document.querySelectorAll('meta[name="theme-color"]').forEach((m) => m.remove());
  const meta = document.createElement('meta');
  meta.name = 'theme-color';
  meta.content = on ? '#0f171d' : '#edf7f6';
  document.head.appendChild(meta);
}

apply(dark.value);

/**
 * @brief Toggle the persisted theme and apply it to the document root.
 */
export function toggleTheme(): void {
  dark.value = !dark.value;
  localStorage.setItem(KEY, dark.value ? 'On' : 'Off');
  apply(dark.value);
}
