import { useState } from 'preact/hooks';
import { postJson } from '../api';
import { t } from '../i18n';
import type { MqttCfg, WifiCfg } from '../types/config';
import { mqttStateKey, isMqttConnected, isMqttError, shouldShowMqttIndicator } from '../util/status';
import { lastOther, commsOk, rssiBars } from '../comms';
import { DetailRow, StatusModal } from './StatusModal';
import { Icon, wifiSignalIconName } from './icons';

/**
 * @brief Top-bar MQTT status indicator and details modal.
 */
export function MqttSignal() {
  const [open, setOpen] = useState(false);
  const [cfg, setCfg] = useState<MqttCfg | null | undefined>(undefined);
  const state = lastOther.value?.MQTT;

  if (!shouldShowMqttIndicator(state)) return null;

  const connected = isMqttConnected(state);
  const error = isMqttError(state);
  const label = t(mqttStateKey(state));

  function load() {
    setOpen(true);
    setCfg(undefined);
    postJson<MqttCfg>('/getmqtt/').then(setCfg).catch(() => setCfg(null));
  }

  return (
    <>
      <button type="button" class={`mqtt-sig${connected ? ' on' : ''}${error ? ' err' : ''}`} title={`MQTT: ${label}`} aria-label={`MQTT: ${label}`} onClick={load}>
        <span class="mqtt-dot" />
        <span class="mqtt-text">MQTT</span>
      </button>
      {open && (
        <StatusModal title="MQTT" onClose={() => setOpen(false)}>
          <DetailRow label={t('status.state')} value={label} />
          <DetailRow label={t('mqtt.enable')} value={cfg === undefined ? t('common.loading') : cfg?.enableMqtt === false ? t('common.no') : t('common.yes')} />
          <DetailRow label={t('mqtt.server')} value={cfg?.mqttServer || t('common.loading')} />
          <DetailRow label={t('mqtt.port')} value={cfg?.mqttPort ?? t('common.loading')} />
          <DetailRow label={t('mqtt.clientId')} value={cfg?.mqttClientId || '--'} />
          <DetailRow label={t('mqtt.baseTopic')} value={cfg?.mqttBaseTopic || '--'} />
          <DetailRow label={t('mqtt.interval')} value={cfg?.mqttTelemetryInterval != null ? `${cfg.mqttTelemetryInterval}s` : t('common.loading')} />
        </StatusModal>
      )}
    </>
  );
}

/**
 * @brief Top-bar Wi-Fi/live-connection indicator and details modal.
 */
export function WifiSignal() {
  const [open, setOpen] = useState(false);
  const [cfg, setCfg] = useState<WifiCfg | null | undefined>(undefined);
  const connected = commsOk.value;
  const rssi = lastOther.value?.RSSI;
  const bars = connected && rssi != null ? rssiBars(rssi) : 0;
  const title = rssi != null ? `${rssi} dBm` : (connected ? 'Live' : t('common.offline'));

  function load() {
    setOpen(true);
    setCfg(undefined);
    postJson<WifiCfg>('/getwifi/').then(setCfg).catch(() => setCfg(null));
  }

  return (
    <>
      <button type="button" class={`wifi-sig${connected ? '' : ' off'}`} title={title} aria-label={title} onClick={load}>
        <Icon name={connected ? wifiSignalIconName(bars) : 'wifiSlash'} size={24} />
      </button>
      {open && (
        <StatusModal title={t('nav.wifi')} onClose={() => setOpen(false)}>
          <DetailRow label={t('status.state')} value={connected ? 'Live' : t('common.offline')} />
          <DetailRow label="RSSI" value={rssi != null ? `${rssi} dBm` : '--'} />
          <DetailRow label={t('wifi.ssid')} value={cfg?.apSsid || t('common.loading')} />
          <DetailRow label={t('wifi.fallbackEnable')} value={cfg === undefined ? t('common.loading') : cfg?.enableWM ? t('common.yes') : t('common.no')} />
          <DetailRow label={t('wifi.staticEnable')} value={cfg === undefined ? t('common.loading') : cfg?.enableStaticIp4 ? t('common.yes') : t('common.no')} />
          {cfg?.enableStaticIp4 && (
            <>
              <DetailRow label={t('wifi.ip')} value={cfg.ip4Address || '--'} />
              <DetailRow label={t('wifi.gateway')} value={cfg.ip4Gateway || '--'} />
              <DetailRow label={t('wifi.dns1')} value={cfg.ip4DnsPrimary || '--'} />
            </>
          )}
        </StatusModal>
      )}
    </>
  );
}
