/** Text functions. */

import { ERR, FormulaError, fail, isError } from '../errors';
import { formatValue } from '../format';
import { ArrayValue, FnContext, Scalar, Val, deref, numberToText, parseNumberText } from '../values';
import { dateToSerial } from '../dates';
import { boolOr, flattenValuesFrom, num, numOr, text, wildcardToRegExp } from './helpers';
import { define } from './registry';

define('LEN', 1, 1, (a, c) => text(a, 0, c).length, { category: 'Text' });
define('UPPER', 1, 1, (a, c) => text(a, 0, c).toUpperCase(), { category: 'Text' });
define('LOWER', 1, 1, (a, c) => text(a, 0, c).toLowerCase(), { category: 'Text' });

define('PROPER', 1, 1, (a, c) => {
  const s = text(a, 0, c).toLowerCase();
  return s.replace(/(^|[^A-Za-z'])([a-z])/g, (_m, p1: string, p2: string) => p1 + p2.toUpperCase());
}, { category: 'Text' });

define('TRIM', 1, 1, (a, c) => text(a, 0, c).replace(/\s+/g, ' ').trim(), { category: 'Text' });
define('CLEAN', 1, 1, (a, c) => text(a, 0, c).replace(/[\u0000-\u001F]/g, ''), { category: 'Text' });

define('LEFT', 1, 2, (a, c) => {
  const s = text(a, 0, c);
  const n = Math.max(0, Math.trunc(numOr(a, 1, c, 1)));
  return s.slice(0, n);
}, { category: 'Text' });

define('RIGHT', 1, 2, (a, c) => {
  const s = text(a, 0, c);
  const n = Math.max(0, Math.trunc(numOr(a, 1, c, 1)));
  return n === 0 ? '' : s.slice(-n);
}, { category: 'Text' });

define('MID', 3, 3, (a, c) => {
  const s = text(a, 0, c);
  const start = Math.trunc(num(a, 1, c));
  const len = Math.trunc(num(a, 2, c));
  if (start < 1 || len < 0) fail(ERR.VALUE);
  return s.slice(start - 1, start - 1 + len);
}, { category: 'Text' });

define('REPT', 2, 2, (a, c) => {
  const s = text(a, 0, c);
  const n = Math.trunc(num(a, 1, c));
  if (n < 0) fail(ERR.VALUE);
  if (s.length * n > 32767) fail(ERR.VALUE);
  return s.repeat(n);
}, { category: 'Text' });

define('CONCATENATE', 0, 255, (a, c) => flattenValuesFrom(a, c).map((v) => scalarToText(v)).join(''), { category: 'Text' });
define('CONCAT', 0, 255, (a, c) => flattenValuesFrom(a, c).map((v) => scalarToText(v)).join(''), { category: 'Text' });

function scalarToText(v: Scalar | import('../errors').CellError): string {
  if (isError(v)) throw new FormulaError(v);
  if (v === null) return '';
  if (typeof v === 'number') return numberToText(v);
  if (typeof v === 'boolean') return v ? 'TRUE' : 'FALSE';
  return v;
}

define('TEXTJOIN', 3, 255, (a, c) => {
  const delim = text(a, 0, c);
  const ignoreEmpty = boolOr(a, 1, c, true);
  const parts = flattenValuesFrom(a.slice(2), c).map((v) => scalarToText(v));
  const filtered = ignoreEmpty ? parts.filter((p) => p !== '') : parts;
  return filtered.join(delim);
}, { category: 'Text' });

define('EXACT', 2, 2, (a, c) => text(a, 0, c) === text(a, 1, c), { category: 'Text' });

define('FIND', 2, 3, (a, c) => {
  const needle = text(a, 0, c);
  const hay = text(a, 1, c);
  const start = Math.trunc(numOr(a, 2, c, 1));
  if (start < 1 || start > hay.length + 1) fail(ERR.VALUE);
  const idx = hay.indexOf(needle, start - 1);
  if (idx < 0) fail(ERR.VALUE);
  return idx + 1;
}, { category: 'Text' });

define('SEARCH', 2, 3, (a, c) => {
  const needle = text(a, 0, c);
  const hay = text(a, 1, c);
  const start = Math.trunc(numOr(a, 2, c, 1));
  if (start < 1 || start > hay.length + 1) fail(ERR.VALUE);
  const re = wildcardToRegExp(needle);
  const slice = hay.slice(start - 1);
  // wildcardToRegExp is anchored; find the first position that matches a prefix
  for (let i = 0; i <= slice.length; i++) {
    const pattern = new RegExp(re.source.slice(0, -1) + '.*$', 'i');
    if (pattern.test(slice.slice(i))) return start + i;
  }
  fail(ERR.VALUE);
}, { category: 'Text' });

define('SUBSTITUTE', 3, 4, (a, c) => {
  const s = text(a, 0, c);
  const oldText = text(a, 1, c);
  const newText = text(a, 2, c);
  if (oldText === '') return s;
  if (a.length < 4 || a[3] === null) return s.split(oldText).join(newText);
  const instance = Math.trunc(num(a, 3, c));
  if (instance < 1) fail(ERR.VALUE);
  let idx = -1;
  for (let k = 0; k < instance; k++) {
    idx = s.indexOf(oldText, idx + 1);
    if (idx < 0) return s;
  }
  return s.slice(0, idx) + newText + s.slice(idx + oldText.length);
}, { category: 'Text' });

define('REPLACE', 4, 4, (a, c) => {
  const s = text(a, 0, c);
  const start = Math.trunc(num(a, 1, c));
  const len = Math.trunc(num(a, 2, c));
  const newText = text(a, 3, c);
  if (start < 1 || len < 0) fail(ERR.VALUE);
  return s.slice(0, start - 1) + newText + s.slice(start - 1 + len);
}, { category: 'Text' });

define('CHAR', 1, 1, (a, c) => {
  const n = Math.trunc(num(a, 0, c));
  if (n < 1 || n > 255) fail(ERR.VALUE);
  return String.fromCharCode(n);
}, { category: 'Text' });

define('CODE', 1, 1, (a, c) => {
  const s = text(a, 0, c);
  if (!s) fail(ERR.VALUE);
  return s.charCodeAt(0);
}, { category: 'Text' });

define('UNICHAR', 1, 1, (a, c) => {
  const n = Math.trunc(num(a, 0, c));
  if (n <= 0 || n > 0x10ffff) fail(ERR.VALUE);
  return String.fromCodePoint(n);
}, { category: 'Text' });

define('UNICODE', 1, 1, (a, c) => {
  const s = text(a, 0, c);
  if (!s) fail(ERR.VALUE);
  return s.codePointAt(0) ?? 0;
}, { category: 'Text' });

define('VALUE', 1, 1, (a, c) => {
  const v = a[0];
  if (typeof v === 'number') return v;
  if (typeof v === 'boolean') return v ? 1 : 0;
  if (v === null) return 0;
  if (isError(v)) throw new FormulaError(v);
  const parsed = parseNumberText(String(v));
  if (parsed !== null) return parsed;
  const asDate = parseDateText(String(v));
  if (asDate !== null) return asDate;
  fail(ERR.VALUE);
}, { category: 'Text' });

export function parseDateText(input: string): number | null {
  const s = input.trim();
  if (!s) return null;
  const iso = /^(\d{4})-(\d{1,2})-(\d{1,2})(?:[ T](\d{1,2}):(\d{2})(?::(\d{2}))?)?$/.exec(s);
  const us = /^(\d{1,2})\/(\d{1,2})\/(\d{4})(?:\s+(\d{1,2}):(\d{2})(?::(\d{2}))?)?$/.exec(s);
  let y = 0;
  let mo = 0;
  let d = 0;
  let h = 0;
  let mi = 0;
  let sec = 0;
  if (iso) {
    y = +iso[1];
    mo = +iso[2];
    d = +iso[3];
    h = +(iso[4] ?? 0);
    mi = +(iso[5] ?? 0);
    sec = +(iso[6] ?? 0);
  } else if (us) {
    mo = +us[1];
    d = +us[2];
    y = +us[3];
    h = +(us[4] ?? 0);
    mi = +(us[5] ?? 0);
    sec = +(us[6] ?? 0);
  } else {
    return null;
  }
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return null;
  return dateToSerial(new Date(Date.UTC(y, mo - 1, d, h, mi, sec)));
}

define('TEXT', 1, 2, (a, c) => {
  const v = a[0];
  if (a.length < 2 || a[1] === null) return scalarToText(derefSafe(v, c));
  const fmt = text(a, 1, c);
  return formatValue(derefSafe(v, c), fmt);
}, { category: 'Text' });

function derefSafe(v: Val, c: FnContext): Scalar | import('../errors').CellError {
  return deref(v, c);
}

define('T', 1, 1, (a, c) => (typeof a[0] === 'string' ? a[0] : ''), { category: 'Text' });
define('N', 1, 1, (a, c) => {
  const v = a[0];
  if (typeof v === 'number') return v;
  if (typeof v === 'boolean') return v ? 1 : 0;
  return 0;
}, { category: 'Text' });

define('DOLLAR', 1, 2, (a, c) => {
  const n = num(a, 0, c);
  const decimals = Math.trunc(numOr(a, 1, c, 2));
  return formatValue(n, `$#,##0${decimals > 0 ? '.' + '0'.repeat(decimals) : ''}${decimals < 0 ? '' : ''}`);
}, { category: 'Text' });

define('FIXED', 1, 3, (a, c) => {
  const n = num(a, 0, c);
  const decimals = Math.trunc(numOr(a, 1, c, 2));
  const noCommas = boolOr(a, 2, c, false);
  const d = Math.max(0, decimals);
  return formatValue(n, `${noCommas ? '0' : '#,##0'}${d > 0 ? '.' + '0'.repeat(d) : ''}`);
}, { category: 'Text' });

define('TEXTBEFORE', 2, 6, (a, c) => {
  const s = text(a, 0, c);
  const delim = text(a, 1, c);
  const instance = Math.trunc(numOr(a, 2, c, 1));
  const matchEnd = boolOr(a, 4, c, false);
  const positions: number[] = [];
  if (delim === '') return instance === 1 ? '' : fail(ERR.NA);
  let from = 0;
  for (;;) {
    const idx = s.indexOf(delim, from);
    if (idx < 0) break;
    positions.push(idx);
    from = idx + delim.length;
  }
  const notFound = a.length > 5 ? a[5] : null;
  const pick = (): string | null => {
    if (positions.length === 0) return null;
    if (instance > 0) {
      if (instance > positions.length) return null;
      return s.slice(0, positions[instance - 1]);
    }
    const fromEnd = positions.length + instance;
    if (fromEnd < 0) return null;
    return s.slice(0, positions[fromEnd]);
  };
  const out = pick();
  if (out === null) {
    if (notFound !== null) return notFound as Scalar;
    if (matchEnd) return instance > 0 ? s : '';
    fail(ERR.NA);
  }
  return out;
}, { category: 'Text' });

define('TEXTAFTER', 2, 6, (a, c) => {
  const s = text(a, 0, c);
  const delim = text(a, 1, c);
  const instance = Math.trunc(numOr(a, 2, c, 1));
  const matchEnd = boolOr(a, 4, c, false);
  const positions: number[] = [];
  let from = 0;
  for (;;) {
    const idx = s.indexOf(delim, from);
    if (idx < 0) break;
    positions.push(idx);
    from = idx + delim.length;
  }
  const notFound = a.length > 5 ? a[5] : null;
  const pick = (): string | null => {
    if (positions.length === 0) return null;
    if (instance > 0) {
      if (instance > positions.length) return null;
      return s.slice(positions[instance - 1] + delim.length);
    }
    const fromEnd = positions.length + instance;
    if (fromEnd < 0) return null;
    return s.slice(positions[fromEnd] + delim.length);
  };
  const out = pick();
  if (out === null) {
    if (notFound !== null) return notFound as Scalar;
    if (matchEnd) return instance > 0 ? '' : s;
    fail(ERR.NA);
  }
  return out;
}, { category: 'Text' });

define('TEXTSPLIT', 2, 4, (a, c) => {
  const s = text(a, 0, c);
  const colDelim = a[1] === null ? null : text(a, 1, c);
  const rowDelim = a.length > 2 && a[2] !== null ? text(a, 2, c) : null;
  const ignoreEmpty = boolOr(a, 3, c, false);
  const rowsText = rowDelim ? s.split(rowDelim) : [s];
  const rows = rowsText.map((line) => {
    const cells = colDelim ? line.split(colDelim) : [line];
    const kept = ignoreEmpty ? cells.filter((x) => x !== '') : cells;
    return kept.map((x) => x as Scalar);
  });
  const width = Math.max(...rows.map((r) => r.length));
  return new ArrayValue(rows.map((r) => [...r, ...Array(width - r.length).fill('')]));
}, { category: 'Text' });

define('VALUETOTEXT', 1, 2, (a, c) => scalarToText(derefSafe(a[0], c)), { category: 'Text' });
