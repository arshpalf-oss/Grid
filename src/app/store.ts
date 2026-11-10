/**
 * Application store: workbook + UI state, undo/redo, clipboard, persistence.
 * A tiny observable store keeps React out of the engine's hot path.
 */

import { MAX_COL_INDEX, MAX_ROW_INDEX, indexToCol, rangeA1, a1, shiftFormula } from '../engine/address';
import { CellError, isError } from '../engine/errors';
import { formatValue } from '../engine/format';
import { CellStyle, Computed, SheetData } from '../engine/model';
import { Workbook } from '../engine/workbook';
import { WorkbookJSON } from '../engine/model';

export interface Selection {
  anchorRow: number;
  anchorCol: number;
  focusRow: number;
  focusCol: number;
}

export interface EditState {
  row: number;
  col: number;
  draft: string;
  /** True when editing started from the formula bar (no navigation move). */
  fromBar?: boolean;
}

export interface UIState {
  version: number;
  sheetId: string;
  selection: Selection;
  editing: EditState | null;
  /** Selection of whole rows/columns for header highlighting. */
  selectAll: boolean;
  toast: string | null;
  showFunctions: boolean;
  zoom: number;
  autosaveAt: number | null;
}

interface Snapshot {
  sheets: SheetData[];
  names: { name: string; sheetId: string; formula: string }[];
  sheetId: string;
  selection: Selection;
}

interface ClipCell {
  raw: string;
  style?: CellStyle;
}

const AUTOSAVE_KEY = 'grid.workbook.v1';
const HISTORY_LIMIT = 150;

const cloneSheets = (sheets: SheetData[]): SheetData[] =>
  sheets.map((s) => ({
    ...s,
    cells: new Map(s.cells),
    colWidths: new Map(s.colWidths),
    rowHeights: new Map(s.rowHeights),
  }));

export class GridStore {
  wb: Workbook;
  state: UIState;
  private listeners = new Set<() => void>();
  private past: Snapshot[] = [];
  private future: Snapshot[] = [];
  private clipboard: { cells: ClipCell[][]; from: { row: number; col: number }; cut: boolean } | null = null;
  private internalText: string | null = null;
  private saveTimer: ReturnType<typeof setTimeout> | null = null;
  private committing = false;

  constructor() {
    this.wb = GridStore.loadSaved() ?? seedWorkbook();
    this.state = {
      version: this.wb.version,
      sheetId: this.wb.sheets[0].id,
      selection: { anchorRow: 0, anchorCol: 0, focusRow: 0, focusCol: 0 },
      editing: null,
      selectAll: false,
      toast: null,
      showFunctions: false,
      zoom: 1,
      autosaveAt: null,
    };
  }

  // ------------------------------------------------------------- plumbing

  subscribe = (fn: () => void): (() => void) => {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  };

  getSnapshot = (): UIState => this.state;

  private emit(patch: Partial<UIState> = {}): void {
    this.state = { ...this.state, ...patch, version: this.wb.version };
    for (const fn of this.listeners) fn();
  }

  private capture(): Snapshot {
    return {
      sheets: cloneSheets(this.wb.sheets),
      names: this.wb.listDefinedNames(),
      sheetId: this.state.sheetId,
      selection: this.state.selection,
    };
  }

  private restore(snap: Snapshot): void {
    this.wb.setSheets(cloneSheets(snap.sheets));
    this.emit({ sheetId: snap.sheetId, selection: snap.selection, editing: null });
  }

  /** Any interaction commits whatever is currently being typed. */
  private commitIfEditing(): void {
    if (this.state.editing) this.mutate(() => {});
  }

  private mutate(fn: () => void): void {
    const before = this.capture();
    if (!this.committing && this.state.editing) {
      const edit = this.state.editing;
      if (edit.draft !== this.raw(edit.row, edit.col)) {
        this.wb.setCell(this.state.sheetId, edit.row, edit.col, edit.draft);
      }
    }
    fn();
    this.past.push(before);
    if (this.past.length > HISTORY_LIMIT) this.past.shift();
    this.future = [];
    this.scheduleSave();
    this.emit({ editing: null });
  }

  canUndo(): boolean {
    return this.past.length > 0;
  }
  canRedo(): boolean {
    return this.future.length > 0;
  }

  undo(): void {
    this.commitIfEditing();
    const snap = this.past.pop();
    if (!snap) return;
    this.future.push(this.capture());
    this.restore(snap);
    this.scheduleSave();
  }

  redo(): void {
    this.commitIfEditing();
    const snap = this.future.pop();
    if (!snap) return;
    this.past.push(this.capture());
    this.restore(snap);
    this.scheduleSave();
  }

  toast(message: string): void {
    this.emit({ toast: message });
    setTimeout(() => {
      if (this.state.toast === message) this.emit({ toast: null });
    }, 2600);
  }

  // ------------------------------------------------------------ geometry

  get sheet(): SheetData {
    return this.wb.sheet(this.state.sheetId);
  }

  get range(): { r1: number; c1: number; r2: number; c2: number } {
    const { anchorRow, anchorCol, focusRow, focusCol } = this.state.selection;
    return {
      r1: Math.min(anchorRow, focusRow),
      c1: Math.min(anchorCol, focusCol),
      r2: Math.max(anchorRow, focusRow),
      c2: Math.max(anchorCol, focusCol),
    };
  }

  get activeCell(): { row: number; col: number } {
    return { row: this.state.selection.focusRow, col: this.state.selection.focusCol };
  }

  get activeAddress(): string {
    const { row, col } = this.activeCell;
    return a1(row, col);
  }

  get rangeAddress(): string {
    const r = this.range;
    return rangeA1(r.r1, r.c1, r.r2, r.c2);
  }

  computed(row: number, col: number): Computed {
    return this.wb.getComputed(this.state.sheetId, row, col);
  }

  display(row: number, col: number): string {
    return this.wb.getDisplayText(this.state.sheetId, row, col);
  }

  raw(row: number, col: number): string {
    return this.wb.getRaw(this.state.sheetId, row, col);
  }

  style(row: number, col: number): CellStyle | undefined {
    return this.wb.getStyle(this.state.sheetId, row, col);
  }

  activeStyle(): CellStyle {
    const { row, col } = this.activeCell;
    return this.style(row, col) ?? {};
  }

  activeRaw(): string {
    const { row, col } = this.activeCell;
    return this.raw(row, col);
  }

  // ----------------------------------------------------------- selection

  select(row: number, col: number, extend = false): void {
    this.commitIfEditing();
    const r = Math.max(0, Math.min(MAX_ROW_INDEX, row));
    const c = Math.max(0, Math.min(MAX_COL_INDEX, col));
    const sel = this.state.selection;
    this.emit({
      selection: extend
        ? { ...sel, focusRow: r, focusCol: c }
        : { anchorRow: r, anchorCol: c, focusRow: r, focusCol: c },
      editing: null,
    });
  }

  move(dRow: number, dCol: number, extend = false): void {
    const { row, col } = this.activeCell;
    this.select(row + dRow, col + dCol, extend);
  }

  selectColumn(col: number, extend = false): void {
    this.commitIfEditing();
    const sel = this.state.selection;
    if (extend) {
      this.emit({ selection: { ...sel, focusRow: MAX_ROW_INDEX, focusCol: col } });
    } else {
      this.emit({ selection: { anchorRow: 0, anchorCol: col, focusRow: MAX_ROW_INDEX, focusCol: col } });
    }
  }

  selectRow(row: number, extend = false): void {
    this.commitIfEditing();
    const sel = this.state.selection;
    if (extend) {
      this.emit({ selection: { ...sel, focusRow: row, focusCol: MAX_COL_INDEX } });
    } else {
      this.emit({ selection: { anchorRow: row, anchorCol: 0, focusRow: row, focusCol: MAX_COL_INDEX } });
    }
  }

  selectRange(r1: number, c1: number, r2: number, c2: number): void {
    this.commitIfEditing();
    this.emit({
      selection: {
        anchorRow: Math.max(0, Math.min(MAX_ROW_INDEX, r1)),
        anchorCol: Math.max(0, Math.min(MAX_COL_INDEX, c1)),
        focusRow: Math.max(0, Math.min(MAX_ROW_INDEX, r2)),
        focusCol: Math.max(0, Math.min(MAX_COL_INDEX, c2)),
      },
      editing: null,
    });
  }

  selectAllCells(): void {
    this.commitIfEditing();
    this.emit({
      selection: { anchorRow: 0, anchorCol: 0, focusRow: MAX_ROW_INDEX, focusCol: MAX_COL_INDEX },
    });
  }

  /** Ctrl+Arrow: run to the edge of the current block, or the next filled cell. */
  jump(dRow: number, dCol: number, extend = false): void {
    const id = this.state.sheetId;
    const { row, col } = this.activeCell;
    const inBounds = (r: number, c: number) => r >= 0 && c >= 0 && r <= MAX_ROW_INDEX && c <= MAX_COL_INDEX;
    const has = (r: number, c: number) =>
      this.wb.getRaw(id, r, c) !== '' || this.wb.getComputed(id, r, c).value !== null;

    const startHasData = has(row, col);
    let r = row;
    let c = col;

    for (;;) {
      const nr = r + dRow;
      const nc = c + dCol;
      if (!inBounds(nr, nc)) break;
      if (startHasData) {
        if (!has(nr, nc)) break;
        r = nr;
        c = nc;
      } else {
        r = nr;
        c = nc;
        if (has(r, c)) break;
      }
    }
    this.select(r, c, extend);
  }

  // -------------------------------------------------------------- editing

  beginEdit(initial?: string): void {
    const { row, col } = this.activeCell;
    this.beginEditAt(row, col, initial);
  }

  beginEditAt(row: number, col: number, initial?: string): void {
    const existing = this.raw(row, col);
    this.emit({
      selection: { anchorRow: row, anchorCol: col, focusRow: row, focusCol: col },
      editing: {
        row,
        col,
        draft: initial !== undefined ? initial : existing,
      },
    });
  }

  setDraft(draft: string): void {
    const editing = this.state.editing;
    if (!editing) return;
    this.emit({ editing: { ...editing, draft } });
  }

  commitEdit(move: 'down' | 'up' | 'right' | 'left' | 'none' = 'down'): void {
    const editing = this.state.editing;
    if (!editing) return;
    const { row, col, draft } = editing;
    if (draft !== this.raw(row, col)) {
      this.committing = true;
      try {
        this.mutate(() => this.wb.setCell(this.state.sheetId, row, col, draft));
      } finally {
        this.committing = false;
      }
    } else {
      this.emit({ editing: null });
    }
    if (move !== 'none') {
      const delta = {
        down: [1, 0],
        up: [-1, 0],
        right: [0, 1],
        left: [0, -1],
      }[move];
      this.select(row + delta[0], col + delta[1]);
    } else {
      this.select(row, col);
    }
  }

  cancelEdit(): void {
    this.emit({ editing: null });
  }

  // ------------------------------------------------------------- mutation

  setCellRaw(row: number, col: number, raw: string): void {
    this.mutate(() => this.wb.setCell(this.state.sheetId, row, col, raw));
  }

  clearContents(): void {
    const r = this.range;
    this.mutate(() => this.wb.clearRange(this.state.sheetId, r.r1, r.c1, r.r2, r.c2));
  }

  setStyle(patch: CellStyle | null): void {
    const r = this.range;
    this.mutate(() => this.wb.applyStyleToRange(this.state.sheetId, r.r1, r.c1, r.r2, r.c2, patch));
  }

  toggleStyle(key: 'bold' | 'italic' | 'underline' | 'strike'): void {
    const value = !this.activeStyle()[key];
    this.setStyle({ [key]: value } as CellStyle);
  }

  setNumberFormat(numFmt: string | undefined): void {
    this.setStyle({ numFmt });
  }

  // ------------------------------------------------------------ clipboard

  private selectionCells(): ClipCell[][] {
    const r = this.range;
    const out: ClipCell[][] = [];
    for (let row = r.r1; row <= r.r2; row++) {
      const line: ClipCell[] = [];
      for (let col = r.c1; col <= r.c2; col++) {
        line.push({
          raw: this.wb.getRaw(this.state.sheetId, row, col),
          style: this.wb.getStyle(this.state.sheetId, row, col),
        });
      }
      out.push(line);
    }
    return out;
  }

  copyToClipboard(cut = false): string {
    const cells = this.selectionCells();
    this.clipboard = { cells, from: this.activeCell, cut };
    const tsv = cells
      .map((row) =>
        row
          .map((cell) => {
            const computed = this.wb.getComputed(this.state.sheetId, this.activeCell.row, this.activeCell.col);
            void computed;
            return cell.raw;
          })
          .join('\t')
      )
      .join('\n');
    return tsv;
  }

  /** Plain text for the system clipboard (values, like Excel). */
  copyText(): string {
    const cells = this.selectionCells();
    this.clipboard = { cells, from: this.activeCell, cut: false };
    const id = this.state.sheetId;
    const r = this.range;
    const lines: string[] = [];
    for (let row = r.r1; row <= r.r2; row++) {
      const line: string[] = [];
      for (let col = r.c1; col <= r.c2; col++) {
        const v = this.wb.getScalar(id, row, col);
        line.push(isError(v) ? (v as CellError).code : v === null ? '' : String(v));
      }
      lines.push(line.join('\t'));
    }
    this.internalText = lines.join('\n');
    return this.internalText;
  }

  cutText(): string {
    const text = this.copyText();
    this.clipboard = { ...(this.clipboard as NonNullable<typeof this.clipboard>), cut: true };
    return text;
  }

  /** Paste either our own rich clipboard or plain text from the OS. */
  paste(text: string): void {
    const sameAsInternal = this.clipboard !== null && this.internalText !== null && text === this.internalText;
    if (sameAsInternal || (this.clipboard !== null && text.trim() === '')) {
      this.pasteInternal();
      return;
    }
    if (text === '') return;
    this.pasteFromText(text);
  }

  pasteFromText(text: string): void {
    const rows = text.replace(/\r\n/g, '\n').replace(/\r/g, '\n').split('\n');
    if (rows.length && rows[rows.length - 1] === '') rows.pop();
    const grid = rows.map((line) => line.split('\t'));
    const target = this.activeCell;

    this.mutate(() => {
      grid.forEach((line, i) => {
        line.forEach((value, j) => {
          const r = target.row + i;
          const c = target.col + j;
          if (r > MAX_ROW_INDEX || c > MAX_COL_INDEX) return;
          this.wb.setCell(this.state.sheetId, r, c, value);
        });
      });
    });
    const height = grid.length;
    const width = Math.max(...grid.map((l) => l.length));
    this.emit({
      selection: {
        anchorRow: target.row,
        anchorCol: target.col,
        focusRow: Math.min(MAX_ROW_INDEX, target.row + height - 1),
        focusCol: Math.min(MAX_COL_INDEX, target.col + width - 1),
      },
    });
  }

  /** Paste using the internal clipboard (keeps formulas and styles). */
  pasteInternal(): boolean {
    const src = this.clipboard;
    if (!src) return false;
    const target = this.activeCell;
    const height = src.cells.length;
    const width = Math.max(...src.cells.map((r) => r.length));
    const cut = src.cut;
    const sourceRange = {
      r1: src.from.row,
      c1: src.from.col,
      r2: src.from.row + height - 1,
      c2: src.from.col + width - 1,
    };

    this.mutate(() => {
      src.cells.forEach((line, i) => {
        line.forEach((cell, j) => {
          const r = target.row + i;
          const c = target.col + j;
          if (r > MAX_ROW_INDEX || c > MAX_COL_INDEX) return;
          const raw = cut ? cell.raw : shiftFormula(cell.raw, target.row - src.from.row, target.col - src.from.col);
          this.wb.setCell(this.state.sheetId, r, c, raw, cell.style);
        });
      });
      if (cut) {
        this.wb.clearRange(this.state.sheetId, sourceRange.r1, sourceRange.c1, sourceRange.r2, sourceRange.c2);
      }
    });
    if (cut) this.clipboard = null;
    this.emit({
      selection: {
        anchorRow: target.row,
        anchorCol: target.col,
        focusRow: Math.min(MAX_ROW_INDEX, target.row + height - 1),
        focusCol: Math.min(MAX_COL_INDEX, target.col + width - 1),
      },
    });
    return true;
  }

  /**
   * Fill handle: repeat the source block across the target area, shifting
   * relative references the way Excel does when you drag the handle.
   */
  fillRange(
    targetR1: number,
    targetC1: number,
    targetR2: number,
    targetC2: number,
    srcR1: number,
    srcC1: number,
    srcR2: number,
    srcC2: number
  ): void {
    const id = this.state.sheetId;
    const srcH = srcR2 - srcR1 + 1;
    const srcW = srcC2 - srcC1 + 1;
    if (srcH < 1 || srcW < 1) return;

    this.mutate(() => {
      for (let r = targetR1; r <= targetR2; r++) {
        for (let c = targetC1; c <= targetC2; c++) {
          const insideSource = r >= srcR1 && r <= srcR2 && c >= srcC1 && c <= srcC2;
          if (insideSource) continue;
          const i = (((r - srcR1) % srcH) + srcH) % srcH;
          const j = (((c - srcC1) % srcW) + srcW) % srcW;
          const sr = srcR1 + i;
          const sc = srcC1 + j;
          const raw = this.wb.getRaw(id, sr, sc);
          if (raw === '') continue;
          this.wb.setCell(
            id,
            r,
            c,
            shiftFormula(raw, r - sr, c - sc),
            this.wb.getStyle(id, sr, sc)
          );
        }
      }
    });
  }

  /** Fit a column to its widest visible value (double-click a header). */
  autofitColumn(col: number): void {
    const id = this.state.sheetId;
    const bounds = this.wb.usedBounds(id);
    const lastRow = Math.min(bounds ? bounds.r2 : 200, 5000);
    let widest = 4;
    for (let row = 0; row <= lastRow; row++) {
      const text = this.wb.getDisplayText(id, row, col);
      if (!text) continue;
      const hasFill = !!this.wb.getStyle(id, row, col)?.fill;
      void hasFill;
      widest = Math.max(widest, text.length);
    }
    const width = Math.max(40, Math.min(420, widest * 7.6 + 18));
    this.setColWidth(col, width);
  }

  /** Select the anchor of a spilled array (clicking a spilled cell). */
  selectSpillAnchor(row: number, col: number): boolean {
    const owner = this.wb.spillOwnerOf(this.state.sheetId, row, col);
    if (!owner) return false;
    this.select(owner.row, owner.col);
    return true;
  }

  // ----------------------------------------------------------- structure

  insertRows(count = 1): void {
    const r = this.range;
    this.mutate(() => this.wb.insertRows(this.state.sheetId, r.r1, count));
  }

  insertCols(count = 1): void {
    const r = this.range;
    this.mutate(() => this.wb.insertCols(this.state.sheetId, r.c1, count));
  }

  deleteRows(): void {
    const r = this.range;
    this.mutate(() => this.wb.deleteRows(this.state.sheetId, r.r1, r.r2 - r.r1 + 1));
  }

  deleteCols(): void {
    const r = this.range;
    this.mutate(() => this.wb.deleteCols(this.state.sheetId, r.c1, r.c2 - r.c1 + 1));
  }

  setColWidth(col: number, width: number): void {
    this.mutate(() => this.wb.setColWidth(this.state.sheetId, col, width));
  }

  setRowHeight(row: number, height: number): void {
    this.mutate(() => this.wb.setRowHeight(this.state.sheetId, row, height));
  }

  // ---------------------------------------------------------------- sheets

  addSheet(): void {
    const sheet = this.wb.addSheet();
    this.emit({ sheetId: sheet.id, selection: { anchorRow: 0, anchorCol: 0, focusRow: 0, focusCol: 0 } });
    this.scheduleSave();
  }

  selectSheet(id: string): void {
    this.commitIfEditing();
    if (!this.wb.getSheet(id)) return;
    this.emit({
      sheetId: id,
      selection: { anchorRow: 0, anchorCol: 0, focusRow: 0, focusCol: 0 },
      editing: null,
    });
  }

  renameSheet(id: string, name: string): void {
    if (!this.wb.renameSheet(id, name)) {
      this.toast('That sheet name is already in use');
      return;
    }
    this.emit();
    this.scheduleSave();
  }

  deleteSheet(id: string): void {
    if (this.wb.sheets.length <= 1) {
      this.toast('A workbook needs at least one sheet');
      return;
    }
    const wasActive = id === this.state.sheetId;
    if (!this.wb.removeSheet(id)) return;
    this.scheduleSave();
    this.emit({
      sheetId: wasActive ? this.wb.sheets[0].id : this.state.sheetId,
      selection: wasActive ? { anchorRow: 0, anchorCol: 0, focusRow: 0, focusCol: 0 } : this.state.selection,
    });
  }

  // ------------------------------------------------------------ file ops

  toJSON(): WorkbookJSON {
    return this.wb.toJSON(this.state.sheetId);
  }

  loadJSON(json: WorkbookJSON): void {
    const wb = Workbook.fromJSON(json);
    this.wb = wb;
    this.past = [];
    this.future = [];
    this.emit({
      sheetId: json.activeSheetId && wb.getSheet(json.activeSheetId) ? json.activeSheetId : wb.sheets[0].id,
      selection: { anchorRow: 0, anchorCol: 0, focusRow: 0, focusCol: 0 },
      editing: null,
    });
    this.scheduleSave();
  }

  newWorkbook(): void {
    this.loadJSON(seedWorkbook().toJSON() as WorkbookJSON);
    this.toast('New workbook');
  }

  exportCSV(): string {
    const sheet = this.sheet;
    const bounds = this.wb.usedBounds(sheet.id);
    if (!bounds) return '';
    const lines: string[] = [];
    for (let r = bounds.r1; r <= bounds.r2; r++) {
      const line: string[] = [];
      for (let c = bounds.c1; c <= bounds.c2; c++) {
        const text = this.wb.getDisplayText(sheet.id, r, c);
        line.push(/[",\n]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text);
      }
      lines.push(line.join(','));
    }
    return lines.join('\n');
  }

  private scheduleSave(): void {
    if (this.saveTimer) clearTimeout(this.saveTimer);
    this.saveTimer = setTimeout(() => {
      try {
        localStorage.setItem(AUTOSAVE_KEY, JSON.stringify(this.toJSON()));
        this.emit({ autosaveAt: Date.now() });
      } catch {
        // storage may be unavailable; ignore
      }
    }, 400);
  }

  private static loadSaved(): Workbook | null {
    try {
      const raw = localStorage.getItem(AUTOSAVE_KEY);
      if (!raw) return null;
      const json = JSON.parse(raw) as WorkbookJSON;
      if (!json || json.version !== 1 || !Array.isArray(json.sheets)) return null;
      return Workbook.fromJSON(json);
    } catch {
      return null;
    }
  }

  resetSaved(): void {
    try {
      localStorage.removeItem(AUTOSAVE_KEY);
    } catch {
      /* ignore */
    }
  }

  setShowFunctions(show: boolean): void {
    this.emit({ showFunctions: show });
  }

  cellLabel(row: number, col: number): string {
    return a1(row, col);
  }

  columnLabel(col: number): string {
    return indexToCol(col);
  }

  /** Formatted text for a formula result preview (status bar). */
  previewValue(v: unknown): string {
    if (isError(v)) return v.code;
    return formatValue(v as never, null);
  }
}

function seedWorkbook(): Workbook {
  const wb = new Workbook('Grid demo');
  const id = wb.sheets[0].id;
  const rows: [number, number, string][] = [
    [0, 0, 'Region'],
    [0, 1, 'Rep'],
    [0, 2, 'Units'],
    [0, 3, 'Price'],
    [0, 4, 'Revenue'],
    [1, 0, 'North'],
    [1, 1, 'Ada'],
    [1, 2, '120'],
    [1, 3, '18.5'],
    [1, 4, '=C2*D2'],
    [2, 0, 'South'],
    [2, 1, 'Grace'],
    [2, 2, '90'],
    [2, 3, '22'],
    [2, 4, '=C3*D3'],
    [3, 0, 'East'],
    [3, 1, 'Linus'],
    [3, 2, '150'],
    [3, 3, '15'],
    [3, 4, '=C4*D4'],
    [4, 0, 'West'],
    [4, 1, 'Margaret'],
    [4, 2, '210'],
    [4, 3, '12.75'],
    [4, 4, '=C5*D5'],
    [6, 0, 'Total'],
    [6, 4, '=SUM(E2:E5)'],
    [6, 5, '=AVERAGE(E2:E5)'],
    [8, 0, 'Try'],
    [8, 1, '=XLOOKUP("East",A2:A5,E2:E5)'],
    [9, 0, 'Spill'],
    [9, 1, '=SEQUENCE(3,3)'],
  ];
  for (const [r, c, raw] of rows) wb.setCell(id, r, c, raw);
  wb.setStyle(id, 0, 0, { bold: true, fill: '#eef2f7' });
  for (let c = 0; c < 5; c++) wb.setStyle(id, 0, c, { bold: true, fill: '#eef2f7' });
  for (let r = 1; r <= 4; r++) wb.setStyle(id, r, 4, { numFmt: '"$"#,##0.00' });
  wb.setStyle(id, 6, 4, { bold: true, numFmt: '"$"#,##0.00' });
  wb.setStyle(id, 6, 0, { bold: true });
  wb.setStyle(id, 6, 5, { bold: true, numFmt: '"$"#,##0.00' });
  wb.setColWidth(id, 0, 110);
  wb.setColWidth(id, 4, 120);
  wb.setColWidth(id, 5, 120);
  wb.invalidate();
  return wb;
}

export const store = new GridStore();
