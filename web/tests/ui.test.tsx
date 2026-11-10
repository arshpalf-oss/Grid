// @vitest-environment happy-dom
import React from 'react';
import { afterEach, beforeAll, describe, expect, it } from 'vitest';
import { act } from 'react-dom/test-utils';
import { createRoot, Root } from 'react-dom/client';
import { App } from '../src/app/App';
import { store } from '../src/app/store';

beforeAll(() => {
  // happy-dom does not implement ResizeObserver
  (globalThis as unknown as { ResizeObserver: unknown }).ResizeObserver = class {
    observe() {}
    unobserve() {}
    disconnect() {}
  };
});

let root: Root | null = null;
let container: HTMLDivElement | null = null;

async function mount(): Promise<HTMLDivElement> {
  container = document.createElement('div');
  document.body.appendChild(container);
  root = createRoot(container);
  await act(async () => {
    root!.render(<App />);
  });
  return container;
}

afterEach(async () => {
  if (root) await act(async () => root!.unmount());
  container?.remove();
  root = null;
  container = null;
});

describe('application shell', () => {
  it('mounts and paints grid cells', async () => {
    const el = await mount();
    expect(el.querySelector('.ribbon')).toBeTruthy();
    expect(el.querySelector('.formula-bar')).toBeTruthy();
    expect(el.querySelector('.grid-scroll')).toBeTruthy();
    expect(el.querySelectorAll('.cell').length).toBeGreaterThan(0);
  });

  it('shows the seeded demo workbook', async () => {
    const el = await mount();
    expect(el.textContent).toContain('Revenue');
    expect(el.textContent).toContain('Region');
  });

  it('renders sheet tabs and status bar', async () => {
    const el = await mount();
    expect(el.querySelectorAll('.tab').length).toBeGreaterThanOrEqual(1);
    expect(el.querySelector('.status-bar')).toBeTruthy();
  });

  it('reflects a committed edit in the grid', async () => {
    const el = await mount();
    await act(async () => {
      store.select(20, 0);
      store.beginEdit();
      store.setDraft('hello grid');
      store.commitEdit('none');
    });
    expect(el.textContent).toContain('hello grid');
  });

  it('computes formulas through the ui store', async () => {
    await mount();
    await act(async () => {
      store.select(22, 0);
      store.beginEdit();
      store.setDraft('2');
      store.commitEdit('none');
      store.select(22, 1);
      store.beginEdit();
      store.setDraft('3');
      store.commitEdit('none');
      store.select(22, 2);
      store.beginEdit();
      store.setDraft('=A23*B23');
      store.commitEdit('none');
    });
    expect(store.computed(22, 2).value).toBe(6);
  });
});

describe('demo workbook', () => {
  it('computes totals and formats currency', () => {
    const id = store.wb.sheets[0].id;
    expect(store.wb.getDisplayText(id, 1, 4)).toBe('$2,220.00');
    expect(store.wb.getDisplayText(id, 6, 4)).toBe('$9,127.50');
  });

  it('spills SEQUENCE into neighbouring cells', () => {
    const id = store.wb.sheets[0].id;
    expect(store.wb.getScalar(id, 9, 1)).toBe(1);
    expect(store.wb.getScalar(id, 11, 3)).toBe(9);
  });
});

describe('grid interactions', () => {
  it('fills a pattern and shifts relative references', async () => {
    await mount();
    await act(async () => {
      store.selectRange(30, 0, 31, 0);
      store.wb.setCell(store.state.sheetId, 30, 0, '1');
      store.wb.setCell(store.state.sheetId, 31, 0, '2');
      store.selectRange(30, 0, 31, 0);
      store.fillRange(30, 0, 33, 0, 30, 0, 31, 0);
    });
    expect(store.wb.getScalar(store.state.sheetId, 32, 0)).toBe(1);
    expect(store.wb.getScalar(store.state.sheetId, 33, 0)).toBe(2);
  });

  it('selects the spill anchor when a spilled cell is clicked', async () => {
    await mount();
    await act(async () => {
      store.selectRange(40, 0, 40, 0);
      store.wb.setCell(store.state.sheetId, 40, 0, '=SEQUENCE(3)');
    });
    const spilled = store.selectSpillAnchor(41, 0);
    expect(spilled).toBe(true);
    expect(store.activeCell.row).toBe(40);
  });

  it('autofits a column to its content', async () => {
    await mount();
    const before = store.wb.colWidth(store.state.sheetId, 1);
    await act(async () => {
      store.wb.setCell(store.state.sheetId, 50, 1, 'a very wide value in this column');
      store.autofitColumn(1);
    });
    expect(store.wb.colWidth(store.state.sheetId, 1)).toBeGreaterThan(before);
  });
});

describe('editing lifecycle', () => {
  it('commits the draft when the selection moves', async () => {
    await mount();
    await act(async () => {
      store.selectRange(60, 0, 60, 0);
      store.beginEdit();
      store.setDraft('committed on click');
      store.select(61, 0);
    });
    expect(store.wb.getRaw(store.state.sheetId, 60, 0)).toBe('committed on click');
    expect(store.state.editing).toBeNull();
  });

  it('undo restores the previous value', async () => {
    await mount();
    await act(async () => {
      store.selectRange(62, 0, 62, 0);
      store.setCellRaw(62, 0, 'first');
      store.setCellRaw(62, 0, 'second');
    });
    expect(store.wb.getRaw(store.state.sheetId, 62, 0)).toBe('second');
    await act(async () => store.undo());
    expect(store.wb.getRaw(store.state.sheetId, 62, 0)).toBe('first');
  });
});
