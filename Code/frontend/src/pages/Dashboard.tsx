import type { JSX } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { Card, Row, NumberInput, Slider, Toggle } from '../components/ui';
import { ControlTile } from '../components/ControlTile';
import { LedDisplay } from '../components/LedDisplay';
import { Thermostat } from '../components/Thermostat';
import { DashboardGrid } from '../components/DashboardGrid';
import { Icon } from '../components/icons';
import { confirmDialog, showCalResult } from '../components/feedback';
import { SHOW_HEAT_LOSS_CALIBRATION } from '../flags';
import { lastStates, lastTimes, lastOther, sendCommand, useCommsPolling } from '../comms';
import { postJson } from '../api';
import { getTimeSinceText, t2rText, t2rDuration, dhms, comma, money } from '../util/format';
import { layout, editMode, saveLayout, resetLayout, migrateFromDevice, type WidgetId } from '../util/dashboardLayout';
import { t } from '../i18n';

const WIDGET_TITLE: Record<WidgetId, string> = {
  display: 'dashboard.display', thermostat: 'dashboard.temperature', controls: 'dashboard.controls',
  buttons: 'dashboard.buttons', water: 'dashboard.waterQuality', timer: 'dashboard.timer',
  totals: 'dashboard.counters', energy: 'dashboard.energy',
};

/**
 * @brief Live dashboard route with a user-editable grid of widgets.
 *
 * Widget order, width, and visibility live client-side in localStorage (see
 * dashboardLayout). Edit mode adds drag/resize/enable controls per widget.
 */
export function Dashboard() {
  useCommsPolling();
  const edit = editMode.value;
  useEffect(() => { void migrateFromDevice(); }, []);

  const lay = layout.value;
  const s = lastStates.value;
  const tm = lastTimes.value;
  const o = lastOther.value;

  // Show the calibration result dialog when a run ends while watching the
  // dashboard (the Expert page handles the same transition while mounted).
  const prevCal = useRef(false);
  useEffect(() => {
    if (SHOW_HEAT_LOSS_CALIBRATION && prevCal.current && s && !s.HLCAL) {
      postJson<{ CURRENT: number; RESULT: number; ERROR?: string }>('/gethlcal/', {})
        .then(showCalResult).catch(() => {});
    }
    prevCal.current = !!s?.HLCAL;
  }, [s?.HLCAL]);

  if (!s) return (
    <div aria-busy="true" aria-label={t('dashboard.loading')}>
      <div class="card hero"><div class="hero-inner">
        <div class="skeleton sk-line" style="width:35%" />
        <div class="skeleton sk-block" style="height:88px;margin:12px 0" />
        <div class="skeleton sk-circle" />
      </div></div>
      <div class="card"><div class="skeleton sk-line" style="width:45%" /><div class="skeleton sk-block" style="height:48px;margin-top:12px" /></div>
      <div class="card"><div class="skeleton sk-line" style="width:30%" /><div class="tile-grid" style="margin-top:12px">{[0,1,2,3].map((i) => <div key={i} class="skeleton sk-tile" />)}</div></div>
    </div>
  );

  const unit = s.UNT; // true = °C
  const u = unit ? '°C' : '°F';
  const heaterOn = s.RED || s.GRN;
  const heating = s.RED; // red = actively heating; GRN alone = target reached, standby
  const reached = s.GRN && !s.RED; // hold phase: frame goes green instead of red
  const weather = !!o?.WEATHER;
  const hasJets = !!o?.HASJETS;

  // Mirror the pump's own LED: render the raw display characters verbatim
  // (temperature, "co", error codes like "E02"/"FLt", ...) and only tag a
  // unit when the readout is a plain number.
  const rawDisplay = String.fromCharCode(s.CH1, s.CH2, s.CH3).trim();
  const isNumeric = /^-?\d+$/.test(rawDisplay);
  const ledMain = rawDisplay || '--';
  const ledUnit = isNumeric ? u : undefined;
  // brightness boost feature: edit the base level, not the momentarily boosted one
  const brtBase = s.BRTBASE ?? s.BRT;

  const widgets: Record<WidgetId, () => JSX.Element | null> = {
    display: () => (
      <Card id="sectionDisplay" className={`hero${heating ? ' heating' : reached ? ' reached' : ''}`}>
        <div class="hero-inner">
          <div class="hero-head"><p class="eyebrow">{t('dashboard.display')}</p></div>
          <LedDisplay main={ledMain} unit={ledUnit} brt={s.BRT} />
        </div>
      </Card>
    ),
    thermostat: () => (
      <Card id="sectionTemperature" className={`hero${heating ? ' heating' : reached ? ' reached' : ''}`}>
        <div class="hero-inner">
          <div class="overview-panel">
            <span class="target-label">{t('dashboard.targetTemp')}</span>
            <Thermostat value={s.TGT} current={s.TMP} min={unit ? 20 : 68} max={unit ? 40 : 104}
              unit={u} heating={heating} onChange={(v) => sendCommand('setTargetSelector', v)} />
            {tm && <p class="th-ready">{t('dashboard.readyIn')}: <b>{t2rText(tm.T2R)}</b></p>}
          </div>
        </div>
      </Card>
    ),
    controls: () => (
      <Card title={t('dashboard.controls')} className="control-card">
        <Row label={t('dashboard.ambientTemp')}>
          {weather
            ? <span class="muted" title={t('dashboard.weatherSet')}>{s.AMB}{u}</span>
            : <Slider value={s.AMB} min={-40} max={unit ? 60 : 140} unit={u}
                onChange={(v) => sendCommand(unit ? 'setAmbientC' : 'setAmbientF', v)} />}
        </Row>
        <Row label={t('dashboard.brightness')}>
          <Slider value={brtBase} min={0} max={8}
            onChange={(v) => sendCommand('setBrightnessSelector', v)} />
        </Row>
      </Card>
    ),
    buttons: () => (
      <Card title={t('dashboard.buttons')} className="control-bank">
        <div class="tile-grid">
          <ControlTile icon="power" label={t('dashboard.power')} on={s.PWR}
            stateOn={t('badge.on')} stateOff={t('badge.off')} onClick={() => sendCommand('togglePWR')} />
          <ControlTile icon="lock" label={t('dashboard.lock')} on={s.LCK}
            stateOn={t('badge.on')} stateOff={t('badge.off')} onClick={() => sendCommand('toggleLCK')} />
          <ControlTile icon="airjet" label={t('dashboard.airjet')} on={s.AIR}
            stateOn={t('badge.on')} stateOff={t('badge.off')} onClick={() => sendCommand('toggleBubbles', !s.AIR)} />
          <ControlTile icon="heater" label={t('dashboard.heater')} on={heaterOn} tone={heating || !heaterOn ? 'heat' : 'ok'}
            stateOn={t('badge.on')} stateOff={t('badge.off')} onClick={() => sendCommand('toggleHeater', !heaterOn)} />
          <ControlTile icon="pump" label={t('dashboard.pump')} on={s.FLT}
            stateOn={t('badge.on')} stateOff={t('badge.off')} onClick={() => sendCommand('togglePump', !s.FLT)} />
          {hasJets && <ControlTile icon="hydrojet" label={t('dashboard.hydrojet')} on={s.HJT}
            stateOn={t('badge.on')} stateOff={t('badge.off')} onClick={() => sendCommand('toggleHydroJets', !s.HJT)} />}
        </div>
      </Card>
    ),
    water: () => <WaterQuality showCya={lay.showCya} showAlk={lay.showAlk} />,
    timer: () => !tm ? null : (
      <Card title={t('dashboard.timer')} className="metric-card timer-card">
        <TimerRow label={t('dashboard.timerChlorine')} time={tm.CLTIME} overdueDays={tm.CLINT} cmd="resetTimerChlorine" confirm={t('confirm.resetChlorine')} />
        <TimerRow label={t('dashboard.timerFilterClean')} time={tm.FCTIME} overdueDays={tm.FCINT} cmd="resetTimerCleanFilter" confirm={t('confirm.resetFilterClean')} />
        <TimerRow label={t('dashboard.timerFilterChange')} time={tm.FTIME} overdueDays={tm.FINT} cmd="resetTimerFilter" confirm={t('confirm.resetFilter')} />
        <TimerRow label={t('dashboard.timerWaterChange')} time={tm.WCTIME} overdueDays={tm.WCINT} cmd="resetTimerWaterChange" confirm={t('confirm.resetWaterChange')} />
      </Card>
    ),
    totals: () => !tm ? null : (
      <Card title={t('dashboard.counters')} className="metric-card">
        <Row label={t('dashboard.readyIn')}><span>{t2rDuration(tm.T2R)}</span></Row>
        <Row label={t('dashboard.runtime')}><span>{dhms(tm.UPTIME)}</span></Row>
        <Row label={t('dashboard.pumpTime')}><span>{dhms(tm.PUMPTIME)}</span></Row>
        <Row label={t('dashboard.heatingTime')}><span>{dhms(tm.HEATINGTIME)}</span></Row>
        <Row label={t('dashboard.airtime')}><span>{dhms(tm.AIRTIME)}</span></Row>
        {hasJets && <Row label={t('dashboard.jettime')}><span>{dhms(tm.JETTIME)}</span></Row>}
        <button class="btn secondary" style="margin-top:10px"
          onClick={async () => { if (await confirmDialog(t('confirm.resetTotals'), { danger: true })) sendCommand('resetTotals'); }}>
          {t('dashboard.resetTotals')}
        </button>
      </Card>
    ),
    energy: () => !tm ? null : (
      <Card title={t('dashboard.energy')} className="metric-card">
        {tm.WATT !== undefined && <Row label={t('dashboard.currentPower')}><span>{tm.WATT} W</span></Row>}
        {tm.KWHD !== undefined && <Row label={t('dashboard.energyToday')}><span>{comma(tm.KWHD)} kWh</span></Row>}
        {tm.COSTD !== undefined && <Row label={t('dashboard.costToday')}><span>{money(tm.COSTD)}</span></Row>}
        {tm.KWH !== undefined && <Row label={t('dashboard.energyTotal')}><span>{comma(tm.KWH)} kWh</span></Row>}
        <Row label={t('dashboard.estimatedCost')}><span>{money(tm.COST)}</span></Row>
      </Card>
    ),
  };

  return (
    <>
      {SHOW_HEAT_LOSS_CALIBRATION && s.HLCAL && (
        <div class="cal-banner" role="status" aria-live="polite">{t('dashboard.calRunning')}</div>
      )}
      {edit && (
        <div class="dash-toolbar">
          <span class="dash-toolbar-hint">{t('web.layoutHint')}</span>
          <button class="btn secondary" onClick={async () => { if (await confirmDialog(t('dash.resetConfirm'))) resetLayout(); }}>
            <Icon name="trash" size={18} /> {t('dash.reset')}
          </button>
        </div>
      )}
      <DashboardGrid
        edit={edit}
        title={(id) => t(WIDGET_TITLE[id])}
        render={(id) => widgets[id]()}
        chrome={(id) => id === 'water' ? (
          <>
            <MiniToggle label="CYA" title={t('web.showCyanuric')} checked={lay.showCya}
              onChange={(v) => saveLayout({ ...layout.value, showCya: v })} />
            <MiniToggle label="Alk" title={t('web.showAlkalinity')} checked={lay.showAlk}
              onChange={(v) => saveLayout({ ...layout.value, showAlk: v })} />
          </>
        ) : null}
      />
    </>
  );
}

function MiniToggle({ label, title, checked, onChange }: { label: string; title: string; checked: boolean; onChange: (v: boolean) => void }) {
  return (
    <span class="dash-mini-toggle" title={title}>
      <span>{label}</span>
      <Toggle checked={checked} onChange={onChange} />
    </span>
  );
}

/**
 * @brief Timer maintenance row with overdue styling and reset action.
 */
function TimerRow(props: { label: string; time: number; overdueDays: number; cmd: string; confirm: string }) {
  const sinceSec = Math.floor(Date.now() / 1000 - props.time);
  const overdue = sinceSec / 86400 > props.overdueDays && sinceSec <= 20 * 365 * 86400;
  return (
    <div class="timer-row">
      <div class="timer-label">{props.label}</div>
      <div class="timer-time">{t('dashboard.lastTime')}: {getTimeSinceText(sinceSec)}</div>
      <button class={`btn ${overdue ? 'alert' : 'ok'}`}
        onClick={async () => { if (await confirmDialog(props.confirm, { danger: true })) sendCommand(props.cmd); }}>
        {t('dashboard.reset')}
      </button>
    </div>
  );
}

/**
 * @brief Water quality editor for pH and chlorine values.
 *
 * Inputs debounce writes for one second and avoid clobbering active edits with
 * incoming live data.
 */
function WaterQuality({ showCya, showAlk }: { showCya: boolean; showAlk: boolean }) {
  const tm = lastTimes.value;
  const [ph, setPh] = useState<string>('7.2');
  const [cl, setCl] = useState<string>('1.0');
  const [cya, setCya] = useState<string>('30');
  const [alk, setAlk] = useState<string>('100');
  const editing = useRef<{ ph: boolean; cl: boolean; cya: boolean; alk: boolean }>({ ph: false, cl: false, cya: false, alk: false });
  const timers = useRef<{ ph?: number; cl?: number; cya?: number; alk?: number }>({});

  useEffect(() => {
    if (!tm) return;
    if (!editing.current.ph) setPh(String((tm.PHVAL ?? 72) / 10));
    if (!editing.current.cl) setCl(String((tm.CLVAL ?? 10) / 10));
    if (!editing.current.cya) setCya(String((tm.CYAVAL ?? 300) / 10));
    if (!editing.current.alk) setAlk(String(tm.ALKVAL ?? 100));
  }, [tm]);

  function edit(which: 'ph' | 'cl' | 'cya' | 'alk', raw: string) {
    const v = parseFloat(raw.replace(',', '.'));
    if (which === 'ph') setPh(raw);
    else if (which === 'cl') setCl(raw);
    else if (which === 'cya') setCya(raw);
    else setAlk(raw);
    editing.current[which] = true;
    clearTimeout(timers.current[which]);
    timers.current[which] = window.setTimeout(() => {
      if (which === 'ph' && v >= 0 && v <= 14) sendCommand('setPhValue', Math.round(v * 10));
      if (which === 'cl' && v >= 0 && v <= 10) sendCommand('setClValue', Math.round(v * 10));
      if (which === 'cya' && v >= 0 && v <= 200) sendCommand('setCyaValue', Math.round(v * 10));
      if (which === 'alk' && v >= 0 && v <= 300) sendCommand('setAlkValue', Math.round(v));
      editing.current[which] = false;
    }, 1000);
  }

  return (
    <Card title={t('dashboard.waterQuality')} className="water-card">
      <Row label={t('dashboard.phValue')}>
        <NumberInput value={ph} min={0} max={14} step={0.1} onInput={(v) => edit('ph', v)} />
      </Row>
      <RangeBar value={parseFloat(ph.replace(',', '.'))} min={0} max={14} low={7.2} high={7.6} />
      <p class="hint">{t('dashboard.idealRange')}: 7,2 - 7,6 · {t('dashboard.lastChecked')}: {tm ? getTimeSinceText(Math.floor(Date.now() / 1000 - tm.PHTIME)) : '-'}</p>
      <Row label={t('dashboard.chlorineValue')}>
        <NumberInput value={cl} min={0} max={10} step={0.1} onInput={(v) => edit('cl', v)} />
      </Row>
      <RangeBar value={parseFloat(cl.replace(',', '.'))} min={0} max={10} low={1.0} high={3.0} />
      <p class="hint">{t('dashboard.idealRange')}: 1,0 - 3,0 · {t('dashboard.lastChecked')}: {tm ? getTimeSinceText(Math.floor(Date.now() / 1000 - tm.CLVTIME)) : '-'}</p>
      {showCya && (
        <>
          <Row label={t('dashboard.cyanuricValue')}>
            <NumberInput value={cya} min={0} max={200} step={1} onInput={(v) => edit('cya', v)} />
          </Row>
          <RangeBar value={parseFloat(cya.replace(',', '.'))} min={0} max={200} low={30} high={50} />
          <p class="hint">{t('dashboard.idealRange')}: 30 - 50 · {t('dashboard.lastChecked')}: {tm?.CYATIME ? getTimeSinceText(Math.floor(Date.now() / 1000 - tm.CYATIME)) : '-'}</p>
        </>
      )}
      {showAlk && (
        <>
          <Row label={t('dashboard.alkalinityValue')}>
            <NumberInput value={alk} min={0} max={300} step={1} onInput={(v) => edit('alk', v)} />
          </Row>
          <RangeBar value={parseFloat(alk.replace(',', '.'))} min={0} max={300} low={80} high={120} />
          <p class="hint">{t('dashboard.idealRange')}: 80 - 120 · {t('dashboard.lastChecked')}: {tm?.ALKTIME ? getTimeSinceText(Math.floor(Date.now() / 1000 - tm.ALKTIME)) : '-'}</p>
        </>
      )}
    </Card>
  );
}

/**
 * @brief Thin range indicator showing a value relative to its ideal band.
 */
function RangeBar({ value, min, max, low, high }: { value: number; min: number; max: number; low: number; high: number }) {
  if (!Number.isFinite(value)) return null;
  const pct = (v: number) => `${Math.min(100, Math.max(0, ((v - min) / (max - min)) * 100))}%`;
  const tol = (max - min) * 0.1;
  const tone = value >= low && value <= high ? 'ok'
    : value >= low - tol && value <= high + tol ? 'warn' : 'danger';
  return (
    <div class="range-bar" role="presentation">
      <span class="range-bar-band" style={`left:${pct(low)};right:calc(100% - ${pct(high)})`} />
      <span class={`range-bar-mark ${tone}`} style={`left:${pct(value)}`} />
    </div>
  );
}
