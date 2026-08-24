import { signal } from '@preact/signals';
import { useState } from 'preact/hooks';
import { t } from '../i18n';
import { dec } from '../util/format';
import { Modal } from './ui';

const MODAL_ANIM_MS = 200;

interface Toast { msg: string; err: boolean; id: number; }

/** @brief Currently visible toast notification, if any. */
export const toast = signal<Toast | null>(null);
let tid = 0;

/**
 * @brief Show a transient toast message.
 * @param msg Message text.
 * @param err Whether to render the toast as an error.
 * @param ms Lifetime in milliseconds.
 */
export function showToast(msg: string, err = false, ms = 3500): void {
  const id = ++tid;
  toast.value = { msg, err, id };
  setTimeout(() => { if (toast.value?.id === id) toast.value = null; }, ms);
}

/**
 * @brief Active reboot notice text, or null when no reboot is pending.
 */
export const rebooting = signal<string | null>(null);

/**
 * @brief Show the reboot overlay and reload after the firmware has time to boot.
 * @param reason Optional reboot reason code from the firmware ('net' | 'mqtt' | 'hw').
 *               Unknown/legacy values are shown verbatim for forward compatibility.
 * @param returnTo Route to land on after the reload (default: dashboard).
 */
export function showRestart(reason?: string, returnTo = '/'): void {
  // t() returns the key itself when missing, so an unknown/legacy reason falls through.
  const msg = reason ? t(`restart.${reason}`) : '';
  // Stay empty without a reason: RebootView always renders the generic wait
  // notice, so putting it here too printed it twice (manual restart).
  rebooting.value = (msg && msg !== `restart.${reason}`) ? msg : (reason || '');
  setTimeout(() => { location.hash = returnTo; location.reload(); }, 30000);
}

interface Dialog {
  title: string;
  message: string;
  confirmLabel: string;
  cancelLabel: string;
  danger: boolean;
  noCancel?: boolean; // info dialog: single OK button
  resolve: (ok: boolean) => void;
}

/** @brief Currently open confirmation dialog, if any. */
export const dialog = signal<Dialog | null>(null);

/**
 * @brief Open a confirmation dialog and resolve with the user's choice.
 * @param message Dialog body text.
 * @param opts Optional labels/title/danger styling.
 * @returns Promise resolving true when confirmed.
 */
export function confirmDialog(message: string, opts: Partial<Pick<Dialog, 'title' | 'confirmLabel' | 'cancelLabel' | 'danger' | 'noCancel'>> = {}): Promise<boolean> {
  if (dialog.value) dialog.value.resolve(false);
  return new Promise((resolve) => {
    dialog.value = {
      title: opts.title || t('common.confirm'),
      message,
      confirmLabel: opts.confirmLabel || t('common.confirm'),
      cancelLabel: opts.cancelLabel || t('common.cancel'),
      danger: opts.danger ?? false,
      noCancel: opts.noCancel ?? false,
      resolve,
    };
  });
}

/**
 * @brief Result dialog after a heat-loss calibration run ended.
 *
 * Shared by the Expert page (live polling) and the Dashboard (HLCAL flag
 * transition), so the user sees the outcome wherever they are.
 */
export function showCalResult(d: { CURRENT?: number; RESULT?: number; ERROR?: string }): void {
  if (d.RESULT) {
    void confirmDialog(t('expert.calDone', { v: dec(d.CURRENT ?? d.RESULT, 2) }), {
      title: t('expert.calDoneTitle'), confirmLabel: t('common.ok'), noCancel: true,
    });
  } else if (d.ERROR) {
    const key = `expert.calAbort.${d.ERROR}`;
    const msg = t(key);
    void confirmDialog(msg === key ? t('common.error') : msg, {
      title: t('expert.calAbortTitle'), confirmLabel: t('common.ok'), noCancel: true, danger: true,
    });
  }
}

/**
 * @brief Render the active toast notification.
 */
export function ToastView() {
  if (!toast.value) return null;
  return <div class={`toast${toast.value.err ? ' err' : ''}`}>{toast.value.msg}</div>;
}

/**
 * @brief Render the active confirmation dialog.
 */
export function DialogView() {
  const d = dialog.value;
  const [closing, setClosing] = useState(false);
  if (!d) return null;

  // Hold the dialog open through the exit animation, then resolve + unmount.
  function close(ok: boolean) {
    if (closing) return;
    const current = dialog.value;
    setClosing(true);
    window.setTimeout(() => {
      setClosing(false);
      if (current) { dialog.value = null; current.resolve(ok); }
    }, MODAL_ANIM_MS);
  }

  return (
    <Modal onClose={() => close(false)} closing={closing}>
      <div class="confirm-modal" aria-labelledby="confirm-title">
        <h2 id="confirm-title">{d.title}</h2>
        <p>{d.message}</p>
        <div class="modal-actions">
          {!d.noCancel && <button type="button" class="btn secondary" onClick={() => close(false)}>{d.cancelLabel}</button>}
          <button type="button" class={`btn ${d.danger ? 'danger' : ''}`} onClick={() => close(true)}>{d.confirmLabel}</button>
        </div>
      </div>
    </Modal>
  );
}

/**
 * @brief Render the reboot-in-progress overlay.
 */
export function RebootView() {
  if (rebooting.value === null) return null;
  return (
    <div class="modal-scrim">
      <div class="modal" style="text-align:center">
        <h2>{t('common.rebooting')}</h2>
        {rebooting.value && <p class="muted">{rebooting.value}</p>}
        <p class="muted">{t('common.rebootInfo')}</p>
      </div>
    </div>
  );
}
