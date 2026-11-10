/** Date & time functions. */

import {
  addMonthsClamped,
  dateToSerial,
  daysInMonth,
  endOfMonth,
  makeSerial,
  serialParts,
  serialToDate,
} from '../dates';
import { ERR, fail, isError } from '../errors';
import { FnContext, Val } from '../values';
import { flattenValuesFrom, num, numOr } from './helpers';
import { define } from './registry';
import { parseDateText } from './text';

function todaySerial(c: FnContext): number {
  const now = c.source.now();
  return makeSerial(now.getFullYear(), now.getMonth() + 1, now.getDate());
}

function nowSerial(c: FnContext): number {
  const now = c.source.now();
  return (
    makeSerial(now.getFullYear(), now.getMonth() + 1, now.getDate()) +
    (now.getHours() * 3600 + now.getMinutes() * 60 + now.getSeconds()) / 86400
  );
}

function holidaySerials(a: Val[], i: number, c: FnContext): number[] {
  if (a.length <= i || a[i] === null) return [];
  const out: number[] = [];
  for (const v of flattenValuesFrom([a[i]], c)) {
    if (isError(v)) continue;
    if (typeof v === 'number') out.push(Math.floor(v));
    else if (typeof v === 'string') {
      const s = parseDateText(v);
      if (s !== null) out.push(Math.floor(s));
    }
  }
  return out;
}

define('DATE', 3, 3, (a, c) => makeSerial(num(a, 0, c), num(a, 1, c), num(a, 2, c)), { category: 'Date' });
define('DATEVALUE', 1, 1, (a, c) => {
  const v = a[0];
  if (typeof v === 'number') return Math.floor(v);
  if (typeof v !== 'string') fail(ERR.VALUE);
  const serial = parseDateText(v);
  if (serial === null) fail(ERR.VALUE);
  return Math.floor(serial);
}, { category: 'Date' });

define('TIME', 3, 3, (a, c) => {
  const h = num(a, 0, c);
  const m = num(a, 1, c);
  const s = num(a, 2, c);
  const total = h * 3600 + m * 60 + s;
  if (total < 0) fail(ERR.NUM);
  const frac = (total % 86400) / 86400;
  return frac;
}, { category: 'Date' });

define('TIMEVALUE', 1, 1, (a, c) => {
  const v = a[0];
  if (typeof v !== 'string') fail(ERR.VALUE);
  const serial = parseDateText(v);
  if (serial === null) fail(ERR.VALUE);
  return serial - Math.floor(serial);
}, { category: 'Date' });

define('TODAY', 0, 0, (_a, c) => todaySerial(c), { category: 'Date', volatile: true });
define('NOW', 0, 0, (_a, c) => nowSerial(c), { category: 'Date', volatile: true });

define('YEAR', 1, 1, (a, c) => serialParts(num(a, 0, c)).year, { category: 'Date' });
define('MONTH', 1, 1, (a, c) => serialParts(num(a, 0, c)).month, { category: 'Date' });
define('DAY', 1, 1, (a, c) => serialParts(num(a, 0, c)).day, { category: 'Date' });
define('HOUR', 1, 1, (a, c) => serialParts(num(a, 0, c)).hour, { category: 'Date' });
define('MINUTE', 1, 1, (a, c) => serialParts(num(a, 0, c)).minute, { category: 'Date' });
define('SECOND', 1, 1, (a, c) => serialParts(num(a, 0, c)).second, { category: 'Date' });

define('WEEKDAY', 1, 2, (a, c) => {
  const serial = Math.floor(num(a, 0, c));
  const type = Math.trunc(numOr(a, 1, c, 1));
  const dow = serialParts(serial).weekday; // 0 = Sunday
  switch (type) {
    case 1:
      return dow + 1;
    case 2:
      return ((dow + 6) % 7) + 1;
    case 3:
      return (dow + 6) % 7;
    case 11:
      return ((dow + 6) % 7) + 1;
    case 12:
      return ((dow + 5) % 7) + 1;
    case 13:
      return ((dow + 4) % 7) + 1;
    case 14:
      return ((dow + 3) % 7) + 1;
    case 15:
      return ((dow + 2) % 7) + 1;
    case 16:
      return ((dow + 1) % 7) + 1;
    case 17:
      return dow + 1;
    default:
      fail(ERR.NUM);
  }
}, { category: 'Date' });

define('WEEKNUM', 1, 2, (a, c) => {
  const serial = Math.floor(num(a, 0, c));
  const type = Math.trunc(numOr(a, 1, c, 1));
  const p = serialParts(serial);
  if (type === 21) {
    // ISO 8601 week number
    const target = serialToDate(serial);
    const day = (target.getUTCDay() + 6) % 7; // Monday = 0
    target.setUTCDate(target.getUTCDate() - day + 3);
    const firstThursday = new Date(Date.UTC(target.getUTCFullYear(), 0, 4));
    const firstDay = (firstThursday.getUTCDay() + 6) % 7;
    firstThursday.setUTCDate(firstThursday.getUTCDate() - firstDay + 3);
    return 1 + Math.round((target.getTime() - firstThursday.getTime()) / (7 * 86400000));
  }
  const startDay = type === 2 ? 1 : 0;
  const jan1 = makeSerial(p.year, 1, 1);
  const jan1Dow = serialParts(jan1).weekday;
  const offset = (jan1Dow - startDay + 7) % 7;
  const dayOfYear = serial - jan1 + 1;
  return Math.floor((dayOfYear + offset - 1) / 7) + 1;
}, { category: 'Date' });

define('DAYS', 2, 2, (a, c) => Math.floor(num(a, 0, c)) - Math.floor(num(a, 1, c)), { category: 'Date' });

define('EDATE', 2, 2, (a, c) => addMonthsClamped(num(a, 0, c), num(a, 1, c)), { category: 'Date' });
define('EOMONTH', 2, 2, (a, c) => endOfMonth(num(a, 0, c), num(a, 1, c)), { category: 'Date' });

define('DATEDIF', 3, 3, (a, c) => {
  const start = Math.floor(num(a, 0, c));
  const end = Math.floor(num(a, 1, c));
  const unit = String(a[2] ?? '').toUpperCase();
  if (end < start) fail(ERR.NUM);
  const s = serialParts(start);
  const e = serialParts(end);
  const yDiff = e.year - s.year;
  const mDiff = e.month - s.month;
  const dDiff = e.day - s.day;
  switch (unit) {
    case 'Y':
      return yDiff - (mDiff < 0 || (mDiff === 0 && dDiff < 0) ? 1 : 0);
    case 'M': {
      let months = yDiff * 12 + mDiff;
      if (dDiff < 0) months--;
      return months;
    }
    case 'D':
      return end - start;
    case 'YM': {
      let months = mDiff;
      if (dDiff < 0) months--;
      if (months < 0) months += 12;
      return months;
    }
    case 'MD': {
      let d = dDiff;
      if (d < 0) {
        const prevMonth = e.month === 1 ? 12 : e.month - 1;
        const prevYear = e.month === 1 ? e.year - 1 : e.year;
        d += daysInMonth(prevYear, prevMonth);
      }
      return d;
    }
    case 'YD': {
      const anchor = makeSerial(e.year - (mDiff < 0 || (mDiff === 0 && dDiff < 0) ? 1 : 0), s.month, s.day);
      return end - anchor;
    }
    default:
      fail(ERR.NUM);
  }
}, { category: 'Date' });

define('DAYS360', 2, 3, (a, c) => {
  const start = Math.floor(num(a, 0, c));
  const end = Math.floor(num(a, 1, c));
  const european = numOr(a, 2, c, 0) !== 0;
  const s = serialParts(start);
  const e = serialParts(end);
  let d1 = s.day;
  let d2 = e.day;
  if (european) {
    d1 = Math.min(d1, 30);
    d2 = Math.min(d2, 30);
  } else {
    const feb1 = s.month === 2 && d1 === daysInMonth(s.year, 2);
    if (feb1) d1 = 30;
    if (d1 === 31) d1 = 30;
    if (d2 === 31) {
      if (d1 >= 30) d2 = 30;
      else d2 = 31;
    }
  }
  return (e.year - s.year) * 360 + (e.month - s.month) * 30 + (d2 - d1);
}, { category: 'Date' });

function weekendMask(code: number): number[] {
  // 1 or omitted: Sat+Sun. 2..7: Sun..Sat pair. 11-17: Sunday alone variants.
  const pairs: Record<number, number[]> = {
    1: [0, 6],
    2: [0, 1],
    3: [1, 2],
    4: [2, 3],
    5: [3, 4],
    6: [4, 5],
    7: [5, 6],
    11: [0],
    12: [1],
    13: [2],
    14: [3],
    15: [4],
    16: [5],
    17: [6],
  };
  return pairs[code] ?? [0, 6];
}

function isWorkday(serial: number, mask: number[], holidays: Set<number>): boolean {
  const dow = serialParts(serial).weekday;
  return !mask.includes(dow) && !holidays.has(Math.floor(serial));
}

define('NETWORKDAYS', 2, 3, (a, c) => {
  const start = Math.floor(num(a, 0, c));
  const end = Math.floor(num(a, 1, c));
  const holidays = new Set(holidaySerials(a, 2, c));
  const mask = [0, 6];
  let count = 0;
  const step = start <= end ? 1 : -1;
  for (let s = start; step > 0 ? s <= end : s >= end; s += step) {
    if (isWorkday(s, mask, holidays)) count++;
  }
  return step > 0 ? count : -count;
}, { category: 'Date' });

define('NETWORKDAYS.INTL', 2, 4, (a, c) => {
  const start = Math.floor(num(a, 0, c));
  const end = Math.floor(num(a, 1, c));
  const mask = weekendMask(Math.trunc(numOr(a, 2, c, 1)));
  const holidays = new Set(holidaySerials(a, 3, c));
  let count = 0;
  const step = start <= end ? 1 : -1;
  for (let s = start; step > 0 ? s <= end : s >= end; s += step) {
    if (isWorkday(s, mask, holidays)) count++;
  }
  return count;
}, { category: 'Date' });

define('WORKDAY', 2, 3, (a, c) => {
  const start = Math.floor(num(a, 0, c));
  const days = Math.trunc(num(a, 1, c));
  const holidays = new Set(holidaySerials(a, 2, c));
  const mask = [0, 6];
  let s = start;
  let remaining = Math.abs(days);
  const step = days >= 0 ? 1 : -1;
  while (remaining > 0) {
    s += step;
    if (isWorkday(s, mask, holidays)) remaining--;
  }
  return s;
}, { category: 'Date' });

define('WORKDAY.INTL', 2, 4, (a, c) => {
  const start = Math.floor(num(a, 0, c));
  const days = Math.trunc(num(a, 1, c));
  const mask = weekendMask(Math.trunc(numOr(a, 2, c, 1)));
  const holidays = new Set(holidaySerials(a, 3, c));
  let s = start;
  let remaining = Math.abs(days);
  const step = days >= 0 ? 1 : -1;
  while (remaining > 0) {
    s += step;
    if (isWorkday(s, mask, holidays)) remaining--;
  }
  return s;
}, { category: 'Date' });

define('YEARFRAC', 2, 3, (a, c) => {
  const start = Math.floor(num(a, 0, c));
  const end = Math.floor(num(a, 1, c));
  const basis = Math.trunc(numOr(a, 2, c, 0));
  if (start > end) {
    return avgYearFrac(end, start, basis);
  }
  return avgYearFrac(start, end, basis);
}, { category: 'Date' });

function avgYearFrac(start: number, end: number, basis: number): number {
  const s = serialParts(start);
  const e = serialParts(end);
  switch (basis) {
    case 0: {
      let d1 = Math.min(s.day, 30);
      let d2 = Math.min(e.day, 30);
      const days360 = (e.year - s.year) * 360 + (e.month - s.month) * 30 + (d2 - d1);
      return days360 / 360;
    }
    case 1: {
      // Actual/actual: average year length across the span
      const years = e.year - s.year + 1;
      let daysInYears = 0;
      for (let y = s.year; y <= e.year; y++) {
        daysInYears += isLeap(y) ? 366 : 365;
      }
      const avg = daysInYears / years;
      return (end - start) / avg;
    }
    case 2:
      return (end - start) / 360;
    case 3:
      return (end - start) / 365;
    case 4: {
      const d1 = Math.min(s.day, 30);
      const d2 = Math.min(e.day, 30);
      return ((e.year - s.year) * 360 + (e.month - s.month) * 30 + (d2 - d1)) / 360;
    }
    default:
      fail(ERR.NUM);
  }
}

function isLeap(y: number): boolean {
  return (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0;
}

export function serialForDate(date: Date): number {
  return dateToSerial(date);
}
