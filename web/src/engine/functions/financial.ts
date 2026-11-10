/** Financial functions. */

import { ERR, fail } from '../errors';
import { arg, flattenValuesFrom, num, numOr, toGrid } from './helpers';
import { define } from './registry';

function pmt(rate: number, nper: number, pv: number, fv = 0, type = 0): number {
  if (nper === 0) fail(ERR.NUM);
  if (rate === 0) return -(pv + fv) / nper;
  const pvif = Math.pow(1 + rate, nper);
  const factor = ((1 + rate * type) * (pvif - 1)) / rate;
  const r = -(pv * pvif + fv) / factor;
  return Number.isFinite(r) ? r : fail(ERR.NUM);
}

define('PMT', 3, 5, (a, c) => pmt(num(a, 0, c), num(a, 1, c), num(a, 2, c), numOr(a, 3, c, 0), numOr(a, 4, c, 0)), { category: 'Financial' });

define('FV', 3, 5, (a, c) => {
  const rate = num(a, 0, c);
  const nper = num(a, 1, c);
  const p = num(a, 2, c);
  const pv = numOr(a, 3, c, 0);
  const type = numOr(a, 4, c, 0);
  if (rate === 0) return -(pv + p * nper);
  const pvif = Math.pow(1 + rate, nper);
  const factor = ((1 + rate * type) * (pvif - 1)) / rate;
  return -(pv * pvif + p * factor);
}, { category: 'Financial' });

define('PV', 3, 5, (a, c) => {
  const rate = num(a, 0, c);
  const nper = num(a, 1, c);
  const p = num(a, 2, c);
  const fv = numOr(a, 3, c, 0);
  const type = numOr(a, 4, c, 0);
  if (rate === 0) return -(fv + p * nper);
  const pvif = Math.pow(1 + rate, nper);
  const factor = ((1 + rate * type) * (pvif - 1)) / rate;
  return -(fv + p * factor) / pvif;
}, { category: 'Financial' });

define('NPER', 3, 5, (a, c) => {
  const rate = num(a, 0, c);
  const p = num(a, 1, c);
  const pv = num(a, 2, c);
  const fv = numOr(a, 3, c, 0);
  const type = numOr(a, 4, c, 0);
  if (rate === 0) {
    if (p === 0) fail(ERR.DIV0);
    return -(pv + fv) / p;
  }
  if (p === 0) fail(ERR.NUM);
  const z = (p * (1 + rate * type)) / rate;
  const r = Math.log((z - fv) / (z + pv)) / Math.log(1 + rate);
  if (!Number.isFinite(r)) fail(ERR.NUM);
  return r;
}, { category: 'Financial' });

define('RATE', 3, 6, (a, c) => {
  const nper = num(a, 0, c);
  const p = num(a, 1, c);
  const pv = num(a, 2, c);
  const fv = numOr(a, 3, c, 0);
  const type = numOr(a, 4, c, 0);
  const guess = numOr(a, 5, c, 0.1);
  const f = (rate: number) => {
    if (rate === 0) return pv + p * nper + fv;
    const pvif = Math.pow(1 + rate, nper);
    return pv * pvif + p * ((1 + rate * type) * (pvif - 1)) / rate + fv;
  };
  let rate = guess;
  for (let i = 0; i < 100; i++) {
    const y = f(rate);
    const dy = (f(rate + 1e-6) - y) / 1e-6;
    if (!Number.isFinite(dy) || dy === 0) break;
    const next = rate - y / dy;
    if (!Number.isFinite(next)) break;
    if (Math.abs(next - rate) < 1e-10) return next;
    rate = next;
  }
  // bisection fallback
  let lo = -0.9999;
  let hi = 10;
  let flo = f(lo);
  for (let i = 0; i < 200; i++) {
    const mid = (lo + hi) / 2;
    const fmid = f(mid);
    if (Math.abs(fmid) < 1e-10) return mid;
    if (flo * fmid < 0) hi = mid;
    else {
      lo = mid;
      flo = fmid;
    }
  }
  fail(ERR.NUM);
}, { category: 'Financial' });

function interestPayment(rate: number, per: number, nper: number, pv: number, fv: number, type: number): number {
  const payment = pmt(rate, nper, pv, fv, type);
  if (per < 1 || per > nper) fail(ERR.NUM);
  if (type === 1 && per === 1) return 0;
  let balance = pv;
  for (let i = 1; i < per; i++) {
    const interest = balance * rate;
    balance += -(payment - interest);
  }
  return -(balance * rate);
}

define('IPMT', 4, 6, (a, c) => {
  const rate = num(a, 0, c);
  const per = Math.trunc(num(a, 1, c));
  const nper = Math.trunc(num(a, 2, c));
  const pv = num(a, 3, c);
  const fv = numOr(a, 4, c, 0);
  const type = numOr(a, 5, c, 0);
  return interestPayment(rate, per, nper, pv, fv, type);
}, { category: 'Financial' });

define('PPMT', 4, 6, (a, c) => {
  const rate = num(a, 0, c);
  const per = Math.trunc(num(a, 1, c));
  const nper = Math.trunc(num(a, 2, c));
  const pv = num(a, 3, c);
  const fv = numOr(a, 4, c, 0);
  const type = numOr(a, 5, c, 0);
  const payment = pmt(rate, nper, pv, fv, type);
  return payment - interestPayment(rate, per, nper, pv, fv, type);
}, { category: 'Financial' });

define('NPV', 2, 255, (a, c) => {
  const rate = num(a, 0, c);
  const flows: number[] = [];
  for (let i = 1; i < a.length; i++) for (const v of flattenValuesFrom([arg(a, i)], c)) if (typeof v === 'number') flows.push(v);
  if (rate === -1) fail(ERR.DIV0);
  let total = 0;
  for (let i = 0; i < flows.length; i++) total += flows[i] / Math.pow(1 + rate, i + 1);
  return total;
}, { category: 'Financial' });

define('IRR', 1, 3, (a, c) => {
  const flows = toGrid(a[0], c).flat().filter((v): v is number => typeof v === 'number');
  const guess = numOr(a, 1, c, 0.1);
  if (flows.length < 2) fail(ERR.NUM);
  const npvAt = (r: number) => flows.reduce((acc, f, i) => acc + f / Math.pow(1 + r, i), 0);
  let rate = guess;
  for (let i = 0; i < 100; i++) {
    const y = npvAt(rate);
    const dy = (npvAt(rate + 1e-6) - y) / 1e-6;
    if (!Number.isFinite(dy) || dy === 0) break;
    const next = rate - y / dy;
    if (!Number.isFinite(next) || next <= -1) break;
    if (Math.abs(next - rate) < 1e-10) return next;
    rate = next;
  }
  let lo = -0.9999;
  let hi = 10;
  let flo = npvAt(lo);
  for (let i = 0; i < 200; i++) {
    const mid = (lo + hi) / 2;
    const fmid = npvAt(mid);
    if (Math.abs(fmid) < 1e-9) return mid;
    if (flo * fmid < 0) hi = mid;
    else {
      lo = mid;
      flo = fmid;
    }
  }
  fail(ERR.NUM);
}, { category: 'Financial' });

define('SLN', 3, 3, (a, c) => {
  const cost = num(a, 0, c);
  const salvage = num(a, 1, c);
  const life = num(a, 2, c);
  if (life === 0) fail(ERR.DIV0);
  return (cost - salvage) / life;
}, { category: 'Financial' });

define('SYD', 4, 4, (a, c) => {
  const cost = num(a, 0, c);
  const salvage = num(a, 1, c);
  const life = num(a, 2, c);
  const per = num(a, 3, c);
  if (life <= 0 || per <= 0 || per > life) fail(ERR.NUM);
  return ((cost - salvage) * (life - per + 1) * 2) / (life * (life + 1));
}, { category: 'Financial' });

define('DB', 4, 5, (a, c) => {
  const cost = num(a, 0, c);
  const salvage = num(a, 1, c);
  const life = num(a, 2, c);
  const period = num(a, 3, c);
  const month = Math.trunc(numOr(a, 4, c, 12));
  if (cost <= 0 || life <= 0 || period < 1 || month < 1 || month > 12) fail(ERR.NUM);
  const rate = 1 - Math.pow(salvage / cost, 1 / life);
  const rounded = Math.round(rate * 1000) / 1000;
  let total = 0;
  let book = cost;
  for (let p = 1; p <= Math.min(period, life + 1); p++) {
    const dep = p === 1 ? (book * rounded * month) / 12 : (book * rounded);
    const capped = Math.min(dep, book - salvage);
    if (p === Math.ceil(period) || p === Math.floor(period)) {
      if (p === period) return Math.max(0, capped);
    }
    book -= capped;
    total += capped;
  }
  return Math.max(0, total);
}, { category: 'Financial' });

define('DDB', 4, 5, (a, c) => {
  const cost = num(a, 0, c);
  const salvage = num(a, 1, c);
  const life = num(a, 2, c);
  const period = num(a, 3, c);
  const factor = numOr(a, 4, c, 2);
  if (cost < 0 || salvage < 0 || life <= 0 || period <= 0 || factor <= 0) fail(ERR.NUM);
  let book = cost;
  let dep = 0;
  for (let p = 1; p <= Math.floor(period); p++) {
    dep = Math.min((book * factor) / life, book - salvage);
    if (p === Math.floor(period)) return Math.max(0, dep);
    book -= dep;
  }
  return Math.max(0, dep);
}, { category: 'Financial' });

define('EFFECT', 2, 2, (a, c) => {
  const nominal = num(a, 0, c);
  const periods = Math.trunc(num(a, 1, c));
  if (nominal <= 0 || periods < 1) fail(ERR.NUM);
  return Math.pow(1 + nominal / periods, periods) - 1;
}, { category: 'Financial' });

define('NOMINAL', 2, 2, (a, c) => {
  const effect = num(a, 0, c);
  const periods = Math.trunc(num(a, 1, c));
  if (effect <= 0 || periods < 1) fail(ERR.NUM);
  return periods * (Math.pow(1 + effect, 1 / periods) - 1);
}, { category: 'Financial' });

define('ISPMT', 4, 4, (a, c) => {
  const rate = num(a, 0, c);
  const per = num(a, 1, c);
  const nper = num(a, 2, c);
  const pv = num(a, 3, c);
  if (nper === 0) fail(ERR.DIV0);
  return pv * rate * (per / nper - 1);
}, { category: 'Financial' });

define('XNPV', 3, 3, (a, c) => {
  const rate = num(a, 0, c);
  const values = toGrid(a[1], c).flat();
  const dates = toGrid(a[2], c).flat();
  if (values.length !== dates.length) fail(ERR.NUM);
  const first = typeof dates[0] === 'number' ? dates[0] : fail(ERR.VALUE);
  let total = 0;
  for (let i = 0; i < values.length; i++) {
    const v = values[i];
    const d = dates[i];
    if (typeof v !== 'number' || typeof d !== 'number') continue;
    total += v / Math.pow(1 + rate, (d - first) / 365);
  }
  return total;
}, { category: 'Financial' });

define('XIRR', 2, 3, (a, c) => {
  const values = toGrid(a[0], c).flat();
  const dates = toGrid(a[1], c).flat();
  if (values.length !== dates.length) fail(ERR.NUM);
  const nums: { v: number; d: number }[] = [];
  for (let i = 0; i < values.length; i++) {
    const v = values[i];
    const d = dates[i];
    if (typeof v === 'number' && typeof d === 'number') nums.push({ v, d });
  }
  if (nums.length < 2) fail(ERR.NUM);
  const first = nums[0].d;
  const f = (r: number) =>
    nums.reduce((acc, { v, d }) => (r <= -1 ? acc : acc + v / Math.pow(1 + r, (d - first) / 365)), 0);
  let rate = numOr(a, 2, c, 0.1);
  for (let i = 0; i < 100; i++) {
    const y = f(rate);
    const dy = (f(rate + 1e-6) - y) / 1e-6;
    if (!Number.isFinite(dy) || dy === 0) break;
    const next = rate - y / dy;
    if (!Number.isFinite(next) || next <= -1) break;
    if (Math.abs(next - rate) < 1e-10) return next;
    rate = next;
  }
  let lo = -0.9999;
  let hi = 10;
  let flo = f(lo);
  for (let i = 0; i < 300; i++) {
    const mid = (lo + hi) / 2;
    const fmid = f(mid);
    if (Math.abs(fmid) < 1e-9) return mid;
    if (flo * fmid < 0) hi = mid;
    else {
      lo = mid;
      flo = fmid;
    }
  }
  fail(ERR.NUM);
}, { category: 'Financial' });
