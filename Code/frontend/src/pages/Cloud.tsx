import { useEffect, useState } from 'preact/hooks';
import { postJson, getJson } from '../api';
import { Card, SaveButton } from '../components/ui';
import { CheckField } from '../components/fields';
import { LinkHero, type LinkTone } from '../components/LinkHero';
import { PageSkeleton } from '../components/Skeleton';
import { t } from '../i18n';
import type { CloudCfg, MqttCfg } from '../types/config';

// Defaults fill any field the firmware omits, so reads are never undefined.
const CLOUD_DEFAULTS: Required<CloudCfg> = {
  enabled: false, host: '', port: 8080, webUrl: '', state: 'disabled', hasPsk: false,
};

const CLOUD_STATES = ['disabled', 'disconnected', 'connecting', 'handshaking', 'connected'];
const CLOUD_TONE: Record<string, LinkTone> = {
  connected: 'on', connecting: 'pending', handshaking: 'pending', disabled: 'off',
};

interface Pairing { hostname?: string; nonce_b64?: string; proof_b64?: string; }

/** Host part of the provisioned web URL, for the stat tile. */
function webHost(url: string): string {
  try { return new URL(url).host; } catch { return url.trim() || '-'; }
}

/**
 * @brief PoolLink cloud page.
 *
 * The connection details (PSK, server host/port, web URL) are seeded once at
 * provisioning and never edited by the end user. The only user actions are:
 * enabling/disabling the cloud link, and linking the device to a cloud account.
 */
export function Cloud() {
  const [d, setD] = useState<Required<CloudCfg> | null>(null);
  // Pairing needs a running cloud link, so it follows the saved flag from the
  // firmware - not the checkbox, which only takes effect after save + reboot.
  const [savedEnabled, setSavedEnabled] = useState(false);
  const [mqttOn, setMqttOn] = useState(false);
  const [pairErr, setPairErr] = useState('');

  useEffect(() => {
    postJson<CloudCfg>('/getcloud/').then((j) => {
      setD({ ...CLOUD_DEFAULTS, ...j });
      setSavedEnabled(!!j.enabled);
    }).catch(() => {});
    // The firmware runs either the cloud link or MQTT, never both - so the
    // page needs to know whether MQTT currently claims the slot.
    postJson<MqttCfg>('/getmqtt/').then((j) => setMqttOn(!!j.enableMqtt)).catch(() => {});
  }, []);
  if (!d) return <PageSkeleton cards={2} rows={2} actions={false} />;
  const set = (k: keyof CloudCfg, v: unknown) => setD({ ...d, [k]: v });

  const state = CLOUD_STATES.includes(d.state) ? d.state : 'unknown';
  // MQTT owns the link right now: explain why cloud can't be switched on.
  const blocked = mqttOn && !d.enabled;

  // Device-initiated pairing: fetch a fresh proof, then hand the browser to the
  // (provisioned) cloud web app, which authenticates the user and binds this
  // device to their account. The device originates this because the cloud app
  // can't read /getpairing/ cross-origin.
  const connectToCloud = async () => {
    setPairErr('');
    if (!d.webUrl.trim()) { setPairErr(t('cloud.pairNoWebUrl')); return; }
    try {
      const p = await getJson<Pairing>('/getpairing/');
      if (!p.hostname || !p.nonce_b64 || !p.proof_b64) { setPairErr(t('cloud.pairError')); return; }
      const base = d.webUrl.trim().replace(/\/+$/, '');
      const q = new URLSearchParams({ hostname: p.hostname, nonce: p.nonce_b64, proof: p.proof_b64 });
      window.location.href = `${base}/pair?${q.toString()}`;
    } catch { setPairErr(t('cloud.pairError')); }
  }

  return (
    <>
      <h1>{t('nav.cloud')}</h1>
      <LinkHero
        eyebrow={t('cloud.heroEyebrow')}
        state={t('cloud.state.' + state)}
        tone={CLOUD_TONE[state] ?? 'err'}
        remoteIcon="cloud"
        remoteLabel={t('cloud.nodeCloud')}
        stats={[
          { label: t('cloud.statServer'), value: d.host ? `${d.host}:${d.port}` : '-' },
          { label: t('cloud.statWeb'), value: webHost(d.webUrl) },
          { label: t('cloud.statKey'), value: d.hasPsk ? t('cloud.keySet') : t('cloud.keyMissing') },
        ]}
      />
      <p class={`note${blocked ? ' warn' : ''}`}>
        {blocked ? t('cloud.mqttActive') : t('link.exclusiveNote')}
      </p>
      <Card>
        <CheckField label={t('cloud.enable')} checked={d.enabled} disabled={blocked} onChange={(v) => set('enabled', v)} />
        {/* Only the enabled flag is user-controllable; everything else is provisioned. */}
        <div class="card-save">
          <SaveButton url="/setcloud/" data={() => ({ enabled: d.enabled })} label={t('cloud.save')} returnTo="/cloud" />
        </div>
      </Card>

      <Card title={t('cloud.pairTitle')}>
        <p class="muted">{t('cloud.pairInfo')}</p>
        <button class="btn" disabled={!savedEnabled || !d.hasPsk || !d.webUrl.trim()} onClick={connectToCloud}>{t('cloud.pairButton')}</button>
        {/* Explain a disabled button. */}
        {!d.hasPsk
          ? <p class="muted">{t('cloud.pairNeedPsk')}</p>
          : !d.webUrl.trim()
            ? <p class="muted">{t('cloud.pairNoWebUrl')}</p>
            : !savedEnabled
              ? <p class="muted">{t('cloud.pairNeedEnabled')}</p>
              : null}
        {pairErr && <p class="hint error">{pairErr}</p>}
      </Card>
    </>
  );
}
