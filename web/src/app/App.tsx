import React, { useEffect, useMemo, useState } from 'react';
import { allFunctions } from '../engine/functions/registry';
import { store } from './store';
import { useStore } from './useStore';
import { GridView } from './components/Grid';
import { FormulaBar } from './components/FormulaBar';
import { Ribbon } from './components/Ribbon';
import { SheetTabs } from './components/SheetTabs';
import { StatusBar } from './components/StatusBar';

const BLURBS: Record<string, string> = {
  SUM: 'Adds numbers in ranges or arguments.',
  SUMIF: 'Adds cells that meet one condition.',
  SUMIFS: 'Adds cells that meet multiple conditions.',
  SUMPRODUCT: 'Multiplies aligned arrays, then adds the results.',
  AVERAGE: 'Arithmetic mean of numbers.',
  COUNT: 'Counts numeric cells.',
  COUNTA: 'Counts non-empty cells.',
  COUNTIF: 'Counts cells meeting a condition.',
  MAX: 'Largest number in the arguments.',
  MIN: 'Smallest number in the arguments.',
  ROUND: 'Rounds half away from zero to N digits.',
  IF: 'Returns one value when a condition is true, another otherwise.',
  IFS: 'Tests several conditions in order.',
  IFERROR: 'Substitutes a value when a formula errors.',
  AND: 'TRUE when every argument is TRUE.',
  OR: 'TRUE when any argument is TRUE.',
  VLOOKUP: 'Vertical lookup in the first column of a table.',
  HLOOKUP: 'Horizontal lookup in the first row of a table.',
  XLOOKUP: 'Modern lookup with exact match and fallbacks.',
  INDEX: 'Returns a value at a position inside a range.',
  MATCH: 'Position of a value in a one-dimensional range.',
  FILTER: 'Spills rows that meet a condition.',
  SORT: 'Spills a sorted copy of a range.',
  UNIQUE: 'Spills the distinct values of a range.',
  SEQUENCE: 'Spills a sequence of numbers.',
  TRANSPOSE: 'Swaps rows and columns.',
  LET: 'Names intermediate values inside a formula.',
  TEXT: 'Formats a value using a number format.',
  TEXTJOIN: 'Joins text with a delimiter.',
  LEFT: 'First N characters of a string.',
  MID: 'Substring from a position.',
  SUBSTITUTE: 'Replaces occurrences of text.',
  TODAY: 'Current date as an Excel serial number.',
  NOW: 'Current date and time.',
  DATE: 'Builds a date serial from year, month, day.',
  EOMONTH: 'Last day of the month N months away.',
  PMT: 'Loan payment for constant payments and rate.',
  IRR: 'Internal rate of return for cash flows.',
  NPV: 'Net present value of future cash flows.',
  ISERROR: 'TRUE when the value is an error.',
  INDIRECT: 'Turns text into a live reference.',
  OFFSET: 'Reference shifted by rows and columns.',
};

export function App(): React.ReactElement {
  const state = useStore();

  useEffect(() => {
    const el = document.querySelector<HTMLElement>('.grid-scroll');
    el?.focus();
  }, []);

  return (
    <div className="app">
      <Ribbon />
      <FormulaBar />
      <div className="main">
        <GridView />
        {state.showFunctions ? <FunctionPanel /> : null}
      </div>
      <SheetTabs />
      <StatusBar />
      {state.toast ? <div className="toast">{state.toast}</div> : null}
    </div>
  );
}

function FunctionPanel(): React.ReactElement {
  const [query, setQuery] = useState('');
  const [openCategory, setOpenCategory] = useState<string | null>('Math');

  const groups = useMemo(() => {
    const all = allFunctions();
    const filtered = query
      ? all.filter(
          (f) =>
            f.name.toLowerCase().includes(query.toLowerCase()) ||
            (BLURBS[f.name] ?? '').toLowerCase().includes(query.toLowerCase())
        )
      : all;
    const map = new Map<string, typeof all>();
    for (const fn of filtered) {
      const list = map.get(fn.category) ?? [];
      list.push(fn);
      map.set(fn.category, list);
    }
    return [...map.entries()].sort((a, b) => a[0].localeCompare(b[0]));
  }, [query]);

  const insert = (name: string) => {
    if (!store.state.editing) store.beginEdit();
    store.setDraft(`=${name}(`);
    setTimeout(() => {
      const input = document.querySelector<HTMLInputElement>('.formula-input');
      input?.focus();
      const len = input?.value.length ?? 0;
      input?.setSelectionRange(len, len);
    }, 0);
  };

  return (
    <aside className="function-panel">
      <div className="panel-head">
        <h3>Functions</h3>
        <button className="panel-close" onClick={() => store.setShowFunctions(false)} title="Close">
          ×
        </button>
      </div>
      <input
        className="panel-search"
        placeholder={`Search ${allFunctions().length} functions…`}
        value={query}
        onChange={(e) => setQuery(e.target.value)}
      />
      <div className="panel-body">
        {groups.map(([category, fns]) => (
          <div key={category} className="fn-group">
            <button
              className="fn-category"
              onClick={() => setOpenCategory(openCategory === category ? null : category)}
            >
              <span>{category}</span>
              <span className="fn-count">{fns.length}</span>
            </button>
            {openCategory === category || query ? (
              <div className="fn-list">
                {fns.map((fn) => (
                  <button key={fn.name} className="fn-item" onClick={() => insert(fn.name)} title={`Insert ${fn.name}`}>
                    <span className="fn-name">{fn.name}</span>
                    {BLURBS[fn.name] ? <span className="fn-blurb">{BLURBS[fn.name]}</span> : null}
                  </button>
                ))}
              </div>
            ) : null}
          </div>
        ))}
        {groups.length === 0 ? <p className="panel-empty">No function matches “{query}”.</p> : null}
      </div>
    </aside>
  );
}
