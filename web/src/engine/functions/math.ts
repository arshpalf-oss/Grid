/** Math & trigonometry functions. */

import { ERR, FormulaError, fail, isError } from '../errors';
import { FnContext, Scalar, Val, flattenNumbers, toNumber } from '../values';
import {
  Accessor,
  accessor,
  arg,
  boolOr,
  makeCriteria,
  num,
  numOr,
  roundDownTowardZero,
  roundHalfAway,
  roundUpAway,
} from './helpers';
import { define } from './registry';

const unary = (name: string, fn: (x: number) => number) =>
  define(name, 1, 1, (a, c) => {
    const x = num(a, 0, c);
    const r = fn(x);
    return Number.isFinite(r) ? r : fail(ERR.NUM);
  });

define('SUM', 0, 255, (a, c) => {
  let total = 0;
  for (const n of flattenNumbers(a, c)) total += n;
  return total;
}, { category: 'Math' });

define('PRODUCT', 0, 255, (a, c) => {
  const nums = flattenNumbers(a, c);
  if (!nums.length) return 0;
  let p = 1;
  for (const n of nums) p *= n;
  return p;
}, { category: 'Math' });

define('ABS', 1, 1, (a, c) => Math.abs(num(a, 0, c)), { category: 'Math' });

define('SIGN', 1, 1, (a, c) => Math.sign(num(a, 0, c)), { category: 'Math' });

define('INT', 1, 1, (a, c) => Math.floor(num(a, 0, c)), { category: 'Math' });

define('TRUNC', 1, 2, (a, c) => {
  const x = num(a, 0, c);
  const digits = numOr(a, 1, c, 0);
  return roundDownTowardZero(x, digits);
}, { category: 'Math' });

define('ROUND', 1, 2, (a, c) => roundHalfAway(num(a, 0, c), numOr(a, 1, c, 0)), { category: 'Math' });
define('ROUNDUP', 1, 2, (a, c) => roundUpAway(num(a, 0, c), numOr(a, 1, c, 0)), { category: 'Math' });
define('ROUNDDOWN', 1, 2, (a, c) => roundDownTowardZero(num(a, 0, c), numOr(a, 1, c, 0)), { category: 'Math' });

define('MROUND', 2, 2, (a, c) => {
  const x = num(a, 0, c);
  const m = num(a, 1, c);
  if (m === 0) return 0;
  if (x !== 0 && Math.sign(x) !== Math.sign(m)) fail(ERR.NUM);
  return roundHalfAway(x / m, 0) * m;
}, { category: 'Math' });

define('CEILING', 1, 2, (a, c) => {
  const x = num(a, 0, c);
  const sig = numOr(a, 1, c, 1);
  if (sig === 0) return 0;
  if (x > 0 && sig < 0) fail(ERR.NUM);
  return Math.ceil(x / sig) * sig;
}, { category: 'Math' });

define('CEILING.MATH', 1, 3, (a, c) => {
  const x = num(a, 0, c);
  const sig = Math.abs(numOr(a, 1, c, 1)) || 1;
  const mode = numOr(a, 2, c, 0);
  if (x >= 0) return Math.ceil(x / sig) * sig;
  return (mode === 0 ? -Math.ceil(Math.abs(x) / sig) : -Math.floor(Math.abs(x) / sig)) * sig;
}, { category: 'Math' });

define('FLOOR', 1, 2, (a, c) => {
  const x = num(a, 0, c);
  const sig = numOr(a, 1, c, 1);
  if (sig === 0) return 0;
  if (x > 0 && sig < 0) fail(ERR.NUM);
  return Math.floor(x / sig) * sig;
}, { category: 'Math' });

define('FLOOR.MATH', 1, 3, (a, c) => {
  const x = num(a, 0, c);
  const sig = Math.abs(numOr(a, 1, c, 1)) || 1;
  const mode = numOr(a, 2, c, 0);
  if (x >= 0) return Math.floor(x / sig) * sig;
  return (mode === 0 ? -Math.floor(Math.abs(x) / sig) : -Math.ceil(Math.abs(x) / sig)) * sig;
}, { category: 'Math' });

define('EVEN', 1, 1, (a, c) => {
  const x = num(a, 0, c);
  const r = Math.ceil(Math.abs(x) / 2) * 2;
  return x < 0 ? -r : r;
}, { category: 'Math' });

define('ODD', 1, 1, (a, c) => {
  const x = num(a, 0, c);
  let r = Math.ceil(Math.abs(x));
  if (r % 2 === 0) r += 1;
  return x < 0 ? -r : r;
}, { category: 'Math' });

define('MOD', 2, 2, (a, c) => {
  const n = num(a, 0, c);
  const d = num(a, 1, c);
  if (d === 0) fail(ERR.DIV0);
  return n - d * Math.floor(n / d);
}, { category: 'Math' });

define('QUOTIENT', 2, 2, (a, c) => {
  const n = num(a, 0, c);
  const d = num(a, 1, c);
  if (d === 0) fail(ERR.DIV0);
  return Math.trunc(n / d);
}, { category: 'Math' });

define('POWER', 2, 2, (a, c) => {
  const x = num(a, 0, c);
  const y = num(a, 1, c);
  const r = Math.pow(x, y);
  if (Number.isNaN(r)) fail(ERR.NUM);
  return r;
}, { category: 'Math' });

define('SQRT', 1, 1, (a, c) => {
  const x = num(a, 0, c);
  if (x < 0) fail(ERR.NUM);
  return Math.sqrt(x);
}, { category: 'Math' });

define('SQRTPI', 1, 1, (a, c) => {
  const x = num(a, 0, c);
  if (x < 0) fail(ERR.NUM);
  return Math.sqrt(x * Math.PI);
}, { category: 'Math' });

define('EXP', 1, 1, (a, c) => Math.exp(num(a, 0, c)), { category: 'Math' });

define('LN', 1, 1, (a, c) => {
  const x = num(a, 0, c);
  if (x <= 0) fail(ERR.NUM);
  return Math.log(x);
}, { category: 'Math' });

define('LOG', 1, 2, (a, c) => {
  const x = num(a, 0, c);
  const base = numOr(a, 1, c, 10);
  if (x <= 0 || base <= 0 || base === 1) fail(ERR.NUM);
  return Math.log(x) / Math.log(base);
}, { category: 'Math' });

define('LOG10', 1, 1, (a, c) => {
  const x = num(a, 0, c);
  if (x <= 0) fail(ERR.NUM);
  return Math.log10(x);
}, { category: 'Math' });

define('PI', 0, 0, () => Math.PI, { category: 'Math' });
define('DEGREES', 1, 1, (a, c) => (num(a, 0, c) * 180) / Math.PI, { category: 'Math' });
define('RADIANS', 1, 1, (a, c) => (num(a, 0, c) * Math.PI) / 180, { category: 'Math' });

define('ATAN2', 2, 2, (a, c) => Math.atan2(num(a, 0, c), num(a, 1, c)), { category: 'Math' });

unary('SIN', Math.sin);
unary('COS', Math.cos);
unary('TAN', Math.tan);
unary('ASIN', Math.asin);
unary('ACOS', Math.acos);
unary('ATAN', Math.atan);
unary('SINH', Math.sinh);
unary('COSH', Math.cosh);
unary('TANH', Math.tanh);
unary('ASINH', Math.asinh);
unary('ACOSH', Math.acosh);
unary('ATANH', Math.atanh);

define('FACT', 1, 1, (a, c) => {
  const x = Math.floor(num(a, 0, c));
  if (x < 0) fail(ERR.NUM);
  let r = 1;
  for (let i = 2; i <= x; i++) r *= i;
  return r;
}, { category: 'Math' });

define('FACTDOUBLE', 1, 1, (a, c) => {
  const x = Math.floor(num(a, 0, c));
  if (x < -1) fail(ERR.NUM);
  let r = 1;
  for (let i = x; i > 1; i -= 2) r *= i;
  return r;
}, { category: 'Math' });

define('COMBIN', 2, 2, (a, c) => {
  const n = Math.floor(num(a, 0, c));
  const k = Math.floor(num(a, 1, c));
  if (n < 0 || k < 0 || k > n) fail(ERR.NUM);
  let r = 1;
  for (let i = 1; i <= k; i++) r = (r * (n - k + i)) / i;
  return Math.round(r);
}, { category: 'Math' });

define('COMBINA', 2, 2, (a, c) => {
  const n = Math.floor(num(a, 0, c));
  const k = Math.floor(num(a, 1, c));
  if (n < 1 && k > 0) fail(ERR.NUM);
  let r = 1;
  for (let i = 1; i <= k; i++) r = (r * (n + i - 1)) / i;
  return Math.round(r);
}, { category: 'Math' });

define('PERMUT', 2, 2, (a, c) => {
  const n = Math.floor(num(a, 0, c));
  const k = Math.floor(num(a, 1, c));
  if (n < 0 || k < 0 || k > n) fail(ERR.NUM);
  let r = 1;
  for (let i = 0; i < k; i++) r *= n - i;
  return r;
}, { category: 'Math' });

define('GCD', 1, 255, (a, c) => {
  const nums = flattenNumbers(a, c).map((n) => Math.trunc(n));
  if (nums.some((n) => n < 0)) fail(ERR.NUM);
  return nums.reduce((g, n) => {
    let x = Math.abs(g);
    let y = Math.abs(n);
    while (y) [x, y] = [y, x % y];
    return x;
  }, 0);
}, { category: 'Math' });

define('LCM', 1, 255, (a, c) => {
  const nums = flattenNumbers(a, c).map((n) => Math.trunc(n));
  if (nums.some((n) => n < 0)) fail(ERR.NUM);
  return nums.reduce((l, n) => {
    let x = Math.abs(l);
    let y = Math.abs(n);
    if (x === 0 || y === 0) return 0;
    const a0 = x;
    const b0 = y;
    while (y) [x, y] = [y, x % y];
    return (a0 / x) * b0;
  }, 1);
}, { category: 'Math' });

define('SUMSQ', 0, 255, (a, c) => {
  let total = 0;
  for (const n of flattenNumbers(a, c)) total += n * n;
  return total;
}, { category: 'Math' });

define('SUMX2MY2', 2, 2, (a, c) => {
  const x = flattenNumbers([arg(a, 0)], c);
  const y = flattenNumbers([arg(a, 1)], c);
  if (x.length !== y.length) fail(ERR.NA);
  let total = 0;
  for (let i = 0; i < x.length; i++) total += x[i] * x[i] - y[i] * y[i];
  return total;
}, { category: 'Math' });

define('SUMX2PY2', 2, 2, (a, c) => {
  const x = flattenNumbers([arg(a, 0)], c);
  const y = flattenNumbers([arg(a, 1)], c);
  if (x.length !== y.length) fail(ERR.NA);
  let total = 0;
  for (let i = 0; i < x.length; i++) total += x[i] * x[i] + y[i] * y[i];
  return total;
}, { category: 'Math' });

define('SUMXMY2', 2, 2, (a, c) => {
  const x = flattenNumbers([arg(a, 0)], c);
  const y = flattenNumbers([arg(a, 1)], c);
  if (x.length !== y.length) fail(ERR.NA);
  let total = 0;
  for (let i = 0; i < x.length; i++) total += (x[i] - y[i]) ** 2;
  return total;
}, { category: 'Math' });

define('RAND', 0, 0, () => Math.random(), { category: 'Math', volatile: true });

define('RANDBETWEEN', 2, 2, (a, c) => {
  const lo = Math.ceil(num(a, 0, c));
  const hi = Math.floor(num(a, 1, c));
  if (lo > hi) fail(ERR.NUM);
  return lo + Math.floor(Math.random() * (hi - lo + 1));
}, { category: 'Math', volatile: true });

export function sumIfsCore(
  critArgs: Val[],
  tests: ((v: Scalar | import('../errors').CellError) => boolean)[],
  valueArg: Val,
  ctx: FnContext,
  mode: 'sum' | 'count' | 'average' | 'min' | 'max'
): number {
  const critAcc: Accessor[] = critArgs.map((v) => accessor(v, ctx));
  const valAcc = accessor(valueArg, ctx);
  const height = Math.min(...critAcc.map((acc) => acc.height), mode === 'count' ? Infinity : valAcc.height);
  const width = Math.min(...critAcc.map((acc) => acc.width), mode === 'count' ? Infinity : valAcc.width);
  let total = 0;
  let count = 0;
  let min = Infinity;
  let max = -Infinity;
  for (let i = 0; i < height; i++) {
    for (let j = 0; j < width; j++) {
      let ok = true;
      for (let k = 0; k < critAcc.length; k++) {
        if (!tests[k](critAcc[k].at(i, j))) {
          ok = false;
          break;
        }
      }
      if (!ok) continue;
      if (mode === 'count') {
        count++;
        continue;
      }
      const v = valAcc.at(i, j);
      if (isError(v)) throw new FormulaError(v);
      if (typeof v === 'number') {
        total += v;
        count++;
        if (v < min) min = v;
        if (v > max) max = v;
      }
    }
  }
  switch (mode) {
    case 'sum':
      return total;
    case 'count':
      return count;
    case 'average':
      if (!count) fail(ERR.DIV0);
      return total / count;
    case 'min':
      return count ? min : 0;
    case 'max':
      return count ? max : 0;
  }
}

define('SUMIF', 2, 3, (a, c) => {
  const test = makeCriteria(arg(a, 1), c);
  const valueArg = a.length > 2 ? arg(a, 2) : arg(a, 0);
  return sumIfsCore([arg(a, 0)], [test], valueArg, c, 'sum');
}, { category: 'Math' });

define('SUMIFS', 3, 255, (a, c) => {
  const critArgs: Val[] = [];
  const tests: ((v: Scalar | import('../errors').CellError) => boolean)[] = [];
  for (let i = 1; i < a.length; i += 2) {
    critArgs.push(arg(a, i));
    tests.push(makeCriteria(arg(a, i + 1), c));
  }
  return sumIfsCore(critArgs, tests, arg(a, 0), c, 'sum');
}, { category: 'Math' });

define('SUMPRODUCT', 1, 255, (a, c) => {
  const accs = a.map((v) => accessor(v, c));
  const height = Math.min(...accs.map((x) => x.height));
  const width = Math.min(...accs.map((x) => x.width));
  let total = 0;
  for (let i = 0; i < height; i++) {
    for (let j = 0; j < width; j++) {
      let prod = 1;
      let hasError = false;
      for (const acc of accs) {
        const v = acc.at(i, j);
        if (isError(v)) {
          hasError = true;
          break;
        }
        if (typeof v === 'number') prod *= v;
        else if (typeof v === 'boolean') prod *= v ? 1 : 0;
        else prod = 0;
      }
      if (hasError) {
        for (const acc of accs) {
          const v = acc.at(i, j);
          if (isError(v)) throw new FormulaError(v);
        }
      }
      total += prod;
    }
  }
  return total;
}, { category: 'Math' });

const statFns: Record<number, (nums: number[]) => number> = {
  1: (n) => (n.length ? n.reduce((a, b) => a + b, 0) / n.length : fail(ERR.DIV0)),
  2: (n) => n.length,
  3: (n) => n.length,
  4: (n) => (n.length ? Math.max(...n) : 0),
  5: (n) => (n.length ? Math.min(...n) : 0),
  6: (n) => (n.length ? n.reduce((a, b) => a * b, 1) : 0),
  7: (n) => {
    if (n.length < 2) fail(ERR.DIV0);
    const m = n.reduce((a, b) => a + b, 0) / n.length;
    const v = n.reduce((a, b) => a + (b - m) ** 2, 0) / (n.length - 1);
    return Math.sqrt(v);
  },
  8: (n) => {
    if (n.length < 1) fail(ERR.DIV0);
    const m = n.reduce((a, b) => a + b, 0) / n.length;
    return Math.sqrt(n.reduce((a, b) => a + (b - m) ** 2, 0) / n.length);
  },
  9: (n) => n.reduce((a, b) => a + b, 0),
  10: (n) => {
    if (n.length < 2) fail(ERR.DIV0);
    const m = n.reduce((a, b) => a + b, 0) / n.length;
    return n.reduce((a, b) => a + (b - m) ** 2, 0) / (n.length - 1);
  },
  11: (n) => {
    if (n.length < 1) fail(ERR.DIV0);
    const m = n.reduce((a, b) => a + b, 0) / n.length;
    return n.reduce((a, b) => a + (b - m) ** 2, 0) / n.length;
  },
};

define('SUBTOTAL', 2, 255, (a, c) => {
  const fnNum = Math.trunc(num(a, 0, c)) % 100;
  const fn = statFns[fnNum];
  if (!fn) fail(ERR.VALUE);
  // SUBTOTAL ignores nested SUBTOTAL values in Excel; we skip text/bools which
  // is the observable behavior for plain numeric ranges.
  const nums: number[] = [];
  for (let i = 1; i < a.length; i++) {
    for (const v of flattenNumbers([arg(a, i)], c)) nums.push(v);
  }
  return fn(nums);
}, { category: 'Math' });

define('AGGREGATE', 3, 255, (a, c) => {
  const fnNum = Math.trunc(num(a, 0, c));
  const options = Math.trunc(num(a, 1, c));
  const refs = a.slice(2);
  const nums: number[] = [];
  for (const r of refs) {
    if (!r) continue;
    for (const v of flattenNumbers([r], c)) nums.push(v);
  }
  if (options >= 2 && options <= 7) {
    // ignore-errors variants: recompute ignoring errors
    nums.length = 0;
    for (const r of refs) {
      for (const raw of flattenRaw([r], c)) if (!isError(raw) && typeof raw === 'number') nums.push(raw);
    }
  }
  switch (fnNum) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
      return statFns[fnNum](nums);
    case 12:
      return sortedMedian(nums);
    case 14:
      return nums.length ? Math.max(...nums) : 0;
    case 15:
      return nums.length ? Math.min(...nums) : 0;
    case 16:
      return nums.length ? nums.reduce((x, y) => x + y, 0) / nums.length : fail(ERR.DIV0);
    case 17:
      return nums.length > 1 ? Math.sqrt(variance(nums, true)) : fail(ERR.DIV0);
    case 18: {
      const s = [...nums].sort((x, y) => x - y);
      if (!s.length) fail(ERR.NUM);
      const idx = Math.floor(s.length / 2);
      return s.length % 2 ? s[idx] : (s[idx - 1] + s[idx]) / 2;
    }
    case 19: {
      const s = [...nums].sort((x, y) => x - y);
      if (!s.length) fail(ERR.NUM);
      return s[Math.floor((s.length - 1) * 0.9)];
    }
    default:
      fail(ERR.VALUE);
  }
}, { category: 'Math' });

function sortedMedian(nums: number[]): number {
  const s = [...nums].sort((a, b) => a - b);
  if (!s.length) fail(ERR.NUM);
  const mid = Math.floor(s.length / 2);
  return s.length % 2 ? s[mid] : (s[mid - 1] + s[mid]) / 2;
}

function variance(nums: number[], sample: boolean): number {
  if (nums.length < (sample ? 2 : 1)) fail(ERR.DIV0);
  const m = nums.reduce((a, b) => a + b, 0) / nums.length;
  const sum = nums.reduce((a, b) => a + (b - m) ** 2, 0);
  return sum / (sample ? nums.length - 1 : nums.length);
}

function flattenRaw(args: Val[], ctx: FnContext): (Scalar | import('../errors').CellError)[] {
  const out: (Scalar | import('../errors').CellError)[] = [];
  for (const a of args) {
    if (!a) continue;
    if (typeof a === 'object' && 'rows' in (a as object)) {
      for (const row of (a as import('../values').ArrayValue).rows) out.push(...row);
    } else if (typeof a === 'object') {
      const r = a as import('../values').RangeValue;
      for (let i = r.r1; i <= r.r2; i++)
        for (let j = r.c1; j <= r.c2; j++) out.push(ctx.source.getScalar(r.sheetId, i, j));
    } else {
      out.push(a as Scalar);
    }
  }
  return out;
}

define('ROMAN', 1, 2, (a, c) => {
  let n = Math.trunc(num(a, 0, c));
  if (n <= 0 || n >= 4000) fail(ERR.VALUE);
  const table: [number, string][] = [
    [1000, 'M'], [900, 'CM'], [500, 'D'], [400, 'CD'], [100, 'C'], [90, 'XC'],
    [50, 'L'], [40, 'XL'], [10, 'X'], [9, 'IX'], [5, 'V'], [4, 'IV'], [1, 'I'],
  ];
  let out = '';
  for (const [v, s] of table) {
    while (n >= v) {
      out += s;
      n -= v;
    }
  }
  return out;
}, { category: 'Math' });
