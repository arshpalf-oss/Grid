import React, { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import { MAX_COLS, MAX_ROWS } from '../../engine/address';
import { isError } from '../../engine/errors';
import { useStore } from '../useStore';
import { store } from '../store';

const COL_HEAD_H = 26;

/** Variable-size axis with few custom sizes (row heights / column widths). */
class Axis {
  private customs: { index: number; size: number; delta: number }[] = [];
  private totalDelta = 0;

  constructor(
    readonly defaultSize: number,
    custom: Map<number, number>,
    readonly count: number
  ) {
    let delta = 0;
    for (const [index, size] of [...custom.entries()].sort((a, b) => a[0] - b[0])) {
      delta += size - defaultSize;
      this.customs.push({ index, size, delta: size - defaultSize });
    }
    this.totalDelta = delta;
  }

  size(i: number): number {
    const custom = this.customs.find((c) => c.index === i);
    return custom ? custom.size : this.defaultSize;
  }

  offset(i: number): number {
    let base = i * this.defaultSize;
    for (const c of this.customs) {
      if (c.index < i) base += c.delta;
      else break;
    }
    return base;
  }

  total(): number {
    return this.count * this.defaultSize + this.totalDelta;
  }

  indexAt(pos: number): number {
    const clamped = Math.max(0, pos);
    let idx = Math.min(this.count - 1, Math.floor(clamped / this.defaultSize));
    if (this.customs.length === 0) return idx;
    let off = this.offset(idx);
    let guard = 0;
    while (off > clamped && idx > 0 && guard++ < 5000) {
      idx--;
      off = this.offset(idx);
    }
    while (off + this.size(idx) <= clamped && idx < this.count - 1 && guard++ < 5000) {
      idx++;
      off = this.offset(idx);
    }
    return idx;
  }
}

interface CellPos {
  row: number;
  col: number;
}

type DragMode = null | 'select' | 'fill';

export function GridView(): React.ReactElement {
  const state = useStore();
  const scrollRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLDivElement>(null);
  const colHeadRef = useRef<HTMLDivElement>(null);
  const rowHeadRef = useRef<HTMLDivElement>(null);
  const editorRef = useRef<HTMLInputElement>(null);

  const [viewport, setViewport] = useState({ top: 0, left: 0, width: 900, height: 500 });
  const [drag, setDrag] = useState<DragMode>(null);
  const [fillPreview, setFillPreview] = useState<{ r1: number; c1: number; r2: number; c2: number } | null>(null);
  const [previewWidths, setPreviewWidths] = useState<Map<number, number> | null>(null);

  const sheet = store.wb.sheet(state.sheetId);
  const version = state.version;
  const { focusRow, focusCol } = state.selection;
  const selection = store.range;

  const rows = useMemo(
    () => new Axis(sheet.defaultRowHeight, sheet.rowHeights, MAX_ROWS),
    [sheet, version]
  );
  const cols = useMemo(() => {
    if (!previewWidths) return new Axis(sheet.defaultColWidth, sheet.colWidths, MAX_COLS);
    const merged = new Map(sheet.colWidths);
    for (const [col, width] of previewWidths) merged.set(col, width);
    return new Axis(sheet.defaultColWidth, merged, MAX_COLS);
  }, [sheet, version, previewWidths]);

  // -------------------------------------------------------- viewport sync

  useLayoutEffect(() => {
    const el = scrollRef.current;
    if (!el) return;
    const update = () => {
      // Guard against zero-size layout (hidden tab, headless render).
      if (el.clientWidth === 0 && el.clientHeight === 0) return;
      setViewport((v) => {
        const next = { top: el.scrollTop, left: el.scrollLeft, width: el.clientWidth, height: el.clientHeight };
        if (v.top === next.top && v.left === next.left && v.width === next.width && v.height === next.height) return v;
        return next;
      });
      if (colHeadRef.current) colHeadRef.current.style.transform = `translateX(${-el.scrollLeft}px)`;
      if (rowHeadRef.current) rowHeadRef.current.style.transform = `translateY(${-el.scrollTop}px)`;
    };
    update();
    el.addEventListener('scroll', update, { passive: true });
    const ro = new ResizeObserver(update);
    ro.observe(el);
    return () => {
      el.removeEventListener('scroll', update);
      ro.disconnect();
    };
  }, []);

  const ensureVisible = useCallback(
    (row: number, col: number) => {
      const el = scrollRef.current;
      if (!el) return;
      const top = rows.offset(row);
      const bottom = top + rows.size(row);
      const left = cols.offset(col);
      const right = left + cols.size(col);
      if (top < el.scrollTop) el.scrollTop = top;
      else if (bottom > el.scrollTop + el.clientHeight) el.scrollTop = bottom - el.clientHeight;
      if (left < el.scrollLeft) el.scrollLeft = left;
      else if (right > el.scrollLeft + el.clientWidth) el.scrollLeft = right - el.clientWidth;
    },
    [rows, cols]
  );

  useEffect(() => {
    if (!state.editing) ensureVisible(focusRow, focusCol);
  }, [focusRow, focusCol, state.editing, ensureVisible]);

  // -------------------------------------------------------- visible range

  const firstRow = rows.indexAt(viewport.top);
  const firstCol = cols.indexAt(viewport.left);
  const lastRow = Math.min(MAX_ROWS - 1, rows.indexAt(viewport.top + viewport.height - 1) + 1);
  const lastCol = Math.min(MAX_COLS - 1, cols.indexAt(viewport.left + viewport.width - 1) + 1);

  const totalWidth = cols.total();
  const totalHeight = rows.total();

  // ---------------------------------------------------------- coordinates

  const cellAt = (clientX: number, clientY: number): CellPos | null => {
    const canvas = canvasRef.current;
    if (!canvas) return null;
    const rect = canvas.getBoundingClientRect();
    const x = clientX - rect.left;
    const y = clientY - rect.top;
    if (x < 0 || y < 0) return null;
    return { row: rows.indexAt(y), col: cols.indexAt(x) };
  };

  const onMouseDown = (e: React.MouseEvent) => {
    if (e.button !== 0) return;
    const pos = cellAt(e.clientX, e.clientY);
    if (!pos) return;
    scrollRef.current?.focus();
    if (!e.shiftKey && store.selectSpillAnchor(pos.row, pos.col)) {
      setDrag('select');
      return;
    }
    store.select(pos.row, pos.col, e.shiftKey);
    setDrag('select');
  };

  useEffect(() => {
    if (!drag) return;
    const onMove = (e: MouseEvent) => {
      const pos = cellAt(e.clientX, e.clientY);
      if (!pos) return;
      if (drag === 'select') {
        store.select(pos.row, pos.col, true);
      } else if (drag === 'fill' && fillPreview) {
        setFillPreview((prev) => (prev ? { ...prev, r2: pos.row, c2: pos.col } : prev));
      }
    };
    const onUp = () => {
      if (drag === 'fill' && fillPreview) {
        const base = store.range;
        const r1 = Math.min(base.r1, fillPreview.r2);
        const c1 = Math.min(base.c1, fillPreview.c2);
        const r2 = Math.max(base.r2, fillPreview.r2);
        const c2 = Math.max(base.c2, fillPreview.c2);
        if (r1 !== base.r1 || c1 !== base.c1 || r2 !== base.r2 || c2 !== base.c2) {
          store.fillRange(r1, c1, r2, c2, base.r1, base.c1, base.r2, base.c2);
          store.select(r1, c1);
        }
        setFillPreview(null);
      }
      setDrag(null);
    };
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
    return () => {
      window.removeEventListener('mousemove', onMove);
      window.removeEventListener('mouseup', onUp);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [drag, fillPreview]);

  const onDoubleClick = (e: React.MouseEvent) => {
    const pos = cellAt(e.clientX, e.clientY);
    if (!pos) return;
    store.beginEditAt(pos.row, pos.col);
  };

  // ------------------------------------------------------------- keyboard

  const onKeyDown = (e: React.KeyboardEvent) => {
    if (state.editing) return;
    const ctrl = e.ctrlKey || e.metaKey;
    const shift = e.shiftKey;
    const pageRows = Math.max(1, Math.floor(viewport.height / sheet.defaultRowHeight) - 1);

    const handled = () => {
      e.preventDefault();
      e.stopPropagation();
    };

    if (ctrl && !shift && (e.key === 'z' || e.key === 'Z')) {
      store.undo();
      return handled();
    }
    if ((ctrl && e.key === 'y') || (ctrl && shift && (e.key === 'z' || e.key === 'Z'))) {
      store.redo();
      return handled();
    }
    if (ctrl && (e.key === 'b' || e.key === 'i' || e.key === 'u')) {
      store.toggleStyle(e.key === 'b' ? 'bold' : e.key === 'i' ? 'italic' : 'underline');
      return handled();
    }
    if (ctrl && e.key === 'a') {
      store.selectAllCells();
      return handled();
    }
    if (ctrl && e.key === 'Home') {
      store.select(0, 0);
      return handled();
    }

    switch (e.key) {
      case 'ArrowUp':
        ctrl ? store.jump(-1, 0, shift) : store.move(-1, 0, shift);
        return handled();
      case 'ArrowDown':
        ctrl ? store.jump(1, 0, shift) : store.move(1, 0, shift);
        return handled();
      case 'ArrowLeft':
        ctrl ? store.jump(0, -1, shift) : store.move(0, -1, shift);
        return handled();
      case 'ArrowRight':
        ctrl ? store.jump(0, 1, shift) : store.move(0, 1, shift);
        return handled();
      case 'Tab':
        store.move(0, shift ? -1 : 1);
        return handled();
      case 'Enter':
        store.move(shift ? -1 : 1, 0);
        return handled();
      case 'Home':
        store.select(focusRow, 0);
        return handled();
      case 'Delete':
      case 'Backspace':
        store.clearContents();
        return handled();
      case 'F2':
        store.beginEdit();
        return handled();
      case 'Escape':
        store.cancelEdit();
        return handled();
      case 'PageDown':
        store.move(pageRows, 0, shift);
        return handled();
      case 'PageUp':
        store.move(-pageRows, 0, shift);
        return handled();
      default:
        break;
    }

    if (!ctrl && !e.altKey && e.key.length === 1) {
      store.beginEdit();
      store.setDraft(e.key);
      e.preventDefault();
      return;
    }
    if (e.key === ';' || e.key === '=') {
      store.beginEdit();
      store.setDraft(e.key);
      e.preventDefault();
    }
  };

  // ------------------------------------------------------------ clipboard

  const onCopy = (e: React.ClipboardEvent) => {
    if (state.editing) return;
    const text = store.copyText();
    e.clipboardData.setData('text/plain', text);
    e.preventDefault();
    store.toast('Copied');
  };

  const onCut = (e: React.ClipboardEvent) => {
    if (state.editing) return;
    const text = store.cutText();
    e.clipboardData.setData('text/plain', text);
    e.preventDefault();
    store.toast('Cut — paste to move');
  };

  const onPaste = (e: React.ClipboardEvent) => {
    if (state.editing) return;
    const text = e.clipboardData.getData('text/plain');
    e.preventDefault();
    store.paste(text);
  };

  // ------------------------------------------------------------- rendering

  const cells: React.ReactElement[] = [];
  for (let row = firstRow; row <= lastRow; row++) {
    const top = rows.offset(row);
    const height = rows.size(row);
    for (let col = firstCol; col <= lastCol; col++) {
      const left = cols.offset(col);
      const width = cols.size(col);
      const computed = store.computed(row, col);
      const style = store.style(row, col) ?? {};
      const text = store.display(row, col);
      const value = computed.value;
      const isActive = row === focusRow && col === focusCol;
      const inSelection = row >= selection.r1 && row <= selection.r2 && col >= selection.c1 && col <= selection.c2;

      const textAlign =
        style.align ?? (typeof value === 'number' ? 'right' : typeof value === 'boolean' ? 'center' : 'left');

      cells.push(
        <div
          key={`${row}:${col}`}
          className={
            'cell' +
            (isActive ? ' active' : '') +
            (inSelection ? ' selected' : '') +
            (computed.isSpillChild ? ' spill' : '')
          }
          style={{
            left,
            top,
            width,
            height,
            textAlign,
            fontWeight: style.bold ? 600 : undefined,
            fontStyle: style.italic ? 'italic' : undefined,
            textDecoration: style.underline ? 'underline' : style.strike ? 'line-through' : undefined,
            color: style.color,
            background: style.fill,
            fontSize: style.fontSize,
            fontFamily: style.fontFamily,
            whiteSpace: style.wrap ? 'pre-wrap' : 'nowrap',
            alignItems: style.valign === 'top' ? 'flex-start' : style.valign === 'bottom' ? 'flex-end' : 'center',
          }}
        >
          <span className={'cell-text' + (isError(value) ? ' error' : '')}>{text}</span>
        </div>
      );
    }
  }

  const colHeaders: React.ReactElement[] = [];
  for (let col = firstCol; col <= lastCol; col++) {
    const inSelection = col >= selection.c1 && col <= selection.c2;
    colHeaders.push(
      <div
        key={col}
        className={'col-head' + (inSelection ? ' sel' : '')}
        style={{ left: cols.offset(col), width: cols.size(col) }}
        onMouseDown={(e) => {
          e.stopPropagation();
          store.selectColumn(col, e.shiftKey);
          scrollRef.current?.focus();
        }}
        onDoubleClick={(e) => {
          e.stopPropagation();
          store.autofitColumn(col);
        }}
      >
        <span>{store.columnLabel(col)}</span>
        <div
          className="col-resize"
          onMouseDown={(e) => {
            e.stopPropagation();
            e.preventDefault();
            const startX = e.clientX;
            const startWidth = cols.size(col);
            const onMove = (ev: MouseEvent) => {
              const next = Math.max(28, Math.min(600, startWidth + ev.clientX - startX));
              setPreviewWidths(new Map([[col, next]]));
            };
            const onUp = (ev: MouseEvent) => {
              window.removeEventListener('mousemove', onMove);
              window.removeEventListener('mouseup', onUp);
              const next = Math.max(28, Math.min(600, startWidth + ev.clientX - startX));
              setPreviewWidths(null);
              store.setColWidth(col, next);
            };
            window.addEventListener('mousemove', onMove);
            window.addEventListener('mouseup', onUp);
          }}
        />
      </div>
    );
  }

  const rowHeaders: React.ReactElement[] = [];
  for (let row = firstRow; row <= lastRow; row++) {
    const inSelection = row >= selection.r1 && row <= selection.r2;
    rowHeaders.push(
      <div
        key={row}
        className={'row-head' + (inSelection ? ' sel' : '')}
        style={{ top: rows.offset(row), height: rows.size(row) }}
        onMouseDown={(e) => {
          e.stopPropagation();
          store.selectRow(row, e.shiftKey);
          scrollRef.current?.focus();
        }}
      >
        <span>{row + 1}</span>
        <div
          className="row-resize"
          onMouseDown={(e) => {
            e.stopPropagation();
            e.preventDefault();
            const startY = e.clientY;
            const startHeight = rows.size(row);
            const onUp = (ev: MouseEvent) => {
              window.removeEventListener('mouseup', onUp);
              const next = Math.max(16, Math.min(240, startHeight + ev.clientY - startY));
              store.setRowHeight(row, next);
            };
            window.addEventListener('mouseup', onUp);
          }}
        />
      </div>
    );
  }

  const spillRegion = store.computed(focusRow, focusCol).spill;
  const editing = state.editing;

  return (
    <div className="sheet-view">
      <div className="corner">
        <div className="corner-inner" onClick={() => store.selectAllCells()} title="Select all" />
      </div>
      <div className="col-headers">
        <div className="col-headers-inner" ref={colHeadRef} style={{ width: totalWidth }}>
          {colHeaders}
        </div>
      </div>
      <div className="row-headers">
        <div className="row-headers-inner" ref={rowHeadRef} style={{ height: totalHeight }}>
          {rowHeaders}
        </div>
      </div>

      <div
        className="grid-scroll"
        ref={scrollRef}
        tabIndex={0}
        onKeyDown={onKeyDown}
        onCopy={onCopy}
        onCut={onCut}
        onPaste={onPaste}
        onMouseDown={onMouseDown}
        onDoubleClick={onDoubleClick}
      >
        <div className="grid-canvas" ref={canvasRef} style={{ width: totalWidth, height: totalHeight }}>
          {selection.r1 !== selection.r2 || selection.c1 !== selection.c2 ? (
            <div
              className="range-overlay"
              style={{
                left: cols.offset(selection.c1),
                top: rows.offset(selection.r1),
                width: cols.offset(selection.c2) + cols.size(selection.c2) - cols.offset(selection.c1),
                height: rows.offset(selection.r2) + rows.size(selection.r2) - rows.offset(selection.r1),
              }}
            />
          ) : null}

          {cells}

          <div
            className="active-outline"
            style={{
              left: cols.offset(focusCol),
              top: rows.offset(focusRow),
              width: cols.size(focusCol),
              height: rows.size(focusRow),
            }}
          />

          {spillRegion && !editing ? (
            <div
              className="spill-outline"
              style={{
                left: cols.offset(focusCol),
                top: rows.offset(focusRow),
                width: cols.offset(focusCol + spillRegion.width - 1) + cols.size(focusCol + spillRegion.width - 1) - cols.offset(focusCol),
                height: rows.offset(focusRow + spillRegion.height - 1) + rows.size(focusRow + spillRegion.height - 1) - rows.offset(focusRow),
              }}
            />
          ) : null}

          {fillPreview && drag === 'fill' ? (
            <div
              className="fill-preview"
              style={{
                left: cols.offset(Math.min(selection.c1, fillPreview.c2)),
                top: rows.offset(Math.min(selection.r1, fillPreview.r2)),
                width:
                  cols.offset(Math.max(selection.c2, fillPreview.c2)) +
                  cols.size(Math.max(selection.c2, fillPreview.c2)) -
                  cols.offset(Math.min(selection.c1, fillPreview.c2)),
                height:
                  rows.offset(Math.max(selection.r2, fillPreview.r2)) +
                  rows.size(Math.max(selection.r2, fillPreview.r2)) -
                  rows.offset(Math.min(selection.r1, fillPreview.r2)),
              }}
            />
          ) : null}

          {!spillRegion || editing ? null : null}

          <div
            className="fill-handle"
            style={{
              left: cols.offset(selection.c2) + cols.size(selection.c2) - 4,
              top: rows.offset(selection.r2) + rows.size(selection.r2) - 4,
            }}
            onMouseDown={(e) => {
              e.stopPropagation();
              e.preventDefault();
              setFillPreview({ r1: selection.r1, c1: selection.c1, r2: focusRow, c2: focusCol });
              setDrag('fill');
            }}
          />

          {editing ? (
            <input
              ref={editorRef}
              className="cell-editor"
              autoFocus
              value={editing.draft}
              spellCheck={false}
              style={{
                left: cols.offset(editing.col),
                top: rows.offset(editing.row),
                width: Math.max(cols.size(editing.col), 180),
                height: rows.size(editing.row) + 1,
                textAlign: (store.style(editing.row, editing.col)?.align ?? 'left') as 'left',
              }}
              onChange={(e) => store.setDraft(e.target.value)}
              onKeyDown={(e) => {
                e.stopPropagation();
                if (e.key === 'Enter') {
                  e.preventDefault();
                  store.commitEdit(e.shiftKey ? 'up' : 'down');
                  scrollRef.current?.focus();
                } else if (e.key === 'Tab') {
                  e.preventDefault();
                  store.commitEdit(e.shiftKey ? 'left' : 'right');
                  scrollRef.current?.focus();
                } else if (e.key === 'Escape') {
                  e.preventDefault();
                  store.cancelEdit();
                  scrollRef.current?.focus();
                }
              }}
              onBlur={() => {
                if (store.state.editing) store.commitEdit('none');
              }}
            />
          ) : null}
        </div>
      </div>

    </div>
  );
}

