/** Statistical functions. */

import { CellError, ERR, FormulaError, fail, isError } from '../errors';
import { ArrayValue, FnContext, Scalar, Val, flattenNumbers } from '../values';
import { accessor, arg, boolOr, makeCriteria, num, numOr } from './helpers';
import { sumIfsCore } from './math';
import { define } from './registry';

function numbersOf(args: Val[], ctx: FnContext): number[] {
  return flattenNumbers(args, ctx);
}

function sortedNumbers(args: Val[], ctx: FnContext): number[] {
  return numbersOf(args, ctx).sort((a, b) => a - b);
}

define('AVERAGE', 1, 255, (a, c) => {
  const nums = numbersOf(a, c);
  if (!nums.length) fail(ERR.DIV0);
  return nums.reduce((x, y) => x + y, 0) / nums.length;
}, { category: 'Statistical' });

define('AVERAGEA', 1, 255, (a, c) => {
  const nums: number[] = [];
  for (const v of a) {
    for (const raw of flattenForA([v], c)) {
      if (isError(raw)) throw new FormulaError(raw);
      if (raw === null || raw === '') continue;
      if (typeof raw === 'number') nums.push(raw);
      else if (typeof raw === 'boolean') nums.push(raw ? 1 : 0);
      else nums.push(0);
    }
  }
  if (!nums.length) fail(ERR.DIV0);
  return nums.reduce((x, y) => x + y, 0) / nums.length;
}, { category: 'Statistical' });

function flattenForA(args: Val[], ctx: FnContext): (Scalar | CellError)[] {
  const out: (Scalar | CellError)[] = [];
  for (const v of args) {
    if (v && typeof v === 'object' && 'rows' in (v as object)) {
      for (const row of (v as ArrayValue).rows) out.push(...row);
    } else if (v && typeof v === 'object' && 'sheetId' in (v as object)) {
      const r = v as import('../values').RangeValue;
      for (let i = r.r1; i <= r.r2; i++) for (let j = r.c1; j <= r.c2; j++) out.push(ctx.source.getScalar(r.sheetId, i, j));
    } else {
      out.push(v as Scalar);
    }
  }
  return out;
}

define('MAX', 0, 255, (a, c) => {
  const nums = numbersOf(a, c);
  return nums.length ? Math.max(...nums) : 0;
}, { category: 'Statistical' });

define('MIN', 0, 255, (a, c) => {
  const nums = numbersOf(a, c);
  return nums.length ? Math.min(...nums) : 0;
}, { category: 'Statistical' });

const maxA = (pickMax: boolean) => (a: Val[], c: FnContext) => {
  let best: number | null = null;
  for (const v of a) {
    for (const raw of flattenForA([v], c)) {
      if (isError(raw)) throw new FormulaError(raw);
      if (raw === null || raw === '') continue;
      const n = typeof raw === 'number' ? raw : typeof raw === 'boolean' ? (raw ? 1 : 0) : 0;
      if (best === null) best = n;
      else best = pickMax ? Math.max(best, n) : Math.min(best, n);
    }
  }
  return best ?? 0;
};
define('MAXA', 1, 255, maxA(true), { category: 'Statistical' });
define('MINA', 1, 255, maxA(false), { category: 'Statistical' });

define('MEDIAN', 1, 255, (a, c) => {
  const nums = sortedNumbers(a, c);
  if (!nums.length) fail(ERR.NUM);
  const mid = Math.floor(nums.length / 2);
  return nums.length % 2 ? nums[mid] : (nums[mid - 1] + nums[mid]) / 2;
}, { category: 'Statistical' });

define('COUNT', 0, 255, (a, c) => {
  let count = 0;
  for (const v of a) {
    if (v && typeof v === 'object' && ('rows' in (v as object) || 'sheetId' in (v as object))) {
      count += flattenForA([v], c).filter((x) => typeof x === 'number').length;
    } else if (typeof v === 'number') count++;
    else if (typeof v === 'string' && v.trim() !== '' && Number.isFinite(Number(v))) count++;
  }
  return count;
}, { category: 'Statistical' });

define('COUNTA', 0, 255, (a, c) => {
  let count = 0;
  for (const v of a) {
    if (v && typeof v === 'object' && ('rows' in (v as object) || 'sheetId' in (v as object))) {
      count += flattenForA([v], c).filter((x) => x !== null && x !== '').length;
    } else if (v !== null && v !== '') count++;
  }
  return count;
}, { category: 'Statistical' });

define('COUNTBLANK', 1, 1, (a, c) => {
  const vals = flattenForA([a[0]], c);
  return vals.filter((v) => v === null || v === '').length;
}, { category: 'Statistical' });

define('COUNTIF', 2, 2, (a, c) => {
  const test = makeCriteria(arg(a, 1), c);
  return sumIfsCore([arg(a, 0)], [test], arg(a, 0), c, 'count');
}, { category: 'Statistical' });

define('COUNTIFS', 2, 255, (a, c) => {
  const critArgs: Val[] = [];
  const tests: ((v: Scalar | CellError) => boolean)[] = [];
  for (let i = 0; i < a.length; i += 2) {
    critArgs.push(arg(a, i));
    tests.push(makeCriteria(arg(a, i + 1), c));
  }
  return sumIfsCore(critArgs, tests, critArgs[0], c, 'count');
}, { category: 'Statistical' });

define('AVERAGEIF', 2, 3, (a, c) => {
  const test = makeCriteria(arg(a, 1), c);
  const valueArg = a.length > 2 ? arg(a, 2) : arg(a, 0);
  return sumIfsCore([arg(a, 0)], [test], valueArg, c, 'average');
}, { category: 'Statistical' });

define('AVERAGEIFS', 3, 255, (a, c) => {
  const critArgs: Val[] = [];
  const tests: ((v: Scalar | CellError) => boolean)[] = [];
  for (let i = 1; i < a.length; i += 2) {
    critArgs.push(arg(a, i));
    tests.push(makeCriteria(arg(a, i + 1), c));
  }
  return sumIfsCore(critArgs, tests, arg(a, 0), c, 'average');
}, { category: 'Statistical' });

define('MAXIFS', 3, 255, (a, c) => {
  const critArgs: Val[] = [];
  const tests: ((v: Scalar | CellError) => boolean)[] = [];
  for (let i = 1; i < a.length; i += 2) {
    critArgs.push(arg(a, i));
    tests.push(makeCriteria(arg(a, i + 1), c));
  }
  return sumIfsCore(critArgs, tests, arg(a, 0), c, 'max');
}, { category: 'Statistical' });

define('MINIFS', 3, 255, (a, c) => {
  const critArgs: Val[] = [];
  const tests: ((v: Scalar | CellError) => boolean)[] = [];
  for (let i = 1; i < a.length; i += 2) {
    critArgs.push(arg(a, i));
    tests.push(makeCriteria(arg(a, i + 1), c));
  }
  return sumIfsCore(critArgs, tests, arg(a, 0), c, 'min');
}, { category: 'Statistical' });

define('LARGE', 2, 2, (a, c) => {
  const nums = sortedNumbers([arg(a, 0)], c).sort((x, y) => y - x);
  const k = Math.trunc(num(a, 1, c));
  if (k < 1 || k > nums.length) fail(ERR.NUM);
  return nums[k - 1];
}, { category: 'Statistical' });

define('SMALL', 2, 2, (a, c) => {
  const nums = sortedNumbers([arg(a, 0)], c);
  const k = Math.trunc(num(a, 1, c));
  if (k < 1 || k > nums.length) fail(ERR.NUM);
  return nums[k - 1];
}, { category: 'Statistical' });

define('RANK.EQ', 2, 3, (a, c) => {
  const x = num(a, 0, c);
  const nums = numbersOf([arg(a, 1)], c);
  const order = numOr(a, 2, c, 0);
  const sorted = [...nums].sort((p, q) => (order === 0 ? q - p : p - q));
  const idx = sorted.findIndex((n) => n === x);
  if (idx < 0) fail(ERR.NA);
  return idx + 1;
}, { category: 'Statistical' });

define('PERCENTILE.INC', 2, 2, (a, c) => {
  const nums = sortedNumbers([arg(a, 0)], c);
  const k = num(a, 1, c);
  if (!nums.length || k < 0 || k > 1) fail(ERR.NUM);
  const pos = (nums.length - 1) * k;
  const lo = Math.floor(pos);
  const hi = Math.ceil(pos);
  return nums[lo] + (nums[hi] - nums[lo]) * (pos - lo);
}, { category: 'Statistical' });

define('QUARTILE.INC', 2, 2, (a, c) => {
  const nums = sortedNumbers([arg(a, 0)], c);
  const q = Math.trunc(num(a, 1, c));
  if (q < 0 || q > 4) fail(ERR.NUM);
  const k = q / 4;
  if (!nums.length) fail(ERR.NUM);
  const pos = (nums.length - 1) * k;
  const lo = Math.floor(pos);
  const hi = Math.ceil(pos);
  return nums[lo] + (nums[hi] - nums[lo]) * (pos - lo);
}, { category: 'Statistical' });

function varianceOf(nums: number[], sample: boolean): number {
  if (nums.length < (sample ? 2 : 1)) fail(ERR.DIV0);
  const mean = nums.reduce((x, y) => x + y, 0) / nums.length;
  const sq = nums.reduce((x, y) => x + (y - mean) ** 2, 0);
  return sq / (sample ? nums.length - 1 : nums.length);
}

define('VAR.S', 1, 255, (a, c) => varianceOf(numbersOf(a, c), true), { category: 'Statistical' });
define('VAR.P', 1, 255, (a, c) => varianceOf(numbersOf(a, c), false), { category: 'Statistical' });
define('STDEV.S', 1, 255, (a, c) => Math.sqrt(varianceOf(numbersOf(a, c), true)), { category: 'Statistical' });
define('STDEV.P', 1, 255, (a, c) => Math.sqrt(varianceOf(numbersOf(a, c), false)), { category: 'Statistical' });

define('VARA', 1, 255, (a, c) => varianceOf(flattenForA(a, c).map((v) => (typeof v === 'number' ? v : typeof v === 'boolean' ? (v ? 1 : 0) : 0)), true), { category: 'Statistical' });
define('VARPA', 1, 255, (a, c) => varianceOf(flattenForA(a, c).map((v) => (typeof v === 'number' ? v : typeof v === 'boolean' ? (v ? 1 : 0) : 0)), false), { category: 'Statistical' });

define('GEOMEAN', 1, 255, (a, c) => {
  const nums = numbersOf(a, c);
  if (!nums.length || nums.some((n) => n <= 0)) fail(ERR.NUM);
  return Math.exp(nums.reduce((x, y) => x + Math.log(y), 0) / nums.length);
}, { category: 'Statistical' });

define('HARMEAN', 1, 255, (a, c) => {
  const nums = numbersOf(a, c);
  if (!nums.length || nums.some((n) => n <= 0)) fail(ERR.NUM);
  return nums.length / nums.reduce((x, y) => x + 1 / y, 0);
}, { category: 'Statistical' });

define('AVEDEV', 1, 255, (a, c) => {
  const nums = numbersOf(a, c);
  if (!nums.length) fail(ERR.NUM);
  const mean = nums.reduce((x, y) => x + y, 0) / nums.length;
  return nums.reduce((x, y) => x + Math.abs(y - mean), 0) / nums.length;
}, { category: 'Statistical' });

define('DEVSQ', 1, 255, (a, c) => {
  const nums = numbersOf(a, c);
  if (!nums.length) fail(ERR.NUM);
  const mean = nums.reduce((x, y) => x + y, 0) / nums.length;
  return nums.reduce((x, y) => x + (y - mean) ** 2, 0);
}, { category: 'Statistical' });

define('TRIMMEAN', 2, 2, (a, c) => {
  const nums = sortedNumbers([arg(a, 0)], c);
  const pct = num(a, 1, c);
  if (pct < 0 || pct >= 1) fail(ERR.NUM);
  const cut = Math.floor((nums.length * pct) / 2);
  const kept = nums.slice(cut, nums.length - cut);
  if (!kept.length) fail(ERR.NUM);
  return kept.reduce((x, y) => x + y, 0) / kept.length;
}, { category: 'Statistical' });

define('MODE.SNGL', 1, 255, (a, c) => {
  const nums = numbersOf(a, c);
  const counts = new Map<number, number>();
  for (const n of nums) counts.set(n, (counts.get(n) ?? 0) + 1);
  let best: number | null = null;
  let bestCount = 1;
  for (const [n, count] of counts) {
    if (count > bestCount) {
      best = n;
      bestCount = count;
    }
  }
  if (best === null) fail(ERR.NA);
  return best;
}, { category: 'Statistical' });

define('MODE.MULT', 1, 255, (a, c) => {
  const nums = numbersOf(a, c);
  const counts = new Map<number, number>();
  for (const n of nums) counts.set(n, (counts.get(n) ?? 0) + 1);
  const max = Math.max(...counts.values());
  if (max <= 1) fail(ERR.NA);
  const modes = [...counts.entries()].filter(([, v]) => v === max).map(([k]) => k).sort((x, y) => x - y);
  return new ArrayValue(modes.map((m) => [m]));
}, { category: 'Statistical' });

define('STANDARDIZE', 3, 3, (a, c) => {
  const x = num(a, 0, c);
  const mean = num(a, 1, c);
  const sd = num(a, 2, c);
  if (sd <= 0) fail(ERR.NUM);
  return (x - mean) / sd;
}, { category: 'Statistical' });

function pairedNumbers(yArg: Val, xArg: Val, ctx: FnContext): { xs: number[]; ys: number[] } {
  const ya = accessor(yArg, ctx);
  const xa = accessor(xArg, ctx);
  const height = Math.min(ya.height, xa.height);
  const width = Math.min(ya.width, xa.width);
  const xs: number[] = [];
  const ys: number[] = [];
  for (let i = 0; i < height; i++) {
    for (let j = 0; j < width; j++) {
      const y = ya.at(i, j);
      const x = xa.at(i, j);
      if (typeof y === 'number' && typeof x === 'number') {
        ys.push(y);
        xs.push(x);
      }
    }
  }
  return { xs, ys };
}

define('CORREL', 2, 2, (a, c) => {
  const { xs, ys } = pairedNumbers(arg(a, 0), arg(a, 1), c);
  const n = xs.length;
  if (n < 2) fail(ERR.DIV0);
  const mx = xs.reduce((p, q) => p + q, 0) / n;
  const my = ys.reduce((p, q) => p + q, 0) / n;
  let sxy = 0;
  let sxx = 0;
  let syy = 0;
  for (let i = 0; i < n; i++) {
    sxy += (xs[i] - mx) * (ys[i] - my);
    sxx += (xs[i] - mx) ** 2;
    syy += (ys[i] - my) ** 2;
  }
  if (sxx === 0 || syy === 0) fail(ERR.DIV0);
  return sxy / Math.sqrt(sxx * syy);
}, { category: 'Statistical' });

define('COVARIANCE.P', 2, 2, (a, c) => {
  const { xs, ys } = pairedNumbers(arg(a, 0), arg(a, 1), c);
  const n = xs.length;
  if (!n) fail(ERR.DIV0);
  const mx = xs.reduce((p, q) => p + q, 0) / n;
  const my = ys.reduce((p, q) => p + q, 0) / n;
  let s = 0;
  for (let i = 0; i < n; i++) s += (xs[i] - mx) * (ys[i] - my);
  return s / n;
}, { category: 'Statistical' });

define('SLOPE', 2, 2, (a, c) => {
  const { xs, ys } = pairedNumbers(arg(a, 0), arg(a, 1), c);
  const n = xs.length;
  if (n < 2) fail(ERR.DIV0);
  const mx = xs.reduce((p, q) => p + q, 0) / n;
  const my = ys.reduce((p, q) => p + q, 0) / n;
  let numr = 0;
  let den = 0;
  for (let i = 0; i < n; i++) {
    numr += (xs[i] - mx) * (ys[i] - my);
    den += (xs[i] - mx) ** 2;
  }
  if (den === 0) fail(ERR.DIV0);
  return numr / den;
}, { category: 'Statistical' });

define('INTERCEPT', 2, 2, (a, c) => {
  const { xs, ys } = pairedNumbers(arg(a, 0), arg(a, 1), c);
  const n = xs.length;
  if (n < 2) fail(ERR.DIV0);
  const mx = xs.reduce((p, q) => p + q, 0) / n;
  const my = ys.reduce((p, q) => p + q, 0) / n;
  let numr = 0;
  let den = 0;
  for (let i = 0; i < n; i++) {
    numr += (xs[i] - mx) * (ys[i] - my);
    den += (xs[i] - mx) ** 2;
  }
  if (den === 0) fail(ERR.DIV0);
  return my - (numr / den) * mx;
}, { category: 'Statistical' });

define('FORECAST.LINEAR', 3, 3, (a, c) => {
  const x = num(a, 0, c);
  const { xs, ys } = pairedNumbers(arg(a, 1), arg(a, 2), c);
  const n = xs.length;
  if (n < 2) fail(ERR.DIV0);
  const mx = xs.reduce((p, q) => p + q, 0) / n;
  const my = ys.reduce((p, q) => p + q, 0) / n;
  let numr = 0;
  let den = 0;
  for (let i = 0; i < n; i++) {
    numr += (xs[i] - mx) * (ys[i] - my);
    den += (xs[i] - mx) ** 2;
  }
  if (den === 0) fail(ERR.DIV0);
  const slope = numr / den;
  return my + slope * (x - mx);
}, { category: 'Statistical' });

define('FREQUENCY', 2, 2, (a, c) => {
  const data = numbersOf([arg(a, 0)], c);
  const bins = numbersOf([arg(a, 1)], c).sort((x, y) => x - y);
  const counts = new Array(bins.length + 1).fill(0);
  for (const d of data) {
    let placed = false;
    for (let i = 0; i < bins.length; i++) {
      if (d <= bins[i]) {
        counts[i]++;
        placed = true;
        break;
      }
    }
    if (!placed) counts[bins.length]++;
  }
  return new ArrayValue(counts.map((n) => [n]));
}, { category: 'Statistical' });

/** Error function approximation (Abramowitz & Stegun 7.1.26). */
function erf(x: number): number {
  const sign = x < 0 ? -1 : 1;
  const ax = Math.abs(x);
  const t = 1 / (1 + 0.3275911 * ax);
  const y =
    1 -
    ((((1.061405429 * t - 1.453152027) * t + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * Math.exp(-ax * ax);
  return sign * y;
}

function normalCdf(x: number, mean: number, sd: number): number {
  return 0.5 * (1 + erf((x - mean) / (sd * Math.SQRT2)));
}

define('NORM.DIST', 4, 4, (a, c) => {
  const x = num(a, 0, c);
  const mean = num(a, 1, c);
  const sd = num(a, 2, c);
  const cumulative = boolOr(a, 3, c, true);
  if (sd <= 0) fail(ERR.NUM);
  if (cumulative) return normalCdf(x, mean, sd);
  return (1 / (sd * Math.sqrt(2 * Math.PI))) * Math.exp(-((x - mean) ** 2) / (2 * sd * sd));
}, { category: 'Statistical' });

define('NORM.S.DIST', 2, 2, (a, c) => {
  const x = num(a, 0, c);
  const cumulative = boolOr(a, 1, c, true);
  if (cumulative) return normalCdf(x, 0, 1);
  return (1 / Math.sqrt(2 * Math.PI)) * Math.exp(-(x * x) / 2);
}, { category: 'Statistical' });

define('SKEW', 1, 255, (a, c) => {
  const nums = numbersOf(a, c);
  const n = nums.length;
  if (n < 3) fail(ERR.DIV0);
  const mean = nums.reduce((x, y) => x + y, 0) / n;
  const sd = Math.sqrt(varianceOf(nums, true));
  if (sd === 0) fail(ERR.DIV0);
  return (n / ((n - 1) * (n - 2))) * nums.reduce((acc, v) => acc + ((v - mean) / sd) ** 3, 0);
}, { category: 'Statistical' });

define('KURT', 1, 255, (a, c) => {
  const nums = numbersOf(a, c);
  const n = nums.length;
  if (n < 4) fail(ERR.DIV0);
  const mean = nums.reduce((x, y) => x + y, 0) / n;
  const sd = Math.sqrt(varianceOf(nums, true));
  if (sd === 0) fail(ERR.DIV0);
  const fourth = nums.reduce((acc, v) => acc + ((v - mean) / sd) ** 4, 0);
  return (n * (n + 1) * fourth) / ((n - 1) * (n - 2) * (n - 3)) - (3 * (n - 1) ** 2) / ((n - 2) * (n - 3));
}, { category: 'Statistical' });

define('PERCENTRANK.INC', 2, 3, (a, c) => {
  const nums = sortedNumbers([arg(a, 0)], c);
  const x = num(a, 1, c);
  const sig = Math.trunc(numOr(a, 2, c, 3));
  if (!nums.length) fail(ERR.NUM);
  const below = nums.filter((n) => n < x).length;
  const equal = nums.filter((n) => n === x).length;
  if (equal === 0 && (x < nums[0] || x > nums[nums.length - 1])) fail(ERR.NA);
  const rank = equal > 0 ? below / (nums.length - 1) : below / (nums.length - 1);
  return Number(rank.toFixed(Math.max(0, Math.min(20, sig))));
}, { category: 'Statistical' });
