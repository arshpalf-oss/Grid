/**
 * Number/date formatting engine (the custom-format language Excel exposes).
 * Supports the common subset: sections (positive;negative;zero;text), digit
 * placeholders, thousands separators and scaling, percent, scientific, text
 * placeholder, and date/time tokens.
 */

import { serialParts } from './dates';
import { CellError, isError } from './errors';
import { Scalar, numberToText } from './values';

type Tok =
  | { k: 'lit'; t: string }
  | { k: 'digit'; c: '0' | '#' | '?' }
  | { k: 'dot' }
  | { k: 'comma' }
  | { k: 'pct' }
  | { k: 'at' }
  | { k: 'exp' }
  | { k: 'date'; c: 'y' | 'm' | 'd' | 'h' | 's' | 'min' | 'ampm' | 'elapsed'; n: number; unit?: string }
  | { k: 'frac'; n: number };

const MONTHS = [
  'January', 'February', 'March', 'April', 'May', 'June',
  'July', 'August', 'September', 'October', 'November', 'December',
];
const DAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];

/** Split a multi-section format, respecting quoted literals. */
export function splitSections(fmt: string): string[] {
  const out: string[] = [];
  let cur = '';
  let inQuote = false;
  for (let i = 0; i < fmt.length; i++) {
    const ch = fmt[i];
    if (ch === '"') inQuote = !inQuote;
    if (ch === '\\' && !inQuote) {
      cur += ch + (fmt[i + 1] ?? '');
      i++;
      continue;
    }
    if (ch === ';' && !inQuote) {
      out.push(cur);
      cur = '';
      continue;
    }
    cur += ch;
  }
  out.push(cur);
  return out;
}

export function tokenizeFormat(fmt: string): Tok[] {
  const toks: Tok[] = [];
  let i = 0;
  const n = fmt.length;
  while (i < n) {
    const ch = fmt[i];
    if (ch === '"') {
      let text = '';
      i++;
      while (i < n && fmt[i] !== '"') text += fmt[i++];
      i++;
      toks.push({ k: 'lit', t: text });
      continue;
    }
    if (ch === '\\') {
      toks.push({ k: 'lit', t: fmt[i + 1] ?? '' });
      i += 2;
      continue;
    }
    if (ch === '_') {
      toks.push({ k: 'lit', t: ' ' });
      i += 2;
      continue;
    }
    if (ch === '*') {
      i += 2;
      continue;
    }
    if (ch === '[') {
      const end = fmt.indexOf(']', i);
      const inner = end > i ? fmt.slice(i + 1, end) : '';
      i = end > i ? end + 1 : n;
      if (/^[hms]+$/i.test(inner)) {
        toks.push({ k: 'date', c: 'elapsed', n: inner.length, unit: inner[0].toLowerCase() });
      }
      // colors, locale ids and conditions are ignored for rendering
      continue;
    }
    if (ch === '0' || ch === '#' || ch === '?') {
      toks.push({ k: 'digit', c: ch });
      i++;
      continue;
    }
    if (ch === '.') {
      // fractional seconds after a seconds token
      if (/^\.[0]+$/.test(fmt.slice(i, i + 4)) && toks.some((t) => t.k === 'date' && t.c === 's')) {
        let j = i + 1;
        let count = 0;
        while (fmt[j] === '0') {
          count++;
          j++;
        }
        toks.push({ k: 'frac', n: count });
        i = j;
        continue;
      }
      toks.push({ k: 'dot' });
      i++;
      continue;
    }
    if (ch === ',') {
      toks.push({ k: 'comma' });
      i++;
      continue;
    }
    if (ch === '%') {
      toks.push({ k: 'pct' });
      i++;
      continue;
    }
    if (ch === '@') {
      toks.push({ k: 'at' });
      i++;
      continue;
    }
    if ((ch === 'E' || ch === 'e') && (fmt[i + 1] === '+' || fmt[i + 1] === '-')) {
      toks.push({ k: 'exp' });
      i += 2;
      while (fmt[i] === '0' || fmt[i] === '#') i++;
      continue;
    }
    const upper = ch.toUpperCase();
    if (upper === 'A' && /^AM\/PM/i.test(fmt.slice(i, i + 5))) {
      toks.push({ k: 'date', c: 'ampm', n: 1 });
      i += 5;
      continue;
    }
    if (upper === 'A' && /^A\/P/i.test(fmt.slice(i, i + 3))) {
      toks.push({ k: 'date', c: 'ampm', n: 0 });
      i += 3;
      continue;
    }
    if ('ymdhs'.includes(upper.toLowerCase()) && /[ymdhs]/i.test(ch)) {
      let run = 0;
      while (i + run < n && fmt[i + run].toLowerCase() === upper.toLowerCase()) run++;
      toks.push({ k: 'date', c: upper.toLowerCase() as 'y' | 'm' | 'd' | 'h' | 's', n: run });
      i += run;
      continue;
    }
    toks.push({ k: 'lit', t: ch });
    i++;
  }

  // Disambiguate m (month vs minute): an m-run right after an hour token is minutes.
  let sawHour = false;
  for (let idx = 0; idx < toks.length; idx++) {
    const t = toks[idx];
    if (t.k === 'date' && t.c === 'h') sawHour = true;
    else if (t.k === 'date' && (t.c === 'd' || t.c === 'y')) sawHour = false;
    else if (t.k === 'date' && t.c === 'm' && sawHour) toks[idx] = { k: 'date', c: 'min', n: t.n };
  }
  return toks;
}

const isDateToken = (t: Tok) => t.k === 'date' || t.k === 'frac';

export function isDateFormat(fmt: string): boolean {
  const toks = tokenizeFormat(fmt);
  return toks.some(isDateToken) && !hasNumericPlaceholders(toks);
}

function hasNumericPlaceholders(toks: Tok[]): boolean {
  return toks.some((t) => t.k === 'digit' || t.k === 'dot' || t.k === 'exp');
}

function pad(n: number, width: number): string {
  return String(n).padStart(width, '0');
}

function group(intStr: string): string {
  return intStr.replace(/\B(?=(\d{3})+(?!\d))/g, ',');
}

/** Excel's "General" display, trimmed to roughly one default column. */
export function generalDisplay(value: number): string {
  if (!Number.isFinite(value)) return '#NUM!';
  if (value === 0) return '0';
  const abs = Math.abs(value);
  if (Number.isInteger(value) && abs < 1e11) return String(value);
  if (abs >= 1e11) {
    const exp = value.toExponential(5);
    const [m, e] = exp.split('e');
    const mantissa = m.replace(/\.?0+$/, '');
    const sign = e.startsWith('-') ? '-' : '+';
    return `${mantissa}E${sign}${pad(Math.abs(parseInt(e, 10)), 2)}`;
  }
  const intDigits = abs >= 1 ? String(Math.trunc(abs)).length : 0;
  const decimals = Math.max(0, Math.min(10, 10 - intDigits));
  let s = value.toFixed(decimals);
  if (s.includes('.')) s = s.replace(/\.?0+$/, '');
  if (s === '-0') s = '0';
  if (s === '0' && value !== 0) {
    const exp = value.toExponential(5);
    const [m, e] = exp.split('e');
    const mantissa = m.replace(/\.?0+$/, '');
    const sign = e.startsWith('-') ? '-' : '+';
    return `${mantissa}E${sign}${pad(Math.abs(parseInt(e, 10)), 2)}`;
  }
  return s;
}

function renderDate(serial: number, toks: Tok[], ampmStyle: number | null): string {
  const p = serialParts(serial);
  let out = '';
  const isAmPm = toks.some((t) => t.k === 'date' && t.c === 'ampm');
  const totalSeconds = Math.round((serial - Math.floor(serial)) * 86400);
  for (const t of toks) {
    if (t.k === 'lit') {
      out += t.t;
      continue;
    }
    if (t.k === 'digit') {
      out += t.c === '0' ? '0' : '';
      continue;
    }
    if (t.k === 'dot') {
      out += '.';
      continue;
    }
    if (t.k === 'comma') {
      out += ',';
      continue;
    }
    if (t.k === 'pct') {
      out += '%';
      continue;
    }
    if (t.k === 'at') continue;
    if (t.k === 'frac') {
      const ms = Math.round((((serial - Math.floor(serial)) * 86400) % 1) * 1000);
      out += '.' + pad(ms, Math.max(1, t.n));
      continue;
    }
    if (t.k !== 'date') continue;
    switch (t.c) {
      case 'y': {
        if (t.n <= 2) out += pad(p.year % 100, 2);
        else out += String(p.year);
        break;
      }
      case 'm': {
        if (t.n === 1) out += String(p.month);
        else if (t.n === 2) out += pad(p.month, 2);
        else if (t.n === 3) out += MONTHS[p.month - 1].slice(0, 3);
        else if (t.n === 4) out += MONTHS[p.month - 1];
        else out += MONTHS[p.month - 1][0];
        break;
      }
      case 'min': {
        out += t.n === 1 ? String(p.minute) : pad(p.minute, 2);
        break;
      }
      case 'd': {
        if (t.n === 1) out += String(p.day);
        else if (t.n === 2) out += pad(p.day, 2);
        else if (t.n === 3) out += DAYS[p.weekday].slice(0, 3);
        else out += DAYS[p.weekday];
        break;
      }
      case 'h': {
        let hour = p.hour;
        if (isAmPm) {
          hour = hour % 12;
          if (hour === 0) hour = 12;
        }
        out += t.n === 1 ? String(hour) : pad(hour, 2);
        break;
      }
      case 's': {
        out += t.n === 1 ? String(p.second) : pad(p.second, 2);
        break;
      }
      case 'ampm': {
        if (ampmStyle === 0) out += p.hour < 12 ? 'A' : 'P';
        else out += p.hour < 12 ? 'AM' : 'PM';
        break;
      }
      case 'elapsed': {
        const unit = (t.unit ?? 'h').toLowerCase();
        const totalHours = Math.floor(serial * 24);
        const totalMinutes = Math.floor(serial * 1440);
        const totalSecs = Math.floor(serial * 86400);
        void totalSeconds;
        if (unit === 'h') out += t.n === 1 ? String(totalHours) : pad(totalHours, Math.min(t.n, 2));
        else if (unit === 'm') out += t.n === 1 ? String(totalMinutes) : pad(totalMinutes, 2);
        else out += t.n === 1 ? String(totalSecs) : pad(totalSecs, 2);
        break;
      }
      default:
        break;
    }
  }
  return out;
}

function renderNumber(value: number, toks: Tok[]): string {
  const pctCount = toks.filter((t) => t.k === 'pct').length;
  let v = value * Math.pow(100, pctCount);

  // trailing commas scale by thousands
  const lastDigit = [...toks].map((t) => t.k).lastIndexOf('digit');
  let scale = 0;
  toks.forEach((t, idx) => {
    if (t.k === 'comma' && idx > lastDigit) scale++;
  });
  if (scale) v = v / Math.pow(1000, scale);

  const dotIdx = toks.findIndex((t) => t.k === 'dot');
  const intToks = (dotIdx < 0 ? toks : toks.slice(0, dotIdx)).filter((t) => t.k === 'digit') as { c: string }[];
  const fracToks = (dotIdx < 0 ? [] : toks.slice(dotIdx + 1)).filter((t) => t.k === 'digit') as { c: string }[];
  const decimals = fracToks.length;
  const minInt = intToks.filter((t) => t.c === '0').length;
  const hasExp = toks.some((t) => t.k === 'exp');
  const grouping = toks.slice(0, dotIdx < 0 ? toks.length : dotIdx).some((t) => t.k === 'comma') && scale === 0;

  const negative = v < 0;
  let abs = Math.abs(v);

  let intStr: string;
  let fracStr = '';
  if (hasExp) {
    const mantissaDecimals = decimals;
    let exp = abs === 0 ? 0 : Math.floor(Math.log10(abs));
    const mantissa = abs === 0 ? 0 : abs / Math.pow(10, exp);
    intStr = mantissa.toFixed(mantissaDecimals);
    const parts = intStr.split('.');
    intStr = parts[0];
    fracStr = parts[1] ?? '';
    const expSign = exp < 0 ? '-' : '+';
    const expDigits = pad(Math.abs(exp), 2);
    let out = '';
    for (const t of toks) {
      if (t.k === 'exp') out += `E${expSign}${expDigits}`;
      else if (t.k === 'digit' || t.k === 'dot') continue;
      else if (t.k === 'lit') out += t.t;
      else if (t.k === 'pct') out += '%';
    }
    return (negative ? '-' : '') + intStr + (fracStr ? '.' + fracStr : '') + out;
  }

  const rounded = roundForFormat(abs, decimals);
  const fixed = rounded.toFixed(decimals);
  const [ip, fp = ''] = fixed.split('.');
  intStr = ip;
  fracStr = fp;
  if (intStr === '0' && minInt === 0) intStr = '';
  while (intStr.length < minInt) intStr = '0' + intStr;
  if (grouping) intStr = group(intStr);

  let out = '';
  let wroteInt = false;
  let wroteFrac = false;
  for (let idx = 0; idx < toks.length; idx++) {
    const t = toks[idx];
    switch (t.k) {
      case 'lit':
        out += t.t;
        break;
      case 'digit':
        if (dotIdx < 0 || idx < dotIdx) {
          if (!wroteInt) {
            out += intStr;
            wroteInt = true;
          }
        } else if (!wroteFrac) {
          out += fracStr;
          wroteFrac = true;
        }
        break;
      case 'dot':
        if (decimals > 0) out += '.';
        break;
      case 'pct':
        out += '%';
        break;
      case 'comma':
        break;
      case 'at':
        break;
      default:
        break;
    }
  }
  if (!wroteInt && intStr) out += intStr;
  const sign = negative && !out.includes('-') ? '-' : '';
  // A second/third section implies the sign is expressed by the format
  return sign + out;
}

function roundForFormat(x: number, digits: number): number {
  if (!Number.isFinite(x)) return x;
  const shifted = Number(`${x}e${digits}`);
  const rounded = Math.round(shifted);
  return Number(`${rounded}e${-digits}`);
}

function scalarToTextValue(v: Scalar): string {
  if (v === null) return '';
  if (typeof v === 'number') return generalDisplay(v);
  if (typeof v === 'boolean') return v ? 'TRUE' : 'FALSE';
  return v;
}

/** Public entry point. */
export function formatValue(value: Scalar | CellError, fmt?: string | null): string {
  if (isError(value)) return value.code;
  if (value === null) return '';
  if (typeof value === 'boolean') return value ? 'TRUE' : 'FALSE';

  const format = fmt && fmt.trim() !== '' ? fmt : null;
  if (!format || format.trim().toLowerCase() === 'general') {
    return typeof value === 'number' ? generalDisplay(value) : value;
  }

  const sections = splitSections(format);
  const isText = typeof value === 'string';

  if (isText) {
    const textSection = sections.length >= 4 ? sections[3] : null;
    if (textSection !== null) {
      const toks = tokenizeFormat(textSection);
      if (toks.some((t) => t.k === 'at')) {
        return toks.map((t) => (t.k === 'at' ? value : t.k === 'lit' ? t.t : t.k === 'digit' ? '' : '')).join('');
      }
      return toks.map((t) => (t.k === 'lit' ? t.t : '')).join('') || value;
    }
    return value;
  }

  const n = typeof value === 'number' ? value : 0;
  let sectionIdx = 0;
  let useAbs = false;
  if (n < 0) {
    if (sections.length >= 2) {
      sectionIdx = 1;
      useAbs = true;
    }
  } else if (n === 0 && sections.length >= 3) {
    sectionIdx = 2;
    useAbs = true;
  }
  const section = sections[sectionIdx] ?? sections[0];
  if (!section.trim()) return generalDisplay(n);

  const toks = tokenizeFormat(section);
  const ampmToken = toks.find((t) => t.k === 'date' && t.c === 'ampm') as
    | { k: 'date'; c: 'ampm'; n: number; unit?: string }
    | undefined;

  if (toks.some(isDateToken)) {
    return renderDate(useAbs ? Math.abs(n) : n, toks, ampmToken ? ampmToken.n : null);
  }
  if (!hasNumericPlaceholders(toks)) {
    return toks.map((t) => (t.k === 'lit' ? t.t : '')).join('');
  }
  return renderNumber(useAbs ? Math.abs(n) : n, toks);
}

/** Value as displayed in a cell (handles text/number/boolean/blank). */
export function displayScalar(value: Scalar | CellError, fmt?: string | null): string {
  if (isError(value)) return value.code;
  if (value === null) return '';
  if (typeof value === 'number') return formatValue(value, fmt);
  return formatValue(value, fmt);
}

export { numberToText };
