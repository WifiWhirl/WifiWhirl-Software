import { useEffect, useId, useState } from 'preact/hooks';
import { postJson, getText, getJson } from '../api';
import { Card, SaveButton } from '../components/ui';
import { TextField, CheckField } from '../components/fields';
import { confirmDialog, showRestart } from '../components/feedback';
import { LinkHero } from '../components/LinkHero';
import { PageSkeleton } from '../components/Skeleton';
import { Icon, wifiSignalIconName } from '../components/icons';
import { lastOther } from '../comms';
import { t } from '../i18n';
import type { WifiCfg, WifiNetwork, WifiScanResult } from '../types/config';
import { validHostnameOrIp, validIpv4 } from '../util/validation';

// Defaults fill any field the firmware omits, so form inputs are never bound to undefined.
const WIFI_DEFAULTS: Required<WifiCfg> = {
  enableAp: false, apSsid: '', enableWM: false, enableStaticIp4: false,
  ip4Address: '', ip4Subnet: '', ip4Gateway: '', ip4DnsPrimary: '',
  ip4DnsSecondary: '', ip4NTP: '',
};

/**
 * @brief Wi-Fi access point, fallback, static IP, and NTP configuration route.
 */
export function Wifi() {
  const [d, setD] = useState<Required<WifiCfg> | null>(null);
  const [pwd, setPwd] = useState('');
  const [networks, setNetworks] = useState<WifiNetwork[] | null>(null);
  const [scanState, setScanState] = useState<'idle' | 'scanning' | 'error'>('idle');
  const ssidId = useId();

  useEffect(() => { postJson<WifiCfg>('/getwifi/').then((j) => setD({ ...WIFI_DEFAULTS, ...j })).catch(() => {}); }, []);
  if (!d) return <PageSkeleton cards={4} rows={2} />;
  const set = (k: keyof WifiCfg, v: unknown) => setD({ ...d, [k]: v });

  const payload = () => {
    // enableWM lives on the Expert page now; /setwifi/ merges by key, so leaving
    // it out here keeps the stored fallback setting untouched.
    const p: Record<string, unknown> = {
      enableAp: d.enableAp, apSsid: d.apSsid,
      enableStaticIp4: d.enableStaticIp4, ip4Address: d.ip4Address,
      ip4Subnet: d.ip4Subnet, ip4Gateway: d.ip4Gateway,
      ip4DnsPrimary: d.ip4DnsPrimary, ip4DnsSecondary: d.ip4DnsSecondary,
      ip4NTP: d.ip4NTP,
    };
    if (pwd) p.apPwd = pwd; // only change the password when one was typed
    return p;
  };

  const validate = (): string | null => {
    if (d.enableAp && (!d.apSsid.trim() || d.apSsid.length > 32)) return t('wifi.errSsid');
    if (pwd && (pwd.length < 8 || pwd.length > 63)) return t('wifi.errPassword');
    if (d.enableStaticIp4) {
      if (!validIpv4(d.ip4Address) ||
          !validIpv4(d.ip4Subnet) ||
          !validIpv4(d.ip4Gateway) ||
          !validIpv4(d.ip4DnsPrimary) ||
          (d.ip4DnsSecondary.trim() && !validIpv4(d.ip4DnsSecondary))) {
        return t('wifi.errStaticIp');
      }
    }
    if (!validHostnameOrIp(d.ip4NTP, true)) return t('wifi.errNtp');
    return null;
  }

  async function resetWifi() {
    if (!await confirmDialog(t('wifi.resetConfirm'), { danger: true })) return;
    try { await getText('/resetwifi/'); } catch { /* reboot drops the connection */ }
    showRestart();
  }

  async function scanWifi() {
    setScanState('scanning');
    try {
      const result = await getJson<WifiScanResult>('/scanwifi/');
      const found = Array.isArray(result.networks)
        ? result.networks.filter((n) => n.ssid).sort((a, b) => b.rssi - a.rssi)
        : [];
      setNetworks(found);
      setScanState('idle');
    } catch {
      setScanState('error');
    }
  }

  function selectNetwork(ssid: string) {
    set('apSsid', ssid);
  }

  // Live signal strength from the poll loop; the page is state-first, so the
  // hero answers "how good is my spa's Wi-Fi right now?" before any form.
  const rssi = lastOther.value?.RSSI;

  return (
    <>
      <h1>{t('nav.wifi')}</h1>
      <LinkHero
        eyebrow={t('wifi.heroEyebrow')}
        state={rssi != null ? t('wifi.stateConnected') : t('wifi.stateUnknown')}
        tone={rssi != null ? 'on' : 'pending'}
        remoteIcon={wifiSignalIconName(rssi != null ? signalBars(rssi) : 0)}
        remoteLabel={t('wifi.nodeRouter')}
        stats={[
          { label: t('wifi.statSsid'), value: d.apSsid.trim() || '-' },
          { label: t('wifi.statSignal'), value: rssi != null ? `${rssi} dBm · ${signalLabel(rssi)}` : '-' },
          { label: t('wifi.statIp'), value: d.enableStaticIp4 ? d.ip4Address.trim() || '-' : t('wifi.dhcp') },
        ]}
      />
      <Card title={t('wifi.ap')}>
        <div class="row wifi-ssid-row">
          <label for={ssidId}>{t('wifi.ssid')}</label>
          <div>
            <div class="wifi-ssid-control">
              <button
                type="button"
                class="iconbtn wifi-scan-btn"
                disabled={scanState === 'scanning'}
                aria-label={scanState === 'scanning' ? t('wifi.scanning') : t('wifi.scan')}
                title={scanState === 'scanning' ? t('wifi.scanning') : t('wifi.scan')}
                onClick={scanWifi}
              >
                <Icon name="search" size={20} />
              </button>
              <input
                id={ssidId}
                type="text"
                value={d.apSsid}
                onInput={(e) => set('apSsid', (e.target as HTMLInputElement).value)}
              />
            </div>
            {scanState === 'error' && <span class="scan-status error">{t('wifi.scanError')}</span>}
            {networks && networks.length === 0 && <span class="scan-status">{t('wifi.scanEmpty')}</span>}
          </div>
        </div>
        {networks && networks.length > 0 && (
          <div class="wifi-network-list" role="list" aria-label={t('wifi.scanResults')}>
            {networks.map((network) => (
              <button
                key={`${network.ssid}-${network.rssi}`}
                type="button"
                class={`wifi-network${network.ssid === d.apSsid ? ' active' : ''}`}
                onClick={() => selectNetwork(network.ssid)}
              >
                <span class="wifi-network-name">{network.ssid}</span>
                <span class="wifi-network-meta">
                  {isEncrypted(network.enc) && <span class="wifi-network-chip">{t('wifi.secured')}</span>}
                  <span
                    class="wifi-network-signal"
                    title={signalLabel(network.rssi)}
                    aria-label={signalLabel(network.rssi)}
                  >
                    <Icon name={wifiSignalIconName(signalBars(network.rssi))} size={24} />
                  </span>
                </span>
              </button>
            ))}
          </div>
        )}
        <TextField label={t('wifi.password')} type="password" value={pwd}
          placeholder={t('mqtt.passwordPlaceholder')} onInput={setPwd} />
      </Card>
      <Card title={t('wifi.static')}>
        <CheckField label={t('wifi.staticEnable')} checked={d.enableStaticIp4} onChange={(v) => set('enableStaticIp4', v)} />
        {d.enableStaticIp4 && (
          <>
            <TextField label={t('wifi.ip')} inputMode="numeric" value={d.ip4Address} onInput={(v) => set('ip4Address', v)} />
            <TextField label={t('wifi.subnet')} inputMode="numeric" value={d.ip4Subnet} onInput={(v) => set('ip4Subnet', v)} />
            <TextField label={t('wifi.gateway')} inputMode="numeric" value={d.ip4Gateway} onInput={(v) => set('ip4Gateway', v)} />
            <TextField label={t('wifi.dns1')} inputMode="numeric" value={d.ip4DnsPrimary} onInput={(v) => set('ip4DnsPrimary', v)} />
            <TextField label={t('wifi.dns2')} inputMode="numeric" value={d.ip4DnsSecondary} onInput={(v) => set('ip4DnsSecondary', v)} />
          </>
        )}
      </Card>
      <Card title={t('wifi.ntp')}>
        <TextField label={t('wifi.ntpHost')} value={d.ip4NTP} onInput={(v) => set('ip4NTP', v)} />
      </Card>
      <div class="actions">
        <SaveButton url="/setwifi/" data={payload} validate={validate} label={t('wifi.save')} />
        <button class="btn danger" onClick={resetWifi}>{t('wifi.reset')}</button>
      </div>
    </>
  );
}

function isEncrypted(enc: WifiNetwork['enc']): boolean {
  return enc === true || (typeof enc === 'number' && enc !== 0);
}

function signalBars(rssi: number): number {
  if (rssi <= -80) return 1;
  if (rssi <= -70) return 2;
  if (rssi <= -67) return 3;
  return 4;
}

function signalLabel(rssi: number): string {
  if (rssi >= -55) return t('wifi.signalExcellent');
  if (rssi >= -67) return t('wifi.signalGood');
  if (rssi >= -75) return t('wifi.signalFair');
  return t('wifi.signalWeak');
}
