/** Information functions. */

import { a1 } from '../address';
import { CellError, ERR, isError } from '../errors';
import { Val, isArray, isRange, toNumber } from '../values';
import { define } from './registry';

const first = (a: Val[]): Val => (a.length ? a[0] : null);

define('ISBLANK', 1, 1, (a) => {
  const v = first(a);
  if (isRange(v)) return v.isSingle === false ? false : false;
  return v === null;
}, { category: 'Information', toleratesErrors: true });

define('ISNUMBER', 1, 1, (a) => typeof first(a) === 'number', { category: 'Information', toleratesErrors: true });
define('ISTEXT', 1, 1, (a) => typeof first(a) === 'string', { category: 'Information', toleratesErrors: true });
define('ISNONTEXT', 1, 1, (a) => typeof first(a) !== 'string', { category: 'Information', toleratesErrors: true });
define('ISLOGICAL', 1, 1, (a) => typeof first(a) === 'boolean', { category: 'Information', toleratesErrors: true });
define('ISREF', 1, 1, (a) => isRange(first(a)), { category: 'Information', toleratesErrors: true });

define('ISERROR', 1, 1, (a) => isError(first(a)), { category: 'Information', toleratesErrors: true });
define('ISERR', 1, 1, (a) => {
  const v = first(a);
  return isError(v) && v.code !== '#N/A';
}, { category: 'Information', toleratesErrors: true });
define('ISNA', 1, 1, (a) => {
  const v = first(a);
  return isError(v) && v.code === '#N/A';
}, { category: 'Information', toleratesErrors: true });

define('ISEVEN', 1, 1, (a, c) => Math.abs(Math.trunc(toNumber(first(a), c))) % 2 === 0, { category: 'Information' });
define('ISODD', 1, 1, (a, c) => Math.abs(Math.trunc(toNumber(first(a), c))) % 2 === 1, { category: 'Information' });

define('NA', 0, 0, () => ERR.NA, { category: 'Information', toleratesErrors: true });

define('ERROR.TYPE', 1, 1, (a) => {
  const v = first(a);
  if (!isError(v)) return ERR.NA;
  const map: Record<string, number> = {
    '#NULL!': 1,
    '#DIV/0!': 2,
    '#VALUE!': 3,
    '#REF!': 4,
    '#NAME?': 5,
    '#NUM!': 6,
    '#N/A': 7,
    '#SPILL!': 9,
  };
  return map[v.code] ?? ERR.NA;
}, { category: 'Information', toleratesErrors: true });

define('TYPE', 1, 1, (a) => {
  const v = first(a);
  if (isRange(v) || isArray(v)) return 64;
  if (isError(v)) return 16;
  if (typeof v === 'number') return 1;
  if (typeof v === 'string') return 2;
  if (typeof v === 'boolean') return 4;
  return 1;
}, { category: 'Information' });

define('SHEET', 0, 1, () => 1, { category: 'Information' });

define('SHEETS', 0, 1, (_a, c) => {
  // The workbook exposes its sheet count through sheet lookup failures; a
  // dedicated accessor keeps this simple.
  return (c.source as unknown as { sheetCount?: number }).sheetCount ?? 1;
}, { category: 'Information' });

define('CELL', 1, 2, (a, c) => {
  const infoType = String(a[0] ?? '').toLowerCase();
  const ref = a.length > 1 ? a[1] : null;
  if (isRange(ref)) {
    switch (infoType) {
      case 'address':
        return `$${a1(ref.r1, ref.c1).replace(/(\d+)/, '$$$1')}`;
      case 'row':
        return ref.r1 + 1;
      case 'col':
        return ref.c1 + 1;
      case 'width':
        return 8.43;
      case 'filename':
        return '';
      case 'contents':
        return c.source.getScalar(ref.sheetId, ref.r1, ref.c1);
      case 'type': {
        const v = c.source.getScalar(ref.sheetId, ref.r1, ref.c1);
        if (v === null) return 'b';
        if (typeof v === 'number') return 'v';
        if (typeof v === 'string') return 'l';
        return 'v';
      }
      default:
        return ERR.VALUE;
    }
  }
  switch (infoType) {
    case 'address':
      return `$${a1(c.row, c.col).replace(/(\d+)/, '$$$1')}`;
    case 'row':
      return c.row + 1;
    case 'col':
      return c.col + 1;
    case 'width':
      return 8.43;
    case 'filename':
      return '';
    default:
      return ERR.VALUE;
  }
}, { category: 'Information' });

define('INFO', 1, 1, (a) => {
  const what = String(a[0] ?? '').toLowerCase();
  switch (what) {
    case 'numfile':
      return 1;
    case 'release':
      return '16.0';
    case 'system':
      return 'pcdos';
    default:
      return ERR.VALUE;
  }
}, { category: 'Information' });

void (0 as unknown as CellError);
