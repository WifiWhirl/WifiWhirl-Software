import { useEffect, useState } from 'preact/hooks';
import { postJson } from '../api';
import { Card, SaveButton } from '../components/ui';
import { TextField, NumField, CheckField } from '../components/fields';
import { LinkHero, type LinkTone } from '../components/LinkHero';
import { PageSkeleton } from '../components/Skeleton';
import { lastOther } from '../comms';
import { mqttStateKey, isMqttConnected } from '../util/status';
import { t } from '../i18n';
import type { CloudCfg, MqttCfg } from '../types/config';
import { validHostnameOrIp, validMqttTopic, validNoControl } from '../util/validation';

// Defaults fill any field the firmware omits, so form inputs are never bound to undefined.
const MQTT_DEFAULTS: Required<MqttCfg> = {
  enableMqtt: false, mqttServer: '', mqttPort: 1883, mqttUsername: '',
  mqttClientId: '', mqttBaseTopic: '', mqttTelemetryInterval: 600,
};

/**
 * @brief MQTT broker and telemetry configuration route.
 */
export function Mqtt() {
  const [d, setD] = useState<Required<MqttCfg> | null>(null);
  const [cloudOn, setCloudOn] = useState(false);
  const [pwd, setPwd] = useState('');

  useEffect(() => {
    postJson<MqttCfg>('/getmqtt/').then((j) => setD({ ...MQTT_DEFAULTS, ...j })).catch(() => {});
    // The firmware runs either MQTT or the cloud link, never both - so the
    // page needs to know whether the cloud connection currently claims the slot.
    postJson<CloudCfg>('/getcloud/').then((j) => setCloudOn(!!j.enabled)).catch(() => {});
  }, []);
  if (!d) return <PageSkeleton cards={2} rows={7} actions={false} />;
  const set = (k: keyof MqttCfg, v: unknown) => setD({ ...d, [k]: v });

  // Live broker state pushed over the websocket (PubSubClient code).
  const live = lastOther.value?.MQTT;
  const tone: LinkTone = !d.enableMqtt ? 'off'
    : isMqttConnected(live) ? 'on'
    : live == null || live === -1 ? 'pending'
    : 'err';
  const state = !d.enableMqtt ? t('mqtt.state.disabled') : t(mqttStateKey(live));
  // Cloud owns the link right now: explain why MQTT can't be switched on.
  const blocked = cloudOn && !d.enableMqtt;

  const payload = () => {
    const p: Record<string, unknown> = {
      enableMqtt: d.enableMqtt, mqttServer: d.mqttServer, mqttPort: Number(d.mqttPort),
      mqttUsername: d.mqttUsername, mqttClientId: d.mqttClientId,
      mqttBaseTopic: d.mqttBaseTopic, mqttTelemetryInterval: Number(d.mqttTelemetryInterval),
    };
    if (pwd) p.mqttPassword = pwd;
    return p;
  };

  const validate = (): string | null => {
    const port = Number(d.mqttPort);
    const interval = Number(d.mqttTelemetryInterval);
    if (!Number.isInteger(port) || port < 1 || port > 65535) return t('mqtt.errPort');
    if (!Number.isInteger(interval) || interval < 10 || interval > 86400) return t('mqtt.errInterval');
    if ((d.enableMqtt || d.mqttServer.trim()) && !validHostnameOrIp(d.mqttServer)) return t('mqtt.errServer');
    if (!validNoControl(d.mqttUsername, 128, true) || !validNoControl(pwd, 128, true)) return t('mqtt.errCredentials');
    if (!validNoControl(d.mqttClientId, 64, !d.enableMqtt)) return t('mqtt.errClientId');
    if (!validMqttTopic(d.mqttBaseTopic)) return t('mqtt.errBaseTopic');
    return null;
  }

  return (
    <>
      <h1>{t('nav.mqtt')}</h1>
      <LinkHero
        eyebrow={t('mqtt.heroEyebrow')}
        state={state}
        tone={tone}
        remoteIcon="mqtt"
        remoteLabel={t('mqtt.nodeBroker')}
        stats={[
          { label: t('mqtt.statBroker'), value: d.mqttServer.trim() ? `${d.mqttServer}:${d.mqttPort}` : '-' },
          { label: t('mqtt.statTopic'), value: d.mqttBaseTopic.trim() || '-' },
          { label: t('mqtt.statInterval'), value: `${d.mqttTelemetryInterval} s` },
        ]}
      />
      <p class={`note${blocked ? ' warn' : ''}`}>
        {blocked ? t('mqtt.cloudActive') : t('link.exclusiveNote')}
      </p>
      <Card>
        <CheckField label={t('mqtt.enable')} checked={d.enableMqtt} disabled={blocked} onChange={(v) => set('enableMqtt', v)} />
        <TextField label={t('mqtt.server')} value={d.mqttServer} onInput={(v) => set('mqttServer', v)} />
        <NumField label={t('mqtt.port')} value={d.mqttPort} min={1} max={65535} step={1} onInput={(v) => set('mqttPort', v)} />
        <TextField label={t('mqtt.username')} value={d.mqttUsername} onInput={(v) => set('mqttUsername', v)} />
        <TextField label={t('mqtt.password')} type="password" value={pwd}
          placeholder={t('mqtt.passwordPlaceholder')} onInput={setPwd} />
        <TextField label={t('mqtt.clientId')} value={d.mqttClientId} onInput={(v) => set('mqttClientId', v)} />
        <TextField label={t('mqtt.baseTopic')} value={d.mqttBaseTopic} onInput={(v) => set('mqttBaseTopic', v)} />
        <NumField label={t('mqtt.interval')} value={d.mqttTelemetryInterval} min={10} max={86400} step={1} onInput={(v) => set('mqttTelemetryInterval', v)} />
        <div class="card-save">
          <SaveButton url="/setmqtt/" data={payload} validate={validate} label={t('mqtt.save')} />
        </div>
      </Card>
    </>
  );
}
