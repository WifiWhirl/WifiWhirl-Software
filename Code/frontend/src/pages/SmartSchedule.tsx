import { useEffect, useState } from 'preact/hooks';
import { postJson } from '../api';
import { lastStates } from '../comms';
import { Card, Segmented, TimelineStep } from '../components/ui';
import { Thermostat } from '../components/Thermostat';
import { confirmDialog, showToast } from '../components/feedback';
import { PageSkeleton } from '../components/Skeleton';
import { t } from '../i18n';
import { dec, durLong, money } from '../util/format';

interface Sched {
  ACTIVE?: boolean; GLOBALTARGET?: number; TARGETTEMP?: number; CURRENTTEMP?: number;
  ACCURATETEMP?: number; ESTIMATE?: number; BUFFER?: number; ESTIMATED_KWH?: number;
  ESTIMATED_COST?: number; KEEPON?: boolean; REPEAT?: number; HEATER?: boolean;
  HEATERRED?: boolean; HEATERGRN?: boolean; READING_STATE?: number;
  REMAINING_HEATING_TIME?: number; TARGETTIME?: number; STARTTIME?: number;
  NEXTCHECK?: number; CHECKCOMPLETED?: boolean; TIMEREMAINING?: number; TIMEUNTILSTART?: number;
}

const dt = (ts: number) => new Date(ts * 1000).toLocaleString();

/* The smart-schedule API stores TARGETTEMP in Celsius, but the live values
   (GLOBALTARGET, CURRENTTEMP, ACCURATETEMP) come in the tub's display unit.
   Keep state in Celsius and convert at the display/input boundary. */
const f2c = (v: number) => Math.round((v - 32) * 5 / 9);
const c2f = (c: number) => Math.round(c * 9 / 5 + 32);
const isC = () => lastStates.value?.UNT !== false; // unknown states -> assume °C

// HEATER alone can't tell "heating up" (red) from "target reached, holding"
// (green). Fall back to HEATER for firmware without the split keys.
const heatingRed = (d: Sched) => d.HEATERRED ?? !!d.HEATER;
const reachedGrn = (d: Sched) => !!d.HEATERGRN && !d.HEATERRED;

function defaultDateTime(): string {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}T19:00`;
}

/**
 * @brief Smart heating schedule route.
 */
export function SmartSchedule() {
  const [data, setData] = useState<Sched | null>(null);
  const [targetDateTime, setDT] = useState(defaultDateTime());
  const [targetTemp, setTemp] = useState(38);
  const [keepOn, setKeepOn] = useState(false);
  const [repeat, setRepeat] = useState(0);

  function refresh() {
    postJson<Sched>('/getsmartschedule/', {}).then((d) => {
      setData(d);
      // Seed only once the unit is known (states may arrive after the first
      // schedule poll) - GLOBALTARGET is a display-unit value.
      if (d.GLOBALTARGET && d.GLOBALTARGET > 0 && lastStates.value) {
        const globalC = isC() ? d.GLOBALTARGET : f2c(d.GLOBALTARGET);
        setTemp((prev) => (prev === 38 ? globalC : prev));
      }
    }).catch(() => {});
  }
  useEffect(() => {
    refresh();
    const id = setInterval(() => { if (!document.hidden) refresh(); }, 5000);
    const onVis = () => { if (!document.hidden) refresh(); };
    document.addEventListener('visibilitychange', onVis);
    return () => { clearInterval(id); document.removeEventListener('visibilitychange', onVis); };
  }, []);

  async function activate() {
    if (!targetDateTime) { showToast(t('ss.errNoDateTime'), true); return; }
    if (targetTemp < 20 || targetTemp > 40) { showToast(t('ss.errTempRange'), true); return; }
    const ts = Math.floor(new Date(targetDateTime).getTime() / 1000);
    if (ts <= Math.floor(Date.now() / 1000)) { showToast(t('ss.errFuture'), true); return; }
    try {
      await postJson('/setsmartschedule/', { TARGETTIME: ts, TARGETTEMP: targetTemp, KEEPON: keepOn, REPEAT: repeat });
      showToast(t('ss.activated'));
      refresh();
    } catch { showToast(t('ss.errActivate'), true); }
  }

  async function updateKeep(v: boolean) {
    try { await postJson('/updatesmartschedule/', { KEEPON: v }); refresh(); }
    catch { showToast(t('common.error'), true); refresh(); }
  }

  async function updateRepeat(v: number) {
    try { await postJson('/updatesmartschedule/', { KEEPON: !!data?.KEEPON, REPEAT: v }); refresh(); }
    catch { showToast(t('common.error'), true); refresh(); }
  }

  async function cancel() {
    if (!await confirmDialog(t('ss.confirmCancel'), { danger: true })) return;
    try { await postJson('/cancelsmartschedule/', {}); showToast(t('ss.cancelled')); refresh(); }
    catch { showToast(t('common.error'), true); }
  }

  if (!data) return <PageSkeleton cards={1} rows={6} actions={false} />;

  const yesNo = [{ value: 'no', label: t('common.no') }, { value: 'yes', label: t('common.yes') }];
  const repeatOpts = [
    { value: '0', label: t('ss.repeatNever') },
    { value: '1', label: t('ss.repeatDaily') },
    { value: '7', label: t('ss.repeatWeekly') },
  ];

  if (data.ACTIVE) {
    const est = data.ESTIMATE ?? 0;
    const costText = (data.ESTIMATED_KWH ?? 0) > 0
      ? `${money(data.ESTIMATED_COST ?? 0)} (${dec(data.ESTIMATED_KWH, 2)} kWh)`
      : est <= 0 ? `${money(0)} (${dec(0, 2)} kWh)` : '--';
    const remainingHeating = remainingHeatingText(data);
    const readingState = readingStateText(data);
    const status = scheduleStatusText(data);
    return (
      <>
        <h1>{t('nav.smartschedule')}</h1>
        <Card className={`ss-hero${heatingRed(data) ? ' heating' : reachedGrn(data) ? ' reached' : ''}`}>
          <div class="ss-hero-head">
            <div>
              <p class="eyebrow">{t('ss.activeTitle')}</p>
              <h2>{data.TARGETTIME ? dt(data.TARGETTIME) : '--'}</h2>
              <p class="hint">{status}</p>
            </div>
            <div class="ss-temp-badge">
              <span>{dec(isC() ? data.TARGETTEMP : c2f(data.TARGETTEMP ?? 0))}</span>
              <small>{isC() ? '°C' : '°F'}</small>
            </div>
          </div>
          <div class="ss-hero-grid">
            <ScheduleStat label={t('ss.targetCountdown')} value={data.TIMEREMAINING != null ? durLong(data.TIMEREMAINING) : '--'} />
            <ScheduleStat label={t('ss.startIn')} value={startInText(data)}
              tone={heatingRed(data) ? 'heat' : reachedGrn(data) ? 'ok' : undefined} />
            <ScheduleStat label={t('ss.heater')} value={data.HEATER ? t('ss.heaterOn') : t('ss.heaterOff')}
              tone={heatingRed(data) ? 'heat' : reachedGrn(data) ? 'ok' : undefined} />
          </div>
          <button class="btn danger ss-cancel" onClick={cancel}>{t('ss.cancel')}</button>
        </Card>

        <Card title={t('ss.startTime')}>
          <ScheduleTimeline data={data} />
        </Card>

        <Card title={t('ss.heatDurationEst')}>
          <div class="ss-metric-grid">
            <ScheduleMetric label={t('ss.currentTemp')} value={`${dec(data.CURRENTTEMP)} ${isC() ? '°C' : '°F'}`} />
            <ScheduleMetric
              label={t('ss.accurateMeasure')}
              value={data.ACCURATETEMP && data.ACCURATETEMP > 0 ? `${dec(data.ACCURATETEMP)} ${isC() ? '°C' : '°F'}` : t('ss.measuring')}
            />
            <ScheduleMetric
              label={t('ss.heatDurationEst')}
              value={est >= 999 ? t('ss.impossible') : est > 0 ? durLong(Math.round(est * 3600)) : t('ss.alreadyTemp')}
            />
            <ScheduleMetric
              label={t('ss.safetyBuffer')}
              value={est > 0 && est < 999 && (data.BUFFER ?? 0) > 0 ? durLong(Math.round(data.BUFFER! * 3600)) : '--'}
            />
            <ScheduleMetric label={t('ss.estCost')} value={costText} />
            {remainingHeating && <ScheduleMetric label={t('ss.remainingHeating')} value={remainingHeating} />}
            <ScheduleMetric label={t('ss.nextCheck')} value={nextCheckText(data)} wide />
            {readingState && <ScheduleMetric label={t('ss.readingState')} value={readingState} wide tone={reachedGrn(data) ? 'ok' : 'info'} />}
          </div>
        </Card>

        <Card title={t('ss.options')}>
          <div class="ss-option-row">
            <div>
              <b>{t('ss.keepOn')}</b>
              <p class="hint">{t('ss.helpTarget')}</p>
            </div>
            <Segmented value={data.KEEPON ? 'yes' : 'no'} options={yesNo} onChange={(v) => updateKeep(v === 'yes')} />
          </div>
          <div class="ss-option-row">
            <div>
              <b>{t('ss.repeat')}</b>
              <p class="hint">{t('ss.repeatHint')}</p>
            </div>
            <Segmented value={String(data.REPEAT ?? 0)} options={repeatOpts} onChange={(v) => updateRepeat(Number(v))} />
          </div>
        </Card>
      </>
    );
  }

  // Dial works in the tub's display unit; targetTemp state stays Celsius.
  const unitC = isC();
  const currentTemp = data.CURRENTTEMP ?? data.ACCURATETEMP ?? (unitC ? targetTemp : c2f(targetTemp));
  return (
    <>
      <h1>{t('nav.smartschedule')}</h1>
      <Card className="ss-create">
        <div class="ss-create-head">
          <p class="eyebrow">{t('ss.newTitle')}</p>
          <h2>{t('ss.targetTime')}</h2>
          <p class="hint">{t('ss.intro')}</p>
        </div>
        <div class="ss-create-grid">
          <label class="ss-form-tile">
            <span>{t('ss.targetTime')}</span>
            <input type="datetime-local" value={targetDateTime} onInput={(e) => setDT((e.target as HTMLInputElement).value)} />
          </label>
          <div class="ss-form-tile ss-temp-tile">
            <span>{t('ss.targetTempC')}</span>
            <Thermostat value={unitC ? targetTemp : c2f(targetTemp)} current={currentTemp}
              min={unitC ? 20 : 68} max={unitC ? 40 : 104} unit={unitC ? '°C' : '°F'}
              onChange={(v) => setTemp(unitC ? v : f2c(v))} />
          </div>
          <div class="ss-form-tile">
            <span>{t('ss.keepOn')}</span>
            <Segmented value={keepOn ? 'yes' : 'no'} options={yesNo} onChange={(v) => setKeepOn(v === 'yes')} />
          </div>
          <div class="ss-form-tile">
            <span>{t('ss.repeat')}</span>
            <Segmented value={String(repeat)} options={repeatOpts} onChange={(v) => setRepeat(Number(v))} />
          </div>
        </div>
        <button class="btn block ss-activate" onClick={activate}>{t('ss.activate')}</button>
      </Card>
      <SmartScheduleHelp />
    </>
  );
}

function ScheduleStat({ label, value, tone }: { label: string; value: string; tone?: 'heat' | 'ok' }) {
  return (
    <div class={`ss-stat${tone ? ' ' + tone : ''}`}>
      <span>{label}</span>
      <b>{value}</b>
    </div>
  );
}

function ScheduleMetric({ label, value, wide, tone }: { label: string; value: string; wide?: boolean; tone?: 'info' | 'ok' }) {
  return (
    <div class={`ss-metric${wide ? ' wide' : ''}${tone ? ' ' + tone : ''}`}>
      <span>{label}</span>
      <b>{value}</b>
    </div>
  );
}

function ScheduleTimeline({ data }: { data: Sched }) {
  const now = Math.floor(Date.now() / 1000);
  const target = data.TARGETTIME ?? 0;
  const start = data.STARTTIME ?? 0;
  const progress = target > 0 ? Math.max(0, Math.min(100, ((now - Math.max(0, target - 86400)) / (target - Math.max(0, target - 86400))) * 100)) : 0;
  const heating = data.HEATER || (start > 0 && now >= start);

  return (
    <div class="ss-timeline" style={`--ss-progress:${progress}%`}>
      <div class="ss-timeline-bar"><span /></div>
      <div class="ss-timeline-steps">
        <TimelineStep label={t('ss.currentTemp')} value={`${dec(data.CURRENTTEMP)} ${isC() ? '°C' : '°F'}`} active />
        <TimelineStep label={t('ss.nextCheck')} value={nextCheckText(data)} active={!data.CHECKCOMPLETED && !heating} />
        <TimelineStep label={t('ss.startTime')} value={start > 0 ? dt(start) : t('ss.calculating')} active={heating} />
        <TimelineStep label={t('ss.targetTime')} value={target > 0 ? dt(target) : '--'} active={target > 0 && now >= target} />
      </div>
    </div>
  );
}

function startInText(data: Sched): string {
  if (!data.STARTTIME || data.STARTTIME <= 0) return t('ss.calculating');
  if ((data.TIMEUNTILSTART ?? 0) <= 0) {
    if (reachedGrn(data)) return t('ss.targetReached');
    return heatingRed(data) ? t('ss.nowHeating') : t('ss.startingSoon');
  }
  return durLong(data.TIMEUNTILSTART!);
}

function scheduleStatusText(data: Sched): string {
  const readingState = readingStateText(data);
  if (readingState) return readingState;
  if (!data.STARTTIME || data.STARTTIME <= 0) return t('ss.calculating');
  if ((data.TIMEUNTILSTART ?? 0) > 0) return `${t('ss.startIn')}: ${durLong(data.TIMEUNTILSTART!)}`;
  return t('ss.startingSoon');
}

function remainingHeatingText(data: Sched): string {
  if (data.HEATER && data.REMAINING_HEATING_TIME != null && data.REMAINING_HEATING_TIME >= 0) {
    if (data.REMAINING_HEATING_TIME === 0) return t('ss.alreadyTemp');
    if (data.REMAINING_HEATING_TIME >= 999) return t('ready.notComputable');
    return durLong(Math.round(data.REMAINING_HEATING_TIME * 3600));
  }
  if ((data.ESTIMATE ?? 0) <= 0) return t('ss.alreadyTemp');
  return '';
}

function nextCheckText(data: Sched): string {
  if (data.CHECKCOMPLETED) return t('ss.checkCompleted');
  if (data.NEXTCHECK) return dt(data.NEXTCHECK);
  return t('ss.calculating');
}

function readingStateText(data: Sched): string {
  if (reachedGrn(data)) return t('ss.targetReachedStatus');
  if (heatingRed(data)) return t('ss.readingHeating');
  if (data.READING_STATE === 1) return t('ss.readingPump');
  if (data.READING_STATE === 2) return t('ss.readingTemp');
  return '';
}

function SmartScheduleHelp() {
  return (
    <Card title={t('ss.helpTitle')}>
      <p class="hint">{t('ss.helpIntro')}</p>
      <p class="hint">{t('ss.helpMeasure')}</p>
      <p class="hint">{t('ss.helpAdaptive')}</p>
      <p class="hint">{t('ss.helpTarget')}</p>
    </Card>
  );
}
