import { useEffect } from 'preact/hooks';
import { route, navigate } from '../router';
import { t } from '../i18n';
import { dark, toggleTheme } from '../theme';
import { lastOther } from '../comms';
import { postJson, logout, authSessionActive, refreshAuthStatus, expertMode, cloudAvailable } from '../api';
import { confirmDialog, showRestart } from './feedback';
import { Icon, type IconName } from './icons';

interface Item { path: string; key: string; icon: IconName; }

const items: Item[] = [
  { path: '/', key: 'nav.dashboard', icon: 'dashboard' },
  { path: '/automation', key: 'nav.automation', icon: 'automation' },
  { path: '/smartschedule', key: 'nav.smartschedule', icon: 'smartschedule' },
  { path: '/config', key: 'nav.config', icon: 'config' },
  { path: '/mqtt', key: 'nav.mqtt', icon: 'mqtt' },
  { path: '/wifi', key: 'nav.wifi', icon: 'wifi' },
  { path: '/device', key: 'nav.device', icon: 'device' },
  { path: '/hardware', key: 'nav.hardware', icon: 'hardware' },
];

// Expert (wattage) page is only navigable while expert mode is enabled.
const expertItem: Item = { path: '/expert', key: 'nav.expert', icon: 'binary' };
// Cloud page is only shown on preseeded units (a PSK is provisioned).
const cloudItem: Item = { path: '/cloud', key: 'nav.cloud', icon: 'cloud' };

/**
 * @brief Primary side navigation with restart/logout and preferences.
 */
export function NavBar({
  open,
  collapsed,
  onClose,
  onToggleCollapse,
}: {
  open: boolean;
  collapsed: boolean;
  onClose: () => void;
  onToggleCollapse: () => void;
}) {
  useEffect(() => { void refreshAuthStatus(); }, []);

  function go(path: string) { navigate(path); onClose(); }

  // Newer firmware found by the device's daily check. Badges the Info entry.
  const newFw = lastOther.value?.NEWFW;

  // Insert cloud (preseeded only) after MQTT, and expert at the end, per state.
  const navItems = [...items];
  if (cloudAvailable.value) navItems.splice(navItems.findIndex((i) => i.path === '/mqtt') + 1, 0, cloudItem);
  if (expertMode.value) navItems.push(expertItem);

  async function restart() {
    if (!await confirmDialog(t('confirm.restart'), { danger: true })) return;
    onClose();
    try { await postJson('/restart/'); } catch { /* connection drops on reboot */ }
    showRestart();
  }

  async function doLogout() {
    await logout();
    location.reload();
  }

  return (
    <>
      {open && <div class="nav-scrim" onClick={onClose} />}
      <nav class={`nav${open ? ' open' : ''}${collapsed ? ' collapsed' : ''}`} aria-label={t('nav.menu')}>
        <button
          type="button"
          class="nav-collapse"
          aria-label={t('nav.menu')}
          aria-pressed={collapsed}
          title={t('nav.menu')}
          onClick={onToggleCollapse}
        >
          <Icon name="menu" size={22} />
        </button>
        <div class="nav-group">
          {navItems.map((it) => (
            <a
              key={it.path}
              href={`#${it.path}`}
              class={route.value === it.path ? 'active' : ''}
              aria-label={t(it.key)}
              onClick={(e) => { e.preventDefault(); go(it.path); }}
              title={t(it.key)}
            >
              <Icon name={it.icon} size={20} stroke={1.8} class="ic" />
              <span class="nav-label">{t(it.key)}</span>
            </a>
          ))}
        </div>
        <div class="nav-group nav-bottom">
          <a
            href="#/info"
            class={route.value === '/info' ? 'active' : ''}
            aria-label={newFw ? `${t('nav.info')} - ${t('info.updateBadge')}` : t('nav.info')}
            onClick={(e) => { e.preventDefault(); go('/info'); }}
            title={newFw ? `${t('info.updateBadge')} (${newFw})` : t('nav.info')}
          >
            <Icon name="info" size={20} stroke={1.8} class="ic" />
            <span class="nav-label">{t('nav.info')}</span>
            {newFw && <span class="nav-badge" aria-hidden="true" />}
          </a>
          <a href="#" class="danger" aria-label={t('nav.restart')} title={t('nav.restart')} onClick={(e) => { e.preventDefault(); restart(); }}>
            <Icon name="restart" size={20} stroke={1.8} class="ic" />
            <span class="nav-label">{t('nav.restart')}</span>
          </a>
          {authSessionActive.value && (
            <a href="#" aria-label={t('login.logout')} title={t('login.logout')} onClick={(e) => { e.preventDefault(); doLogout(); }}>
              <Icon name="logout" size={20} stroke={1.8} class="ic" />
              <span class="nav-label">{t('login.logout')}</span>
            </a>
          )}
        </div>
        <div class="nav-prefs">
          <button type="button" class={`theme-toggle${dark.value ? ' on' : ''}`} title={t('common.theme')} aria-label={t('common.theme')} aria-pressed={dark.value} onClick={toggleTheme}>
            <span class="theme-toggle-track"><span class="theme-toggle-thumb"><Icon name={dark.value ? 'moon' : 'sun'} size={17} /></span></span>
          </button>
          {lastOther.value?.FW && <span class="fw-tag">v{lastOther.value.FW}</span>}
        </div>
      </nav>
    </>
  );
}
