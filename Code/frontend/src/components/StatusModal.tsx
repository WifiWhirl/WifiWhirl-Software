import type { ComponentChildren } from 'preact';
import { useState } from 'preact/hooks';
import { t } from '../i18n';
import { Modal } from './ui';

const MODAL_ANIM_MS = 200;

/**
 * @brief Modal used by compact status indicators.
 */
export function StatusModal({ title, children, onClose }: { title: string; children: ComponentChildren; onClose: () => void }) {
  const [closing, setClosing] = useState(false);

  function close() {
    if (closing) return;
    setClosing(true);
    window.setTimeout(onClose, MODAL_ANIM_MS);
  }

  return (
    <Modal onClose={close} closing={closing}>
      <div class="status-modal">
        <h2>{title}</h2>
        <div class="status-list">{children}</div>
        <div class="modal-actions">
          <button type="button" class="btn secondary" onClick={close}>{t('common.close')}</button>
        </div>
      </div>
    </Modal>
  );
}

/**
 * @brief Label/value row inside a status modal.
 */
export function DetailRow({ label, value }: { label: string; value: string | number }) {
  return (
    <div class="status-row">
      <span>{label.replace(/:$/, '')}</span>
      <b>{value}</b>
    </div>
  );
}
