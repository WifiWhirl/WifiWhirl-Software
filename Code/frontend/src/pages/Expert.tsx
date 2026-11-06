import { useEffect, useRef, useState } from 'preact/hooks';
import { needsLogin, postJson, postJsonResponse, saveConfig } from '../api';
import { Card, Row, SaveButton, TimelineStep } from '../components/ui';
import { CheckField, NumField } from '../components/fields';
import { showToast, confirmDialog, showCalResult, calAbortText } from '../components/feedback';
import { PageSkeleton } from '../components/Skeleton';
import { t } from '../i18n';
import { dec, durLong } from '../util/format';
import { SHOW_HEAT_LOSS_CALIBRATION } from '../flags';

interface PowerSet { heater: number; pump: number; air: number; idle: number; jet: number }
interface DevPower {
  hasjets?: boolean;
  power?: PowerSet;
  powerDefaults?: PowerSet;
}
interface HlCal {
  ACTIVE?: boolean; PHASE?: number; CURRENT?: number; DEFAULT?: number; TARGETDROP?: number;
  RESULT?: number; ERROR?: string;
  STARTAMB?: number; ANCHORED?: boolean; RESTORING?: boolean; DISCARDED?: number;
  PLANNED?: number; MINDIFF?: number; NOW?: number;
  // the last run, kept until the next start (epoch seconds, water in °C)
  REQTS?: number; REQWATER?: number; STARTTS?: number; STARTWATER?: number;
  ANCHORTS?: number; ANCHORWATER?: number; ENDTS?: number; ENDWATER?: number;
}
/** today 22:00, as a datetime-local value */
function tonight(): string {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}T22:00`;
}

const EMPTY: PowerSet = { heater: 0, pump: 0, air: 0, idle: 0, jet: 0 };

// The firmware stores heat loss as a 32-bit float, so e.g. 4.85 round-trips as
// 4.849999905. Round to 2 decimals for display/edit.
const HEAT_LOSS_DEFAULT = 7.5; // mirrors BWC::HEAT_LOSS_DEFAULT
const r2 = (v?: number) => Math.round((v ?? HEAT_LOSS_DEFAULT) * 100) / 100;

/**
 * @brief Expert mode: user-configurable power (wattage) values plus heat-loss
 * calibration. Only reachable while expert mode is enabled (Device page).
 */
export function Expert() {
  const [power, setPower] = useState<PowerSet>(EMPTY);
  const [defaults, setDefaults] = useState<PowerSet>(EMPTY);
  const [hasjets, setHasjets] = useState(false);
  const [loaded, setLoaded] = useState(false);

  const [wmFallback, setWmFallback] = useState(false); // WiFi AP fallback (moved here)
  const [hl, setHl] = useState<number>(HEAT_LOSS_DEFAULT);
  const [cal, setCal] = useState<HlCal | null>(null);
  const [planAt, setPlanAt] = useState(tonight());
  const prevActive = useRef(false);
  const firstCal = useRef(true);

  function refreshCal() {
    postJson<HlCal>('/gethlcal/', {}).then((d) => {
      setCal(d);
      // Adopt the value on first load and whenever a run just finished, so the
      // input mirrors the calibrated/stored coefficient without clobbering edits.
      if (firstCal.current) { setHl(r2(d.CURRENT)); firstCal.current = false; }
      else if (prevActive.current && !d.ACTIVE) {
        setHl(r2(d.CURRENT));
        if (SHOW_HEAT_LOSS_CALIBRATION) showCalResult(d);
      }
      prevActive.current = !!d.ACTIVE;
    }).catch(() => {});
  }

  useEffect(() => {
    postJson<DevPower>('/getdevice/').then((j) => {
      setPower(j.power || EMPTY);
      setDefaults(j.powerDefaults || EMPTY);
      setHasjets(!!j.hasjets);
      setLoaded(true);
    }).catch(() => {});
    postJson<{ enableWM?: boolean }>('/getwifi/').then((j) => setWmFallback(!!j.enableWM)).catch(() => {});
    // Still needed while hidden: /gethlcal/ carries the stored coefficient (CURRENT)
    // and the firmware default that the reset button restores.
    refreshCal();
  }, []);

  useEffect(() => {
    if (!cal?.ACTIVE) return;
    const id = setInterval(() => { if (!document.hidden) refreshCal(); }, 5000);
    return () => clearInterval(id);
  }, [cal?.ACTIVE]);

  if (!loaded) return <PageSkeleton cards={2} rows={5} />;

  const set = (k: keyof PowerSet) => (v: string) =>
    setPower((p) => ({ ...p, [k]: v === '' ? 0 : Number(v) }));

  async function save(values: PowerSet) {
    const r = await saveConfig('/setdevice/', { power: values });
    if (r.kind === 'error') showToast(t('common.error'), true);
    else showToast(t('common.saved'));
  }

  function resetDefaults() {
    setPower(defaults);
    save(defaults);
  }

  const field = (k: keyof PowerSet, label: string) => (
    <NumField label={label} value={power[k]} min={k === 'heater' ? 1 : 0} max={5000} step={10} unit="W"
      onInput={set(k)} />
  );

  async function saveHeatLoss(v: number) {
    v = Math.min(50, Math.max(0.5, v || 0)); // same limits as the firmware
    setHl(v);
    const r = await saveConfig('/setheatloss/', { HLOSS: v });
    if (r.kind === 'error') showToast(t('common.error'), true);
    else { showToast(t('common.saved')); refreshCal(); } // the old timeline goes
  }

  async function startCal() {
    // Spell out the hardware changes and confirm before touching anything.
    if (!await confirmDialog(t('expert.calConfirm'), { title: t('expert.calTitle'), confirmLabel: t('expert.calStart') }))
      return;
    try {
      const res = await postJsonResponse('/starthlcal/', {});
      if (res.ok) { showToast(t('expert.calStarted')); refreshCal(); }
      else {
        const key = `expert.calErr.${(await res.text()).trim()}`;
        const msg = t(key, { min: dec(cal?.MINDIFF ?? 15, 0) });
        showToast(msg === key ? t('common.error') : msg, true); // t() echoes the key when missing
      }
    } catch {
      if (!needsLogin.value) showToast(t('common.error'), true);
    }
  }

  async function planCal() {
    const at = Math.floor(new Date(planAt).getTime() / 1000);
    if (!planAt || at <= Math.floor(Date.now() / 1000)) { showToast(t('ss.errFuture'), true); return; }
    try {
      const res = await postJsonResponse('/starthlcal/', { AT: at });
      if (res.ok) { showToast(t('expert.calPlannedToast')); refreshCal(); }
      else showToast(t('common.error'), true);
    } catch {
      if (!needsLogin.value) showToast(t('common.error'), true);
    }
  }

  async function cancelCal() {
    await postJson('/cancelhlcal/', {});
    refreshCal();
  }

  // Gated at the source, so the hero state and the timeline stay inert while the
  // calibration UI is hidden - even if a run were started outside the frontend.
  const active = SHOW_HEAT_LOSS_CALIBRATION && !!cal?.ACTIVE;
  // the last run stays visible until the next start, also after an abort
  const hasRun = SHOW_HEAT_LOSS_CALIBRATION && !!cal?.REQTS && cal?.ERROR !== 'tempdiff';
  const minDiff = cal?.MINDIFF ?? 15;


  return (
    <>
      <h1>{t('nav.expert')}</h1>

      <Card title={t('expert.powerTitle')}>
        <p class="hint">{t('expert.powerInfo')}</p>
        {field('heater', t('expert.heater'))}
        {field('pump', t('expert.pump'))}
        {field('idle', t('expert.idle'))}
        {field('air', t('expert.air'))}
        {hasjets && field('jet', t('expert.jet'))}
        <div class="actions">
          <button class="btn" onClick={() => save(power)}>{t('common.save')}</button>
          <button class="btn ghost" onClick={resetDefaults}>{t('expert.resetDefaults')}</button>
        </div>
      </Card>

      <Card title={SHOW_HEAT_LOSS_CALIBRATION ? t('expert.calTitle') : t('expert.heatLoss')}>
        <p class="hint">{t('expert.heatLossInfo')}</p>
        <NumField label={t('expert.heatLoss')} value={hl} min={0.5} max={50} step={0.1} unit="W/K"
          onInput={(v) => setHl(v === '' ? 0 : Number(v))} />
        <div class="actions">
          <button class="btn" onClick={() => saveHeatLoss(hl)}>{t('common.save')}</button>
          <button class="btn ghost" onClick={() => saveHeatLoss(r2(cal?.DEFAULT))}>{t('expert.resetDefaults')}</button>
        </div>

        {SHOW_HEAT_LOSS_CALIBRATION && (
          <>
            {/* customer-facing: what it does and how to get a good result */}
            <p class="hint" style="margin-top:16px">{t('expert.calInfo')}</p>
            {(() => {
              const [head, ...tips] = t('expert.calTips', { min: dec(minDiff, 0) }).split('\n');
              return <div class="hint cal-tips">{head}<ul>{tips.map((x) => <li key={x}>{x}</li>)}</ul></div>;
            })()}
            {hasRun && cal && <CalTimeline cal={cal} />}
            {/* the last run ended without a result: say why, not only in the dialog
                someone watching the page got */}
            {!active && cal?.ERROR && <p class="hint warn">{calAbortText(cal)}</p>}
            {!active && !!cal?.PLANNED && (
              <div class="actions">
                <p class="hint">{t('expert.calPlannedAt', { at: new Date(cal.PLANNED * 1000).toLocaleString() })}</p>
                <button class="btn ghost" onClick={cancelCal}>{t('expert.calPlanCancel')}</button>
              </div>
            )}
            {!active && !cal?.PLANNED && (
              <Row label={t('expert.calPlanAt')}>
                <input type="datetime-local" value={planAt} onInput={(e) => setPlanAt((e.target as HTMLInputElement).value)} />
              </Row>
            )}
            {/* start and plan side by side under the line, like save/reset above */}
            {active ? (
              <div class="actions">
                <button class="btn ghost" onClick={cancelCal}>{t('expert.calCancel')}</button>
              </div>
            ) : (
              <div class="actions">
                <button class="btn" onClick={startCal}>{t('expert.calStart')}</button>
                {!cal?.PLANNED && <button class="btn secondary" onClick={planCal}>{t('expert.calPlan')}</button>}
              </div>
            )}
            {!active && !cal?.PLANNED && <p class="hint">{t('expert.calPlanHint')}</p>}
          </>
        )}
      </Card>

      {/* Moved off the network page: recovering a device through the fallback AP
          is an expert task. /setwifi/ merges by key, so sending enableWM alone
          leaves SSID and static-IP settings alone (it still reboots). */}
      <Card title={t('wifi.fallback')}>
        <p class="hint">{t('wifi.fallbackInfo')}</p>
        <CheckField label={t('wifi.fallbackEnable')} checked={wmFallback} onChange={setWmFallback} />
        <div class="card-save">
          <SaveButton url="/setwifi/" data={() => ({ enableWM: wmFallback })} label={t('wifi.save')} />
        </div>
      </Card>
    </>
  );
}

/**
 * @brief The last calibration run on the shared ss-timeline: start, first and
 * second sensor step, result. Water temperature at each point, the time
 * between them under the line. Stays until the next
 * start, so a finished or aborted run can still be read afterwards.
 */
function CalTimeline({ cal }: { cal: HlCal }) {
  const now = cal.NOW ?? Math.floor(Date.now() / 1000);
  const end = cal.ENDTS ?? now;
  const temp = (v?: number) => (v != null ? `${dec(v, 1)} °C` : '…');
  const ok = (cal.RESULT ?? 0) > 0 && !cal.ERROR;
  // 0 start, 1 first step, 2 second step, 3 result
  const reached = ok ? 3 : cal.ANCHORTS ? 1 : 0;
  // which step is in progress: 0 pump settling, 1 first step, 2 second step
  const waiting = cal.ACTIVE ? (!cal.STARTTS ? 0 : cal.ANCHORTS ? 2 : 1) : -1;
  // "Vorbereitung" is the reading after the pump has run (STARTTS), not the
  // one at the request, which the still sensor may take from standing water.
  const gap1 = cal.STARTTS ? (cal.ANCHORTS ?? end) - cal.STARTTS : 0;
  const gap2 = cal.ANCHORTS ? (ok ? (cal.ENDTS ?? end) : end) - cal.ANCHORTS : 0;
  const gaps = [gap1 > 0 ? durLong(gap1) : '', cal.ANCHORTS && gap2 > 0 ? durLong(gap2) : '', ''];
  return (
    <>
      <div class="ss-timeline cal-timeline" style={`--ss-progress:${(Math.max(reached, waiting - 0.5, 0) / 3) * 100}%`}>
        <div class="ss-timeline-bar"><span /></div>
        <div class="cal-gaps" aria-hidden="true">
          {gaps.map((g, i) => <span key={i} style={`left:${(i + 1) * 25}%`}>{g}</span>)}
        </div>
        <div class="ss-timeline-steps">
          <TimelineStep label={t('expert.calStepPrep')} value={cal.STARTTS ? temp(cal.STARTWATER) : waiting === 0 ? '…' : '--'} active={waiting === 0} done={!!cal.STARTTS} />
          <TimelineStep label={t('expert.calStepWaitTick')}
            value={cal.ANCHORTS ? temp(cal.ANCHORWATER) : waiting === 1 ? '…' : '--'}
            active={waiting === 1} done={reached >= 1} />
          <TimelineStep label={t('expert.calStepSecondTick')}
            value={ok ? temp(cal.ENDWATER) : waiting === 2 ? '…' : '--'}
            active={waiting === 2} done={ok} />
          <TimelineStep label={t('expert.calStepResult')}
            value={ok ? `${dec(cal.RESULT, 2)} W/K` : '--'} done={ok} />
        </div>
      </div>
      {/* phones show the timeline vertically: the gaps go into a line below */}
      <p class="hint cal-gaps-inline">
        {gaps[0] && `${t('expert.calStepPrep')} → ${t('expert.calStepWaitTick')}: ${gaps[0]}`}
        {gaps[1] && ` · ${t('expert.calStepWaitTick')} → ${t('expert.calStepSecondTick')}: ${gaps[1]}`}
      </p>
      {cal.ACTIVE && cal.PHASE !== 1 && <p class="hint">{t('expert.calPreparing')}</p>}
      {cal.ACTIVE && cal.PHASE === 1 && !cal.ANCHORED && <p class="hint">{t('expert.calWaitTick')}</p>}
      {cal.RESTORING && <p class="hint warn">{t('expert.calRestoring')}</p>}
      {cal.ACTIVE && (cal.DISCARDED ?? 0) > 0 &&
        <p class="hint">{t('expert.calDiscarded', { n: cal.DISCARDED! })}</p>}
      {cal.ANCHORTS && cal.STARTAMB != null && <Row label={t('expert.calAmbient')}><span>{dec(cal.STARTAMB, 1)} °C</span></Row>}
    </>
  );
}
