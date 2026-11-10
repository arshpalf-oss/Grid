/** Shared coercion, rounding and criteria-matching helpers for the function library. */

import { CellError, ERR, FormulaError, fail, isError } from '../errors';
import {
  FnContext,
  RangeValue,
  Scalar,
  Val,
  deref,
  flattenArgs,
  clampRange,
  isArray,
  isRange,
  toBool,
  toGrid,
  toNumber,
  toText,
} from '../values';

export { toBool, toGrid, toNumber, toText };

export function arg(args: Val[], i: number): Val {
  return i < args.length ? args[i] : null;
}

export function num(args: Val[], i: number, ctx: FnContext): number {
  if (i >= args.length) fail(ERR.VALUE);
  return toNumber(args[i], ctx);
}

export function numOr(args: Val[], i: number, ctx: FnContext, fallback: number): number {
  if (i >= args.length || args[i] === null) return fallback;
  return toNumber(args[i], ctx);
}

export function text(args: Val[], i: number, ctx: FnContext): string {
  if (i >= args.length) fail(ERR.VALUE);
  return toText(args[i], ctx);
}

export function bool(args: Val[], i: number, ctx: FnContext): boolean {
  if (i >= args.length) fail(ERR.VALUE);
  return toBool(args[i], ctx);
}

export function boolOr(args: Val[], i: number, ctx: FnContext, fallback: boolean): boolean {
  if (i >= args.length || args[i] === null) return fallback;
  return toBool(args[i], ctx);
}

export function isBlank(v: Val): boolean {
  return v === null || v === '';
}

/** 1-based index handling used by INDEX/CHOOSE/ROWS-style functions. */
export function intIndex(v: Val, ctx: FnContext): number {
  const n = toNumber(v, ctx);
  if (!Number.isFinite(n)) fail(ERR.VALUE);
  return Math.trunc(n);
}

export function roundHalfAway(x: number, digits: number): number {
  if (!Number.isFinite(x) || x === 0) return x;
  const sign = x < 0 ? -1 : 1;
  const abs = Math.abs(x);
  const shift = Number(`${abs}e${digits}`);
  if (!Number.isFinite(shift)) return x;
  const rounded = Math.round(shift);
  return sign * Number(`${rounded}e${-digits}`);
}

export function roundUpAway(x: number, digits: number): number {
  if (!Number.isFinite(x) || x === 0) return x;
  const sign = x < 0 ? -1 : 1;
  const abs = Math.abs(x);
  const factor = Math.pow(10, digits);
  const scaled = Number(`${abs}e${digits}`);
  const r = Math.ceil(scaled - 1e-9);
  return sign * Number(`${r}e${-digits}`) || sign * (Math.ceil(abs * factor) / factor);
}

export function roundDownTowardZero(x: number, digits: number): number {
  if (!Number.isFinite(x) || x === 0) return x;
  const sign = x < 0 ? -1 : 1;
  const abs = Math.abs(x);
  const scaled = Number(`${abs}e${digits}`);
  const r = Math.floor(scaled + 1e-9);
  return sign * Number(`${r}e${-digits}`);
}

/** Excel wildcards: * and ?, escaped with ~ */
export function wildcardToRegExp(pattern: string, caseSensitive = false): RegExp {
  let out = '^';
  for (let i = 0; i < pattern.length; i++) {
    const ch = pattern[i];
    if (ch === '~' && i + 1 < pattern.length) {
      out += pattern[i + 1].replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
      i++;
    } else if (ch === '*') {
      out += '.*';
    } else if (ch === '?') {
      out += '.';
    } else {
      out += ch.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    }
  }
  out += '$';
  return new RegExp(out, caseSensitive ? '' : 'i');
}

export function hasWildcard(s: string): boolean {
  return /(^|[^~])[*?]/.test(s);
}

const CRITERIA_OPS = ['>=', '<=', '<>', '>', '<', '='] as const;

/**
 * Build a predicate from a SUMIF/COUNTIF-style criteria argument.
 * Supports ">5", "<>x", "apple", "*an*", booleans and cell references.
 */
export function makeCriteria(crit: Val, ctx: FnContext): (v: Scalar | CellError) => boolean {
  let raw: Scalar | CellError;
  if (isRange(crit) || isArray(crit)) raw = deref(crit, ctx);
  else raw = crit as Scalar | CellError;
  if (isError(raw)) throw new FormulaError(raw);

  if (raw === null) return (v) => v === null || v === '';
  if (typeof raw === 'number') return (v) => v === raw;
  if (typeof raw === 'boolean') return (v) => v === raw;

  let op = '=';
  let operand = raw;
  for (const candidate of CRITERIA_OPS) {
    if (typeof operand === 'string' && operand.startsWith(candidate)) {
      op = candidate;
      operand = operand.slice(candidate.length);
      break;
    }
  }

  const numeric = typeof operand === 'string' ? Number(operand.replace(/,/g, '')) : NaN;
  const isNumeric = typeof operand === 'string' && operand.trim() !== '' && Number.isFinite(numeric);
  const operandText = String(operand);
  const empty = operandText === '';

  const eq = (v: Scalar | CellError): boolean => {
    if (isError(v)) return false;
    if (empty) return v === null || v === '';
    if (isNumeric) {
      if (typeof v === 'number') return v === numeric;
      if (typeof v === 'string' && v.trim() !== '') {
        const n = Number(v);
        return Number.isFinite(n) && n === numeric;
      }
      return false;
    }
    const upper = operandText.toUpperCase();
    if (upper === 'TRUE' || upper === 'FALSE') {
      const b = upper === 'TRUE';
      if (typeof v === 'boolean') return v === b;
      if (typeof v === 'string') return v.toUpperCase() === upper;
      return false;
    }
    if (typeof v === 'string') {
      if (hasWildcard(operandText)) return wildcardToRegExp(operandText).test(v);
      return v.toLowerCase() === operandText.toLowerCase();
    }
    if (typeof v === 'number') return false;
    if (typeof v === 'boolean') return false;
    return false;
  };

  const cmp = (v: Scalar | CellError): number | null => {
    if (isError(v)) return null;
    if (v === null) return null;
    if (isNumeric) {
      const n = typeof v === 'number' ? v : typeof v === 'string' && v.trim() !== '' ? Number(v) : NaN;
      if (!Number.isFinite(n)) return null;
      return n === numeric ? 0 : n < numeric ? -1 : 1;
    }
    if (typeof v !== 'string') return null;
    const a = v.toLowerCase();
    const b = operandText.toLowerCase();
    return a === b ? 0 : a < b ? -1 : 1;
  };

  return (v) => {
    switch (op) {
      case '=':
        return eq(v);
      case '<>':
        return !eq(v);
      case '>':
        return cmp(v) === 1;
      case '<':
        return cmp(v) === -1;
      case '>=':
        return cmp(v) === 0 || cmp(v) === 1;
      case '<=':
        return cmp(v) === 0 || cmp(v) === -1;
      default:
        return false;
    }
  };
}

export function rangeOf(v: Val, ctx: FnContext, what = 'range'): RangeValue {
  if (isRange(v)) return v;
  if (isArray(v)) fail(ERR.VALUE);
  fail(ERR.VALUE);
}

/** Values of a range as a flat array (used by lookup functions). */
export function flatRange(v: Val, ctx: FnContext): (Scalar | CellError)[] {
  return flattenArgs([v], ctx);
}

export function singleColumnOf(v: Val, ctx: FnContext): (Scalar | CellError)[] {
  const grid = toGrid(v, ctx);
  if (grid.length === 0) return [];
  if (grid[0].length !== 1) return grid.map((r) => r[0]);
  return grid.map((r) => r[0]);
}

export function singleRowOf(v: Val, ctx: FnContext): (Scalar | CellError)[] {
  const grid = toGrid(v, ctx);
  return grid.length ? grid[0] : [];
}

/** Approximate match on an ascending-sorted list (Excel's TRUE mode). */
export function approximateMatch(
  lookup: Scalar,
  list: (Scalar | CellError)[]
): number {
  let best = -1;
  for (let i = 0; i < list.length; i++) {
    const v = list[i];
    if (isError(v) || v === null) continue;
    if (typeof v !== typeof lookup) {
      if (!(typeof v === 'number' && typeof lookup === 'number')) continue;
    }
    const cmp = compareRaw(v, lookup);
    if (cmp <= 0) best = i;
    else break;
  }
  return best;
}

function compareRaw(a: Scalar, b: Scalar): number {
  if (typeof a === 'number' && typeof b === 'number') return a === b ? 0 : a < b ? -1 : 1;
  const sa = String(a).toLowerCase();
  const sb = String(b).toLowerCase();
  return sa === sb ? 0 : sa < sb ? -1 : 1;
}

export function valueToBoolForLookup(v: Scalar | CellError): boolean {
  return v === true;
}

export function safeStr(v: Scalar | CellError): string {
  if (isError(v)) return v.code;
  if (v === null) return '';
  if (typeof v === 'number') return String(v);
  if (typeof v === 'boolean') return v ? 'TRUE' : 'FALSE';
  return v;
}

/** NumPy-ish matrix helpers used by array functions. */
export function matrixToArray(grid: (Scalar | CellError)[][]) {
  return grid;
}

export function ensureFinite(n: number): number {
  if (!Number.isFinite(n)) fail(ERR.NUM);
  return n;
}

/** Flatten arguments from an offset (TEXTJOIN/CONCAT style). */
export function flattenValuesFrom(args: Val[], ctx: FnContext): (Scalar | CellError)[] {
  return flattenArgs(args, ctx);
}

/** Uniform random-access view over a range, array or single value. */
export interface Accessor {
  height: number;
  width: number;
  at(i: number, j: number): Scalar | CellError;
}

export function accessor(v: Val, ctx: FnContext): Accessor {
  if (isRange(v)) {
    const r = clampRange(v, ctx);
    const height = r.r2 - r.r1 + 1;
    const width = r.c2 - r.c1 + 1;
    return {
      height,
      width,
      at: (i, j) => ctx.source.getScalar(r.sheetId, r.r1 + Math.min(i, height - 1), r.c1 + Math.min(j, width - 1)),
    };
  }
  if (isArray(v)) {
    return { height: v.height, width: v.width, at: (i, j) => v.at(i, j) };
  }
  const scalar = v as Scalar | CellError;
  return { height: 1, width: 1, at: () => scalar };
}

/** Iterate criteria/sum ranges in lock-step, skipping mismatched sizes. */
export function alignAccessors(a: Accessor, b: Accessor): { height: number; width: number } {
  return { height: Math.min(a.height, b.height), width: Math.min(a.width, b.width) };
}
