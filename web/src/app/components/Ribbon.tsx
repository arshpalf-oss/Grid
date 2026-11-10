import React, { useRef, useState } from 'react';
import { store } from '../store';
import { useStore } from '../useStore';
import { WorkbookJSON } from '../../engine/model';

const NUMBER_FORMATS: { label: string; value: string | undefined }[] = [
  { label: 'General', value: undefined },
  { label: 'Number 0.00', value: '0.00' },
  { label: 'Number #,##0.00', value: '#,##0.00' },
  { label: 'Integer', value: '#,##0' },
  { label: 'Currency', value: '"$"#,##0.00' },
  { label: 'Accounting', value: '_("$"* #,##0.00_)' },
  { label: 'Percent', value: '0%' },
  { label: 'Percent 0.00%', value: '0.00%' },
  { label: 'Scientific', value: '0.00E+00' },
  { label: 'Short date', value: 'yyyy-mm-dd' },
  { label: 'Long date', value: 'dddd, mmmm d, yyyy' },
  { label: 'Time', value: 'h:mm AM/PM' },
  { label: 'Fraction', value: '# ?/?' },
  { label: 'Text', value: '@' },
];

const FONTS = ['Inter', 'System UI', 'Arial', 'Georgia', 'Times New Roman', 'Courier New', 'Verdana'];
const SIZES = [9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 36];

function download(filename: string, content: string, mime = 'application/json'): void {
  const blob = new Blob([content], { type: mime });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function Ribbon(): React.ReactElement {
  const state = useStore();
  const style = store.activeStyle();
  const [menu, setMenu] = useState<'file' | null>(null);
  const [customFormat, setCustomFormat] = useState<string | null>(null);
  const fileInput = useRef<HTMLInputElement>(null);

  const noFocus = (e: React.MouseEvent) => e.preventDefault();

  const fmtValue = (() => {
    const explicit = style.numFmt;
    if (!explicit) return 'General';
    const item = NUMBER_FORMATS.find((f) => f.value === explicit);
    return item ? item.label : 'Custom';
  })();

  return (
    <div className="ribbon">
      <div className="brand">
        <span className="brand-mark">▦</span>
        <span className="brand-name">Grid</span>
      </div>

      <div className="ribbon-group">
        <button
          className="ribbon-btn wide"
          onMouseDown={noFocus}
          onClick={() => setMenu(menu === 'file' ? null : 'file')}
        >
          File ▾
        </button>
        {menu === 'file' ? (
          <div className="menu" onMouseLeave={() => setMenu(null)}>
            <button
              onClick={() => {
                store.newWorkbook();
                setMenu(null);
              }}
            >
              New workbook
            </button>
            <button
              onClick={() => {
                download('grid-workbook.json', JSON.stringify(store.toJSON(), null, 2));
                setMenu(null);
              }}
            >
              Save as JSON
            </button>
            <button onClick={() => fileInput.current?.click()}>Open JSON…</button>
            <button
              onClick={() => {
                download('grid-sheet.csv', store.exportCSV(), 'text/csv');
                setMenu(null);
              }}
            >
              Export sheet as CSV
            </button>
            <button
              onClick={() => {
                store.resetSaved();
                store.toast('Local autosave cleared');
                setMenu(null);
              }}
            >
              Reset autosave
            </button>
          </div>
        ) : null}
        <input
          ref={fileInput}
          type="file"
          accept="application/json"
          style={{ display: 'none' }}
          onChange={async (e) => {
            const file = e.target.files?.[0];
            if (!file) return;
            try {
              const text = await file.text();
              store.loadJSON(JSON.parse(text) as WorkbookJSON);
              store.toast(`Opened ${file.name}`);
            } catch {
              store.toast('Could not read that file');
            }
            e.target.value = '';
          }}
        />
      </div>

      <div className="ribbon-group">
        <button className="ribbon-btn" disabled={!store.canUndo()} onMouseDown={noFocus} onClick={() => store.undo()} title="Undo (Ctrl+Z)">
          ↶
        </button>
        <button className="ribbon-btn" disabled={!store.canRedo()} onMouseDown={noFocus} onClick={() => store.redo()} title="Redo (Ctrl+Y)">
          ↷
        </button>
      </div>

      <div className="ribbon-group">
        <select
          className="ribbon-select font"
          value={style.fontFamily ?? 'Inter'}
          onMouseDown={(e) => e.stopPropagation()}
          onChange={(e) => store.setStyle({ fontFamily: e.target.value })}
        >
          {FONTS.map((f) => (
            <option key={f} value={f}>
              {f}
            </option>
          ))}
        </select>
        <select
          className="ribbon-select size"
          value={style.fontSize ?? 13}
          onChange={(e) => store.setStyle({ fontSize: Number(e.target.value) })}
        >
          {SIZES.map((s) => (
            <option key={s} value={s}>
              {s}
            </option>
          ))}
        </select>
        <div className="btn-row">
          <button className={'ribbon-btn bold' + (style.bold ? ' on' : '')} onMouseDown={noFocus} onClick={() => store.toggleStyle('bold')} title="Bold">
            B
          </button>
          <button className={'ribbon-btn italic' + (style.italic ? ' on' : '')} onMouseDown={noFocus} onClick={() => store.toggleStyle('italic')} title="Italic">
            I
          </button>
          <button className={'ribbon-btn underline' + (style.underline ? ' on' : '')} onMouseDown={noFocus} onClick={() => store.toggleStyle('underline')} title="Underline">
            U
          </button>
          <button className={'ribbon-btn strike' + (style.strike ? ' on' : '')} onMouseDown={noFocus} onClick={() => store.toggleStyle('strike')} title="Strikethrough">
            S
          </button>
        </div>
        <label className="color-chip" title="Fill colour" style={{ background: style.fill ?? '#ffffff' }}>
          <input type="color" value={style.fill ?? '#ffffff'} onChange={(e) => store.setStyle({ fill: e.target.value })} />
          <span>fill</span>
        </label>
        <label className="color-chip text" title="Text colour" style={{ color: style.color ?? '#1f2329' }}>
          <input type="color" value={style.color ?? '#1f2329'} onChange={(e) => store.setStyle({ color: e.target.value })} />
          <span>A</span>
        </label>
      </div>

      <div className="ribbon-group">
        {(['left', 'center', 'right'] as const).map((align) => (
          <button
            key={align}
            className={'ribbon-btn' + (style.align === align ? ' on' : '')}
            onMouseDown={noFocus}
            onClick={() => store.setStyle({ align })}
            title={`Align ${align}`}
          >
            {align === 'left' ? '⯇' : align === 'center' ? '≡' : '⯈'}
          </button>
        ))}
        <button
          className={'ribbon-btn' + (style.wrap ? ' on' : '')}
          onMouseDown={noFocus}
          onClick={() => store.setStyle({ wrap: !style.wrap })}
          title="Wrap text"
        >
          ⤶
        </button>
        <button
          className="ribbon-btn"
          onMouseDown={noFocus}
          onClick={() => store.setStyle({ align: undefined, valign: undefined, wrap: undefined })}
          title="Clear alignment"
        >
          ⌫
        </button>
      </div>

      <div className="ribbon-group">
        <select
          className="ribbon-select numfmt"
          value={fmtValue}
          onChange={(e) => {
            const label = e.target.value;
            if (label === 'Custom') {
              setCustomFormat(style.numFmt ?? '');
              return;
            }
            const item = NUMBER_FORMATS.find((f) => f.label === label);
            store.setNumberFormat(item?.value);
          }}
        >
          {NUMBER_FORMATS.filter((f) => f.value !== undefined || f.label === 'General').map((f) => (
            <option key={f.label} value={f.label}>
              {f.label}
            </option>
          ))}
          <option value="Custom">Custom…</option>
        </select>
        <div className="btn-row">
          <button className="ribbon-btn" onMouseDown={noFocus} onClick={() => store.setNumberFormat('#,##0.00')} title="Add decimals">
            0.0
          </button>
          <button className="ribbon-btn" onMouseDown={noFocus} onClick={() => store.setNumberFormat('#,##0')} title="Remove decimals">
            .0
          </button>
        </div>
      </div>

      <div className="ribbon-group">
        <button className="ribbon-btn wide" onMouseDown={noFocus} onClick={() => store.insertRows()} title="Insert row above">
          + Row
        </button>
        <button className="ribbon-btn wide" onMouseDown={noFocus} onClick={() => store.insertCols()} title="Insert column left">
          + Col
        </button>
        <button className="ribbon-btn wide" onMouseDown={noFocus} onClick={() => store.deleteRows()} title="Delete rows">
          − Row
        </button>
        <button className="ribbon-btn wide" onMouseDown={noFocus} onClick={() => store.deleteCols()} title="Delete columns">
          − Col
        </button>
      </div>

      <div className="ribbon-group">
        <button className="ribbon-btn wide" onMouseDown={noFocus} onClick={() => store.clearContents()} title="Clear contents (Del)">
          Clear
        </button>
        <button className="ribbon-btn wide" onMouseDown={noFocus} onClick={() => store.setStyle(null)} title="Clear formatting">
          Clear fmt
        </button>
      </div>

      <div className="ribbon-spacer" />
      <div className="ribbon-group">
        <button
          className={'ribbon-btn wide' + (state.showFunctions ? ' on' : '')}
          onMouseDown={noFocus}
          onClick={() => store.setShowFunctions(!state.showFunctions)}
          title="Function reference"
        >
          Functions
        </button>
      </div>

      {customFormat !== null ? (
        <div className="modal-backdrop" onMouseDown={() => setCustomFormat(null)}>
          <div className="modal" onMouseDown={(e) => e.stopPropagation()}>
            <h3>Custom number format</h3>
            <p className="hint">
              Sections: positive;negative;zero;text. Examples: <code>#,##0.00</code>, <code>"$"#,##0</code>,{' '}
              <code>yyyy-mm-dd</code>, <code>0.00%</code>, <code>[h]:mm</code>
            </p>
            <input autoFocus value={customFormat} onChange={(e) => setCustomFormat(e.target.value)} />
            <div className="modal-actions">
              <button onClick={() => setCustomFormat(null)}>Cancel</button>
              <button
                className="primary"
                onClick={() => {
                  store.setNumberFormat(customFormat || undefined);
                  setCustomFormat(null);
                }}
              >
                Apply
              </button>
            </div>
          </div>
        </div>
      ) : null}
    </div>
  );
}
