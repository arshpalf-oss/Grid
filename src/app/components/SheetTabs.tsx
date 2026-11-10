import React, { useState } from 'react';
import { store } from '../store';
import { useStore } from '../useStore';

export function SheetTabs(): React.ReactElement {
  const state = useStore();
  const [renaming, setRenaming] = useState<{ id: string; value: string } | null>(null);
  const [menu, setMenu] = useState<{ id: string; x: number; y: number } | null>(null);

  return (
    <div className="sheet-tabs">
      <button className="tab-add" title="Add sheet" onClick={() => store.addSheet()}>
        +
      </button>
      <div className="tabs-scroll">
        {store.wb.sheets.map((sheet) => {
          const active = sheet.id === state.sheetId;
          if (renaming && renaming.id === sheet.id) {
            return (
              <input
                key={sheet.id}
                className="tab-rename"
                autoFocus
                value={renaming.value}
                onChange={(e) => setRenaming({ id: sheet.id, value: e.target.value })}
                onBlur={() => {
                  store.renameSheet(sheet.id, renaming.value);
                  setRenaming(null);
                }}
                onKeyDown={(e) => {
                  if (e.key === 'Enter') {
                    store.renameSheet(sheet.id, renaming.value);
                    setRenaming(null);
                  } else if (e.key === 'Escape') {
                    setRenaming(null);
                  }
                }}
              />
            );
          }
          return (
            <button
              key={sheet.id}
              className={'tab' + (active ? ' active' : '')}
              onClick={() => store.selectSheet(sheet.id)}
              onDoubleClick={() => setRenaming({ id: sheet.id, value: sheet.name })}
              onContextMenu={(e) => {
                e.preventDefault();
                setMenu({ id: sheet.id, x: e.clientX, y: e.clientY });
              }}
            >
              {sheet.name}
            </button>
          );
        })}
      </div>
      <div className="tabs-spacer" />
      <span className="sheet-count">{store.wb.sheets.length} sheet{store.wb.sheets.length === 1 ? '' : 's'}</span>

      {menu ? (
        <>
          <div className="context-overlay" onMouseDown={() => setMenu(null)} />
          <div className="context-menu" style={{ left: menu.x, top: menu.y - 8 }}>
            <button
              onClick={() => {
                const sheet = store.wb.getSheet(menu.id);
                if (sheet) setRenaming({ id: sheet.id, value: sheet.name });
                setMenu(null);
              }}
            >
              Rename
            </button>
            <button
              onClick={() => {
                store.deleteSheet(menu.id);
                setMenu(null);
              }}
            >
              Delete
            </button>
          </div>
        </>
      ) : null}
    </div>
  );
}
