import type { ComponentChildren } from 'preact';
import { Card } from './ui';
import { Icon, type IconName } from './icons';
import { t } from '../i18n';

export type LinkTone = 'on' | 'pending' | 'off' | 'err';

/**
 * @brief State-first hero card: eyebrow, big live state, and stat tiles.
 *
 * Pages lead with this so the answer to "what is it doing right now?" comes
 * before any form. children render between the state heading and the stats
 * (LinkHero puts its animated wire there).
 */
export function StatusHero(props: {
  eyebrow: string;
  state: string;
  tone?: LinkTone;
  stats: { label: string; value: string }[];
  children?: ComponentChildren;
}) {
  return (
    <Card className={`link-hero${props.tone ? ' ' + props.tone : ''}`}>
      <p class="eyebrow">{props.eyebrow}</p>
      <h2>{props.state}</h2>
      {props.children}
      <div class="link-hero-grid">
        {props.stats.map((s) => (
          <div class="link-stat" key={s.label}>
            <span>{s.label}</span>
            <b>{s.value}</b>
          </div>
        ))}
      </div>
    </Card>
  );
}

/**
 * @brief Hero card for connectivity pages: spa → wire → remote endpoint.
 *
 * The wire animates while the link is up (or being established), so the page
 * answers "is my tub talking to X?" at a glance. Facts about the endpoint go
 * into the stat tiles below, matching the hero rhythm of the other pages.
 */
export function LinkHero(props: {
  eyebrow: string;
  state: string;
  tone: LinkTone;
  remoteIcon: IconName;
  remoteLabel: string;
  stats: { label: string; value: string }[];
}) {
  return (
    <StatusHero eyebrow={props.eyebrow} state={props.state} tone={props.tone} stats={props.stats}>
      {/* Decorative: the link state is already announced by the heading. */}
      <div class="link-path" aria-hidden="true">
        <span class="link-node">
          <span class="link-node-ic"><Icon name="hydrojet" size={26} /></span>
          {t('link.spa')}
        </span>
        <span class="link-wire" />
        <span class="link-node remote">
          <span class="link-node-ic"><Icon name={props.remoteIcon} size={26} /></span>
          {props.remoteLabel}
        </span>
      </div>
    </StatusHero>
  );
}
