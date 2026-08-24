import type { ComponentChildren } from 'preact';
import { useState } from 'preact/hooks';
import { needsLogin, login } from '../api';
import { t } from '../i18n';
import { Modal } from './ui';

/**
 * @brief Login form shown when the firmware requires global auth.
 */
function Login() {
  const [user, setUser] = useState('');
  const [pwd, setPwd] = useState('');
  const [keep, setKeep] = useState(false);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState(false);
  const [retryAfter, setRetryAfter] = useState(0);

  async function submit(e: Event) {
    e.preventDefault();
    setBusy(true);
    setErr(false);
    setRetryAfter(0);
    const result = await login(user, pwd, keep);
    if (result.ok) {
      // Reload so every guarded fetch starts with the new cookie.
      location.reload();
    } else {
      setErr(true);
      setRetryAfter(result.retryAfter || 0);
      setBusy(false);
    }
  }

  return (
    <Modal>
      <form onSubmit={submit}>
        <div class="login-brand">
          <img src="/logo.png" alt="" />
          <h2>WifiWhirl</h2>
          <p>{t('login.title')}</p>
        </div>
        <p class="hint">{t('login.user')}</p>
        <input type="text" autocomplete="username" autofocus value={user}
          onInput={(e) => setUser((e.target as HTMLInputElement).value)} />
        <p class="hint">{t('login.password')}</p>
        <input type="password" autocomplete="current-password" value={pwd}
          onInput={(e) => setPwd((e.target as HTMLInputElement).value)} />
        <label class="login-keep">
          <input type="checkbox" checked={keep}
            onChange={(e) => setKeep((e.target as HTMLInputElement).checked)} />
          <span>{t('login.keep')}</span>
        </label>
        {err && <p style="color:var(--danger);margin-top:10px">{t('login.error')}</p>}
        {retryAfter > 0 && <p class="login-throttle">{t('login.throttled', { seconds: retryAfter })}</p>}
        <button class="btn block" style="margin-top:16px" disabled={busy} type="submit">
          {t('login.submit')}
        </button>
      </form>
    </Modal>
  );
}

/**
 * @brief Replaces protected UI with the login form when auth is required.
 */
export function AuthGate({ children }: { children: ComponentChildren }) {
  if (needsLogin.value) return <Login />;
  return <>{children}</>;
}
