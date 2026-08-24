import { useState } from 'preact/hooks';
import { Card } from '../components/ui';
import { t } from '../i18n';

function formatBytes(bytes: number): string {
  if (!bytes) return '0 Bytes';
  const k = 1024;
  const sizes = ['Bytes', 'KiB', 'MiB', 'GiB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return `${parseFloat((bytes / Math.pow(k, i)).toFixed(2))} ${sizes[i]}`;
}

/**
 * @brief Firmware update upload route.
 */
export function Update() {
  const [file, setFile] = useState<File | null>(null);
  const [status, setStatus] = useState('');
  const [pct, setPct] = useState<number | null>(null);
  const [done, setDone] = useState(false);

  // The firmware's updater rejects a POST only after the whole .bin has been
  // received, so waiting for its 401 meant the browser asked for the OTA
  // credentials once the upload had already run - and then sent the file again.
  // A GET on the same path with ?basic=1 triggers that prompt up front; the
  // browser reuses the credentials for the POST that follows.
  function authenticate(): Promise<void> {
    return new Promise((resolve, reject) => {
      const pre = new XMLHttpRequest();
      pre.open('GET', '/update?basic=1', true);
      pre.onload = () => (pre.status === 200 ? resolve() : reject(new Error(String(pre.status))));
      pre.onerror = () => reject(new Error('network'));
      pre.send();
    });
  }

  async function upload() {
    if (!file) return;
    if (!file.name.endsWith('.fw.bin')) {
      setStatus(t('upd.wrongFile'));
      return;
    }
    setStatus(t('upd.authPending'));
    try {
      await authenticate();
    } catch {
      setStatus(t('upd.authFailed'));
      return;
    }
    const fd = new FormData();
    fd.append('firmware', file, file.name); // httpUpdater file field
    const xhr = new XMLHttpRequest();
    xhr.upload.onprogress = (e) => {
      const p = Math.round((e.loaded / e.total) * 100);
      setPct(p);
      setStatus(`${p}% - ${formatBytes(e.loaded)} / ${formatBytes(e.total)}`);
    };
    xhr.upload.onload = () => {
      setDone(true);
      setPct(null);
      setStatus(t('upd.success'));
    };
    xhr.upload.onerror = () => setStatus(t('upd.failed'));
    xhr.upload.onabort = () => setStatus(t('upd.aborted'));
    xhr.open('POST', '/update', true);
    xhr.send(fd);
  }

  return (
    <>
      <h1>{t('update.title')}</h1>
      <Card>
        <p class="note">{t('update.authNote')}</p>
        <p class="hint">{t('update.warning')}</p>
        {!done && (
          <>
            <input type="file" accept=".fw.bin"
              onChange={(e) => { setFile((e.target as HTMLInputElement).files?.[0] || null); setStatus(''); }} />
            {pct !== null && <progress max={100} value={pct} style="width:100%;margin-top:12px" />}
            <button class="btn block" style="margin-top:14px" disabled={!file} onClick={() => void upload()}>
              {t('update.upload')}
            </button>
          </>
        )}
        {status && <p style="margin-top:14px">{status}</p>}
      </Card>
    </>
  );
}
