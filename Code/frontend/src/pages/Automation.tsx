import { useEffect, useState } from 'preact/hooks';
import { postJson, postBlob, postRaw } from '../api';
import { Card, NumberInput, Row, Segmented, Toggle } from '../components/ui';
import { lastOther } from '../comms';
import { confirmDialog, showToast } from '../components/feedback';
import { QueueSkeleton } from '../components/Skeleton';
import { Icon, type IconName } from '../components/icons';
import { t } from '../i18n';

// CMD id -> locale key (index = CMD). Mirrors automation.html commandlist.
const CMD_KEY: Record<number, string> = {
  0: 'cmd.setTarget', 1: 'cmd.setUnit', 2: 'cmd.airjetToggle', 3: 'cmd.heaterToggle',
  4: 'cmd.filterPumpToggle', 5: 'cmd.resetQueue', 6: 'cmd.restartWifi',
  8: 'cmd.resetTimes', 11: 'cmd.hydrojetToggle', 12: 'cmd.setBrightness',
  19: 'cmd.printText', 22: 'cmd.powerToggle', 23: 'cmd.lockToggle', 26: 'cmd.lockButtons',
};

const OPTIONS = [
  { v: 4 }, { v: 3 }, { v: 2 }, { v: 11, jets: true },
  { v: 22 }, { v: 23 }, { v: 26 }, { v: 12 }, { v: 19 },
];

const REPEAT_PRESETS = [
  { value: 'once', label: 'auto.repeatOnce', hours: 0 },
  { value: 'hourly', label: 'auto.repeatHourly', hours: 1 },
  { value: 'daily', label: 'auto.repeatDaily', hours: 24 },
  { value: 'weekly', label: 'auto.repeatWeekly', hours: 168 },
  { value: 'custom', label: 'auto.repeatCustom', hours: null },
] as const;

const DAILY_INTERVAL_SECONDS = 24 * 3600;
const FILTER_PUMP_CMD = 4;

type Flag01 = 0 | 1;
interface Queue { LEN: number; QEN: boolean; CMD: number[]; VALUE: number[]; XTIME: number[]; INTERVAL: number[]; EN: boolean[]; }
type QueuePayload = {
  LEN?: number;
  QEN?: Flag01;
  CMD?: number[];
  VALUE?: number[];
  XTIME?: number[];
  INTERVAL?: number[];
  EN?: Flag01[];
};
interface QueueEntry { idx: number; cmd: number; value: number; xtime: number; interval: number; enabled: boolean; }
type RepeatPreset = typeof REPEAT_PRESETS[number]['value'];

const CMD_ICON: Record<number, IconName> = {
  2: 'airjet',
  3: 'heater',
  4: 'pump',
  11: 'hydrojet',
  12: 'sun',
  19: 'edit',
  22: 'power',
  23: 'lock',
  26: 'lock',
};

const CMD_TONE: Record<number, 'heat' | 'lock' | 'text' | 'bright' | undefined> = {
  3: 'heat',
  12: 'bright',
  19: 'text',
  23: 'lock',
  26: 'lock',
};

function nowLocal(): string {
  const d = new Date(Date.now() - new Date().getTimezoneOffset() * 60000);
  return d.toISOString().slice(0, 16);
}

function nextTimeToday(clock: string, base = new Date()): Date | null {
  const [hours, minutes] = clock.split(':').map((part) => Number(part));
  if (!Number.isFinite(hours) || !Number.isFinite(minutes)) return null;
  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) return null;
  const d = new Date(base);
  d.setHours(hours, minutes, 0, 0);
  if (d.getTime() <= base.getTime()) d.setDate(d.getDate() + 1);
  return d;
}

function timeOnSameDate(clock: string, base: Date): Date | null {
  const [hours, minutes] = clock.split(':').map((part) => Number(part));
  if (!Number.isFinite(hours) || !Number.isFinite(minutes)) return null;
  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) return null;
  const d = new Date(base);
  d.setHours(hours, minutes, 0, 0);
  return d;
}

function formatValue(cmd: number, value: number): string {
  if (cmd === 12) return String(value);
  if (cmd === 19) return '';
  if (cmd === 26) return value === 0 ? t('auto.lock') : t('auto.unlock');
  return value === 1 ? t('auto.switchOn') : value === 0 ? t('auto.switchOff') : String(value);
}

function commandLabel(cmd: number): string {
  return CMD_KEY[cmd] ? t(CMD_KEY[cmd]) : `CMD ${cmd}`;
}

function commandSummary(cmd: number, value: number): string {
  const detail = formatValue(cmd, value);
  return detail ? `${commandLabel(cmd)} ${detail}` : commandLabel(cmd);
}

function queueEntries(queue: Queue | null): QueueEntry[] {
  if (!queue) return [];
  const len = queue.LEN || 0;
  if (len <= 0) return [];
  return queue.CMD.slice(0, len).map((cmd, idx) => ({
    idx,
    cmd,
    value: queue.VALUE[idx] ?? 0,
    xtime: queue.XTIME[idx] ?? 0,
    interval: queue.INTERVAL[idx] ?? 0,
    enabled: queue.EN[idx] !== false,
  }));
}

function enabledValue(value: Flag01 | undefined): boolean {
  return value !== 0;
}

function normalizeQueue(queue: QueuePayload | null | undefined): Queue {
  const len = Math.max(0, Number(queue?.LEN) || 0);
  const en = Array.isArray(queue?.EN) ? queue.EN : [];
  return {
    LEN: len,
    QEN: enabledValue(queue?.QEN),
    CMD: Array.isArray(queue?.CMD) ? queue.CMD : [],
    VALUE: Array.isArray(queue?.VALUE) ? queue.VALUE : [],
    XTIME: Array.isArray(queue?.XTIME) ? queue.XTIME : [],
    INTERVAL: Array.isArray(queue?.INTERVAL) ? queue.INTERVAL : [],
    EN: en.map(enabledValue),
  };
}

function nextEntry(entries: QueueEntry[]): QueueEntry | null {
  const enabled = entries.filter((item) => item.enabled);
  if (!enabled.length) return null;
  return enabled.reduce((best, item) => item.xtime < best.xtime ? item : best, enabled[0]);
}

function repeatText(seconds: number, withPrefix = true): string {
  if (!seconds) return t('spa.onceShort');
  const span = `${seconds / 3600} ${t('spa.hours')}`;
  return withPrefix ? `${t('spa.repeat')} ${span}` : span;
}

function nextRunText(ts: number): string {
  return ts > 0 ? new Date(ts * 1000).toLocaleString() : t('time.never');
}

/**
 * @brief Automation command queue route.
 */
export function Automation() {
  const hasJets = lastOther.value?.HASJETS === true;
  const [cmd, setCmd] = useState(4);
  const [valOn, setValOn] = useState(true);
  const [bright, setBright] = useState(8);
  const [txt, setTxt] = useState('');
  const [xtime, setXtime] = useState(nowLocal());
  const [interval, setIntv] = useState('0');
  const [repeatPreset, setRepeatPreset] = useState<RepeatPreset>('once');
  const [presetStart, setPresetStart] = useState('08:00');
  const [presetEnd, setPresetEnd] = useState('10:00');
  const [queue, setQueue] = useState<Queue | null>(null);
  const [queueError, setQueueError] = useState(false);

  function load() {
    postJson<QueuePayload>('/getcommands/')
      .then((q) => { setQueue(normalizeQueue(q)); setQueueError(false); })
      .catch(() => { setQueueError(true); setQueue(normalizeQueue(null)); });
  }
  useEffect(() => {
    load();
    const id = setInterval(() => { if (!document.hidden) load(); }, 4000);
    const onVis = () => { if (!document.hidden) load(); };
    document.addEventListener('visibilitychange', onVis);
    return () => { clearInterval(id); document.removeEventListener('visibilitychange', onVis); };
  }, []);

  function buildJson() {
    let value: number;
    if (cmd === 12) value = bright;
    else if (cmd === 19) value = 0;
    else value = cmd === 26 ? (valOn ? 0 : 1) : (valOn ? 1 : 0);
    const parsed = Date.parse(xtime) / 1000;
    const preset = REPEAT_PRESETS.find((item) => item.value === repeatPreset);
    const intervalHours = preset?.hours ?? (parseInt(interval) || 0);
    return {
      CMD: cmd, VALUE: value,
      XTIME: isNaN(parsed) ? 0 : Math.floor(parsed),
      INTERVAL: intervalHours * 3600,
      TXT: txt,
    };
  }

  async function add() {
    try { await postJson('/addcommand/', buildJson()); load(); }
    catch (e) { showToast(String(e), true); }
  }
  async function addDailyPumpPreset() {
    if (presetStart === presetEnd) {
      showToast(t('auto.invalidTimeWindow'), true);
      return;
    }
    const start = nextTimeToday(presetStart);
    const end = start ? timeOnSameDate(presetEnd, start) : null;
    if (!start || !end) {
      showToast(t('common.error'), true);
      return;
    }
    if (end.getTime() <= start.getTime()) end.setDate(end.getDate() + 1);
    const startTs = Math.floor(start.getTime() / 1000);
    const endTs = Math.floor(end.getTime() / 1000);
    try {
      await postJson('/addcommand/', {
        CMD: FILTER_PUMP_CMD, VALUE: 1, XTIME: startTs, INTERVAL: DAILY_INTERVAL_SECONDS, TXT: '',
      });
      await postJson('/addcommand/', {
        CMD: FILTER_PUMP_CMD, VALUE: 0, XTIME: endTs, INTERVAL: DAILY_INTERVAL_SECONDS, TXT: '',
      });
      showToast(t('auto.presetAdded'));
      load();
    } catch (e) {
      showToast(String(e), true);
    }
  }
  async function edit(idx: number) {
    try { await postJson('/editcommand/', { ...buildJson(), IDX: idx }); load(); }
    catch { showToast(t('common.error'), true); }
  }
  async function del(idx: number) {
    try { await postJson('/delcommand/', { IDX: idx }); load(); }
    catch { showToast(t('common.error'), true); }
  }
  async function setQueueEnabled(enabled: boolean) {
    setQueue((q) => q ? { ...q, QEN: enabled } : q);
    try { await postJson('/setcommands/', { QEN: enabled ? 1 : 0 }); load(); }
    catch { showToast(t('common.error'), true); load(); }
  }
  async function setEntryEnabled(idx: number, enabled: boolean) {
    setQueue((q) => {
      if (!q) return q;
      const nextEnabled = q.EN.slice();
      nextEnabled[idx] = enabled;
      return { ...q, EN: nextEnabled };
    });
    try { await postJson('/setcommands/', { IDX: idx, EN: enabled ? 1 : 0 }); load(); }
    catch { showToast(t('common.error'), true); load(); }
  }
  async function clearQueue() {
    if (!await confirmDialog(t('auto.confirmClear'), { danger: true })) return;
    try { await postJson('/addcommand/', { CMD: 5, VALUE: 1, XTIME: 0, INTERVAL: 0 }); load(); }
    catch { showToast(t('common.error'), true); }
  }
  async function saveFile() {
    if (!queue || queue.LEN <= 0) {
      showToast(t('auto.saveEmpty'), true);
      return;
    }
    let blob: Blob;
    try { blob = await postBlob('/cmdq_file/', { ACT: 'download' }); }
    catch { showToast(t('common.error'), true); return; }
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `wifiwhirl_queue_${Date.now()}.json`;
    a.click();
    URL.revokeObjectURL(a.href);
  }
  function restoreFile() {
    const input = document.createElement('input');
    input.type = 'file'; input.accept = '.json';
    input.onchange = async () => {
      const f = input.files?.[0]; if (!f) return;
      const content = await f.text();
      try { await postRaw('/cmdq_file/?action=upload', content); showToast(t('auto.restored')); load(); }
      catch { showToast(t('common.error'), true); }
    };
    input.click();
  }

  const opts = OPTIONS.filter((o) => !o.jets || hasJets);
  const entries = queueEntries(queue);
  const queueEnabled = queue?.QEN !== false;
  const next = queueEnabled ? nextEntry(entries) : null;
  const canSaveQueue = !queueError && (queue?.LEN ?? 0) > 0;
  const hasPumpCycle =
    entries.some((e) => e.cmd === FILTER_PUMP_CMD && e.value === 1) &&
    entries.some((e) => e.cmd === FILTER_PUMP_CMD && e.value === 0);

  useEffect(() => {
    if (!opts.some((o) => o.v === cmd)) setCmd(4);
  }, [cmd, hasJets]);

  return (
    <>
      <h1>{t('automation.title')}</h1>

      <Card className="auto-hero">
        <div class="auto-hero-main">
          <span class="auto-icon auto-hero-icon">
            <Icon name={next ? (CMD_ICON[next.cmd] ?? 'automation') : 'automation'} size={30} />
          </span>
          <div>
            <p class="eyebrow">{t('auto.overview')}</p>
            {/* The headline is the next run time - the command itself is named in
                the "next command" stat right below, so it was said twice. */}
            <h2>{next ? `${t('spa.nextRun')} ${nextRunText(next.xtime)}` : t('auto.emptyTitle')}</h2>
            {(!queueEnabled || !next) && (
              <p class="hint">{queueEnabled ? t('auto.emptyHint') : t('auto.queueDisabled')}</p>
            )}
          </div>
        </div>
        <div class="auto-hero-grid">
          <AutomationStat label={t('auto.nextCommand')} value={next ? commandSummary(next.cmd, next.value) : '--'} />
          <AutomationStat label={t('auto.repeatCommand')} value={next ? repeatText(next.interval, false) : '--'} />
          <AutomationStat label={t('auto.queueCount')} value={queue ? String(queue.LEN) : '--'} />
        </div>
      </Card>

      {!hasPumpCycle && (
      <Card className="auto-preset">
        <div class="auto-preset-info">
          <p class="eyebrow">{t('auto.presets')}</p>
          <h3>{t('auto.dailyPumpRun')}</h3>
        </div>
        <div class="auto-preset-fields">
          <input type="time" aria-label={t('auto.startTime')} value={presetStart} onInput={(e) => setPresetStart((e.target as HTMLInputElement).value)} />
          <span class="auto-preset-sep">-</span>
          <input type="time" aria-label={t('auto.endTime')} value={presetEnd} onInput={(e) => setPresetEnd((e.target as HTMLInputElement).value)} />
          <button class="btn secondary" onClick={addDailyPumpPreset}>{t('auto.addDailyPumpRun')}</button>
        </div>
      </Card>
      )}

      <Card className="auto-planner">
        <div class="auto-section-head">
          <div>
            <p class="eyebrow">{t('auto.plannerTitle')}</p>
            <h2>{t('auto.addCommand')}</h2>
            <p class="hint">{t('auto.plannerHint')}</p>
          </div>
        </div>

        <div class="auto-command-grid" aria-label={t('auto.chooseCommand')}>
          {opts.map((o) => (
            <button
              key={o.v}
              type="button"
              class={`auto-command${cmd === o.v ? ' active' : ''}${CMD_TONE[o.v] ? ' ' + CMD_TONE[o.v] : ''}`}
              aria-pressed={cmd === o.v}
              onClick={() => setCmd(o.v)}
            >
              <span class="auto-icon"><Icon name={CMD_ICON[o.v] ?? 'automation'} size={24} /></span>
              <span>{t(CMD_KEY[o.v])}</span>
            </button>
          ))}
        </div>

        <div class="auto-planner-grid">
          <div class="auto-panel">
            <h3>{t('auto.commandDetails')}</h3>
            {cmd === 12 ? (
              <Row label={t('spa.brightness')}>
                <span class="range-control">
                  <input type="range" min={0} max={8} value={bright}
                    onInput={(e) => setBright(parseInt((e.target as HTMLInputElement).value))} />
                  <b>{bright}</b>
                </span>
              </Row>
            ) : cmd === 19 ? (
              <Row label={t('spa.text')}>
                <input type="text" maxLength={24} pattern="[a-z0-9-]*" value={txt}
                  onInput={(e) => setTxt((e.target as HTMLInputElement).value)} />
              </Row>
            ) : (
              <Row label={cmd === 26 ? t('auto.controlButtons') : t('spa.toggle')}>
                <Segmented
                  value={valOn ? 'on' : 'off'}
                  options={[
                    { value: 'on', label: cmd === 26 ? t('auto.lock') : t('spa.on') },
                    { value: 'off', label: cmd === 26 ? t('auto.unlock') : t('spa.off') },
                  ]}
                  onChange={(v) => setValOn(v === 'on')}
                />
              </Row>
            )}
          </div>

          <div class="auto-panel">
            <h3>{t('auto.scheduleDetails')}</h3>
            <Row label={t('spa.execTime')}>
              <input type="datetime-local" value={xtime} onInput={(e) => setXtime((e.target as HTMLInputElement).value)} />
            </Row>
            <Row label={t('auto.repeat')}>
              <div class="auto-repeat-control">
                <Segmented
                  value={repeatPreset}
                  options={REPEAT_PRESETS.map((item) => ({ value: item.value, label: t(item.label) }))}
                  onChange={(v) => {
                    setRepeatPreset(v);
                    const preset = REPEAT_PRESETS.find((item) => item.value === v);
                    if (preset?.hours != null) setIntv(String(preset.hours));
                    else if ((parseInt(interval) || 0) < 1) setIntv('1');
                  }}
                />
              </div>
            </Row>
            {repeatPreset === 'custom' && (
              <Row label={t('auto.repeatEvery')}>
                <NumberInput value={interval} min={1} step={1} unit={t('spa.hours')} onInput={(v) => {
                  setIntv(v);
                }} />
              </Row>
            )}
            <p class="hint">{repeatPreset === 'once' ? t('auto.repeatOnceHint') : t('auto.repeatHint')}</p>
          </div>
        </div>
        <button class="btn block" onClick={add}>{t('spa.add')}</button>
      </Card>

      <Card title={t('auto.queueTimeline')}>
        {queueError ? <QueueUnavailable />
          : !queue ? <QueueSkeleton />
          : entries.length === 0 ? <AutomationEmpty />
            : <div class="auto-timeline">
              {entries.map((item) => (
                <div key={item.idx} class={`auto-timeline-item${!item.enabled ? ' disabled' : ''}${CMD_TONE[item.cmd] ? ' ' + CMD_TONE[item.cmd] : ''}`}>
                  <span class="auto-icon auto-timeline-icon">
                    <Icon name={CMD_ICON[item.cmd] ?? 'automation'} size={22} />
                  </span>
                  <div class="auto-timeline-body">
                    <b>{commandSummary(item.cmd, item.value)}</b>
                    <span>{t('spa.nextRun')} {nextRunText(item.xtime)}</span>
                    <span>{repeatText(item.interval)}</span>
                    {!item.enabled && <span>{t('auto.commandDisabled')}</span>}
                  </div>
                  <span class="inline-actions auto-row-actions">
                    <button class="btn secondary" onClick={() => setEntryEnabled(item.idx, !item.enabled)}>
                      {item.enabled ? t('auto.disableCommand') : t('auto.enableCommand')}
                    </button>
                    <button class="btn secondary" onClick={() => edit(item.idx)}>{t('spa.overwrite')}</button>
                    <button class="btn danger" onClick={() => del(item.idx)}>{t('spa.delete')}</button>
                  </span>
                </div>
              ))}
            </div>}
      </Card>

      <Card title={t('auto.queueTools')}>
        <div class="auto-queue-state">
          <span>{t('auto.queueEnabled')}</span>
          <Toggle checked={queueEnabled} onChange={setQueueEnabled} />
        </div>
        <div class="actions">
          <button class="btn secondary" disabled={!canSaveQueue} title={!canSaveQueue ? t('auto.saveEmpty') : undefined} onClick={saveFile}>
            {t('spa.saveQueue')}
          </button>
          <button class="btn secondary" onClick={restoreFile}>{t('spa.restoreQueue')}</button>
          <button class="btn danger" onClick={clearQueue}>{t('spa.clearQueue')}</button>
        </div>
        {!canSaveQueue && <p class="hint">{t('auto.saveEmpty')}</p>}
      </Card>
    </>
  );
}

function AutomationStat({ label, value }: { label: string; value: string }) {
  return (
    <div class="auto-stat">
      <span>{label}</span>
      <b>{value}</b>
    </div>
  );
}

function AutomationEmpty() {
  return (
    <div class="auto-empty">
      <span class="auto-icon"><Icon name="automation" size={28} /></span>
      <b>{t('auto.emptyTitle')}</b>
      <p class="hint">{t('auto.emptyHint')}</p>
    </div>
  );
}

function QueueUnavailable() {
  return (
    <div class="auto-empty">
      <span class="auto-icon"><Icon name="automation" size={28} /></span>
      <b>{t('auto.queueUnavailable')}</b>
      <p class="hint">{t('common.offline')}</p>
    </div>
  );
}
