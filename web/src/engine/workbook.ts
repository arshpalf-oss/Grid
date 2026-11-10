/**
 * Workbook: sheets, cells, the computed-value cache, spill regions and
 * recalculation bookkeeping.
 *
 * Recalculation strategy (v1): a mutation bumps the epoch and drops every
 * cached value. Values are then recomputed lazily as the UI (or a formula)
 * pulls on them, so the cost is proportional to what is actually touched.
 */

import { MAX_COL_INDEX, MAX_COLS, MAX_ROW_INDEX, Rect, cellKey, keyToRC, a1 } from './address';
import { isError } from './errors';
import { CellError, ERR } from './errors';
import { EvalHost, evaluateFormula } from './evaluator';
import { formattedCellValue } from './display';
import { Cell, CellStyle, Computed, SheetData, SheetJSON, WorkbookJSON } from './model';
import { ArrayValue, FnContext, RangeValue, Scalar, Val, isArray, isRange, parseNumberText, toGrid } from './values';
import { parseDateText } from './functions/text';
import { parseCellAddr } from './address';
import { parseAreaText } from './tokenizer';

interface SpillCell {
  ownerRow: number;
  ownerCol: number;
  value: Scalar | CellError;
}

let sheetCounter = 0;

export class Workbook implements EvalHost {
  name = 'Untitled workbook';
  sheets: SheetData[] = [];
  version = 0;

  private byId = new Map<string, SheetData>();
  private byName = new Map<string, string>();
  private definedNames = new Map<string, { sheetId: string; formula: string }>();

  private valueCache = new Map<string, Map<number, Computed>>();
  private spillMap = new Map<string, Map<number, SpillCell>>();
  private inFlight = new Set<string>();
  private circularKeys = new Set<string>();
  private usedCache = new Map<string, Rect | null>();

  private nowFn: () => Date = () => new Date();
  private idCounter = 0;

  constructor(name?: string) {
    if (name) this.name = name;
    this.addSheet('Sheet1');
  }

  // ---------------------------------------------------------------- sheets

  addSheet(name?: string, index?: number): SheetData {
    const safeName = this.uniqueSheetName(name ?? `Sheet${this.sheets.length + 1}`);
    const id = `s${++this.idCounter}_${++sheetCounter}`;
    const sheet: SheetData = {
      id,
      name: safeName,
      cells: new Map(),
      colWidths: new Map(),
      rowHeights: new Map(),
      defaultColWidth: 96,
      defaultRowHeight: 24,
      frozenRows: 0,
      frozenCols: 0,
    };
    if (index === undefined || index >= this.sheets.length) this.sheets.push(sheet);
    else this.sheets.splice(Math.max(0, index), 0, sheet);
    this.byId.set(id, sheet);
    this.byName.set(safeName.toLowerCase(), id);
    this.invalidate();
    return sheet;
  }

  removeSheet(id: string): boolean {
    if (this.sheets.length <= 1) return false;
    const idx = this.sheets.findIndex((s) => s.id === id);
    if (idx < 0) return false;
    const [sheet] = this.sheets.splice(idx, 1);
    this.byId.delete(id);
    this.byName.delete(sheet.name.toLowerCase());
    this.valueCache.delete(id);
    this.spillMap.delete(id);
    this.usedCache.delete(id);
    this.invalidate();
    return true;
  }

  renameSheet(id: string, name: string): boolean {
    const sheet = this.byId.get(id);
    if (!sheet) return false;
    const trimmed = name.trim();
    if (!trimmed) return false;
    const existing = this.byName.get(trimmed.toLowerCase());
    if (existing && existing !== id) return false;
    this.byName.delete(sheet.name.toLowerCase());
    sheet.name = trimmed;
    this.byName.set(trimmed.toLowerCase(), id);
    this.invalidate();
    return true;
  }

  private uniqueSheetName(base: string): string {
    let candidate = base;
    let n = 2;
    while (this.byName.has(candidate.toLowerCase())) {
      candidate = `${base}${n++}`;
    }
    return candidate;
  }

  sheet(id: string): SheetData {
    const sheet = this.byId.get(id);
    if (!sheet) throw new Error(`Unknown sheet ${id}`);
    return sheet;
  }

  getSheet(id: string): SheetData | undefined {
    return this.byId.get(id);
  }

  sheetIdByName(name: string): string | null {
    let key = name.trim();
    if (key.startsWith("'") && key.endsWith("'")) key = key.slice(1, -1).replace(/''/g, "'");
    return this.byName.get(key.toLowerCase()) ?? null;
  }

  sheetName(id: string): string {
    return this.byId.get(id)?.name ?? '';
  }

  sheetCount(): number {
    return this.sheets.length;
  }

  now(): Date {
    return this.nowFn();
  }

  setNowProvider(fn: () => Date): void {
    this.nowFn = fn;
    this.invalidate();
  }

  // ----------------------------------------------------------------- cells

  getCell(sheetId: string, row: number, col: number): Cell | undefined {
    return this.byId.get(sheetId)?.cells.get(cellKey(row, col));
  }

  getRaw(sheetId: string, row: number, col: number): string {
    return this.getCell(sheetId, row, col)?.raw ?? '';
  }

  getFormula(sheetId: string, row: number, col: number): string {
    const raw = this.getRaw(sheetId, row, col);
    return raw.startsWith('=') ? raw : '';
  }

  getStyle(sheetId: string, row: number, col: number): CellStyle | undefined {
    return this.getCell(sheetId, row, col)?.style;
  }

  setCell(sheetId: string, row: number, col: number, raw: string, style?: CellStyle): void {
    const sheet = this.sheet(sheetId);
    if (row < 0 || col < 0 || row > MAX_ROW_INDEX || col > MAX_COL_INDEX) return;
    const key = cellKey(row, col);
    const existing = sheet.cells.get(key);
    const nextStyle = style ?? existing?.style;
    if (raw === '') {
      if (nextStyle && Object.keys(nextStyle).length > 0) {
        sheet.cells.set(key, { raw: '', style: nextStyle });
      } else {
        sheet.cells.delete(key);
      }
    } else {
      sheet.cells.set(key, nextStyle ? { raw, style: nextStyle } : { raw });
    }
    this.invalidate();
  }

  setStyle(sheetId: string, row: number, col: number, patch: CellStyle | null): void {
    const sheet = this.sheet(sheetId);
    const key = cellKey(row, col);
    const existing = sheet.cells.get(key);
    let style: CellStyle | undefined;
    if (patch === null) {
      style = undefined;
    } else {
      style = { ...(existing?.style ?? {}) };
      for (const [k, v] of Object.entries(patch)) {
        if (v === undefined || v === null || v === '') delete (style as Record<string, unknown>)[k];
        else (style as Record<string, unknown>)[k] = v;
      }
      if (Object.keys(style).length === 0) style = undefined;
    }
    const raw = existing?.raw ?? '';
    if (!style && raw === '') sheet.cells.delete(key);
    else sheet.cells.set(key, { raw, style });
    this.invalidate();
  }

  applyStyleToRange(
    sheetId: string,
    r1: number,
    c1: number,
    r2: number,
    c2: number,
    patch: CellStyle | null,
    skip?: (row: number, col: number) => boolean
  ): void {
    for (let r = Math.min(r1, r2); r <= Math.max(r1, r2); r++) {
      for (let c = Math.min(c1, c2); c <= Math.max(c1, c2); c++) {
        if (skip && skip(r, c)) continue;
        this.setStyle(sheetId, r, c, patch);
      }
    }
  }

  clearRange(sheetId: string, r1: number, c1: number, r2: number, c2: number): void {
    const sheet = this.sheet(sheetId);
    for (let r = Math.min(r1, r2); r <= Math.max(r1, r2); r++) {
      for (let c = Math.min(c1, c2); c <= Math.max(c1, c2); c++) {
        const key = cellKey(r, c);
        const cell = sheet.cells.get(key);
        if (!cell) continue;
        if (cell.style && Object.keys(cell.style).length) sheet.cells.set(key, { raw: '', style: cell.style });
        else sheet.cells.delete(key);
      }
    }
    this.invalidate();
  }

  // ------------------------------------------------------------- recompute

  private ctxFor(sheetId: string, row: number, col: number): FnContext {
    return {
      source: this,
      sheetId,
      row,
      col,
      vars: new Map(),
      parseRefString: (text) => parseAreaText(text, this.sheetName(sheetId)),
      evalNode: () => {
        throw new Error('not available in this context');
      },
    };
  }

  getComputed(sheetId: string, row: number, col: number): Computed {
    if (row < 0 || col < 0 || row > MAX_ROW_INDEX || col > MAX_COL_INDEX) return { value: ERR.REF };
    const sheet = this.byId.get(sheetId);
    if (!sheet) return { value: ERR.REF };

    const key = cellKey(row, col);
    let cache = this.valueCache.get(sheetId);
    if (!cache) {
      cache = new Map();
      this.valueCache.set(sheetId, cache);
    }
    const cached = cache.get(key);
    if (cached) return cached;

    const cell = sheet.cells.get(key);
    if (!cell || !cell.raw) {
      const spill = this.spillMap.get(sheetId)?.get(key);
      if (spill) {
        const computed: Computed = { value: spill.value, isSpillChild: true };
        cache.set(key, computed);
        return computed;
      }
      return { value: null };
    }

    const token = `${sheetId}\u0000${key}`;
    if (this.inFlight.has(token)) {
      this.circularKeys.add(token);
      return { value: 0, circular: true };
    }

    this.inFlight.add(token);
    let computed: Computed;
    try {
      computed = cell.raw.startsWith('=')
        ? this.computeFormula(sheet, row, col, cell.raw)
        : this.computeLiteral(cell.raw);
    } catch {
      computed = { value: ERR.VALUE };
    } finally {
      this.inFlight.delete(token);
    }

    if (this.circularKeys.has(token)) computed = { ...computed, circular: true };
    cache.set(key, computed);
    return computed;
  }

  private computeFormula(sheet: SheetData, row: number, col: number, raw: string): Computed {
    const ctx = this.ctxFor(sheet.id, row, col);
    const result = evaluateFormula(this, sheet.id, row, col, raw);
    let grid: (Scalar | CellError)[][] | null = null;
    let scalar: Scalar | CellError;

    if (isRange(result.value)) grid = toGrid(result.value, ctx);
    else if (isArray(result.value)) grid = result.value.rows;
    else scalar = result.value as Scalar | CellError;

    if (grid) {
      const height = grid.length;
      const width = grid[0]?.length ?? 0;
      if (height === 0 || width === 0) return { value: ERR.CALC, formula: true };
      if (height === 1 && width === 1) return { value: grid[0][0], formula: true };
      if (this.spillConflicts(sheet.id, row, col, height, width)) {
        return { value: ERR.SPILL, formula: true };
      }
      this.registerSpill(sheet.id, row, col, grid);
      return { value: grid[0][0], formula: true, spill: { height, width } };
    }

    void ctx;
    return { value: scalar!, formula: true };
  }

  private spillConflicts(sheetId: string, row: number, col: number, height: number, width: number): boolean {
    const sheet = this.sheet(sheetId);
    for (let i = 0; i < height; i++) {
      for (let j = 0; j < width; j++) {
        if (i === 0 && j === 0) continue;
        const r = row + i;
        const c = col + j;
        if (r > MAX_ROW_INDEX || c > MAX_COL_INDEX) return true;
        const cell = sheet.cells.get(cellKey(r, c));
        if (cell && cell.raw) return true;
      }
    }
    return false;
  }

  private registerSpill(sheetId: string, row: number, col: number, grid: (Scalar | CellError)[][]): void {
    let map = this.spillMap.get(sheetId);
    if (!map) {
      map = new Map();
      this.spillMap.set(sheetId, map);
    }
    const cache = this.valueCache.get(sheetId);
    for (let i = 0; i < grid.length; i++) {
      const width = grid[i]?.length ?? 0;
      for (let j = 0; j < width; j++) {
        if (i === 0 && j === 0) continue;
        const key = cellKey(row + i, col + j);
        map.set(key, { ownerRow: row, ownerCol: col, value: grid[i][j] });
        cache?.delete(key);
      }
    }
  }

  private hasSpillOwner(sheetId: string, row: number, col: number): boolean {
    const map = this.spillMap.get(sheetId);
    if (!map) return false;
    for (const cell of map.values()) {
      if (cell.ownerRow === row && cell.ownerCol === col) return true;
    }
    return false;
  }

  private computeLiteral(raw: string): Computed {
    if (raw === '') return { value: null };
    if (raw.startsWith("'")) return { value: raw.slice(1) };

    const upper = raw.trim().toUpperCase();
    if (upper === 'TRUE') return { value: true };
    if (upper === 'FALSE') return { value: false };
    if (/^#(NULL!|DIV\/0!|VALUE!|REF!|NAME\?|NUM!|N\/A|SPILL!|CALC!)$/.test(upper)) {
      return { value: CellError.byCode(upper) };
    }

    const percent = /%$/.test(raw.trim());
    const currency = /^[$€£¥₹]/.test(raw.trim());
    const parens = /^\(.*\)$/.test(raw.trim());
    const hasSeparators = raw.includes(',') && /\d,\d/.test(raw);

    const numeric = parseNumberText(raw);
    if (numeric !== null && raw.trim() !== '') {
      let format: string | undefined;
      if (percent) format = /\.\d/.test(raw) ? '0.00%' : '0%';
      else if (currency) format = '"$"#,##0.00';
      else if (hasSeparators) format = /\.\d/.test(raw) ? '#,##0.00' : '#,##0';
      else if (parens) format = '#,##0.00;(#,##0.00)';
      return { value: numeric, numberFormat: format };
    }

    const date = parseDateText(raw);
    if (date !== null) {
      const hasTime = /\d:\d/.test(raw);
      return { value: date, numberFormat: hasTime ? 'yyyy-mm-dd hh:mm' : 'yyyy-mm-dd' };
    }

    return { value: raw };
  }

  invalidate(): void {
    this.valueCache.clear();
    this.spillMap.clear();
    this.inFlight.clear();
    this.circularKeys.clear();
    this.usedCache.clear();
    this.version++;
  }

  // ----------------------------------------------------------- value access

  getScalar(sheetId: string, row: number, col: number): Scalar | CellError {
    return this.getComputed(sheetId, row, col).value;
  }

  getValue(sheetId: string, row: number, col: number): Val {
    return this.getComputed(sheetId, row, col).value;
  }

  /** Text as rendered in the cell, applying the effective number format. */
  getDisplayText(sheetId: string, row: number, col: number): string {
    return formattedCellValue(this, sheetId, row, col);
  }

  getNumberFormat(sheetId: string, row: number, col: number): string | undefined {
    const explicit = this.getCell(sheetId, row, col)?.style?.numFmt;
    if (explicit) return explicit;
    return this.getComputed(sheetId, row, col).numberFormat;
  }

  usedBounds(sheetId: string): Rect | null {
    if (this.usedCache.has(sheetId)) return this.usedCache.get(sheetId) ?? null;
    const sheet = this.byId.get(sheetId);
    let rect: Rect | null = null;
    if (sheet) {
      for (const key of sheet.cells.keys()) {
        const { row, col } = keyToRC(key);
        if (!rect) rect = { r1: row, c1: col, r2: row, c2: col };
        else {
          if (row < rect.r1) rect.r1 = row;
          if (col < rect.c1) rect.c1 = col;
          if (row > rect.r2) rect.r2 = row;
          if (col > rect.c2) rect.c2 = col;
        }
      }
    }
    this.usedCache.set(sheetId, rect);
    return rect;
  }

  // --------------------------------------------------------- defined names

  setDefinedName(name: string, sheetId: string, formula: string): void {
    this.definedNames.set(name.toLowerCase(), { sheetId, formula });
    this.invalidate();
  }

  removeDefinedName(name: string): void {
    this.definedNames.delete(name.toLowerCase());
    this.invalidate();
  }

  listDefinedNames(): { name: string; sheetId: string; formula: string }[] {
    return [...this.definedNames.entries()].map(([key, v]) => ({ name: key, ...v }));
  }

  definedName(name: string): { sheetId: string; formula: string } | null {
    return this.definedNames.get(name.toLowerCase()) ?? null;
  }

  // -------------------------------------------------------- structure edit

  insertRows(sheetId: string, before: number, count = 1): void {
    const sheet = this.sheet(sheetId);
    const next = new Map<number, Cell>();
    for (const [key, cell] of sheet.cells) {
      const { row, col } = keyToRC(key);
      const newRow = row >= before ? row + count : row;
      if (newRow <= MAX_ROW_INDEX) next.set(cellKey(newRow, col), cell);
    }
    sheet.cells = next;
    const heights = new Map<number, number>();
    for (const [row, h] of sheet.rowHeights) heights.set(row >= before ? row + count : row, h);
    sheet.rowHeights = heights;
    this.invalidate();
  }

  deleteRows(sheetId: string, start: number, count = 1): void {
    const sheet = this.sheet(sheetId);
    const next = new Map<number, Cell>();
    for (const [key, cell] of sheet.cells) {
      const { row, col } = keyToRC(key);
      if (row >= start && row < start + count) continue;
      const newRow = row >= start + count ? row - count : row;
      next.set(cellKey(newRow, col), cell);
    }
    sheet.cells = next;
    const heights = new Map<number, number>();
    for (const [row, h] of sheet.rowHeights) {
      if (row >= start && row < start + count) continue;
      heights.set(row >= start + count ? row - count : row, h);
    }
    sheet.rowHeights = heights;
    this.invalidate();
  }

  insertCols(sheetId: string, before: number, count = 1): void {
    const sheet = this.sheet(sheetId);
    const next = new Map<number, Cell>();
    for (const [key, cell] of sheet.cells) {
      const { row, col } = keyToRC(key);
      const newCol = col >= before ? col + count : col;
      if (newCol <= MAX_COL_INDEX) next.set(cellKey(row, newCol), cell);
    }
    sheet.cells = next;
    const widths = new Map<number, number>();
    for (const [col, w] of sheet.colWidths) widths.set(col >= before ? col + count : col, w);
    sheet.colWidths = widths;
    this.invalidate();
  }

  deleteCols(sheetId: string, start: number, count = 1): void {
    const sheet = this.sheet(sheetId);
    const next = new Map<number, Cell>();
    for (const [key, cell] of sheet.cells) {
      const { row, col } = keyToRC(key);
      if (col >= start && col < start + count) continue;
      const newCol = col >= start + count ? col - count : col;
      next.set(cellKey(row, newCol), cell);
    }
    sheet.cells = next;
    const widths = new Map<number, number>();
    for (const [col, w] of sheet.colWidths) {
      if (col >= start && col < start + count) continue;
      widths.set(col >= start + count ? col - count : col, w);
    }
    sheet.colWidths = widths;
    this.invalidate();
  }

  setColWidth(sheetId: string, col: number, width: number): void {
    this.sheet(sheetId).colWidths.set(col, Math.max(24, Math.min(800, width)));
    this.version++;
  }

  setRowHeight(sheetId: string, row: number, height: number): void {
    this.sheet(sheetId).rowHeights.set(row, Math.max(16, Math.min(400, height)));
    this.version++;
  }

  colWidth(sheetId: string, col: number): number {
    const sheet = this.byId.get(sheetId);
    return sheet?.colWidths.get(col) ?? sheet?.defaultColWidth ?? 96;
  }

  rowHeight(sheetId: string, row: number): number {
    const sheet = this.byId.get(sheetId);
    return sheet?.rowHeights.get(row) ?? sheet?.defaultRowHeight ?? 24;
  }

  /** Replace all sheets (used by undo/redo). */
  setSheets(sheets: SheetData[]): void {
    this.sheets = sheets;
    this.byId.clear();
    this.byName.clear();
    for (const sheet of sheets) {
      this.byId.set(sheet.id, sheet);
      this.byName.set(sheet.name.toLowerCase(), sheet.id);
    }
    this.invalidate();
  }

  // -------------------------------------------------------- serialisation

  toJSON(activeSheetId?: string): WorkbookJSON {
    return {
      version: 1,
      name: this.name,
      activeSheetId,
      sheets: this.sheets.map((s) => ({
        id: s.id,
        name: s.name,
        defaultColWidth: s.defaultColWidth,
        defaultRowHeight: s.defaultRowHeight,
        frozenRows: s.frozenRows,
        frozenCols: s.frozenCols,
        color: s.color,
        cells: [...s.cells.entries()].map(([key, cell]) => ({ key, raw: cell.raw, style: cell.style })),
        colWidths: [...s.colWidths.entries()],
        rowHeights: [...s.rowHeights.entries()],
      })),
      names: this.listDefinedNames(),
    };
  }

  static fromJSON(json: WorkbookJSON): Workbook {
    const wb = new Workbook();
    wb.sheets = [];
    wb.byId.clear();
    wb.byName.clear();
    wb.name = json.name || 'Untitled workbook';
    let maxId = 0;
    for (const s of json.sheets as SheetJSON[]) {
      const sheet: SheetData = {
        id: s.id,
        name: s.name,
        cells: new Map((s.cells ?? []).map((c) => [c.key, { raw: c.raw, style: c.style }])),
        colWidths: new Map(s.colWidths ?? []),
        rowHeights: new Map(s.rowHeights ?? []),
        defaultColWidth: s.defaultColWidth ?? 96,
        defaultRowHeight: s.defaultRowHeight ?? 24,
        frozenRows: s.frozenRows ?? 0,
        frozenCols: s.frozenCols ?? 0,
        color: s.color,
      };
      wb.sheets.push(sheet);
      wb.byId.set(sheet.id, sheet);
      wb.byName.set(sheet.name.toLowerCase(), sheet.id);
      const num = parseInt(sheet.id.replace(/^s/, ''), 10);
      if (Number.isFinite(num)) maxId = Math.max(maxId, Math.floor(num));
    }
    if (!wb.sheets.length) wb.addSheet('Sheet1');
    wb.idCounter = maxId + 1;
    wb.definedNames.clear();
    for (const n of json.names ?? []) wb.definedNames.set(n.name.toLowerCase(), { sheetId: n.sheetId, formula: n.formula });
    wb.invalidate();
    return wb;
  }

  /** Convenience for the UI: A1 style label of a cell. */
  label(row: number, col: number): string {
    return a1(row, col);
  }

  /** True if the cell is the anchor of a spill. */
  isSpillAnchor(sheetId: string, row: number, col: number): boolean {
    return !!this.getComputed(sheetId, row, col).spill;
  }

  spillOwnerOf(sheetId: string, row: number, col: number): { row: number; col: number } | null {
    const spill = this.spillMap.get(sheetId)?.get(cellKey(row, col));
    return spill ? { row: spill.ownerRow, col: spill.ownerCol } : null;
  }

  /** Resolve a range of cells into a 2-D value grid (used for copy/fill/export). */
  readGrid(sheetId: string, r1: number, c1: number, r2: number, c2: number): (Scalar | CellError)[][] {
    const out: (Scalar | CellError)[][] = [];
    for (let r = Math.min(r1, r2); r <= Math.max(r1, r2); r++) {
      const row: (Scalar | CellError)[] = [];
      for (let c = Math.min(c1, c2); c <= Math.max(c1, c2); c++) row.push(this.getScalar(sheetId, r, c));
      out.push(row);
    }
    return out;
  }

  cellAddress(col: number, row: number): string {
    return a1(row, col);
  }

  hasContent(sheetId: string, row: number, col: number): boolean {
    const cell = this.byId.get(sheetId)?.cells.get(cellKey(row, col));
    if (cell && cell.raw) return true;
    return this.hasSpillOwner(sheetId, row, col) || !!this.spillMap.get(sheetId)?.get(cellKey(row, col));
  }

  parseAddress(text: string): { row: number; col: number } | null {
    const addr = parseCellAddr(text);
    return addr ? { row: addr.row, col: addr.col } : null;
  }

  static emptyContext: FnContext | null = null;

  valueToText(v: Val): string {
    if (isRange(v) && v.isSingle) return String(v);
    if (isArray(v)) return String(v.rows[0]?.[0] ?? '');
    return v === null ? '' : String(v);
  }

  isErrorValue(v: unknown): v is CellError {
    return isError(v);
  }
}

export { MAX_COLS, MAX_ROW_INDEX, MAX_COL_INDEX, ArrayValue };
