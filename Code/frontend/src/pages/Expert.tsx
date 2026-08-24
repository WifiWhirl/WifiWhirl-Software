import { useEffect, useRef, useState } from 'preact/hooks';
import { needsLogin, postJson, postJsonResponse, saveConfig } from '../api';
import { Card, Row, SaveButton, TimelineStep } from '../components/ui';
import { CheckField, NumField } from '../components/fields';
import { showToast, confirmDialog, showCalResult } from '../components/feedback';
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
  STARTWATER?: number; STARTAMB?: number; DROP?: number; ELAPSED?: number;
  ANCHORED?: boolean; ANCHORELAPSED?: number; RESTORING?: boolean; DISCARDED?: number;
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
    setHl(v);
    const r = await saveConfig('/setheatloss/', { HLOSS: v });
    if (r.kind === 'error') showToast(t('common.error'), true);
    else showToast(t('common.saved'));
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
        const msg = t(key);
        showToast(msg === key ? t('common.error') : msg, true); // t() echoes the key when missing
      }
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
  const finished = SHOW_HEAT_LOSS_CALIBRATION && !active && !cal?.ERROR && (cal?.RESULT ?? 0) > 0;
  // 0 prep, 1 waiting for the first sensor tick, 2 measuring the interval, 3 done
  const step = !active ? 3 : cal?.PHASE !== 1 ? 0 : cal?.ANCHORED ? 2 : 1;


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
            <p class="hint" style="margin-top:16px">{t('expert.calInfo')}</p>
            <p class="hint">{t('expert.calHow')}</p>
            {(active || finished) && <CalTimeline cal={cal} step={step} finished={finished} />}
            {active ? (
              <div class="actions">
                <button class="btn ghost" onClick={cancelCal}>{t('expert.calCancel')}</button>
              </div>
            ) : (
              <div class="actions">
                <button class="btn" onClick={startCal}>{t('expert.calStart')}</button>
              </div>
            )}
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
 * @brief Calibration progress on the shared ss-timeline: prepare → wait for a
 * sensor tick → measure between ticks → result.
 */
function CalTimeline({ cal, step, finished }: { cal: HlCal | null; step: number; finished: boolean }) {
  const check = '✓';
  return (
    <>
      <div class="ss-timeline" style={`--ss-progress:${(step / 3) * 100}%`}>
        <div class="ss-timeline-bar"><span /></div>
        <div class="ss-timeline-steps">
          <TimelineStep label={t('expert.calStepPrep')} value={step === 0 ? '…' : check}
            active={step === 0} done={step > 0} />
          <TimelineStep label={t('expert.calStepWaitTick')}
            value={step === 1 ? `${dec(cal?.STARTWATER, 1)} °C · ${durLong(cal?.ELAPSED ?? 0)}` : step > 1 ? check : '--'}
            active={step === 1} done={step > 1} />
          <TimelineStep label={t('expert.calStepMeasure')}
            value={step === 2 ? durLong(cal?.ANCHORELAPSED ?? 0) : step > 2 ? check : '--'}
            active={step === 2} done={step > 2} />
          <TimelineStep label={t('expert.calStepResult')}
            value={finished ? `${dec(cal?.RESULT, 2)} W/K` : '--'}
            active={step === 3} done={finished} />
        </div>
      </div>
      {step === 0 && <p class="hint">{t('expert.calPreparing')}</p>}
      {step === 1 && <p class="hint">{t('expert.calWaitTick')}</p>}
      {cal?.RESTORING && <p class="hint warn">{t('expert.calRestoring')}</p>}
      {step > 0 && step < 3 && (cal?.DISCARDED ?? 0) > 0 &&
        <p class="hint">{t('expert.calDiscarded', { n: cal!.DISCARDED! })}</p>}
      {step > 0 && step < 3 && <Row label={t('expert.calAmbient')}><span>{dec(cal?.STARTAMB, 1)} °C</span></Row>}
    </>
  );
}
