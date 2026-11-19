import { useEffect, useState } from 'preact/hooks';
import { postJson } from '../api';
import { lastStates } from '../comms';
import { confirmDialog, showToast } from './feedback';
import { navigate } from '../router';
import { activeLocale, t } from '../i18n';

interface Sched { ACTIVE?: boolean; TARGETTEMP?: number; TARGETTIME?: number; STARTTIME?: number; HEATER?: boolean }

/** Monday 00:00 of the week `d` is in. */
const weekStart = (d: Date) => { const x = new Date(d); x.setHours(0, 0, 0, 0); x.setDate(x.getDate() - ((x.getDay() + 6) % 7)); return x.getTime(); };

/** "um 16:55 Uhr" today, "am Dienstag um 16:55 Uhr" this week, "am 01.01.2027 um 16:55 Uhr" later. */
const when = (ts: number) => {
  const d = new Date(ts * 1000), now = new Date(), loc = activeLocale.value;
  const time = d.toLocaleTimeString(loc, { hour: '2-digit', minute: '2-digit' });
  if (d.toDateString() === now.toDateString()) return t('ss.whenToday', { time });
  if (weekStart(d) === weekStart(now)) return t('ss.whenWeekday', { day: d.toLocaleDateString(loc, { weekday: 'long' }), time });
  return t('ss.whenDate', { date: d.toLocaleDateString(loc, { day: '2-digit', month: '2-digit', year: 'numeric' }), time });
};

/**
 * Note on the overview while a Smart Schedule is planned or heating. Information
 * only (unlike the calibration nothing is locked): a click opens the Smart
 * Schedule page, "cancel" ends it. Details are fetched only while SSA is set.
 */
export function SmartScheduleNote() {
  const active = !!lastStates.value?.SSA;
  const [d, setD] = useState<Sched | null>(null);
  useEffect(() => {
    if (!active) { setD(null); return; }
    const load = () => postJson<Sched>('/getsmartschedule/', {}).then(setD).catch(() => {});
    load();
    const id = setInterval(load, 60000);
    return () => clearInterval(id);
  }, [active]);
  if (!active || !d?.ACTIVE || !d.TARGETTIME) return null;

  const c = lastStates.value?.UNT !== false;
  const temp = `${Math.round(c ? d.TARGETTEMP ?? 0 : (d.TARGETTEMP ?? 0) * 9 / 5 + 32)} ${c ? '°C' : '°F'}`;
  const start = d.STARTTIME ?? 0;
  const heating = d.HEATER || (start > 0 && Date.now() / 1000 >= start);
  const text = t(heating ? 'ss.noteHeating' : 'ss.notePlanned', { temp, when: when(d.TARGETTIME) });

  async function cancel(e: Event) {
    e.stopPropagation();
    if (!await confirmDialog(t('ss.confirmCancel'), { danger: true })) return;
    try { await postJson('/cancelsmartschedule/', {}); showToast(t('ss.cancelled')); setD(null); }
    catch { showToast(t('common.error'), true); }
  }

  return (
    <div class="cal-banner ss-note" role="status" onClick={() => navigate('/smartschedule')}>
      {text}{' '}
      <button class="btn ghost" onClick={cancel}>{t('ss.cancel')}</button>
    </div>
  );
}
