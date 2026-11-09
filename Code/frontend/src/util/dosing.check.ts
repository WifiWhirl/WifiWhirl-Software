// Self-check of the dosing maths: npx tsx src/util/dosing.check.ts
import { suggestCl, suggestCya, suggestPh, suggestTa } from './dosing';

const c = { POOLCAP: 1000 };
const eq = (a: unknown, b: unknown) => { if (JSON.stringify(a) !== JSON.stringify(b)) throw new Error(`${JSON.stringify(a)} != ${JSON.stringify(b)}`); };
eq(suggestPh(7.8, c), { kind: 'phMinus', amount: 40 });     // ticket example: 40 g per 0.4 pH
eq(suggestPh(7.4, c), null);
eq(suggestPh(6.9, { POOLCAP: 700 }), { kind: 'phPlus', amount: 35 });
eq(suggestCl(0.5, c), { kind: 'clG', amount: 3 });
eq(suggestCl(2, c), null);
eq(suggestTa(60, c), { kind: 'taPlus', amount: 80 });
eq(suggestTa(150, { ...c, DFTAM: 30 }), { kind: 'taMinus', amount: 150 });
eq(suggestCya(80, c), { kind: 'water', amount: 500 });
console.log('dosing ok');
