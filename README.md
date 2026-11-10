# Grid

**Grid is a spreadsheet for Windows, written from scratch in C++ — a native `.exe` that
runs offline, with no browser and no runtime to install.**

It follows Excel's own architecture: a Win32 window that paints a virtualised cell
canvas with GDI, with a separate calculation engine behind it (tokenizer → parser →
evaluator over a sparse cell store, with a computed-value cache and circular-reference
detection), and its own number/date formatting layer.

```
Grid.exe — 1.3 MB, x86-64, GUI subsystem, statically linked
```

## Get it running

A ready-built executable is in the workspace at **`native/release/Grid.exe`**.

To rebuild or build on another machine:

```bash
# on Windows: Visual Studio or MinGW-w64 can compile native/ui/*.cpp + native/engine/engine.cpp
# on Linux/macOS: cross-compile to Windows with zig (bundled clang + mingw-w64)
pip install ziglang
cd native
./build.sh            # → release/Grid.exe
```

Engine tests need no Windows and no GUI — they are plain C++17:

```bash
cd native && ./test.sh    # 138 checks, 0 failures
```

See **[native/README.md](native/README.md)** for the full architecture, function list,
file format and the honest list of what is not implemented yet.

## What you get

* A real spreadsheet window: menu bar, formatting toolbar, name box and formula bar,
  virtualised grid with row/column headers, sheet tabs, status bar with live
  Average/Count/Sum of the selection.
* Formulas with Excel semantics: Excel operator precedence, `$` anchoring, ranges,
  whole columns/rows, cross-sheet references, error values, coercion rules and
  Excel's 1900 date system (including its phantom leap day, so serials match exactly).
* 135+ worksheet functions across maths, statistics, conditional aggregation, text,
  logic, lookup, date and financial categories — `SUMIFS`, `SUMPRODUCT`, `XLOOKUP`,
  `INDEX`/`MATCH`, `TEXTJOIN`, `LET`, `PMT`, `DATEDIF`, `TEXT` and many more.
* Formatting: bold/italic/underline, wrap, alignment, font and size, text and fill
  colours, borders, and an Excel-style number-format engine (currency, percent,
  thousands separators, negatives in parentheses, dates, scientific, custom codes).
* Editing workflow you already know: drag selection, Shift+click, Ctrl+arrows,
  Ctrl+Space/Shift+Space, F2, double-click, `Ctrl+1` Format Cells, fill handle,
  Ctrl+D/Ctrl+R, copy/cut/paste with reference shifting, find and replace.
* Structural edits that keep formulas correct: inserting or deleting rows and columns
  rewrites references (ranges spanning an insertion grow; deleted targets become
  `#REF!`).
* 100 levels of undo/redo, `.grid` save/open, CSV import and export, dirty-state
  prompts.

## Project layout

```
native/          the application
  engine/        calculation engine — portable C++17, no Windows API
  ui/            Win32 + GDI desktop shell
  tests/         engine conformance tests
  build.sh       cross-compile to release/Grid.exe
  test.sh        run the engine tests
web/             the earlier browser prototype (TypeScript/React), kept for reference
```

`web/` is not the deliverable any more. It is the prototype whose engine semantics were
ported to C++ and it remains only as a reference implementation; run it with
`cd web && npm install && npm run dev` if you want to compare behaviour.

## Not there yet

Charts, pivot tables, `.xlsx` import/export, conditional formatting, data validation,
tables, named ranges, dynamic arrays/spilling, printing and macros are **not**
implemented. Grid is a working spreadsheet today, not a drop-in Excel replacement; the
gap list is itemised in [native/README.md](native/README.md#what-is-not-implemented-yet).
