import { useEffect, useRef, useState } from 'preact/hooks';
import { postJson } from '../api';
import { Segmented } from './ui';
import { TextField } from './fields';
import { showToast, confirmDialog } from './feedback';
import { t } from '../i18n';

interface Key { id: number; name: string; control: boolean; created: number }

/**
 * @brief API keys of /api/v1: list, create (shown once), delete.
 * The module only keeps a salted hash, so a lost key can't be shown again.
 */
export function ApiKeys() {
  const [keys, setKeys] = useState<Key[]>([]);
  const [max, setMax] = useState(4);
  const [name, setName] = useState('');
  const [control, setControl] = useState(false); // "read only" preselected: the safer choice
  const [fresh, setFresh] = useState<{ name: string; key: string } | null>(null);
  const keyRef = useRef<HTMLInputElement>(null);

  const load = () => postJson<{ max: number; keys: Key[] }>('/getapikeys/')
    .then((j) => { setKeys(j.keys || []); setMax(j.max || 4); }).catch(() => {});
  useEffect(() => { load(); }, []);
  useEffect(() => { keyRef.current?.select(); }, [fresh]);

  async function create() {
    const n = name.trim();
    if (!n) { showToast(t('expert.apiKeyErrName'), true); return; }
    try {
      const r = await postJson<{ key: string }>('/addapikey/', { name: n, control });
      setFresh({ name: n, key: r.key });
      setName('');
      load();
    } catch { showToast(t('common.error'), true); }
  }

  async function remove(k: Key) {
    if (!await confirmDialog(t('expert.apiKeyDelConfirm', { name: k.name }), { danger: true })) return;
    try { await postJson('/delapikey/', { id: k.id }); load(); } catch { showToast(t('common.error'), true); }
  }

  async function copy() {
    keyRef.current?.select();
    // the module is plain http, where navigator.clipboard is missing
    try {
      if (navigator.clipboard) await navigator.clipboard.writeText(fresh!.key);
      else document.execCommand('copy');
      showToast(t('expert.apiKeyCopied'));
    } catch { /* still selected: the user can copy by hand */ }
  }

  const date = (s: number) => (s ? new Date(s * 1000).toLocaleDateString() : '–');

  return (
    <div class="api-keys">
      <h3>{t('expert.apiKeys')}</h3>
      {keys.length === 0 && <p class="hint">{t('expert.apiKeysNone')}</p>}
      {keys.map((k) => (
        <div class="row api-key" key={k.id}>
          <div>
            <strong>{k.name}</strong>
            <span class="hint"> · {k.control ? t('expert.apiKeyControl') : t('expert.apiKeyRead')} · {date(k.created)}</span>
          </div>
          <button type="button" class="btn danger" onClick={() => remove(k)}>{t('expert.apiKeyDel')}</button>
        </div>
      ))}

      {fresh && (
        <div class="api-key-new">
          <p class="hint warn">{t('expert.apiKeyOnce', { name: fresh.name })}</p>
          <input ref={keyRef} class="mono" readOnly value={fresh.key} aria-label={t('expert.apiKeys')} onFocus={(e) => (e.target as HTMLInputElement).select()} />
          <div class="card-save">
            <button type="button" class="btn" onClick={copy}>{t('expert.apiKeyCopy')}</button>
            <button type="button" class="btn secondary" onClick={() => setFresh(null)}>{t('expert.apiKeyDone')}</button>
          </div>
        </div>
      )}

      {keys.length < max ? (
        <>
          <TextField label={t('expert.apiKeyName')} value={name} maxLength={32} placeholder={t('expert.apiKeyNamePh')} onInput={setName} />
          <Segmented value={control ? 1 : 0} onChange={(v) => setControl(v === 1)}
            options={[{ value: 0, label: t('expert.apiKeyRead') }, { value: 1, label: t('expert.apiKeyControl') }]} />
          <div class="card-save">
            <button type="button" class="btn" onClick={create}>{t('expert.apiKeyCreate')}</button>
          </div>
        </>
      ) : <p class="hint">{t('expert.apiKeysFull', { n: max })}</p>}
    </div>
  );
}
