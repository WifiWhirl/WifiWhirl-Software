import { useEffect, useState } from 'preact/hooks';
import { Card, Toggle } from '../components/ui';
import { HttpError, postJson } from '../api';
import { showRestart, showToast } from '../components/feedback';
import { lastOther, refreshCommsOnce } from '../comms';
import { navigate } from '../router';
import { t } from '../i18n';

interface UpdateInfo {
  enabled: boolean;
  current: string;
  latest?: string;
  available?: boolean;
  error?: string;
}

/**
 * @brief Firmware/software information and support links route.
 */
export function Info() {
  // Pull live OTHER data once so version/build show even on a direct load.
  useEffect(() => { refreshCommsOnce(); }, []);
  const fw = lastOther.value?.FW ?? '...';
  // Show the build env as the model name, hiding the internal "_seed" suffix.
  const buildEnv = lastOther.value?.BUILDENV?.replace(/_seed$/, '');

  const [enabled, setEnabled] = useState(false);
  const [latest, setLatest] = useState('');
  const [available, setAvailable] = useState(false);
  const [busy, setBusy] = useState(false);
  const [checking, setChecking] = useState(false);

  function applyInfo(j: UpdateInfo) {
    setEnabled(!!j.enabled);
    setLatest(j.latest || '');
    setAvailable(!!j.available);
    if (j.error) showToast(`${t('info.webUpdateError')}: ${j.error}`, true);
  }

  // No body: answered from the device's cached daily check, so this is instant.
  useEffect(() => { postJson<UpdateInfo>('/getupdate/').then(applyInfo).catch(() => {}); }, []);

  // Explicit user request: bypass the cache and contact the manifest server now
  async function checkNow() {
    setChecking(true);
    try {
      applyInfo(await postJson<UpdateInfo>('/getupdate/', { force: true }));
    } catch {
      showToast(t('info.webUpdateError'), true);
    } finally {
      setChecking(false);
    }
  }

  async function onToggle(v: boolean) {
    setEnabled(v);
    await postJson('/setdevice/', { webUpdateEnabled: v }).catch(() => {});
    if (v) {
      // enabling lets the device check the manifest now
      void checkNow();
    } else {
      setLatest('');
      setAvailable(false);
    }
  }

  async function install() {
    setBusy(true); // busy bar
    try {
      await postJson('/doupdate/');
      showRestart(); // handler sends 200 then reboots
    } catch (e) {
      if (e instanceof HttpError) {
        // Rejected before anything was flashed (disabled, no newer version,
        // manifest unreachable), so the device is untouched
        showToast(t('info.webUpdateError'), true);
        setBusy(false);
      } else {
        // Connection dropped. The device is most likely still flashing or
        // already rebooting. never call that a failure here, or the user may
        // cut power mid-write. Wait for the reboot instead.
        showRestart();
      }
    }
  }

  return (
    <>
      <h1>{t('nav.info')}</h1>
      <Card title="Software">
        <div class="row"><label>{t('info.installedVersion')}</label><b>{fw}</b></div>
        {buildEnv && <div class="row"><label>{t('info.buildEnv')}</label><b>{buildEnv}</b></div>}
        <button class="btn" style="margin-top:12px" onClick={() => navigate('/update')}>
          {t('info.softwareUpdate')}
        </button>
        <p class="hint">
          {t('info.updateCheckPre')}<a href="https://wifiwhirl.de/Modul/Update/" target="_blank" rel="noreferrer">{t('info.helpPage')}</a>{t('info.updateCheckPost')}
        </p>
      </Card>

      <Card title={t('info.webUpdateTitle')}>
        <p class="hint">{t('info.webUpdateInfo')}</p>
        <div class="row">
          <label>{t('info.webUpdateEnable')}</label>
          <Toggle checked={enabled} onChange={onToggle} />
        </div>
        {enabled && (
          <>
            {latest && <div class="row"><label>{t('info.webUpdateLatest')}</label><b>{latest}</b></div>}
            {latest && !available && <p class="hint">{t('info.webUpdateUpToDate')}</p>}
            {busy ? (
              <div role="status" aria-live="polite">
                <div class="busy-bar skeleton" />
                <p class="hint">{t('info.webUpdateInstalling')}</p>
              </div>
            ) : (
              <div class="card-save">
                <button class="btn secondary" disabled={checking} onClick={checkNow}>
                  {t('info.webUpdateCheck')}
                </button>
                {available && (
                  <button class="btn" onClick={install}>
                    {t('info.webUpdateInstall')}
                  </button>
                )}
              </div>
            )}
          </>
        )}
      </Card>

      <Card title={t('info.helpTitle')}>
        <div class="row"><label>{t('info.guides')}</label>
          <a href="https://wifiwhirl.de/Modul/Bedienungsanleitung/" target="_blank" rel="noreferrer">wifiwhirl.de</a></div>
        <div class="row"><label>FAQ</label>
          <a href="https://wifiwhirl.de/FAQ/" target="_blank" rel="noreferrer">wifiwhirl.de/FAQ</a></div>
        <div class="row"><label>{t('info.supportPackage')}</label>
          <a href="/support/" target="_blank" rel="noreferrer">{t('info.createSupportPackage')}</a></div>
        <div class="row"><label>{t('info.contactLabel')}</label>
          <a href="mailto:hilfe@wifiwhirl.de">hilfe@wifiwhirl.de</a></div>
      </Card>

      <Card title={t('info.licenseTitle')}>
        <p>
          {t('info.basedOn')}{' '}
          <a href="https://github.com/visualapproach/WiFi-remote-for-Bestway-Lay-Z-SPA" target="_blank" rel="noreferrer">
            WiFi-remote-for-Bestway-Lay-Z-SPA</a>{' '}
          {t('info.basedOnAuthor')}.
        </p>
        <p>{t('info.modLicense')}{' '}
          <a href="/license.txt" target="_blank" rel="noreferrer">{t('info.license')}</a>.
        </p>
      </Card>
    </>
  );
}
