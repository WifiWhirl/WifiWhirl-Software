import { signal } from '@preact/signals';
import { useState } from 'preact/hooks';
import { t } from '../i18n';
import { authSessionActive } from '../api';
import { Modal } from './ui';

const MODAL_ANIM_MS = 200;

interface PwdPrompt {
  title: string;
  error: string;
  resolve: (pwd: string | null) => void;
}

const prompt = signal<PwdPrompt | null>(null);
// Kept until the page reloads, so backup, restore and support don't ask twice.
let cachedPwd = '';

function askOtaPassword(title: string, error: string): Promise<string | null> {
  if (prompt.value) prompt.value.resolve(null);
  return new Promise((resolve) => { prompt.value = { title, error, resolve }; });
}

const basic = (user: string, pwd: string): Record<string, string> =>
  ({ Authorization: 'Basic ' + btoa(unescape(encodeURIComponent(`${user}:${pwd}`))) });

/**
 * @brief Run a request that the firmware protects with the OTA password.
 *
 * With global login on, the session covers it (unless `always`, for the
 * firmware updater which can't read cookies). Otherwise the password is asked
 * in an app dialog - only the password: the fixed user name of each endpoint
 * ("backup", "update", "support") is filled in here. A 401 reopens the dialog
 * with a hint; cancel resolves null.
 */
export async function withOtaAuth(
  user: string, title: string,
  run: (headers: Record<string, string>) => Promise<Response>,
  always = false,
): Promise<{ res: Response; headers: Record<string, string> } | null> {
  if (authSessionActive.value && !always) {
    const res = await run({});
    return { res, headers: {} };
  }
  let pwd = cachedPwd;
  let error = '';
  for (;;) {
    if (!pwd) {
      const p = await askOtaPassword(title, error);
      if (p === null) return null;
      pwd = p;
    }
    const headers = basic(user, pwd);
    const res = await run(headers);
    if (res.status !== 401) {
      cachedPwd = pwd;
      return { res, headers };
    }
    cachedPwd = '';
    pwd = '';
    error = t('ota.wrongPwd');
  }
}

/**
 * @brief Render the OTA password dialog while one is requested.
 */
export function OtaPasswordView() {
  const p = prompt.value;
  const [closing, setClosing] = useState(false);
  const [pwd, setPwd] = useState('');
  if (!p) return null;

  function close(value: string | null) {
    if (closing) return;
    const current = prompt.value;
    setClosing(true);
    window.setTimeout(() => {
      setClosing(false);
      setPwd('');
      if (current) { prompt.value = null; current.resolve(value); }
    }, MODAL_ANIM_MS);
  }

  return (
    <Modal onClose={() => close(null)} closing={closing}>
      <form class="confirm-modal" aria-labelledby="ota-title"
        onSubmit={(e) => { e.preventDefault(); if (pwd) close(pwd); }}>
        <h2 id="ota-title">{p.title}</h2>
        <p>{t('ota.hint')}</p>
        <input type="password" autocomplete="current-password" autofocus aria-label={t('ota.password')}
          placeholder={t('ota.password')} value={pwd}
          onInput={(e) => setPwd((e.target as HTMLInputElement).value)} />
        {p.error && <p class="ota-error" role="alert">{p.error}</p>}
        <div class="modal-actions">
          <button type="button" class="btn secondary" onClick={() => close(null)}>{t('common.cancel')}</button>
          <button type="submit" class="btn" disabled={!pwd}>{t('ota.continue')}</button>
        </div>
      </form>
    </Modal>
  );
}

/** @brief Hand a downloaded blob to the browser as a file. */
export function saveBlob(blob: Blob, name: string): void {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
