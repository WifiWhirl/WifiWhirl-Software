import { useEffect, useState } from 'preact/hooks';
import { getText } from '../api';
import { Card, SaveButton } from '../components/ui';
import { Icon, type IconName } from '../components/icons';
import { PageSkeleton } from '../components/Skeleton';
import { t } from '../i18n';

/**
 * @brief Hardware model configuration route.
 *
 * gethardware returns the saved model selection. Pins are fixed in firmware.
 */
export function Hardware() {
  const [loaded, setLoaded] = useState(false);
  const [cio, setCio] = useState(1);

  useEffect(() => {
    getText('/gethardware/').then((txt) => {
      try { setCio(parseInt(String(JSON.parse(txt).cio), 10) || 0); } catch { /* keep the default */ }
      setLoaded(true);
    }).catch(() => {});
  }, []);

  if (!loaded) return <PageSkeleton cards={1} rows={2} actions={false} />;

  const models: { v: number; label: string; icon: IconName }[] = [
    { v: 1, label: t('hardware.airjet'), icon: 'airjet' },
    { v: 2, label: t('hardware.hydrojet'), icon: 'hydrojet' },
    { v: 0, label: t('hardware.egg'), icon: 'pump' },
  ];
  // MSPA is ESP32-only; build_frontend.py sets this for esp32 builds.
  if (import.meta.env.VITE_ENABLE_MSPA === '1') models.push({ v: 3, label: t('hardware.mspa'), icon: 'device' });

  return (
    <>
      <h1>{t('nav.hardware')}</h1>
      <Card>
        <p>{t('hardware.selectModel')}</p>
        <div class="model-grid" role="radiogroup" aria-label={t('hardware.selectModel')}>
          {models.map((m) => (
            <button
              key={m.v}
              type="button"
              role="radio"
              aria-checked={cio === m.v}
              class={`model-pick${cio === m.v ? ' on' : ''}`}
              onClick={() => setCio(m.v)}
            >
              <span class="tile-ic"><Icon name={m.icon} size={26} /></span>
              <b>{m.label}</b>
            </button>
          ))}
        </div>
        <p class="hint">{t('setup.pumpModelInfo')}</p>
        <div class="card-save">
          {/* cio only: the firmware derives dsp from it. Echoing back the loaded
              hwcfg kept the old dsp, leaving the previous display decoder live
              (physical panel keys mapped to the wrong functions). */}
          <SaveButton url="/sethardware/" data={() => ({ cio })} label={t('hardware.save')} />
        </div>
      </Card>
    </>
  );
}
