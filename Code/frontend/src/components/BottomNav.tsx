import { t } from '../i18n';
import { navigate, route } from '../router';
import { Icon, type IconName } from './icons';

/**
 * @brief Mobile bottom navigation with primary tabs and menu opener.
 */
export function BottomNav({ onMenu }: { onMenu: () => void }) {
  const r = route.value;
  const tab = (path: string, icon: IconName, label: string) => (
    <button type="button" class={`bn-item${r === path ? ' active' : ''}`}
      aria-current={r === path ? 'page' : undefined}
      onClick={() => { navigator.vibrate?.(10); navigate(path); }}>
      <Icon name={icon} /><span>{label}</span>
    </button>
  );
  return (
    <nav class="bottomnav" aria-label={t('nav.menu')}>
      {tab('/', 'home', t('tab.home'))}
      {tab('/automation', 'automation', t('tab.auto'))}
      {tab('/smartschedule', 'smartschedule', t('tab.schedule'))}
      <button type="button" class="bn-item" onClick={() => { navigator.vibrate?.(10); onMenu(); }}>
        <Icon name="menu" /><span>{t('tab.menu')}</span>
      </button>
    </nav>
  );
}
