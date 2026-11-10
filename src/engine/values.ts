/**
 * Value model and Excel coercion rules.
 *
 * Values are: blank (null), number, text, boolean, error, range reference, or
 * array. Coercions follow Excel: blank -> 0 / "" / FALSE, text-numbers coerce,
 * "TRUE"/"FALSE" coerce, and a multi-cell range in scalar position is #VALUE!.
 */

import { AreaRef, MAX_COL_INDEX, MAX_ROW_INDEX, Rect, normalizeRect } from './address';
import { CellError, ERR, FormulaError, fail, isError } from './errors';
import type { Node } from './parser';

export type Scalar = number | string | boolean | null;

export class RangeValue {
  readonly isRange = true;
  constructor(
    readonly sheetId: string,
    readonly r1: number,
    readonly c1: number,
    readonly r2: number,
    readonly c2: number,
    readonly openRows: boolean = false,
    readonly openCols: boolean = false
  ) {}
  get height(): number {
    return this.r2 - this.r1 + 1;
  }
  get width(): number {
    return this.c2 - this.c1 + 1;
  }
  get isSingle(): boolean {
    return this.r1 === this.r2 && this.c1 === this.c2;
  }
}

export class ArrayValue {
  readonly isArray = true;
  constructor(readonly rows: (Scalar | CellError)[][]) {}
  get height(): number {
    return this.rows.length;
  }
  get width(): number {
    return this.rows[0]?.length ?? 0;
  }
  get isSingle(): boolean {
    return this.height === 1 && this.width === 1;
  }
  at(r: number, c: number): Scalar | CellError {
    return this.rows[r]?.[c] ?? null;
  }
}

export type Val = Scalar | CellError | RangeValue | ArrayValue;

export const isRange = (v: Val): v is RangeValue => v instanceof RangeValue;
export const isArray = (v: Val): v is ArrayValue => v instanceof ArrayValue;

/** What the evaluator needs from a workbook. */
export interface ValueSource {
  sheetIdByName(name: string): string | null;
  getScalar(sheetId: string, row: number, col: number): Scalar | CellError;
  usedBounds(sheetId: string): Rect | null;
  definedName(name: string): { sheetId: string; formula: string } | null;
  /** Current date; injectable so tests are deterministic. */
  now(): Date;
}

export interface FnContext {
  source: ValueSource;
  sheetId: string;
  row: number;
  col: number;
  vars: Map<string, Val>;
  parseRefString(text: string): AreaRef | null;
  evalNode(node: Node): Val;
}

export function rectOf(area: AreaRef): Rect {
  return normalizeRect(area.start.row, area.start.col, area.end.row, area.end.col);
}

export function rangeFromArea(area: AreaRef, sheetId: string): RangeValue {
  const rect = rectOf(area);
  return new RangeValue(sheetId, rect.r1, rect.c1, rect.r2, rect.c2, !!area.wholeCol, !!area.wholeRow);
}

/** Clamp a reference to the sheet's populated area when it is open-ended. */
export function clampRange(range: RangeValue, ctx: FnContext): RangeValue {
  if (!range.openRows && !range.openCols) return range;
  const used = ctx.source.usedBounds(range.sheetId);
  if (!used) {
    const r = range.openRows ? Math.min(range.r1, range.r2) : range.r1;
    const r2 = range.openRows ? r : range.r2;
    const c = range.openCols ? Math.min(range.c1, range.c2) : range.c1;
    const c2 = range.openCols ? c : range.c2;
    return new RangeValue(range.sheetId, r, c, r2, c2);
  }
  return new RangeValue(
    range.sheetId,
    range.openRows ? Math.min(range.r1, Math.max(used.r1, 0)) : range.r1,
    range.openCols ? Math.min(range.c1, used.c1) : range.c1,
    range.openRows ? Math.max(range.r2, used.r2) : range.r2,
    range.openCols ? Math.max(range.c2, used.c2) : range.c2
  );
}

export function* iterateRange(
  range: RangeValue,
  ctx: FnContext
): Generator<{ row: number; col: number; value: Scalar | CellError }> {
  const r = clampRange(range, ctx);
  const height = r.r2 - r.r1 + 1;
  const width = r.c2 - r.c1 + 1;
  if (height * width > 4_000_000) fail(ERR.NUM);
  for (let row = r.r1; row <= r.r2; row++) {
    for (let col = r.c1; col <= r.c2; col++) {
      yield { row, col, value: ctx.source.getScalar(r.sheetId, row, col) };
    }
  }
}

/** Excel's cross-type ordering: numbers < text < logicals; errors propagate. */
export function typeRank(v: Scalar): number {
  if (v === null) return 0;
  if (typeof v === 'number') return 1;
  if (typeof v === 'string') return 2;
  if (typeof v === 'boolean') return 3;
  return 4;
}

/** Parse text the way Excel's Value() does, for scalar coercion. */
export function parseNumberText(input: string): number | null {
  let s = input.trim();
  if (s === '') return null;
  let negative = false;
  if (/^\(.*\)$/.test(s)) {
    negative = true;
    s = s.slice(1, -1).trim();
  }
  let percent = 0;
  while (s.endsWith('%')) {
    percent++;
    s = s.slice(0, -1).trim();
  }
  s = s.replace(/^[$€£¥₹]\s?/, '');
  if (s.includes(',')) {
    if (!/^\d{1,3}(,\d{3})*(\.\d+)?$/.test(s.replace(/^[-+]/, ''))) return null;
    s = s.replace(/,/g, '');
  }
  if (!/^[-+]?(\d+(\.\d*)?|\.\d+)([eE][-+]?\d+)?$/.test(s)) return null;
  let value = parseFloat(s);
  if (Number.isNaN(value)) return null;
  if (percent) value = value / Math.pow(100, percent);
  return negative ? -Math.abs(value) : value;
}

/** Excel's General number -> text, 15 significant digits, no float noise. */
export function numberToText(n: number): string {
  if (Number.isNaN(n)) return '#NUM!';
  if (!Number.isFinite(n)) return n > 0 ? '#NUM!' : '#NUM!';
  if (n === 0) return '0';
  const abs = Math.abs(n);
  if (abs >= 1e15 || abs < 1e-9) {
    const exp = n.toExponential(10);
    const [mantissa, e] = exp.split('e');
    const m = mantissa.replace(/\.?0+$/, '');
    const sign = e.startsWith('-') ? '-' : '+';
    const digits = String(Math.abs(parseInt(e, 10))).padStart(2, '0');
    return `${m}E${sign}${digits}`;
  }
  let s = n.toPrecision(15);
  if (s.includes('.')) s = s.replace(/\.?0+$/, '');
  if (s === '-0') s = '0';
  return s;
}

export function deref(v: Val, ctx: FnContext): Scalar | CellError {
  if (isRange(v)) {
    if (v.isSingle) return ctx.source.getScalar(v.sheetId, v.r1, v.c1);
    fail(ERR.VALUE);
  }
  if (isArray(v)) {
    if (v.isSingle) return v.rows[0][0];
    fail(ERR.VALUE);
  }
  return v as Scalar | CellError;
}

export function toNumber(v: Val, ctx: FnContext): number {
  const d = deref(v, ctx);
  if (isError(d)) throw new FormulaError(d);
  if (d === null) return 0;
  if (typeof d === 'number') return d;
  if (typeof d === 'boolean') return d ? 1 : 0;
  const parsed = parseNumberText(d);
  if (parsed === null) fail(ERR.VALUE);
  return parsed;
}

export function toText(v: Val, ctx: FnContext): string {
  const d = deref(v, ctx);
  if (isError(d)) throw new FormulaError(d);
  if (d === null) return '';
  if (typeof d === 'string') return d;
  if (typeof d === 'number') return numberToText(d);
  return d ? 'TRUE' : 'FALSE';
}

export function toBool(v: Val, ctx: FnContext): boolean {
  const d = deref(v, ctx);
  if (isError(d)) throw new FormulaError(d);
  if (d === null) return false;
  if (typeof d === 'boolean') return d;
  if (typeof d === 'number') return d !== 0;
  const upper = d.trim().toUpperCase();
  if (upper === 'TRUE') return true;
  if (upper === 'FALSE') return false;
  const num = parseNumberText(d);
  if (num !== null) return num !== 0;
  fail(ERR.VALUE);
}

export function compareScalars(a: Scalar, b: Scalar): number {
  const ra = typeRank(a);
  const rb = typeRank(b);
  if (ra !== rb) return ra < rb ? -1 : 1;
  if (a === null && b === null) return 0;
  if (typeof a === 'number' && typeof b === 'number') return a === b ? 0 : a < b ? -1 : 1;
  if (typeof a === 'string' && typeof b === 'string') {
    const la = a.toLowerCase();
    const lb = b.toLowerCase();
    return la === lb ? 0 : la < lb ? -1 : 1;
  }
  if (typeof a === 'boolean' && typeof b === 'boolean') return a === b ? 0 : a ? 1 : -1;
  return 0;
}

/** Expand arguments into a flat list of scalars (ranges and arrays expanded). */
export function flattenArgs(args: Val[], ctx: FnContext): (Scalar | CellError)[] {
  const out: (Scalar | CellError)[] = [];
  for (const arg of args) {
    if (isRange(arg)) {
      for (const cell of iterateRange(arg, ctx)) out.push(cell.value);
    } else if (isArray(arg)) {
      for (const row of arg.rows) for (const v of row) out.push(v);
    } else {
      out.push(arg as Scalar | CellError);
    }
  }
  return out;
}

/** Numbers only, Excel-style: blanks/text/bools inside ranges are skipped. */
export function flattenNumbers(args: Val[], ctx: FnContext): number[] {
  const out: number[] = [];
  for (const arg of args) {
    if (isRange(arg) || isArray(arg)) {
      for (const v of flattenArgs([arg], ctx)) {
        if (isError(v)) throw new FormulaError(v);
        if (typeof v === 'number') out.push(v);
      }
    } else {
      const v = arg as Scalar | CellError;
      if (isError(v)) throw new FormulaError(v);
      if (v === null) continue;
      if (typeof v === 'boolean') {
        out.push(v ? 1 : 0);
      } else if (typeof v === 'number') {
        out.push(v);
      } else {
        const parsed = parseNumberText(v);
        if (parsed === null) fail(ERR.VALUE);
        out.push(parsed);
      }
    }
  }
  return out;
}

/** Convert a value into a 2-D grid (arrays and ranges expand). */
export function toGrid(v: Val, ctx: FnContext): (Scalar | CellError)[][] {
  if (isArray(v)) return v.rows;
  if (isRange(v)) {
    const r = clampRange(v, ctx);
    const grid: (Scalar | CellError)[][] = [];
    for (let row = r.r1; row <= r.r2; row++) {
      const line: (Scalar | CellError)[] = [];
      for (let col = r.c1; col <= r.c2; col++) line.push(ctx.source.getScalar(r.sheetId, row, col));
      grid.push(line);
    }
    return grid;
  }
  return [[v as Scalar | CellError]];
}

export function makeArray(rows: (Scalar | CellError)[][]): ArrayValue {
  return new ArrayValue(rows);
}

export function arrayOfNumbers(values: number[]): ArrayValue {
  return new ArrayValue(values.map((v) => [v]));
}

/** Convert any value to a single-cell display value. */
export function toDisplayScalar(v: Val, ctx: FnContext): Scalar | CellError {
  if (isError(v)) return v;
  if (isRange(v)) {
    if (v.isSingle) return ctx.source.getScalar(v.sheetId, v.r1, v.c1);
    return ERR.VALUE;
  }
  if (isArray(v)) return v.isSingle ? v.rows[0][0] : ERR.VALUE;
  return v as Scalar | CellError;
}

export { MAX_COL_INDEX, MAX_ROW_INDEX, normalizeRect };
