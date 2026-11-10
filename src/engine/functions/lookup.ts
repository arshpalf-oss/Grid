/** Lookup, reference and dynamic-array functions. */

import { a1, indexToCol, MAX_COL_INDEX, MAX_ROW_INDEX } from '../address';
import { CellError, ERR, FormulaError, fail, isError } from '../errors';
import {
  ArrayValue,
  FnContext,
  RangeValue,
  Scalar,
  Val,
  compareScalars,
  deref,
  isArray,
  isRange,
  rectOf,
  toGrid,
  toNumber,
  toText,
} from '../values';
import { arg, intIndex, num, numOr, wildcardToRegExp } from './helpers';
import { define } from './registry';

type Cell = Scalar | CellError;

function vectorOf(v: Val, c: FnContext): { vec: Cell[]; horizontal: boolean } {
  const grid = toGrid(v, c);
  if (!grid.length) fail(ERR.NA);
  if (grid.length === 1) return { vec: grid[0], horizontal: true };
  if (grid[0].length === 1) return { vec: grid.map((r) => r[0]), horizontal: false };
  fail(ERR.NA);
}

function gridDim(grid: Cell[][]): { height: number; width: number } {
  return { height: grid.length, width: grid[0]?.length ?? 0 };
}

function scalarEquals(a: Cell, b: Cell, wildcards = false): boolean {
  if (isError(a) || isError(b)) return false;
  if (typeof a === 'string' && typeof b === 'string') {
    if (wildcards && /[*?]/.test(b)) return wildcardToRegExp(b).test(a);
    return a.toLowerCase() === b.toLowerCase();
  }
  if (a === null || a === '') return b === null || b === '';
  if (b === null || b === '') return false;
  if (typeof a === 'number' && typeof b === 'number') return a === b;
  return a === b;
}

define('ROW', 0, 1, (a, c) => {
  if (!a.length || a[0] === null) return c.row + 1;
  const v = a[0];
  if (isRange(v)) return v.r1 + 1;
  fail(ERR.VALUE);
}, { category: 'Lookup' });

define('COLUMN', 0, 1, (a, c) => {
  if (!a.length || a[0] === null) return c.col + 1;
  const v = a[0];
  if (isRange(v)) return v.c1 + 1;
  fail(ERR.VALUE);
}, { category: 'Lookup' });

define('ROWS', 1, 1, (a, c) => {
  const v = a[0];
  if (isRange(v)) return v.height;
  if (isArray(v)) return v.height;
  return 1;
}, { category: 'Lookup' });

define('COLUMNS', 1, 1, (a, c) => {
  const v = a[0];
  if (isRange(v)) return v.width;
  if (isArray(v)) return v.width;
  return 1;
}, { category: 'Lookup' });

define('AREAS', 1, 1, () => 1, { category: 'Lookup' });

define('INDEX', 2, 4, (a, c) => {
  const grid = toGrid(a[0], c);
  const { height, width } = gridDim(grid);
  const rowNum = Math.trunc(num(a, 1, c));
  const hasCol = a.length > 2 && a[2] !== null;
  const colNum = hasCol ? Math.trunc(num(a, 2, c)) : null;
  if (rowNum < 0 || (colNum !== null && colNum < 0)) fail(ERR.VALUE);

  if (rowNum === 0 && colNum !== null) {
    if (colNum === 0) return new ArrayValue(grid);
    if (colNum > width) fail(ERR.REF);
    return new ArrayValue(grid.map((r) => [r[colNum - 1]]));
  }
  if (colNum === 0) {
    if (rowNum > height) fail(ERR.REF);
    return new ArrayValue([grid[rowNum - 1]]);
  }
  if (colNum === null) {
    if (width === 1) {
      if (rowNum > height) fail(ERR.REF);
      return grid[rowNum - 1][0];
    }
    if (rowNum > height) fail(ERR.REF);
    return new ArrayValue([grid[rowNum - 1]]);
  }
  if (rowNum > height || colNum > width) fail(ERR.REF);
  return grid[rowNum - 1][colNum - 1];
}, { category: 'Lookup' });

define('MATCH', 2, 3, (a, c) => {
  const lookup = deref(a[0], c);
  if (isError(lookup)) throw new FormulaError(lookup);
  const { vec } = vectorOf(a[1], c);
  const matchType = a.length > 2 && a[2] !== null ? Math.trunc(num(a, 2, c)) : 1;

  if (matchType === 0) {
    const wildcards = typeof lookup === 'string' && /[*?]/.test(lookup);
    for (let i = 0; i < vec.length; i++) {
      if (scalarEquals(vec[i], lookup, wildcards)) return i + 1;
    }
    fail(ERR.NA);
  }

  if (matchType > 0) {
    let best = -1;
    for (let i = 0; i < vec.length; i++) {
      const v = vec[i];
      if (isError(v) || v === null) continue;
      if (compareScalars(v, lookup as Scalar) <= 0) best = i;
      else break;
    }
    if (best < 0) fail(ERR.NA);
    return best + 1;
  }

  let best = -1;
  for (let i = 0; i < vec.length; i++) {
    const v = vec[i];
    if (isError(v) || v === null) continue;
    if (compareScalars(v, lookup as Scalar) >= 0) best = i;
    else break;
  }
  if (best < 0) fail(ERR.NA);
  return best + 1;
}, { category: 'Lookup' });

define('VLOOKUP', 3, 4, (a, c) => {
  const lookup = deref(a[0], c);
  if (isError(lookup)) throw new FormulaError(lookup);
  const grid = toGrid(a[1], c);
  const { width } = gridDim(grid);
  const colIndex = Math.trunc(num(a, 2, c));
  const approximate = a.length > 3 && a[3] !== null ? Boolean(numOr(a, 3, c, 1)) : true;
  if (colIndex < 1) fail(ERR.VALUE);
  if (colIndex > width) fail(ERR.REF);

  if (approximate) {
    let best = -1;
    for (let i = 0; i < grid.length; i++) {
      const v = grid[i][0];
      if (isError(v) || v === null) continue;
      if (compareScalars(v, lookup as Scalar) <= 0) best = i;
      else break;
    }
    if (best < 0) fail(ERR.NA);
    return grid[best][colIndex - 1];
  }
  const wildcards = typeof lookup === 'string' && /[*?]/.test(lookup);
  for (let i = 0; i < grid.length; i++) {
    if (scalarEquals(grid[i][0], lookup, wildcards)) return grid[i][colIndex - 1];
  }
  fail(ERR.NA);
}, { category: 'Lookup' });

define('HLOOKUP', 3, 4, (a, c) => {
  const lookup = deref(a[0], c);
  if (isError(lookup)) throw new FormulaError(lookup);
  const grid = toGrid(a[1], c);
  const { height, width } = gridDim(grid);
  const rowIndex = Math.trunc(num(a, 2, c));
  const approximate = a.length > 3 && a[3] !== null ? Boolean(numOr(a, 3, c, 1)) : true;
  if (rowIndex < 1) fail(ERR.VALUE);
  if (rowIndex > height) fail(ERR.REF);
  const header = grid[0];

  if (approximate) {
    let best = -1;
    for (let j = 0; j < width; j++) {
      const v = header[j];
      if (isError(v) || v === null) continue;
      if (compareScalars(v, lookup as Scalar) <= 0) best = j;
      else break;
    }
    if (best < 0) fail(ERR.NA);
    return grid[rowIndex - 1][best];
  }
  const wildcards = typeof lookup === 'string' && /[*?]/.test(lookup);
  for (let j = 0; j < width; j++) {
    if (scalarEquals(header[j], lookup, wildcards)) return grid[rowIndex - 1][j];
  }
  fail(ERR.NA);
}, { category: 'Lookup' });

define('XLOOKUP', 3, 6, (a, c) => {
  const lookup = deref(a[0], c);
  if (isError(lookup)) throw new FormulaError(lookup);
  const lookupVec = vectorOf(a[1], c);
  const returnGrid = toGrid(a[2], c);
  const ifNotFound = a.length > 3 && a[3] !== null ? deref(a[3], c) : undefined;
  const matchMode = a.length > 4 && a[4] !== null ? Math.trunc(num(a, 4, c)) : 0;
  const searchMode = a.length > 5 && a[5] !== null ? Math.trunc(num(a, 5, c)) : 1;

  const n = lookupVec.vec.length;
  const indices: number[] = [];
  if (searchMode === -1 || searchMode === -2) {
    for (let i = n - 1; i >= 0; i--) indices.push(i);
  } else {
    for (let i = 0; i < n; i++) indices.push(i);
  }

  let found = -1;
  const wildcards = matchMode === 2;
  for (const i of indices) {
    if (scalarEquals(lookupVec.vec[i], lookup, wildcards)) {
      found = i;
      break;
    }
  }

  if (found < 0) {
    if (matchMode === -1 || matchMode === 1) {
      // next smaller / next larger
      let best = -1;
      let bestValue: Scalar | null = null;
      for (let i = 0; i < n; i++) {
        const v = lookupVec.vec[i];
        if (isError(v) || v === null) continue;
        const cmp = compareScalars(v, lookup as Scalar);
        if (matchMode === -1 && cmp < 0) {
          if (bestValue === null || compareScalars(v, bestValue) > 0) {
            best = i;
            bestValue = v as Scalar;
          }
        }
        if (matchMode === 1 && cmp > 0) {
          if (bestValue === null || compareScalars(v, bestValue) < 0) {
            best = i;
            bestValue = v as Scalar;
          }
        }
      }
      found = best;
    }
  }

  if (found < 0) {
    if (ifNotFound !== undefined) return ifNotFound;
    fail(ERR.NA);
  }

  const returnHeight = returnGrid.length;
  const returnWidth = returnGrid[0]?.length ?? 0;
  if (lookupVec.horizontal) {
    if (returnGrid.length === 1) return returnGrid[0][found] ?? fail(ERR.REF);
    return new ArrayValue(returnGrid.map((r) => [r[found] ?? ERR.REF]));
  }
  if (returnHeight < n) fail(ERR.VALUE);
  const row = returnGrid[found];
  if (returnWidth === 1) return row[0];
  return new ArrayValue([row]);
}, { category: 'Lookup' });

define('XMATCH', 2, 4, (a, c) => {
  const lookup = deref(a[0], c);
  if (isError(lookup)) throw new FormulaError(lookup);
  const { vec } = vectorOf(a[1], c);
  const matchMode = a.length > 2 && a[2] !== null ? Math.trunc(num(a, 2, c)) : 0;
  const searchMode = a.length > 3 && a[3] !== null ? Math.trunc(num(a, 3, c)) : 1;
  const order: number[] = [];
  if (searchMode === -1 || searchMode === -2) for (let i = vec.length - 1; i >= 0; i--) order.push(i);
  else for (let i = 0; i < vec.length; i++) order.push(i);

  for (const i of order) {
    if (scalarEquals(vec[i], lookup, matchMode === 2)) return i + 1;
  }
  if (matchMode === -1 || matchMode === 1) {
    let best = -1;
    let bestValue: Scalar | null = null;
    for (let i = 0; i < vec.length; i++) {
      const v = vec[i];
      if (isError(v) || v === null) continue;
      const cmp = compareScalars(v, lookup as Scalar);
      if (matchMode === -1 && cmp < 0 && (bestValue === null || compareScalars(v, bestValue) > 0)) {
        best = i;
        bestValue = v as Scalar;
      }
      if (matchMode === 1 && cmp > 0 && (bestValue === null || compareScalars(v, bestValue) < 0)) {
        best = i;
        bestValue = v as Scalar;
      }
    }
    if (best >= 0) return best + 1;
  }
  fail(ERR.NA);
}, { category: 'Lookup' });

define('LOOKUP', 2, 3, (a, c) => {
  const lookup = deref(a[0], c);
  if (isError(lookup)) throw new FormulaError(lookup);
  const { vec } = vectorOf(a[1], c);
  let best = -1;
  for (let i = 0; i < vec.length; i++) {
    const v = vec[i];
    if (isError(v) || v === null) continue;
    if (compareScalars(v, lookup as Scalar) <= 0) best = i;
    else break;
  }
  if (best < 0) fail(ERR.NA);
  if (a.length > 2 && a[2] !== null) {
    const result = vectorOf(a[2], c).vec;
    return result[best] ?? fail(ERR.NA);
  }
  return vec[best];
}, { category: 'Lookup' });

define('OFFSET', 3, 5, (a, c) => {
  const ref = a[0];
  if (!isRange(ref)) fail(ERR.VALUE);
  const rows = Math.trunc(num(a, 1, c));
  const cols = Math.trunc(num(a, 2, c));
  const height = a.length > 3 && a[3] !== null ? Math.trunc(num(a, 3, c)) : ref.height;
  const width = a.length > 4 && a[4] !== null ? Math.trunc(num(a, 4, c)) : ref.width;
  if (height < 1 || width < 1) fail(ERR.REF);
  const r1 = ref.r1 + rows;
  const c1 = ref.c1 + cols;
  if (r1 < 0 || c1 < 0 || r1 + height - 1 > MAX_ROW_INDEX || c1 + width - 1 > MAX_COL_INDEX) fail(ERR.REF);
  return new RangeValue(ref.sheetId, r1, c1, r1 + height - 1, c1 + width - 1);
}, { category: 'Lookup', volatile: true });

define('INDIRECT', 1, 2, (a, c) => {
  const textRef = toText(a[0], c);
  const area = c.parseRefString(textRef);
  if (!area) fail(ERR.REF);
  const sheetId = area.sheet ? c.source.sheetIdByName(area.sheet) : c.sheetId;
  if (!sheetId) fail(ERR.REF);
  const rect = rectOf(area);
  return new RangeValue(sheetId, rect.r1, rect.c1, rect.r2, rect.c2, !!area.wholeCol, !!area.wholeRow);
}, { category: 'Lookup', volatile: true });

define('ADDRESS', 2, 5, (a, c) => {
  const row = Math.trunc(num(a, 0, c));
  const col = Math.trunc(num(a, 1, c));
  const absNum = a.length > 2 && a[2] !== null ? Math.trunc(num(a, 2, c)) : 1;
  const useA1 = a.length > 3 && a[3] !== null ? Boolean(numOr(a, 3, c, 1)) : true;
  const sheet = a.length > 4 && a[4] !== null ? toText(a[4], c) : null;
  if (row < 1 || col < 1 || row > MAX_ROW_INDEX + 1 || col > MAX_COL_INDEX + 1) fail(ERR.VALUE);
  let ref: string;
  if (useA1) {
    const rowAbs = absNum === 1 || absNum === 2;
    const colAbs = absNum === 1 || absNum === 3;
    ref = a1(row - 1, col - 1, rowAbs, colAbs);
  } else {
    const rowAbs = absNum === 1 || absNum === 2 ? `R${row}` : `R[${row}]`;
    const colAbs = absNum === 1 || absNum === 3 ? `C${col}` : `C[${col}]`;
    ref = rowAbs + colAbs;
  }
  if (sheet) {
    const needsQuotes = /[^A-Za-z0-9_]/.test(sheet);
    return `${needsQuotes ? `'${sheet.replace(/'/g, "''")}'` : sheet}!${ref}`;
  }
  return ref;
}, { category: 'Lookup' });

define('TRANSPOSE', 1, 1, (a, c) => {
  const grid = toGrid(a[0], c);
  const { height, width } = gridDim(grid);
  const out: Cell[][] = [];
  for (let j = 0; j < width; j++) {
    const row: Cell[] = [];
    for (let i = 0; i < height; i++) row.push(grid[i][j]);
    out.push(row);
  }
  return new ArrayValue(out);
}, { category: 'Lookup' });

function uniqueKey(row: Cell[]): string {
  return row
    .map((v) => (v === null ? '\u0000' : typeof v === 'string' ? `s:${v.toLowerCase()}` : `${typeof v}:${String(v)}`))
    .join('\u0001');
}

define('UNIQUE', 1, 3, (a, c) => {
  const grid = toGrid(a[0], c);
  const byCol = a.length > 1 && a[1] !== null ? Boolean(numOr(a, 1, c, 0)) : false;
  const exactlyOnce = a.length > 2 && a[2] !== null ? Boolean(numOr(a, 2, c, 0)) : false;

  if (!byCol) {
    const counts = new Map<string, number>();
    for (const row of grid) counts.set(uniqueKey(row), (counts.get(uniqueKey(row)) ?? 0) + 1);
    const seen = new Set<string>();
    const out: Cell[][] = [];
    for (const row of grid) {
      const key = uniqueKey(row);
      if (seen.has(key)) continue;
      seen.add(key);
      if (exactlyOnce && (counts.get(key) ?? 0) !== 1) continue;
      out.push(row);
    }
    return new ArrayValue(out.length ? out : [[null]]);
  }

  const { height, width } = gridDim(grid);
  const colKey = (j: number) => grid.map((r) => r[j]).map((v) => (v === null ? '\u0000' : `${typeof v}:${String(v)}`)).join('\u0001');
  const counts = new Map<string, number>();
  for (let j = 0; j < width; j++) counts.set(colKey(j), (counts.get(colKey(j)) ?? 0) + 1);
  const keep: number[] = [];
  const seen = new Set<string>();
  for (let j = 0; j < width; j++) {
    const key = colKey(j);
    if (seen.has(key)) continue;
    seen.add(key);
    if (exactlyOnce && counts.get(key) !== 1) continue;
    keep.push(j);
  }
  const out: Cell[][] = [];
  for (let i = 0; i < height; i++) out.push(keep.map((j) => grid[i][j]));
  return new ArrayValue(out.length ? out : [[null]]);
}, { category: 'Lookup' });

function compareCells(a: Cell, b: Cell): number {
  const aBlank = a === null || a === '';
  const bBlank = b === null || b === '';
  if (aBlank && bBlank) return 0;
  if (aBlank) return 1;
  if (bBlank) return -1;
  if (isError(a) && isError(b)) return 0;
  if (isError(a)) return 1;
  if (isError(b)) return -1;
  return compareScalars(a as Scalar, b as Scalar);
}

define('SORT', 1, 4, (a, c) => {
  const grid = toGrid(a[0], c);
  const sortIndex = a.length > 1 && a[1] !== null ? Math.trunc(num(a, 1, c)) : 1;
  const order = a.length > 2 && a[2] !== null ? Math.trunc(num(a, 2, c)) : 1;
  const byCol = a.length > 3 && a[3] !== null ? Boolean(numOr(a, 3, c, 0)) : false;
  const dir = order < 0 ? -1 : 1;

  if (!byCol) {
    const { width } = gridDim(grid);
    const idx = Math.min(Math.max(1, sortIndex), Math.max(1, width)) - 1;
    const sorted = [...grid].sort((x, y) => dir * compareCells(x[idx], y[idx]));
    return new ArrayValue(sorted);
  }
  const { height, width } = gridDim(grid);
  const idx = Math.min(Math.max(1, sortIndex), Math.max(1, height)) - 1;
  const cols: Cell[][] = [];
  for (let j = 0; j < width; j++) cols.push(grid.map((r) => r[j]));
  cols.sort((x, y) => dir * compareCells(x[idx], y[idx]));
  const out: Cell[][] = [];
  for (let i = 0; i < height; i++) out.push(cols.map((col) => col[i]));
  return new ArrayValue(out);
}, { category: 'Lookup' });

define('SORTBY', 2, 255, (a, c) => {
  const grid = toGrid(a[0], c);
  const { height } = gridDim(grid);
  const keys: { vec: Cell[]; dir: number }[] = [];
  for (let i = 1; i + 0 < a.length; i += 2) {
    if (!a[i]) continue;
    const { vec } = vectorOf(a[i], c);
    const dir = a.length > i + 1 && a[i + 1] !== null ? (numOr(a, i + 1, c, 1) < 0 ? -1 : 1) : 1;
    keys.push({ vec, dir });
  }
  if (!keys.length) fail(ERR.VALUE);
  const indices = Array.from({ length: height }, (_, i) => i);
  indices.sort((x, y) => {
    for (const k of keys) {
      const cmp = compareCells(k.vec[x] ?? null, k.vec[y] ?? null);
      if (cmp !== 0) return k.dir * cmp;
    }
    return 0;
  });
  return new ArrayValue(indices.map((i) => grid[i]));
}, { category: 'Lookup' });

function truthy(v: Cell): boolean {
  if (isError(v)) throw new FormulaError(v);
  if (v === null) return false;
  if (typeof v === 'boolean') return v;
  if (typeof v === 'number') return v !== 0;
  return v.toUpperCase() === 'TRUE';
}

define('FILTER', 2, 3, (a, c) => {
  const grid = toGrid(a[0], c);
  const { height, width } = gridDim(grid);
  const include = toGrid(a[1], c);
  const byRow = include.length > 1 || (include.length === 1 && include[0].length === 1 && height > 1);
  const ifEmpty = a.length > 2 && a[2] !== null ? deref(a[2], c) : ERR.CALC;

  if (!byRow) {
    const flags = include[0] ?? [];
    const rows = grid.filter((_r, i) => truthy(flags[i] ?? false));
    if (!rows.length) {
      if (ifEmpty === ERR.CALC) fail(ERR.CALC);
      return ifEmpty;
    }
    return new ArrayValue(rows);
  }

  const flags = include.map((r) => truthy(r[0]));
  if (flags.length !== height) fail(ERR.VALUE);
  const out = grid.filter((_r, i) => flags[i]);
  if (!out.length) {
    if (ifEmpty === ERR.CALC) fail(ERR.CALC);
    return ifEmpty;
  }
  return new ArrayValue(out);
}, { category: 'Lookup' });

define('SEQUENCE', 1, 4, (a, c) => {
  const rows = Math.trunc(numOr(a, 0, c, 1));
  const cols = Math.trunc(numOr(a, 1, c, 1));
  const start = numOr(a, 2, c, 1);
  const step = numOr(a, 3, c, 1);
  if (rows < 1 || cols < 1 || rows * cols > 1_000_000) fail(ERR.VALUE);
  const out: Cell[][] = [];
  let value = start;
  for (let i = 0; i < rows; i++) {
    const row: Cell[] = [];
    for (let j = 0; j < cols; j++) {
      row.push(value);
      value += step;
    }
    out.push(row);
  }
  return new ArrayValue(out);
}, { category: 'Lookup' });

define('TAKE', 1, 3, (a, c) => {
  const grid = toGrid(a[0], c);
  const { height, width } = gridDim(grid);
  let rows = a.length > 1 && a[1] !== null ? Math.trunc(num(a, 1, c)) : height;
  let cols = a.length > 2 && a[2] !== null ? Math.trunc(num(a, 2, c)) : width;
  const takeRows = rows >= 0 ? grid.slice(0, rows) : grid.slice(rows);
  const finalRows = takeRows.map((r) => (cols >= 0 ? r.slice(0, cols) : r.slice(cols)));
  return new ArrayValue(finalRows.length ? finalRows : [[null]]);
}, { category: 'Lookup' });

define('DROP', 1, 3, (a, c) => {
  const grid = toGrid(a[0], c);
  const { height, width } = gridDim(grid);
  let rows = a.length > 1 && a[1] !== null ? Math.trunc(num(a, 1, c)) : 0;
  let cols = a.length > 2 && a[2] !== null ? Math.trunc(num(a, 2, c)) : 0;
  const slicedRows = rows >= 0 ? grid.slice(rows) : grid.slice(0, rows);
  const out = slicedRows.map((r) => (cols >= 0 ? r.slice(cols) : r.slice(0, cols)));
  return new ArrayValue(out.length ? out : [[null]]);
}, { category: 'Lookup' });

define('CHOOSEROWS', 2, 255, (a, c) => {
  const grid = toGrid(a[0], c);
  const { height } = gridDim(grid);
  const out: Cell[][] = [];
  for (let i = 1; i < a.length; i++) {
    if (a[i] === null) continue;
    const n = Math.trunc(num(a, i, c));
    const idx = n > 0 ? n - 1 : height + n;
    if (idx < 0 || idx >= height) fail(ERR.VALUE);
    out.push(grid[idx]);
  }
  return new ArrayValue(out);
}, { category: 'Lookup' });

define('CHOOSECOLS', 2, 255, (a, c) => {
  const grid = toGrid(a[0], c);
  const { width } = gridDim(grid);
  const idxs: number[] = [];
  for (let i = 1; i < a.length; i++) {
    if (a[i] === null) continue;
    const n = Math.trunc(num(a, i, c));
    const idx = n > 0 ? n - 1 : width + n;
    if (idx < 0 || idx >= width) fail(ERR.VALUE);
    idxs.push(idx);
  }
  return new ArrayValue(grid.map((r) => idxs.map((j) => r[j])));
}, { category: 'Lookup' });

define('HSTACK', 1, 255, (a, c) => {
  const grids = a.map((v) => toGrid(v, c));
  const height = Math.max(...grids.map((g) => g.length));
  const out: Cell[][] = [];
  for (let i = 0; i < height; i++) {
    const row: Cell[] = [];
    for (const g of grids) {
      const w = g[0]?.length ?? 0;
      if (i < g.length) row.push(...g[i]);
      else row.push(...Array(w).fill(ERR.NA));
    }
    out.push(row);
  }
  return new ArrayValue(out);
}, { category: 'Lookup' });

define('VSTACK', 1, 255, (a, c) => {
  const grids = a.map((v) => toGrid(v, c));
  const width = Math.max(...grids.map((g) => g[0]?.length ?? 0));
  const out: Cell[][] = [];
  for (const g of grids) {
    const w = g[0]?.length ?? 0;
    for (const row of g) out.push([...row, ...Array(width - w).fill(ERR.NA)]);
  }
  return new ArrayValue(out);
}, { category: 'Lookup' });

void arg;
void intIndex;
void indexToCol;
void toNumber;
