# Grid

A spreadsheet with an Excel-compatible calculation engine and a UI that is built
to be re-skinned. Built from scratch: tokenizer → parser → evaluator, not a
`eval()` wrapper.

```bash
npm install
npm run dev      # http://localhost:5173
npm test         # engine + UI test suite
npm run build    # production bundle in dist/
```

## What works today

**Calculation engine (`src/engine/`)**

- Full formula grammar: A1 references, `$` anchoring, ranges, whole columns
  (`A:A`) and rows (`1:1`), cross-sheet references (`Data!A1`), quoted sheet names
  (`'Q1 Sales'!B2`), unary/postfix operators and Excel's precedence quirks
  (`-2^2 = 4`, `2^3^2 = 64`, `2^2% = 2^0.02`, `^` left-associative).
- Excel coercion rules: blanks read as `0` / `""` / `FALSE`, text-numbers coerce,
  cross-type comparison ordering (numbers < text < logicals), `""` vs empty cell.
- Excel error values and propagation: `#DIV/0!`, `#VALUE!`, `#REF!`, `#NAME?`,
  `#NUM!`, `#N/A`, `#NULL!`, `#SPILL!`, `#CALC!`.
- Lazy, pull-based recalculation with cycle detection (circular cells report
  `circular` and evaluate to `0`, like Excel's warning mode).
- Dynamic arrays with spill ranges: array results spill into neighbours, conflicts
  raise `#SPILL!`, and spilled cells are readable by other formulas.
- Array lifting for operators — `SUMPRODUCT((A1:A9="x")*B1:B9)` works.
- ~190 worksheet functions across Math, Statistical, Text, Date, Logical, Lookup,
  Financial and Information categories (`src/engine/functions/`).
- Lazy special forms: `IF`, `IFS`, `IFERROR`, `IFNA`, `CHOOSE`, `SWITCH`, `LET`
  (so the untaken branch is never evaluated).
- Number-format engine: sections (`pos;neg;zero;text`), `#,##0.00`, scaling commas,
  percent, scientific, currency literals, and date/time tokens including
  `dddd, mmmm d, yyyy`, `h:mm AM/PM` and elapsed `[h]:mm`.
- Excel's 1900 date system including the phantom 1900-02-29 leap-day bug.
- Workbook model: multiple sheets, per-sheet column widths / row heights,
  frozen-pane metadata, insert/delete rows and columns, defined names,
  JSON serialisation and autosave.

**Application (`src/app/`)**

- Virtualised grid: 1,048,576 × 16,384 cells, only the visible window is rendered.
- Keyboard model: arrows, Tab/Shift+Tab, Enter/Shift+Enter, PageUp/Down,
  Home / Ctrl+Home, Ctrl+Arrow block jumps, F2, Esc, Delete, Ctrl+B/I/U,
  Ctrl+Z / Ctrl+Y.
- Selection: click, Shift+click, drag, whole row/column, select-all, fill handle.
- Editing with in-cell input plus the formula bar, and a Name Box that accepts
  `A1`, `A1:B9`.
- Clipboard with real Excel semantics: copy keeps formulas and relative-reference
  shifting on paste; cut moves; plain-text TSV paste from other apps.
- Formatting: bold/italic/underline/strike, font family/size, text and fill
  colour, alignment, wrap, number-format presets and customs.
- Undo/redo across edits, formatting, structural changes; autosave to
  `localStorage`; JSON save/open; CSV export.
- Function reference panel with search (inserts a call into the formula bar).
- Status bar with live Sum / Average / Count for the selection.

## Architecture

```
src/engine/
  tokenizer.ts    formula lexer (references, sheet names, row/col ranges)
  parser.ts       recursive-descent parser → AST (Excel precedence)
  evaluator.ts    AST evaluation, special forms, array lifting
  workbook.ts     sheets, cells, computed cache, spill registry, recalculation
  values.ts       value model + Excel coercions (RangeValue, ArrayValue)
  format.ts       number/date format engine
  dates.ts        serial <-> date, 1900 leap-year bug
  functions/      function library, registered by category
src/app/
  store.ts        UI state, undo/redo, clipboard, persistence
  components/     Ribbon, FormulaBar, Grid, SheetTabs, StatusBar
  styles.css      all visual tokens live in :root
```

**Recalculation model.** Any mutation bumps an epoch and drops the computed-value
cache. Values are then recomputed lazily as the UI (or another formula) pulls on
them, so the cost tracks what is actually displayed or referenced rather than the
size of the sheet. The next performance step is a dependency graph with
fine-grained invalidation, which lets recalculation be incremental.

## Re-skinning

Every colour, radius and size in the interface resolves from CSS custom
properties declared in `:root` in `src/app/styles.css` (`--accent`, `--bg-surface`,
`--grid-line`, `--row-head-w`, …). Replacing that block re-themes the whole app
without touching a component.

## Known gaps (honest roadmap)

Excel is a decades-deep product; this is a working foundation, not a clone of
every feature. Not yet implemented:

- Charting, pivot tables, pivot charts, sparklines, conditional formatting rules.
- Data validation, data tables, Goal Seek / Solver, Power Query, macros (VBA), Python.
- Merged cells, frozen-pane *interaction* (metadata exists, UI not wired),
  borders per edge, rich text inside a cell.
- Formula rewriting on row/column insert (references currently do **not** adjust).
- Named ranges are supported by the engine but have no UI yet. `LAMBDA`, `MAP`,
  `REDUCE`, `SCAN`, `BYROW`, `BYCOL` are not implemented.
- Undo granularity for sheet-level operations, and 3-D references (`Sheet1:Sheet3!A1`
  parses but evaluates to `#REF!`).
- XLSX import/export (JSON and CSV today), printing, file locking, collaboration.

The engine is structured so these land as additions, not rewrites: new functions
are a `define()` call, new formats extend `format.ts`, and new commands become
`store` mutations that automatically participate in undo and autosave.
