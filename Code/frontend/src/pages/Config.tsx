import { useEffect, useState } from 'preact/hooks';
import { postJson, getText } from '../api';
import { Card, Row, Toggle, Segmented, SaveButton } from '../components/ui';
import { NumField } from '../components/fields';
import { Icon } from '../components/icons';
import { confirmDialog } from '../components/feedback';
import { PageSkeleton } from '../components/Skeleton';
import { lastStates, sendCommand } from '../comms';
import { t } from '../i18n';
import { currencies, currency, currencySymbol, setCurrency } from '../currency';

interface Cfg {
  PRICE: number; PRICE_NIGHT: number; NIGHT_ENABLED: boolean;
  NIGHT_START_MIN: number; NIGHT_END_MIN: number; NIGHT_WEEKEND: boolean;
  CLINT: number; FINT: number; FCINT: number; WCINT: number;
  AUDIO: boolean; TIMEZONE: string; TIMEZONE_NAME: string; REBOOTTIME: number;
  WEATHER: boolean; HASJETS: boolean; POOLCAP: number;
  AIRTO: number; HJTO: number; BRTSTEP: number; BRTDUR: number;
  GSTMODE: boolean; GSTMAXTGT: number; GSTHTRTO: number;
  LCK: boolean; TMR: boolean; AIR: boolean; UNT: boolean; HTR: boolean;
  FLT: boolean; DN: boolean; UP: boolean; PWR: boolean; HJT: boolean;
}

const BTNS: { k: keyof Cfg; key: string; jetsOnly?: boolean }[] = [
  { k: 'PWR', key: 'dashboard.power' }, { k: 'LCK', key: 'dashboard.lock' },
  { k: 'AIR', key: 'dashboard.airjet' }, { k: 'HJT', key: 'dashboard.hydrojet', jetsOnly: true },
  { k: 'FLT', key: 'dashboard.pump' }, { k: 'UP', key: 'cfg.btnUp' },
  { k: 'TMR', key: 'dashboard.timer' }, { k: 'HTR', key: 'dashboard.heater' },
  { k: 'DN', key: 'cfg.btnDown' }, { k: 'UNT', key: 'cfg.btnUnit' },
];

function toHM(min: number): string {
  return `${String(Math.floor(min / 60)).padStart(2, '0')}:${String(min % 60).padStart(2, '0')}`;
}

function toMin(hm: string): number {
  const [h, m] = hm.split(':').map(Number);
  return (h || 0) * 60 + (m || 0);
}

function tariffGradient(startMin: number, endMin: number): string {
  const s = (startMin / 1440) * 100;
  const e = (endMin / 1440) * 100;
  return s <= e
    ? `linear-gradient(90deg, var(--surface-3) 0 ${s}%, var(--accent) ${s}% ${e}%, var(--surface-3) ${e}% 100%)`
    : `linear-gradient(90deg, var(--accent) 0 ${e}%, var(--surface-3) ${e}% ${s}%, var(--accent) ${s}% 100%)`;
}

function enabledText(v: boolean): string {
  return v ? t('cfg.enabled') : t('cfg.disabled');
}

/**
 * @brief SPA/runtime configuration route.
 */
export function Config() {
  const [d, setD] = useState<Cfg | null>(null);
  const [city, setCity] = useState<string>('');

  useEffect(() => {
    postJson<Cfg>('/getconfig/').then((j) => {
      setD(j);
      if (j.WEATHER) loadCity();
    }).catch(() => {});
  }, []);

  async function loadCity() {
    try { setCity(await getText('/getweather/')); }
    catch { setCity(''); }
  }

  if (!d) return <PageSkeleton cards={5} rows={3} />;
  const set = (k: keyof Cfg, v: unknown) => setD({ ...d, [k]: v });
  const refreshWeatherAfterSave = async (kind: string) => {
    if (kind !== 'saved' && kind !== 'nochange') return;
    if (d.WEATHER) await loadCity();
    else setCity('');
  }

  // confirm before disabling a physical button (control then only via WifiWhirl)
  async function toggleBtn(k: keyof Cfg, v: boolean) {
    if (!v && !await confirmDialog(t('cfg.disableConfirm'), { danger: true })) return;
    set(k, v);
  }

  const visibleBtns = BTNS.filter((b) => !b.jetsOnly || d.HASJETS);
  const allOn = visibleBtns.every((b) => d[b.k]);
  const disabledButtons = visibleBtns.filter((b) => !d[b.k]).length;
  async function toggleAll(v: boolean) {
    if (!v && !await confirmDialog(t('cfg.disableConfirm'), { danger: true })) return;
    const next: Record<string, unknown> = { ...d };
    for (const b of visibleBtns) next[b.k] = v;
    setD(next as unknown as Cfg);
  }

  // Each card saves only its own fields; the backend (setJSONSettings) merges by
  // key, so partial POSTs don't clobber untouched settings.
  const save = (fields: () => Record<string, unknown>) => (
    <div class="card-save">
      <SaveButton url="/setconfig/" data={fields} label={t('spa.save')} />
    </div>
  );

  // Current display unit comes from the live states (config UNT is the
  // unit *button* enable flag). Optimistic override until the next poll.
  const [unitSel, setUnitSel] = useState<boolean | null>(null);
  const unitC = unitSel ?? !!lastStates.value?.UNT;

  const lastBoot = d.REBOOTTIME ? new Date(d.REBOOTTIME * 1000).toLocaleString() : '';
  const weatherReady = true;
  const weatherSummary = !d.WEATHER
    ? t('cfg.weatherOff')
    : city || t('cfg.weatherConfigured');
  const ready = weatherReady;
  const maintenance = [
    { key: 'CLINT' as const, label: t('spa.chlorineInterval'), value: d.CLINT },
    // FCINT is the *clean* interval and FINT the *change* one - matching FCTIME
    // and FTIME on the Dashboard, the firmware's _fc_/_filter_ members and the
    // Home Assistant entities. These two were bound the wrong way round, so the
    // page wrote each value into the other's setting.
    { key: 'FCINT' as const, label: t('spa.filterCleanInterval'), value: d.FCINT },
    { key: 'FINT' as const, label: t('spa.filterChangeInterval'), value: d.FINT },
    { key: 'WCINT' as const, label: t('spa.waterChangeInterval'), value: d.WCINT },
  ];

  return (
    <>
      <h1>{t('nav.config')}</h1>

      <Card className={`cfg-hero${ready ? '' : ' attention'}`}>
        <div class="cfg-hero-main">
          <span class="auto-icon auto-hero-icon">
            <Icon name="config" size={30} />
          </span>
          <div>
            <p class="eyebrow">{t('cfg.profile')}</p>
            <h2>{ready ? t('cfg.ready') : t('cfg.needsAttention')}</h2>
            <p class="hint">{ready ? t('cfg.readyHint') : t('cfg.attentionHint')}</p>
          </div>
        </div>
        <div class="cfg-hero-grid">
          <ConfigStat label={t('cfg.pool')} value={`${Number(d.POOLCAP) || 0} ${t('spa.liters')}`} />
          <ConfigStat label={t('cfg.weather')} value={weatherSummary} tone={!weatherReady ? 'warn' : undefined} />
          <ConfigStat label={t('cfg.energy')} value={`${Number(d.PRICE).toFixed(2)} ${currencySymbol()}/kWh`} />
          <ConfigStat label={t('config.unit')} value={unitC ? '°C' : '°F'} />
          <ConfigStat label={t('spa.lastBoot')} value={lastBoot || '--'} />
          <ConfigStat label={t('cfg.controlButtons')} value={disabledButtons === 0 ? t('cfg.allButtonsOn') : t('cfg.disabledButtons', { count: disabledButtons })} tone={disabledButtons > 0 ? 'warn' : undefined} />
        </div>
      </Card>

      <Card className="cfg-section">
        <div class="auto-section-head">
          <div>
            <p class="eyebrow">{t('cfg.essentials')}</p>
            <h2>{t('cfg.poolWeather')}</h2>
            <p class="hint">{t('cfg.essentialsHint')}</p>
          </div>
        </div>
        <div class="cfg-essentials-grid">
          <NumField label={t('spa.poolCap')} value={d.POOLCAP} min={100} max={9999} unit={t('spa.liters')} onInput={(v) => set('POOLCAP', v)} />
          <Row label={t('config.unit')}>
            <Segmented<'c' | 'f'>
              value={unitC ? 'c' : 'f'}
              options={[{ value: 'c', label: '°C' }, { value: 'f', label: '°F' }]}
              onChange={(v) => {
                const next = v === 'c';
                if (next !== unitC) {
                  setUnitSel(next);
                  sendCommand('toggleUnit', next);
                }
              }}
            />
          </Row>
          <NumField label={t('spa.price')} value={d.PRICE} min={0} max={9.99} step={0.01} unit={currencySymbol()}
            onInput={(v) => set('PRICE', v)} />
          <Row label={t('cfg.currency')}>
            <Segmented
              value={currency.value}
              options={currencies.map((c) => ({ value: c.code, label: c.symbol }))}
              onChange={setCurrency}
            />
          </Row>
        </div>

        <div class="cfg-subsection">
          <h3>{t('cfg.nightTariffEnable')}</h3>
          <Row label={t('cfg.nightEnable')}>
            <Toggle checked={d.NIGHT_ENABLED} onChange={(v) => set('NIGHT_ENABLED', v)} />
          </Row>
          {d.NIGHT_ENABLED && (
            <>
              <NumField label={t('cfg.nightPrice')} value={d.PRICE_NIGHT} min={0} max={9.99} step={0.01} unit={currencySymbol()}
                onInput={(v) => set('PRICE_NIGHT', v)} />
              <Row label={t('cfg.nightStart')}>
                <input type="time" value={toHM(d.NIGHT_START_MIN)}
                  onInput={(e) => set('NIGHT_START_MIN', toMin((e.target as HTMLInputElement).value))} />
              </Row>
              <Row label={t('cfg.nightEnd')}>
                <input type="time" value={toHM(d.NIGHT_END_MIN)}
                  onInput={(e) => set('NIGHT_END_MIN', toMin((e.target as HTMLInputElement).value))} />
              </Row>
              <Row label={t('cfg.nightWeekend')}>
                <Toggle checked={d.NIGHT_WEEKEND} onChange={(v) => set('NIGHT_WEEKEND', v)} />
              </Row>
              <div class="tariff-bar" style={{ background: tariffGradient(d.NIGHT_START_MIN, d.NIGHT_END_MIN) }} />
              <div class="tariff-bar-scale">
                <span>0h</span><span>6h</span><span>12h</span><span>18h</span><span>24h</span>
              </div>
              <p class="hint">{t('cfg.nightTariffHint')}</p>
            </>
          )}
        </div>

        <div class="cfg-subsection">
          <h3>{t('cfg.weather')}</h3>
          <Row label={t('cfg.weatherData')}>
            <Segmented<'off' | 'on'>
              value={d.WEATHER ? 'on' : 'off'}
              options={[{ value: 'off', label: t('cfg.disabled') }, { value: 'on', label: t('cfg.enabled') }]}
              onChange={(v) => {
                const next = v === 'on';
                set('WEATHER', next);
                if (next) loadCity();
                if (!next) setCity('');
              }}
            />
          </Row>
          {d.WEATHER && (
            city
              ? <p class="hint">{t('spa.cityLookup', { city })}</p>
              : <p class="hint">{t('cfg.weatherFromCloud')}</p>
          )}
        </div>

        <div class="card-save">
          <SaveButton
            url="/setconfig/"
            data={() => ({
              PRICE: Math.round(Number(d.PRICE) * 100) / 100,
              PRICE_NIGHT: Math.round(Number(d.PRICE_NIGHT) * 100) / 100,
              NIGHT_ENABLED: d.NIGHT_ENABLED,
              NIGHT_START_MIN: Number(d.NIGHT_START_MIN),
              NIGHT_END_MIN: Number(d.NIGHT_END_MIN),
              NIGHT_WEEKEND: d.NIGHT_WEEKEND,
              POOLCAP: Number(d.POOLCAP), WEATHER: d.WEATHER,
            })}
            label={t('spa.save')}
            onResult={(result) => refreshWeatherAfterSave(result.kind)}
          />
        </div>
      </Card>

      <Card className="cfg-section">
        <div class="auto-section-head">
          <div>
            <p class="eyebrow">{t('cfg.plan')}</p>
            <h2>{t('cfg.maintenance')}</h2>
          </div>
        </div>
        <div class="cfg-maint-grid">
          {maintenance.map((item) => (
            <div class="cfg-maint-tile" key={item.key}>
              <span>{item.label}</span>
              <b>{t('cfg.everyDays', { days: Number(item.value) || 0 })}</b>
              <input
                type="number"
                min={1}
                max={99}
                value={item.value}
                inputMode="numeric"
                onInput={(e) => set(item.key, (e.target as HTMLInputElement).value)}
              />
            </div>
          ))}
        </div>
        {save(() => ({ CLINT: Number(d.CLINT), FINT: Number(d.FINT), FCINT: Number(d.FCINT), WCINT: Number(d.WCINT) }))}
      </Card>

      <Card className="cfg-section">
        <div class="auto-section-head">
          <div>
            <p class="eyebrow">{t('cfg.behavior')}</p>
            <h2>{t('device.title')}</h2>
          </div>
        </div>
        <Row label={t('spa.audio')}><Toggle checked={d.AUDIO} onChange={(v) => set('AUDIO', v)} /></Row>
        <NumField label={t('cfg.airjetTimeout')} value={d.AIRTO} min={5} max={30} unit={t('cfg.minUnit')} onInput={(v) => set('AIRTO', v)} />
        {d.HASJETS && <NumField label={t('cfg.hydrojetTimeout')} value={d.HJTO} min={5} max={60} unit={t('cfg.minUnit')} onInput={(v) => set('HJTO', v)} />}

        <div class="cfg-subsection">
          <h3>{t('config.brightnessBoost')}</h3>
          <p class="hint">{t('config.brightnessBoostInfo')}</p>
          <NumField label={t('config.brtBoostSteps')} value={d.BRTSTEP} min={1} max={8} unit={t('config.steps')} onInput={(v) => set('BRTSTEP', v)} />
          <NumField label={t('config.brtBoostDuration')} value={d.BRTDUR} min={1} max={60} unit={t('config.seconds')} onInput={(v) => set('BRTDUR', v)} />
        </div>
        {save(() => ({
          AUDIO: d.AUDIO, AIRTO: Number(d.AIRTO) || 30, HJTO: Number(d.HJTO) || 60,
          BRTSTEP: Number(d.BRTSTEP) || 1, BRTDUR: Number(d.BRTDUR) || 5,
        }))}
      </Card>

      <Card className="cfg-section cfg-controls">
        <div class="auto-section-head">
          <div>
            <p class="eyebrow">{t('cfg.physicalPanel')}</p>
            <h2>{t('cfg.controlButtons')}</h2>
            <p class="hint">{t('cfg.buttonsInfo')}</p>
          </div>
          <span class={`badge${allOn ? ' on' : ' off'}`}>{allOn ? t('cfg.allButtonsOn') : t('cfg.disabledButtons', { count: disabledButtons })}</span>
        </div>
        <Row label={t('cfg.toggleAll')}><Toggle checked={allOn} onChange={toggleAll} /></Row>
        <div class="cfg-button-grid">
          {visibleBtns.map((b) => (
            <label key={b.k} class={`cfg-button-toggle${d[b.k] ? ' on' : ''}`}>
              <span>{t(b.key)}</span>
              <small>{enabledText(d[b.k] as boolean)}</small>
              <Toggle checked={d[b.k] as boolean} onChange={(v) => toggleBtn(b.k, v)} />
            </label>
          ))}
        </div>
        {save(() => ({ LCK: d.LCK, TMR: d.TMR, AIR: d.AIR, UNT: d.UNT, HTR: d.HTR, FLT: d.FLT, DN: d.DN, UP: d.UP, PWR: d.PWR, HJT: d.HJT }))}
      </Card>

      <Card className="cfg-section">
        <div class="auto-section-head">
          <div>
            <p class="eyebrow">{t('cfg.hostMode')}</p>
            <h2>{t('cfg.hostModeTitle')}</h2>
            <p class="hint">{t('cfg.hostModeInfo')}</p>
          </div>
          <span class={`badge${d.GSTMODE ? ' on' : ' off'}`}>{enabledText(d.GSTMODE)}</span>
        </div>
        <Row label={t('cfg.hostModeEnable')}><Toggle checked={d.GSTMODE} onChange={(v) => set('GSTMODE', v)} /></Row>
        <NumField label={t('cfg.hostMaxTarget')} value={d.GSTMAXTGT} min={20} max={40} unit="°C" onInput={(v) => set('GSTMAXTGT', v)} />
        <NumField label={t('cfg.hostHeaterTimeout')} value={d.GSTHTRTO} min={0} max={1440} unit={t('cfg.minUnit')} onInput={(v) => set('GSTHTRTO', v)} />
        <p class="hint">{t('cfg.hostHeaterTimeoutHint')}</p>
        {save(() => ({ GSTMODE: d.GSTMODE, GSTMAXTGT: Number(d.GSTMAXTGT) || 40, GSTHTRTO: Number(d.GSTHTRTO) || 0 }))}
      </Card>
    </>
  );
}

function ConfigStat({ label, value, tone }: { label: string; value: string; tone?: 'warn' }) {
  return (
    <div class={`cfg-stat${tone ? ' ' + tone : ''}`}>
      <span>{label}</span>
      <b>{value}</b>
    </div>
  );
}
