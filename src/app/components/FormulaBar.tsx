import React, { useEffect, useRef, useState } from 'react';
import { parseArea } from '../../engine/address';
import { store } from '../store';
import { useStore } from '../useStore';

export function FormulaBar(): React.ReactElement {
  const state = useStore();
  const [text, setText] = useState('');
  const [focused, setFocused] = useState(false);
  const [nameText, setNameText] = useState(store.rangeAddress);
  const [nameFocused, setNameFocused] = useState(false);
  const inputRef = useRef<HTMLInputElement>(null);

  const external = state.editing ? state.editing.draft : store.activeRaw();

  useEffect(() => {
    if (!focused) setText(external);
  }, [external, focused, state.version, state.selection]);

  useEffect(() => {
    if (!nameFocused) setNameText(store.rangeAddress);
  }, [state.selection, nameFocused]);

  const commitAndMove = (move: 'down' | 'none') => {
    store.commitEdit(move);
    setFocused(false);
    inputRef.current?.blur();
  };

  return (
    <div className="formula-bar">
      <input
        className="name-box"
        value={nameText}
        spellCheck={false}
        title="Name Box — type A1 or a range, then Enter"
        onChange={(e) => setNameText(e.target.value)}
        onFocus={(e) => {
          setNameFocused(true);
          e.currentTarget.select();
        }}
        onBlur={() => setNameFocused(false)}
        onKeyDown={(e) => {
          if (e.key === 'Enter') {
            const area = parseArea(nameText.trim());
            if (area) {
              store.selectRange(area.start.row, area.start.col, area.end.row, area.end.col);
            } else {
              store.toast('Not a valid range');
            }
            setNameFocused(false);
            e.currentTarget.blur();
            document.querySelector<HTMLElement>('.grid-scroll')?.focus();
          }
          if (e.key === 'Escape') {
            setNameText(store.rangeAddress);
            e.currentTarget.blur();
          }
        }}
      />
      <button
        className={'fx-button' + (state.showFunctions ? ' on' : '')}
        title="Insert function"
        onMouseDown={(e) => e.preventDefault()}
        onClick={() => store.setShowFunctions(!state.showFunctions)}
      >
        fx
      </button>
      <span className="divider-dot" />
      <div className="formula-input-wrap">
        <input
          ref={inputRef}
          className="formula-input"
          value={text}
          spellCheck={false}
          placeholder="Enter a value, or start with = for a formula"
          onChange={(e) => {
            const next = e.target.value;
            setText(next);
            if (!store.state.editing) store.beginEdit();
            store.setDraft(next);
          }}
          onFocus={() => setFocused(true)}
          onBlur={() => {
            setFocused(false);
            if (store.state.editing) store.commitEdit('none');
          }}
          onKeyDown={(e) => {
            if (e.key === 'Enter') {
              e.preventDefault();
              commitAndMove('down');
            } else if (e.key === 'Tab') {
              e.preventDefault();
              store.commitEdit('right');
              setFocused(false);
              document.querySelector<HTMLElement>('.grid-scroll')?.focus();
            } else if (e.key === 'Escape') {
              e.preventDefault();
              store.cancelEdit();
              setFocused(false);
              document.querySelector<HTMLElement>('.grid-scroll')?.focus();
            }
          }}
        />
      </div>
    </div>
  );
}
