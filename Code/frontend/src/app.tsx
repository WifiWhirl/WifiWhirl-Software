import { useEffect, useState } from 'preact/hooks';
import { route, navigate, matchRoute, type RouteDef } from './router';
import { t } from './i18n';
import { NavBar } from './components/NavBar';
import { BottomNav } from './components/BottomNav';
import { MqttSignal, WifiSignal } from './components/StatusIndicators';
import { AuthGate } from './components/AuthGate';
import { DialogView, ToastView, RebootView } from './components/feedback';
import { commsOk, pollingActive, refreshCommsOnce } from './comms';
import { needsLogin, setupNeeded, refreshAuthStatus, cloudAvailable } from './api';
import { SetupAssistant } from './pages/SetupAssistant';
import { Icon } from './components/icons';
import { editMode } from './util/dashboardLayout';

import { Dashboard } from './pages/Dashboard';
import { Config } from './pages/Config';
import { Wifi } from './pages/Wifi';
import { Mqtt } from './pages/Mqtt';
import { Cloud } from './pages/Cloud';
import { Hardware } from './pages/Hardware';
import { Device } from './pages/Device';
import { Expert } from './pages/Expert';
import { Automation } from './pages/Automation';
import { SmartSchedule } from './pages/SmartSchedule';
import { Info } from './pages/Info';
import { Update } from './pages/Update';

const routes: RouteDef[] = [
  { path: '/', component: Dashboard },
  { path: '/config', component: Config },
  { path: '/wifi', component: Wifi },
  { path: '/mqtt', component: Mqtt },
  { path: '/cloud', component: Cloud },
  { path: '/hardware', component: Hardware },
  { path: '/device', component: Device },
  { path: '/expert', component: Expert },
  { path: '/automation', component: Automation },
  { path: '/smartschedule', component: SmartSchedule },
  { path: '/info', component: Info },
  { path: '/update', component: Update },
];

function RouteContent() {
  const currentRoute = route.value;
  const isWeb = currentRoute === '/web';
  // Cloud page only exists on preseeded units; bounce it away otherwise so a
  // stale hash can't reach a page the nav intentionally hides.
  const cloudBlocked = currentRoute === '/cloud' && cloudAvailable.value === false;
  // Flag not fetched yet: hold the route instead of redirecting.
  const cloudPending = currentRoute === '/cloud' && cloudAvailable.value === null;

  // Hook runs unconditionally (stable order); side-effecting redirect/refresh
  // lives here, not during render.
  useEffect(() => {
    if (isWeb) { navigate('/config'); return; }
    if (cloudBlocked) { navigate('/'); return; }
    if (currentRoute !== '/') { void refreshCommsOnce(); editMode.value = false; }
  }, [currentRoute, cloudBlocked]);

  if (isWeb || cloudBlocked || cloudPending) return null;

  const Page = matchRoute(routes, currentRoute) ?? Dashboard;
  // key restarts the entrance animation on each route change without touching the shell.
  return <div key={currentRoute} class="route-fade"><Page /></div>;
}

function DashboardEditButton() {
  const currentRoute = route.value;
  if (currentRoute !== '/') return null;

  return (
    <button
      class={`topbar-edit${editMode.value ? ' active' : ''}`}
      aria-label={t(editMode.value ? 'dash.done' : 'dash.edit')}
      aria-pressed={editMode.value}
      onClick={() => { editMode.value = !editMode.value; }}
    >
      <Icon name={editMode.value ? 'done' : 'edit'} size={20} />
    </button>
  );
}

/**
 * @brief Root SPA component that switches between login and authenticated shell.
 */
export function App() {
  const [navOpen, setNavOpen] = useState(false);
  const [navCollapsed, setNavCollapsed] = useState(() => localStorage.getItem('ww.navCollapsed') === '1');

  // The normal shell only fetches auth status from the NavBar, which never
  // mounts while onboarding, so fetch it here to learn setupComplete/apMode.
  useEffect(() => { void refreshAuthStatus(); }, []);

  // First-run onboarding takes over the whole screen (auth is off on a fresh
  // device, so this also precedes the login gate).
  if (setupNeeded.value) return <SetupAssistant />;

  if (needsLogin.value) {
    return (
      <div class="app login-app">
        <main class="content login-content">
          <AuthGate>
            <RouteContent />
          </AuthGate>
        </main>
        <DialogView />
        <ToastView />
        <RebootView />
      </div>
    );
  }

  return (
    <div class="app">
      <NavBar
        open={navOpen}
        collapsed={navCollapsed}
        onClose={() => setNavOpen(false)}
        onToggleCollapse={() => {
          const next = !navCollapsed;
          setNavCollapsed(next);
          localStorage.setItem('ww.navCollapsed', next ? '1' : '0');
        }}
      />
      <div class="app-main">
        <header class="topbar">
          <a href="#/" class="topbar-brand" title={t('nav.dashboard')} aria-label={t('nav.dashboard')}>
              <img src="/logo.png" alt="" class="topbar-logo" />
            <span class="topbar-tagline">WifiWhirl</span>
          </a>
          <span class="topbar-spacer" />
          <DashboardEditButton />
          <MqttSignal />
          <WifiSignal />
          <button class="iconbtn hamburger" aria-label={t('nav.menu')} onClick={() => setNavOpen(true)}>☰</button>
        </header>
        {pollingActive.value && !commsOk.value && (
          <div class="offline-banner" role="status" aria-live="polite">{t('common.offline')}</div>
        )}
        <main class="content">
          <AuthGate>
            <RouteContent />
          </AuthGate>
        </main>
      </div>
      <BottomNav onMenu={() => setNavOpen(true)} />
      <DialogView />
      <ToastView />
      <RebootView />
    </div>
  );
}
