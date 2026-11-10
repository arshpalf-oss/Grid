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
