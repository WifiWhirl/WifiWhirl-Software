import { useEffect, useState } from 'preact/hooks';
import { postJson } from '../api';
import { lastStates } from '../comms';
import { confirmDialog, showToast } from './feedback';
import { navigate } from '../router';
import { t } from '../i18n';

interface Sched { ACTIVE?: boolean; TARGETTEMP?: number; TARGETTIME?: number; STARTTIME?: number; HEATER?: boolean }

/** "19:00" today, otherwise "Di. 19:00". */
const when = (ts: number) => {
  const d = new Date(ts * 1000);
  const time = d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  return d.toDateString() === new Date().toDateString() ? time : `${d.toLocaleDateString([], { weekday: 'short' })} ${time}`;
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
  const now = Date.now() / 1000;
  const start = d.STARTTIME ?? 0;
  const text = d.HEATER || (start > 0 && now >= start)
    ? t('ss.noteHeating', { temp, time: when(d.TARGETTIME) })
    : start > 0
      ? t('ss.notePlanned', { temp, time: when(d.TARGETTIME), start: when(start) })
      : t('ss.notePlannedNoStart', { temp, time: when(d.TARGETTIME) });

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
