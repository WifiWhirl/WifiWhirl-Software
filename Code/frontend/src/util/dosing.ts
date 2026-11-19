/**
 * Care-product suggestion: how much to add to bring a value outside
 * its ideal range back to the middle of it. Factors are grams per 1000 l
 * (module settings DFPHM..DFTAM, Bayrol SpaTime granules by default):
 * pH-/pH+ per 0.1 pH, chlorine per 1 mg/l, TA+/TA- per 10 mg/l.
 * Same maths in the cloud app (frontend/src/dosing.ts there).
 */
export interface DoseCfg { POOLCAP?: number; DFPHM?: number; DFPHP?: number; DFCL?: number; DFTAP?: number; DFTAM?: number }
export type SuggestKind = 'phMinus' | 'phPlus' | 'clG' | 'taMinus' | 'taPlus' | 'water';
/** grams, or litres to drain for 'water' (cyanuric acid only goes with fresh water) */
export interface Suggestion { kind: SuggestKind; amount: number }

const grams = (factor: number | undefined, fallback: number, steps: number, c: DoseCfg) => {
  const g = Math.round((factor || fallback) * Math.round(steps * 10) / 10 * (c.POOLCAP || 0) / 1000);
  return g >= 1 ? g : 0;
};
const mk = (kind: SuggestKind, amount: number): Suggestion | null => (amount > 0 ? { kind, amount } : null);

export function suggestPh(v: number, c: DoseCfg): Suggestion | null {
  if (!(v > 0)) return null;
  if (v > 7.6) return mk('phMinus', grams(c.DFPHM, 10, (v - 7.4) / 0.1, c));
  if (v < 7.2) return mk('phPlus', grams(c.DFPHP, 10, (7.4 - v) / 0.1, c));
  return null;
}

/** Too much chlorine needs no product, it just wears off. */
export function suggestCl(v: number, c: DoseCfg): Suggestion | null {
  return v >= 0 && v < 1 ? mk('clG', grams(c.DFCL, 2, 2 - v, c)) : null;
}

export function suggestTa(v: number, c: DoseCfg): Suggestion | null {
  if (!(v > 0)) return null;
  if (v < 80) return mk('taPlus', grams(c.DFTAP, 20, (100 - v) / 10, c));
  if (v > 120) return mk('taMinus', grams(c.DFTAM, 24, (v - 100) / 10, c));
  return null;
}

/** Litres to swap for fresh water to get back to 40 mg/l. */
export function suggestCya(v: number, c: DoseCfg): Suggestion | null {
  return v > 50 && c.POOLCAP ? mk('water', Math.round((c.POOLCAP * (v - 40)) / v / 10) * 10) : null;
}
