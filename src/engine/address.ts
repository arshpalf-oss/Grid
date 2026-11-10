/** A1-notation addressing, ranges and coordinate helpers (0-based internally). */

export const MAX_ROW_INDEX = 1_048_575; // Excel: 1,048,576 rows
export const MAX_COL_INDEX = 16_383; // Excel: 16,384 columns (XFD)
export const MAX_ROWS = MAX_ROW_INDEX + 1;
export const MAX_COLS = MAX_COL_INDEX + 1;

export interface CellAddr {
  row: number;
  col: number;
  rowAbs: boolean;
  colAbs: boolean;
}

/** A rectangular reference, optionally qualified with a sheet name. */
export interface AreaRef {
  sheet?: string;
  /** Set for 3-D references such as Sheet1:Sheet3!A1 */
  sheetEnd?: string;
  start: CellAddr;
  end: CellAddr;
  /** A:A style */
  wholeCol?: boolean;
  /** 1:1 style */
  wholeRow?: boolean;
}

export function colToIndex(letters: string): number {
  let n = 0;
  const s = letters.toUpperCase();
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i) - 64;
    if (c < 1 || c > 26) return -1;
    n = n * 26 + c;
  }
  return n - 1;
}

export function indexToCol(index: number): string {
  let n = index + 1;
  let s = '';
  while (n > 0) {
    const rem = (n - 1) % 26;
    s = String.fromCharCode(65 + rem) + s;
    n = Math.floor((n - 1) / 26);
  }
  return s;
}

export function a1(row: number, col: number, rowAbs = false, colAbs = false): string {
  return `${colAbs ? '$' : ''}${indexToCol(col)}${rowAbs ? '$' : ''}${row + 1}`;
}

export function rangeA1(r1: number, c1: number, r2: number, c2: number): string {
  const start = a1(r1, c1);
  const end = a1(r2, c2);
  return start === end ? start : `${start}:${end}`;
}

const CELL_RE = /^\$?([A-Za-z]{1,3})\$?(\d{1,7})$/;
const COL_RE = /^\$?([A-Za-z]{1,3})$/;
const ROW_RE = /^\$?(\d{1,7})$/;

export function parseCellAddr(text: string): CellAddr | null {
  const m = CELL_RE.exec(text.trim());
  if (!m) return null;
  const col = colToIndex(m[1]);
  const row = parseInt(m[2], 10) - 1;
  if (col < 0 || col > MAX_COL_INDEX || row < 0 || row > MAX_ROW_INDEX) return null;
  return {
    row,
    col,
    rowAbs: text.includes('$') && /\$\d/.test(text),
    colAbs: text.trim().startsWith('$'),
  };
}

export function parseWholeCol(text: string): number | null {
  const m = COL_RE.exec(text.trim());
  if (!m) return null;
  const col = colToIndex(m[1]);
  return col < 0 || col > MAX_COL_INDEX ? null : col;
}

export function parseWholeRow(text: string): number | null {
  const m = ROW_RE.exec(text.trim());
  if (!m) return null;
  const row = parseInt(m[1], 10) - 1;
  return row < 0 || row > MAX_ROW_INDEX ? null : row;
}

export function cellAddr(row: number, col: number): CellAddr {
  return { row, col, rowAbs: false, colAbs: false };
}

export function sameArea(a: AreaRef, b: AreaRef): boolean {
  return (
    a.sheet === b.sheet &&
    a.start.row === b.start.row &&
    a.start.col === b.start.col &&
    a.end.row === b.end.row &&
    a.end.col === b.end.col
  );
}

export function areaContains(a: AreaRef, row: number, col: number): boolean {
  return (
    row >= Math.min(a.start.row, a.end.row) &&
    row <= Math.max(a.start.row, a.end.row) &&
    col >= Math.min(a.start.col, a.end.col) &&
    col <= Math.max(a.start.col, a.end.col)
  );
}

export interface Rect {
  r1: number;
  c1: number;
  r2: number;
  c2: number;
}

export function normalizeRect(r1: number, c1: number, r2: number, c2: number): Rect {
  return {
    r1: Math.min(r1, r2),
    c1: Math.min(c1, c2),
    r2: Math.max(r1, r2),
    c2: Math.max(c1, c2),
  };
}

/** Compact numeric key for a cell within a sheet. */
export function cellKey(row: number, col: number): number {
  return row * MAX_COLS + col;
}

export function keyToRC(key: number): { row: number; col: number } {
  return { row: Math.floor(key / MAX_COLS), col: key % MAX_COLS };
}

/** Parse "A1", "A1:B4", "A:A", "1:3" without a sheet prefix. */
export function parseArea(text: string): AreaRef | null {
  const parts = text.split(':');
  if (parts.length === 1) {
    const cell = parseCellAddr(parts[0]);
    if (!cell) return null;
    return { start: cell, end: { ...cell } };
  }
  if (parts.length !== 2) return null;
  const a = parts[0].trim();
  const b = parts[1].trim();
  const ca = parseCellAddr(a);
  const cb = parseCellAddr(b);
  if (ca && cb) return { start: ca, end: cb };
  const colA = parseWholeCol(a);
  const colB = parseWholeCol(b);
  if (colA !== null && colB !== null) {
    const s = cellAddr(0, colA);
    const e = cellAddr(MAX_ROW_INDEX, colB);
    return { start: s, end: e, wholeCol: true };
  }
  const rowA = parseWholeRow(a);
  const rowB = parseWholeRow(b);
  if (rowA !== null && rowB !== null) {
    const s = cellAddr(rowA, 0);
    const e = cellAddr(rowB, MAX_COL_INDEX);
    return { start: s, end: e, wholeRow: true };
  }
  return null;
}

/**
 * Shift a relative reference by a delta (used when filling/copying formulas).
 * Absolute components ($A$1) stay fixed.
 */
export function shiftArea(area: AreaRef, dRow: number, dCol: number): AreaRef {
  const shiftAddr = (a: CellAddr): CellAddr => ({
    row: Math.max(0, Math.min(MAX_ROW_INDEX, a.row + (a.rowAbs ? 0 : dRow))),
    col: Math.max(0, Math.min(MAX_COL_INDEX, a.col + (a.colAbs ? 0 : dCol))),
    rowAbs: a.rowAbs,
    colAbs: a.colAbs,
  });
  return {
    ...area,
    start: shiftAddr(area.start),
    end: shiftAddr(area.end),
  };
}

/** Rewrite a formula's relative references for a copy/fill offset. */
export function shiftFormula(formula: string, dRow: number, dCol: number): string {
  if (!formula.startsWith('=')) return formula;
  // Token-safe rewrite: only touch A1-style references outside of strings.
  let out = '';
  let i = 0;
  const s = formula;
  while (i < s.length) {
    const ch = s[i];
    if (ch === '"') {
      out += ch;
      i++;
      while (i < s.length) {
        out += s[i];
        if (s[i] === '"' && s[i + 1] === '"') {
          out += s[i + 1];
          i += 2;
          continue;
        }
        if (s[i] === '"') {
          i++;
          break;
        }
        i++;
      }
      continue;
    }
    if (ch === "'") {
      out += ch;
      i++;
      while (i < s.length) {
        out += s[i];
        if (s[i] === "'" && s[i + 1] === "'") {
          out += s[i + 1];
          i += 2;
          continue;
        }
        if (s[i] === "'") {
          i++;
          break;
        }
        i++;
      }
      continue;
    }
    if (/[A-Za-z$]/.test(ch)) {
      const start = i;
      while (i < s.length && /[A-Za-z0-9_.$]/.test(s[i])) i++;
      const word = s.slice(start, i);
      const before = start > 0 ? s[start - 1] : '';
      const after = i < s.length ? s[i] : '';
      const isSheetPrefix = after === '!' || before === '!' || before === ':';
      const isFunc = after === '(';
      const addr = !isSheetPrefix && !isFunc ? parseCellAddr(word) : null;
      if (addr) {
        out += a1(
          Math.max(0, Math.min(MAX_ROW_INDEX, addr.row + (addr.rowAbs ? 0 : dRow))),
          Math.max(0, Math.min(MAX_COL_INDEX, addr.col + (addr.colAbs ? 0 : dCol))),
          addr.rowAbs,
          addr.colAbs
        );
      } else {
        out += word;
      }
      continue;
    }
    out += ch;
    i++;
  }
  return out;
}
