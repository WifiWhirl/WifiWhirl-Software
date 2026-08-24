import { useEffect, useRef, useState } from 'preact/hooks';
import { saveConfig, getText, getJson, setupNeeded, apMode } from '../api';
import { Card, Row, Toggle, Segmented } from '../components/ui';
import { NumField, TextField } from '../components/fields';
import { showRestart, showToast, ToastView } from '../components/feedback';
import { lastStates, refreshCommsOnce, sendCommand } from '../comms';
import { currencies, currency, currencySymbol, setCurrency } from '../currency';
import { t, languages, locale, setLocale } from '../i18n';
import { dark, toggleTheme } from '../theme';
import { TIMEZONES, DEFAULT_TZ, DEFAULT_TZ_NAME, nameForPosix } from '../data/timezones';
import { Icon, wifiSignalIconName } from '../components/icons';
import type { WifiNetwork, WifiScanResult } from '../types/config';

/**
 * @brief First-run Setup Assistant. Replaces the old WiFiManager captive portal
 * and chains the existing config endpoints into a guided onboarding flow.
 *
 * Two phases driven by the firmware /auth/status flags:
 *   - apMode (SoftAP captive portal): language -> WiFi. Saving WiFi reboots the
 *     device to join the chosen network; on reconnect the wizard resumes below.
 *   - post-connect (setupComplete false): language -> pump -> pool/region ->
 *     finish (optional login + update auto-check), which sets setupComplete.
 */
export function SetupAssistant() {
  const ap = apMode.value;
  const steps = ap ? ['welcome', 'wifi'] : ['welcome', 'pump', 'pool', 'finish'];
  const [step, setStep] = useState(() => resumeStep(steps));
  const [busy, setBusy] = useState(false);

  // post-connect config, loaded once
  const [cio, setCio] = useState(1);
  const [pool, setPool] = useState('700');
  const [untSel, setUntSel] = useState<boolean | null>(null);
  const [price, setPrice] = useState('0.35');
  const [weather, setWeather] = useState(false);
  const [tzName, setTzName] = useState(DEFAULT_TZ_NAME);
  const [hwRestart, setHwRestart] = useState(false);

  // wifi step
  const [ssid, setSsid] = useState('');
  const [wifiPwd, setWifiPwd] = useState('');
  const [manualSsid, setManualSsid] = useState(false); // hidden networks: type the SSID
  const [focusPwd, setFocusPwd] = useState(false);
  const pwdRef = useRef<HTMLInputElement>(null);
  const [networks, setNetworks] = useState<WifiNetwork[] | null>(null);
  const [scanning, setScanning] = useState(false);
  const [connecting, setConnecting] = useState(false);

  // finish step
  const [authEnabled, setAuthEnabled] = useState(false);
  const [authUser, setAuthUser] = useState('');
  const [authPwd, setAuthPwd] = useState('');
  const [authPwd2, setAuthPwd2] = useState('');
  const [autoUpdate, setAutoUpdate] = useState(false);
  const [finishing, setFinishing] = useState(false);

  useEffect(() => {
    if (ap) return;
    getText('/gethardware/').then((txt) => {
      try { setCio(parseInt(String(JSON.parse(txt).cio), 10) || 0); } catch { /* keep defaults */ }
    }).catch(() => {});
    getJson<Record<string, unknown>>('/getconfig/').then((j) => {
      if (j.POOLCAP != null) setPool(String(j.POOLCAP));
      // Cents grid: firmware < 2.0.0 stored the price in a float, so a stock
      // device reports 0.349999994 instead of 0.35.
      if (j.PRICE != null) setPrice(String(cents(Number(j.PRICE))));
      setWeather(!!j.WEATHER);
      const name = j.TIMEZONE_NAME && TIMEZONES[String(j.TIMEZONE_NAME)]
        ? String(j.TIMEZONE_NAME) : nameForPosix(String(j.TIMEZONE || DEFAULT_TZ));
      setTzName(name);
    }).catch(() => {});
    void refreshCommsOnce(); // live STATES carry the pump's current temperature unit
  }, [ap]);

  const key = steps[step];
  const last = step === steps.length - 1;

  // The panel's current unit comes from the live states. /getconfig UNT is the
  // unit *button* enable flag (see the Config page), so the wizard must never
  // write the unit choice there - that only disabled the physical key.
  const deviceUnt = !!lastStates.value?.UNT;
  const unt = untSel ?? deviceUnt;

  // Scan as soon as the WiFi step opens. Without it the empty list read as
  // "no networks found" and nothing pointed at the scan button.
  useEffect(() => {
    if (key !== 'wifi') return;
    // Never open this step with an input focused: the mobile keyboard would
    // cover half the screen, including the network list.
    (document.activeElement as HTMLElement | null)?.blur?.();
    void scan();
  }, [key]);

  // Picking a network mounts the password field: put the cursor in it, so the
  // next thing typed lands where it should. Only ever in reaction to that tap -
  // the step itself still opens with nothing focused.
  useEffect(() => {
    if (!focusPwd) return;
    pwdRef.current?.focus();
    setFocusPwd(false);
  }, [focusPwd]);

  // Open networks may keep an empty password; everything else needs a WPA key.
  const picked = networks?.find((n) => n.ssid === ssid);
  const pwdOk = wifiPwd.length === 0
    ? picked != null && !isEncrypted(picked.enc)
    : wifiPwd.length >= 8 && wifiPwd.length <= 63;
  const canConnect = !!ssid.trim() && ssid.length <= 32 && pwdOk;

  async function scan() {
    setScanning(true);
    try {
      const r = await getJson<WifiScanResult>('/scanwifi/');
      setNetworks(Array.isArray(r.networks) ? r.networks.filter((n) => n.ssid).sort((a, b) => b.rssi - a.rssi) : []);
    } catch { setNetworks([]); showToast(t('wifi.scanError'), true); } // don't leave the step on "scanning…"
    setScanning(false);
  }

  // Persist the current step, then advance. Returns false on validation/save error.
  async function persistStep(): Promise<boolean> {
    if (key === 'pump') {
      // Send cio only: the firmware derives dsp from it. Keeping the loaded
      // hwcfg (which carries the *old* dsp) left the previous display decoder
      // in place, so the physical panel keys ended up on the wrong functions.
      const r = await saveConfig('/sethardware/', { cio });
      // AirJet and HydroJet use different button sequences, and cio/dsp are
      // built once at boot - so wait out the restart before the unit step
      // instead of driving key sequences on the old model.
      if (r.kind === 'restart') {
        sessionStorage.setItem(RESUME_KEY, 'pool'); // come back after the reboot, not at step 1
        setHwRestart(true);
        setTimeout(() => location.reload(), 30000);
        return false;
      }
      return r.kind !== 'error';
    }
    if (key === 'pool') {
      // SETUNIT is "set to this unit", not a blind toggle, so re-sending is safe.
      if (untSel != null && untSel !== deviceUnt) sendCommand('toggleUnit', untSel);
      const r = await saveConfig('/setconfig/', {
        POOLCAP: Number(pool) || 0, WEATHER: weather,
        PRICE: cents(Number(price)),
        TIMEZONE: TIMEZONES[tzName] || DEFAULT_TZ, TIMEZONE_NAME: tzName,
      });
      return r.kind !== 'error';
    }
    return true;
  }

  async function next() {
    setBusy(true);
    const ok = await persistStep();
    setBusy(false);
    if (ok) setStep(step + 1);
  }

  function connectWifi() {
    if (!ssid.trim() || ssid.length > 32) { showToast(t('wifi.errSsid'), true); return; }
    if (!pwdOk) { showToast(t('wifi.errPassword'), true); return; }
    // Saving WiFi reboots the device, which drops this SoftAP - the HTTP
    // response is usually lost and the device moves to a new address. So show
    // the "reconnecting" panel immediately and fire the request without
    // depending on a reply (mirrors WiFiManager's saved-credentials page).
    setConnecting(true);
    const payload: Record<string, unknown> = { enableAp: true, apSsid: ssid.trim(), enableWM: true };
    if (wifiPwd) payload.apPwd = wifiPwd;
    void saveConfig('/setwifi/', payload); // fire-and-forget; reboot drops the connection
  }

  async function finish() {
    if (authEnabled) {
      if (!authUser.trim()) { showToast(t('dev.errNoUser'), true); return; }
      if (!authPwd) { showToast(t('dev.errNoPwd'), true); return; }
      if (authPwd !== authPwd2) { showToast(t('dev.errPwdMismatch'), true); return; }
    }
    const willReboot = authEnabled; // enabling login restarts the device
    const data: Record<string, unknown> = { webUpdateEnabled: autoUpdate, setupComplete: true };
    if (authEnabled) { data.authEnabled = true; data.authUser = authUser.trim(); data.authPwd = authPwd; }
    setFinishing(true);
    const r = await saveConfig('/setdevice/', data);
    // Leave the wizard on every terminal path. The normal shell (not the wizard)
    // renders the reboot overlay and the login gate, so we must drop setupNeeded
    // before showing them - otherwise the wizard stays mounted and looks stuck.
    if (r.kind === 'saved' || r.kind === 'nochange') { setupNeeded.value = false; return; } // no reboot -> dashboard
    if (r.kind === 'restart' || willReboot) {
      // Device is rebooting (login enabled). setupComplete was persisted before
      // the reboot, so the overlay + reload lands on the login screen even when
      // the reply was lost.
      setupNeeded.value = false;
      showRestart(r.kind === 'restart' ? r.reason : undefined);
      return;
    }
    // Genuine error without a reboot: stay so the user can retry.
    setFinishing(false);
    showToast(t('common.error'), true);
  }

  // Pressing Enter in any input submits the form -> run the current step's
  // primary action (Next / Connect / Finish). Sub-buttons (scan, +/-, segmented,
  // theme, Back) are type="button" so they never submit.
  function submitStep() {
    if (busy || connecting || finishing || hwRestart) return;
    if (key === 'wifi') { connectWifi(); return; }
    if (key === 'finish') { void finish(); return; }
    if (!last) void next();
  }

  return (
    <div class="app login-app">
      <main class="content login-content">
        <form class="setup-assistant" onSubmit={(e) => { e.preventDefault(); submitStep(); }}>
          <header class="setup-head">
            <img src="/logo.png" alt="" class="setup-logo" />
            <h1>{t('setup.title')}</h1>
            <p class="setup-progress">{t('setup.stepOf', { n: step + 1, total: steps.length })}</p>
          </header>

          {key === 'welcome' && (
            <Card title={t('setup.welcomeTitle')}>
              <p class="hint">{ap ? t('setup.welcomeApInfo') : t('setup.welcomeInfo')}</p>
              <Row label={t('common.language')}>
                <select class="language-select" aria-label={t('common.language')}
                  value={locale.value} onChange={(e) => setLocale((e.target as HTMLSelectElement).value)}>
                  {languages.map((l) => (
                    <option key={l.code} value={l.code}>
                      {l.code === 'auto' ? t('common.languageAuto') : l.label}{l.beta ? ` (${t('common.beta')})` : ''}
                    </option>
                  ))}
                </select>
              </Row>
              <Row label={t('common.theme')}>
                <button type="button" class={`theme-toggle${dark.value ? ' on' : ''}`}
                  title={t('common.theme')} aria-label={t('common.theme')} aria-pressed={dark.value} onClick={toggleTheme}>
                  <span class="theme-toggle-track"><span class="theme-toggle-thumb">
                    <Icon name={dark.value ? 'moon' : 'sun'} size={17} />
                  </span></span>
                </button>
              </Row>
            </Card>
          )}

          {key === 'wifi' && connecting && (
            <Card title={t('setup.wifiSavingTitle')}>
              <p>{t('setup.wifiSavingInfo', { ssid: ssid.trim() })}</p>
              <p class="hint">{t('setup.wifiSavingHint')}</p>
            </Card>
          )}

          {key === 'wifi' && !connecting && (
            <Card title={t('setup.wifiTitle')}>
              <p class="hint">{t('setup.wifiInfo')}</p>
              <div class="wifi-list-head">
                <span>{t('wifi.scanResults')}</span>
                <button type="button" class="btn ghost wifi-refresh-btn" disabled={scanning} onClick={scan}>
                  <Icon name="restart" size={17} />
                  {scanning ? t('wifi.scanning') : t('wifi.refresh')}
                </button>
              </div>
              {networks === null && <p class="hint">{t('wifi.scanning')}</p>}
              {networks && networks.length > 0 && (
                <>
                  <div class="wifi-network-list" role="list" aria-label={t('wifi.scanResults')}>
                    {networks.map((n) => (
                      <button key={`${n.ssid}-${n.rssi}`} type="button"
                        class={`wifi-network${n.ssid === ssid ? ' active' : ''}`}
                        onClick={() => { setSsid(n.ssid); setFocusPwd(true); }}>
                        <span class="wifi-network-name">{n.ssid}</span>
                        <span class="wifi-network-meta">
                          {isEncrypted(n.enc) && <span class="wifi-network-chip">{t('wifi.secured')}</span>}
                          <Icon name={wifiSignalIconName(signalBars(n.rssi))} size={24} />
                        </span>
                      </button>
                    ))}
                  </div>
                  {networks.length > 3 && <p class="hint">{t('wifi.scrollHint')}</p>}
                </>
              )}
              {networks && networks.length === 0 && <p class="hint">{t('wifi.scanEmpty')}</p>}
              {/* Render no input at all until the user asks for one: something in
                  the captive-portal browsers focuses the first text field as soon
                  as the list renders, and the keyboard then hides the list. */}
              {!manualSsid && (
                <button type="button" class="btn ghost wifi-manual-btn" onClick={() => setManualSsid(true)}>
                  {t('setup.wifiSsidManual')}
                </button>
              )}
              {manualSsid && <TextField label={t('wifi.ssid')} value={ssid} maxLength={32} onInput={setSsid} />}
              {(manualSsid || ssid) && (
                <>
                  <TextField label={t('wifi.password')} type="password" value={wifiPwd} onInput={setWifiPwd} inputRef={pwdRef} />
                  {!pwdOk && <p class="hint">{t('wifi.errPassword')}</p>}
                </>
              )}
            </Card>
          )}

          {key === 'pump' && hwRestart && (
            <Card title={t('setup.pumpRestartTitle')}>
              <p>{t('setup.pumpRestartInfo')}</p>
              <p class="hint">{t('common.rebootInfo')}</p>
            </Card>
          )}

          {key === 'pump' && !hwRestart && (
            <Card title={t('setup.pumpTitle')}>
              <p class="hint">{t('hardware.selectModel')}</p>
              <Segmented value={cio} onChange={setCio} options={pumpModels()} />
              <p class="hint">{t('setup.pumpModelInfo')}</p>
            </Card>
          )}

          {key === 'pool' && (
            <Card title={t('setup.poolTitle')}>
              <NumField label={t('spa.poolCap')} value={pool} min={100} max={9999} unit={t('spa.liters')} onInput={setPool} />
              <Row label={t('config.unit')}>
                <Segmented<'c' | 'f'> value={unt ? 'c' : 'f'}
                  options={[{ value: 'c', label: '°C' }, { value: 'f', label: '°F' }]}
                  onChange={(v) => setUntSel(v === 'c')} />
              </Row>
              <Row label={t('cfg.timezone')}>
                <select value={tzName} onChange={(e) => setTzName((e.target as HTMLSelectElement).value)}>
                  {Object.keys(TIMEZONES).sort().map((z) => (
                    <option key={z} value={z}>{z.replace(/_/g, ' ')}</option>
                  ))}
                </select>
              </Row>
              <Row label={t('cfg.currency')}>
                <Segmented value={currency.value}
                  options={currencies.map((c) => ({ value: c.code, label: c.symbol }))}
                  onChange={setCurrency} />
              </Row>
              <NumField label={t('spa.price')} value={price} min={0} max={9.99} step={0.01}
                unit={currencySymbol()} onInput={setPrice} />
              <Row label={t('cfg.weatherData')}>
                <Segmented<'off' | 'on'> value={weather ? 'on' : 'off'}
                  options={[{ value: 'off', label: t('cfg.disabled') }, { value: 'on', label: t('cfg.enabled') }]}
                  onChange={(v) => setWeather(v === 'on')} />
              </Row>
              {weather && <p class="hint">{t('cfg.weatherFromCloud')}</p>}
            </Card>
          )}

          {key === 'finish' && finishing && (
            <Card title={t('setup.finishingTitle')}>
              <p>{t('setup.finishingInfo')}</p>
            </Card>
          )}

          {key === 'finish' && !finishing && (
            <Card title={t('setup.finishTitle')}>
              <p class="hint">{t('setup.authInfo')}</p>
              <Row label={t('dev.authEnable')}><Toggle checked={authEnabled} onChange={setAuthEnabled} /></Row>
              {authEnabled && (
                <>
                  <TextField label={t('login.user')} value={authUser} onInput={setAuthUser} />
                  <TextField label={t('login.password')} type="password" value={authPwd} onInput={setAuthPwd} />
                  <TextField label={t('dev.passwordConfirm')} type="password" value={authPwd2} onInput={setAuthPwd2} />
                </>
              )}
              <p class="hint">{t('setup.updateInfo')}</p>
              <Row label={t('setup.autoUpdate')}><Toggle checked={autoUpdate} onChange={setAutoUpdate} /></Row>
            </Card>
          )}

          {!connecting && !finishing && !hwRestart && (
            <div class="actions setup-actions">
              {step > 0 && <button type="button" class="btn" disabled={busy} onClick={() => setStep(step - 1)}>{t('setup.back')}</button>}
              {key === 'wifi' && <button type="submit" class="btn primary" disabled={busy || !canConnect}>{t('setup.connect')}</button>}
              {key === 'finish' && <button type="submit" class="btn primary" disabled={busy}>{t('setup.finishBtn')}</button>}
              {key !== 'wifi' && !last && <button type="submit" class="btn primary" disabled={busy}>{t('setup.next')}</button>}
            </div>
          )}
        </form>
      </main>
      <ToastView />
    </div>
  );
}

// Setting the pump model reboots the device, which reloads this page. Remember
// where to continue so the wizard doesn't restart at the welcome screen.
const RESUME_KEY = 'ww.setupResume';

function resumeStep(steps: string[]): number {
  const key = sessionStorage.getItem(RESUME_KEY);
  if (!key) return 0;
  sessionStorage.removeItem(RESUME_KEY);
  return Math.max(0, steps.indexOf(key));
}

/** @brief Snap a money amount to whole cents (the grid every price input uses). */
function cents(v: number): number {
  return Number.isFinite(v) ? Math.round(v * 100) / 100 : 0;
}

function isEncrypted(enc: WifiNetwork['enc']): boolean {
  return enc === true || (typeof enc === 'number' && enc !== 0);
}

function pumpModels(): { value: number; label: string }[] {
  const m = [
    { value: 1, label: t('hardware.airjet') },
    { value: 2, label: t('hardware.hydrojet') },
    { value: 0, label: t('hardware.egg') },
  ];
  if (import.meta.env.VITE_ENABLE_MSPA === '1') m.push({ value: 3, label: t('hardware.mspa') });
  return m;
}

function signalBars(rssi: number): number {
  if (rssi <= -80) return 1;
  if (rssi <= -70) return 2;
  if (rssi <= -67) return 3;
  return 4;
}
