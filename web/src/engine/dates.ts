/**
 * Excel date serials.
 *
 * Serial 1 = 1900-01-01. Excel deliberately treats 1900 as a leap year (Lotus
 * 1-2-3 compatibility), so serial 60 is the non-existent 1900-02-29. Serials
 * below 61 are therefore one day lower than a pure day count.
 */

export const MS_PER_DAY = 86_400_000;
const EPOCH = Date.UTC(1899, 11, 30); // serial 0 in a pure day count

export function dateToSerial(date: Date): number {
  const utc = Date.UTC(
    date.getUTCFullYear(),
    date.getUTCMonth(),
    date.getUTCDate(),
    date.getUTCHours(),
    date.getUTCMinutes(),
    date.getUTCSeconds(),
    date.getUTCMilliseconds()
  );
  const days = (utc - EPOCH) / MS_PER_DAY;
  return days < 61 ? days - 1 : days;
}

export function serialToDate(serial: number): Date {
  const ms = serial < 61 ? serial + 1 : serial;
  return new Date(EPOCH + ms * MS_PER_DAY);
}

export interface DateParts {
  year: number;
  month: number; // 1-12
  day: number;
  hour: number;
  minute: number;
  second: number;
  weekday: number; // 0 = Sunday
}

export function serialParts(serial: number): DateParts {
  const whole = Math.floor(serial);
  const frac = serial - whole;
  const d = serialToDate(whole);
  let totalSeconds = Math.round(frac * 86400);
  if (totalSeconds >= 86400) totalSeconds = 86399;
  const hour = Math.floor(totalSeconds / 3600);
  const minute = Math.floor((totalSeconds % 3600) / 60);
  const second = totalSeconds % 60;
  return {
    year: d.getUTCFullYear(),
    month: d.getUTCMonth() + 1,
    day: d.getUTCDate(),
    hour,
    minute,
    second,
    weekday: d.getUTCDay(),
  };
}

/** Build a serial from calendar parts, letting months/days overflow like Excel. */
export function makeSerial(year: number, month: number, day: number, hour = 0, minute = 0, second = 0): number {
  // Excel maps years 0-1899 into 1900-3799
  let y = Math.trunc(year);
  if (y >= 0 && y < 1900) y += 1900;
  const ms = Date.UTC(y, Math.trunc(month) - 1, Math.trunc(day), hour, minute, second);
  return dateToSerial(new Date(ms));
}

export function daysInMonth(year: number, month: number): number {
  return new Date(Date.UTC(year, month, 0)).getUTCDate();
}

export function addMonthsClamped(serial: number, months: number): number {
  const p = serialParts(serial);
  const totalMonths = (p.year * 12 + (p.month - 1)) + Math.trunc(months);
  const year = Math.floor(totalMonths / 12);
  const month = (totalMonths % 12) + 1;
  const day = Math.min(p.day, daysInMonth(year, month));
  return makeSerial(year, month, day);
}

export function endOfMonth(serial: number, months: number): number {
  const p = serialParts(serial);
  const totalMonths = (p.year * 12 + (p.month - 1)) + Math.trunc(months);
  const year = Math.floor(totalMonths / 12);
  const month = (totalMonths % 12) + 1;
  return makeSerial(year, month, daysInMonth(year, month));
}

export function isWeekend(serial: number, weekendDays: number[]): boolean {
  return weekendDays.includes(serialParts(serial).weekday);
}
