import { useEffect, useState } from 'preact/hooks';
import { postJson, saveConfig, expertMode } from '../api';
import { Card, SaveButton } from '../components/ui';
import { TextField } from '../components/fields';
import { Toggle } from '../components/ui';
import { Row } from '../components/ui';
import { showRestart, showToast, confirmDialog } from '../components/feedback';
import { StatusHero } from '../components/LinkHero';
import { PageSkeleton } from '../components/Skeleton';
import { languages, locale, setLocale, t } from '../i18n';
import { validDeviceHostname } from '../util/validation';
import { TIMEZONES, DEFAULT_TZ, DEFAULT_TZ_NAME, nameForPosix } from '../data/timezones';

interface DevCfg {
  // No apPwd/otaPwd: the firmware never sends stored passwords back.
  hostname: string;
  authEnabled: boolean; authUser: string;
  webhookEnabled: boolean;
  webhookAuthEnabled: boolean; webhookAuthUser: string; webhookAuthConfigured: boolean;
  apiEnabled: boolean;
  expertMode: boolean;
}

/**
 * @brief Device identity and global login configuration route.
 */
export function Device() {
  const [hostname, setHostname] = useState('');
  const [apPwd, setApPwd] = useState('');
  const [otaPwd, setOtaPwd] = useState('');
  const [authEnabled, setAuthEnabled] = useState(false);
  const [authUser, setAuthUser] = useState('');
  const [pwd, setPwd] = useState('');
  const [pwd2, setPwd2] = useState('');
  const [otaConfirm, setOtaConfirm] = useState('');
  const [authWasEnabled, setWasEnabled] = useState(false);
  const [webhookEnabled, setWebhookEnabled] = useState(true);
  const [webhookAuthEnabled, setWebhookAuthEnabled] = useState(false);
  const [webhookAuthUser, setWebhookAuthUser] = useState('');
  const [webhookPwd, setWebhookPwd] = useState('');
  const [webhookPwd2, setWebhookPwd2] = useState('');
  const [webhookConfigured, setWebhookConfigured] = useState(false);
  const [apiEnabled, setApiEnabled] = useState(false); // set on the Expert page, forces the login on
  const [expert, setExpert] = useState(false);
  const [tzName, setTzName] = useState(DEFAULT_TZ_NAME);
  const [loaded, setLoaded] = useState(false);

  useEffect(() => {
    postJson<DevCfg>('/getdevice/').then((j) => {
      setHostname(j.hostname || '');
      setWasEnabled(!!j.authEnabled);
      setAuthEnabled(!!j.authEnabled);
      setAuthUser(j.authUser || '');
      setWebhookEnabled(j.webhookEnabled !== false);
      setWebhookAuthEnabled(!!j.webhookAuthEnabled);
      setWebhookAuthUser(j.webhookAuthUser || '');
      setWebhookConfigured(!!j.webhookAuthConfigured);
      setApiEnabled(!!j.apiEnabled);
      setExpert(!!j.expertMode);
      setLoaded(true);
    }).catch(() => {});
    // The timezone is a firmware setting, so it comes from /getconfig/.
    postJson<{ TIMEZONE?: string; TIMEZONE_NAME?: string }>('/getconfig/').then((j) => {
      setTzName(j.TIMEZONE_NAME && TIMEZONES[j.TIMEZONE_NAME]
        ? j.TIMEZONE_NAME : nameForPosix(j.TIMEZONE || DEFAULT_TZ));
    }).catch(() => {});
  }, []);

  if (!loaded) return <PageSkeleton cards={2} rows={3} />;
  const devEditable = authEnabled && authWasEnabled;

  async function saveAuth() {
    const data: Record<string, unknown> = { authEnabled };
    if (authEnabled) {
      if (!authUser) { showToast(t('dev.errNoUser'), true); return; }
      data.authUser = authUser;
      if (pwd || pwd2 || !authWasEnabled) {
        if (pwd !== pwd2) { showToast(t('dev.errPwdMismatch'), true); return; }
        if (!pwd) { showToast(t('dev.errNoPwd'), true); return; }
        data.authPwd = pwd;
      }
      // Enabling auth from a disabled state requires the OTA password.
      if (!authWasEnabled) {
        if (!otaConfirm) { showToast(t('dev.errOtaRequired'), true); return; }
        data.otaPwdConfirm = otaConfirm;
      }
    }
    const r = await saveConfig('/setdevice/', data);
    if (r.kind === 'restart') showRestart(r.reason);
    else if (r.kind === 'error') showToast(t('common.error'), true);
    else showToast(t('common.saved'));
  }

  async function saveWebhookAuth() {
    // the API only works with the webhook credentials
    const authOn = webhookAuthEnabled || apiEnabled;
    const data: Record<string, unknown> = { webhookEnabled, webhookAuthEnabled: authOn };
    if ((webhookEnabled || apiEnabled) && authOn) {
      if (!webhookAuthUser) { showToast(t('dev.errNoUser'), true); return; }
      data.webhookAuthUser = webhookAuthUser;
      if (webhookPwd || webhookPwd2 || !webhookConfigured) {
        if (webhookPwd !== webhookPwd2) { showToast(t('dev.errPwdMismatch'), true); return; }
        if (!webhookPwd) { showToast(t('dev.errNoPwd'), true); return; }
        data.webhookAuthPwd = webhookPwd;
      }
    } else if (webhookAuthUser) {
      data.webhookAuthUser = webhookAuthUser;
    }

    const r = await saveConfig('/setdevice/', data);
    if (r.kind === 'restart') showRestart(r.reason);
    else if (r.kind === 'error') showToast(t('common.error'), true);
    else {
      if (authOn && webhookPwd) setWebhookConfigured(true);
      setWebhookPwd('');
      setWebhookPwd2('');
      showToast(t('common.saved'));
    }
  }

  async function saveExpert(v: boolean) {
    setExpert(v);
    const r = await saveConfig('/setdevice/', { expertMode: v });
    if (r.kind === 'error') { showToast(t('common.error'), true); setExpert(!v); return; }
    expertMode.value = v; // reveal/hide the Expert nav item immediately
    showToast(t('common.saved'));
  }

  function validateIdentity(): string | null {
    if (!validDeviceHostname(hostname)) return t('dev.errHostname');
    // the ESP8266 refuses a soft-AP passphrase under 8 chars: no recovery AP
    if (apPwd && (apPwd.length < 8 || apPwd.length > 63)) return t('dev.errApPwd');
    if (otaPwd && otaPwd.length < 8) return t('dev.errOtaPwd');
    return null;
  }

  const onOff = (v: boolean) => (v ? t('common.enabled') : t('common.disabled'));

  return (
    <>
      <h1>{t('device.title')}</h1>
      {/* State-first: who this device is and how protected it currently is.
          Tone mirrors the global login - green when the UI is protected. */}
      <StatusHero
        eyebrow={t('dev.heroEyebrow')}
        state={hostname.trim() || '-'}
        tone={authEnabled ? 'on' : 'off'}
        stats={[
          { label: t('dev.statLogin'), value: onOff(authEnabled) },
          { label: t('dev.statWebhooks'), value: onOff(webhookEnabled) },
          { label: t('expert.title'), value: onOff(expert) },
        ]}
      />
      {/* Language and timezone are device-wide, not part of the spa profile.
          The timezone still lives in the firmware settings, so this card saves
          to /setconfig/ (which merges by key) while the language is local only. */}
      <Card title={t('dev.localeTitle')}>
        <Row label={t('common.language')}>
          <select
            aria-label={t('common.language')}
            value={locale.value}
            onChange={(e) => setLocale((e.target as HTMLSelectElement).value)}
          >
            {languages.map((language) => (
              <option key={language.code} value={language.code}>
                {language.code === 'auto' ? t('common.languageAuto') : language.label}
                {language.beta ? ` (${t('common.beta')})` : ''}
              </option>
            ))}
          </select>
        </Row>
        <Row label={t('cfg.timezone')}>
          <select value={tzName} onChange={(e) => setTzName((e.target as HTMLSelectElement).value)}>
            {Object.keys(TIMEZONES).sort().map((z) => (
              <option key={z} value={z}>{z.replace(/_/g, ' ')}</option>
            ))}
          </select>
        </Row>
        <div class="card-save">
          <SaveButton url="/setconfig/" label={t('spa.save')}
            data={() => ({ TIMEZONE: TIMEZONES[tzName] || DEFAULT_TZ, TIMEZONE_NAME: tzName })} />
        </div>
      </Card>

      <Card title={t('dev.authTitle')}>
        <p class="hint">{t('dev.authInfo')}</p>
        <Row label={t('dev.authEnable')}>
          <Toggle checked={authEnabled} onChange={setAuthEnabled} />
        </Row>
        {authEnabled && (
          <>
            <TextField label={t('login.user')} value={authUser} onInput={setAuthUser} />
            <TextField label={t('login.password')} type="password" value={pwd} onInput={setPwd} />
            <TextField label={t('dev.passwordConfirm')} type="password" value={pwd2} onInput={setPwd2} />
            {!authWasEnabled && (
              <>
                <p class="hint">{t('dev.otaConfirmInfo')}</p>
                <TextField label={t('dev.otaConfirm')} type="password" value={otaConfirm} onInput={setOtaConfirm} />
              </>
            )}
          </>
        )}
        <p class="hint">{t('dev.authResetInfo')}</p>
        <button class="btn" onClick={saveAuth}>{t('dev.saveAuth')}</button>
      </Card>

      <Card title={t('dev.identityTitle')}>
        {!devEditable && <p class="hint">{t('dev.identityLocked')}</p>}
        <Row label={t('dev.hostname')}>
          <input type="text" value={hostname} disabled={!devEditable} maxLength={63}
            onInput={(e) => setHostname((e.target as HTMLInputElement).value)} />
        </Row>
        <Row label={t('dev.apPwd')}>
          <input type="password" value={apPwd} disabled={!devEditable}
            placeholder={t('mqtt.passwordPlaceholder')}
            onInput={(e) => setApPwd((e.target as HTMLInputElement).value)} />
        </Row>
        <Row label={t('dev.otaPwd')}>
          <input type="password" value={otaPwd} disabled={!devEditable}
            placeholder={t('mqtt.passwordPlaceholder')}
            onInput={(e) => setOtaPwd((e.target as HTMLInputElement).value)} />
        </Row>
        {devEditable && (
          <SaveButton url="/setdevice/" label={t('dev.saveIdentity')}
            validate={validateIdentity}
            data={() => {
              // Only send a password the user actually typed. The firmware
              // applies any field that is present, so posting an untouched
              // (empty) one would wipe the stored secret.
              const d: Record<string, unknown> = { hostname };
              if (apPwd) d.apPwd = apPwd;
              if (otaPwd) d.otaPwd = otaPwd;
              return d;
            }} />
        )}
      </Card>
      
      <Card title={t('dev.webhookAuthTitle')}>
        <p class="hint">{t('dev.webhookInfo')}</p>
        <Row label={t('dev.webhookEnable')}>
          <Toggle checked={webhookEnabled} onChange={setWebhookEnabled} />
        </Row>
        {(webhookEnabled || apiEnabled) && (
          <>
            <p class="hint">{apiEnabled ? t('dev.apiAuthRequired') : t('dev.webhookAuthInfo')}</p>
            <Row label={t('dev.webhookAuthEnable')}>
              <Toggle checked={webhookAuthEnabled || apiEnabled} disabled={apiEnabled} onChange={setWebhookAuthEnabled} />
            </Row>
            {(webhookAuthEnabled || apiEnabled) && (
              <>
                <TextField label={t('login.user')} value={webhookAuthUser} onInput={setWebhookAuthUser} />
                <TextField label={t('login.password')} type="password" value={webhookPwd}
                  placeholder={webhookConfigured ? t('mqtt.passwordPlaceholder') : undefined}
                  onInput={setWebhookPwd} />
                <TextField label={t('dev.passwordConfirm')} type="password" value={webhookPwd2} onInput={setWebhookPwd2} />
              </>
            )}
          </>
        )}
        <button class="btn" onClick={saveWebhookAuth}>{t('dev.saveWebhookAuth')}</button>
      </Card>

      <BackupCard login={authWasEnabled} />

      <Card title={t('expert.title')}>
        <p class="hint">{t('expert.modeInfo')}</p>
        <Row label={t('expert.modeEnable')}>
          <Toggle checked={expert} onChange={saveExpert} />
        </Row>
      </Card>
    </>
  );
}

/**
 * @brief download the configuration as a file and restore it (here or on
 * a replacement module). Without global login the browser asks for user
 * "backup" and the OTA password.
 */
function BackupCard({ login }: { login: boolean }) {
  const [file, setFile] = useState<File | null>(null);
  const [pwd, setPwd] = useState('');
  // Without global login the module wants user "backup" + the OTA password.
  // Sent from here: a plain download link got a 401 and the browser never
  // asked for a password.
  const headers = (): Record<string, string> =>
    login ? {} : { Authorization: 'Basic ' + btoa(unescape(encodeURIComponent('backup:' + pwd))) };
  async function download() {
    try {
      const res = await fetch('/backup/', { headers: headers() });
      if (!res.ok) { showToast(res.status === 401 ? t('backup.auth') : t('common.error'), true); return; }
      const url = URL.createObjectURL(await res.blob());
      const a = document.createElement('a');
      a.href = url;
      a.download = 'wifiwhirl-backup.json';
      a.click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
    } catch { showToast(t('common.error'), true); }
  }
  async function restore() {
    if (!file) return;
    if (!await confirmDialog(t('backup.confirm'), { title: t('backup.restore'), confirmLabel: t('backup.restore') })) return;
    const fd = new FormData();
    fd.append('file', file);
    try {
      const res = await fetch('/restore/', { method: 'POST', body: fd, headers: headers() });
      if (res.ok) showRestart('restore');
      else showToast(res.status === 401 ? t('backup.auth') : t('backup.invalid'), true);
    } catch { showToast(t('common.error'), true); }
  }
  return (
    <Card title={t('backup.title')}>
      <p class="hint">{t('backup.info')}</p>
      {!login && (
        <Row label={t('backup.otaPwd')}>
          <input type="password" autocomplete="current-password" value={pwd} onInput={(e) => setPwd((e.target as HTMLInputElement).value)} />
        </Row>
      )}
      <div class="actions">
        <button class="btn" disabled={!login && !pwd} onClick={download}>{t('backup.download')}</button>
      </div>
      <Row label={t('backup.file')}>
        <input type="file" accept=".json,application/json" onChange={(e) => setFile((e.target as HTMLInputElement).files?.[0] ?? null)} />
      </Row>
      <div class="actions">
        <button class="btn secondary" disabled={!file || (!login && !pwd)} onClick={restore}>{t('backup.restore')}</button>
      </div>
    </Card>
  );
}
