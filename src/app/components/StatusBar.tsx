import React from 'react';
import { isError } from '../../engine/errors';
import { a1, rangeA1 } from '../../engine/address';
import { formatValue } from '../../engine/format';
import { store } from '../store';
import { useStore } from '../useStore';

export function StatusBar(): React.ReactElement {
  const state = useStore();
  const r = store.range;
  const cellCount = (r.r2 - r.r1 + 1) * (r.c2 - r.c1 + 1);

  let sum = 0;
  let numeric = 0;
  let nonEmpty = 0;
  let circular = false;

  if (cellCount <= 50000) {
    const id = state.sheetId;
    for (let row = r.r1; row <= r.r2; row++) {
      for (let col = r.c1; col <= r.c2; col++) {
        const computed = store.wb.getComputed(id, row, col);
        if (computed.circular) circular = true;
        const v = computed.value;
        if (v === null || v === '') continue;
        nonEmpty++;
        if (typeof v === 'number') {
          sum += v;
          numeric++;
        }
      }
    }
  }

  const fmt = (n: number) => formatValue(n, '#,##0.00############');

  return (
    <div className="status-bar">
      <span className="status-mode">{state.editing ? 'Edit' : 'Ready'}</span>
      <span className="status-sep" />
      <span className="status-address">{a1(state.selection.focusRow, state.selection.focusCol)}</span>
      {state.selection.anchorRow !== state.selection.focusRow || state.selection.anchorCol !== state.selection.focusCol ? (
        <span className="status-selection">{rangeA1(r.r1, r.c1, r.r2, r.c2)}</span>
      ) : null}
      <span className="status-spacer" />
      {circular ? <span className="status-warn">Circular reference detected</span> : null}
      {numeric > 0 ? (
        <>
          <span>
            Sum <b>{fmt(sum)}</b>
          </span>
          <span>
            Average <b>{fmt(sum / numeric)}</b>
          </span>
        </>
      ) : null}
      {nonEmpty > 0 ? (
        <span>
          Count <b>{nonEmpty}</b>
        </span>
      ) : null}
      <span className="status-sep" />
      <span className="status-saved">{state.autosaveAt ? 'Saved locally' : 'Autosave on'}</span>
    </div>
  );
}
