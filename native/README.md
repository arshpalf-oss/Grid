# Grid — native Windows spreadsheet

Grid is a spreadsheet application written from scratch in C++17 for Windows. It is a
single native `.exe` built with the Win32 API and GDI — the same platform stack Excel
itself is built on. There is no browser, no Electron, no .NET runtime and no network
access at any point: copy the executable to a machine, double-click it, and it works
offline.

```
native/
  engine/      calculation engine (pure C++17, no Windows dependency)
    engine.h   public API
    engine.cpp parser, evaluator, 135+ functions, formats, dates, workbook, file IO
  ui/          Win32 + GDI desktop shell
    app.h      shared application state
    main.cpp   entry point, main window, menus, child edit controls
    util.cpp   fonts, geometry, scrolling, status bar
    sheet.cpp  the spreadsheet canvas: painting, selection, editing, mouse/keyboard
    chrome.cpp toolbar, formula bar, sheet tabs, status bar
    commands.cpp every command: navigation, formatting, clipboard, fills, undo, files
    dialogs.cpp Format Cells, Find & Replace, prompts
  tests/       engine conformance tests (139 assertions, shared with --selftest)
  build.sh     cross-compiles release/Grid.exe
  test.sh      runs the engine tests
```

## Building

The build needs a C++17 compiler targeting Windows. On Windows, Visual Studio or
MinGW-w64 both work. From Linux or macOS you can cross-compile with
[zig](https://ziglang.org) (its bundled clang and mingw-w64 headers/runtime), which is
what `build.sh` automates:

```bash
pip install ziglang            # once, if you don't already have zig
cd native
./build.sh                     # → release/Grid.exe  (x86-64, GUI subsystem, no DLLs)
./build.sh debug               # → release/Grid-debug.exe
```

`build.sh` links `gdi32`, `comdlg32`, `user32`, `kernel32` and `shell32` and produces a
self-contained PE32+ executable (about 1.3 MB). Nothing else is required on the target
machine — Windows 7 or later, 64-bit.

Running the engine tests does not need Windows at all, because the engine is plain
standard C++:

```bash
cd native && ./test.sh         # 139 checks, 0 failures
```

The executable carries the same engine checks internally, so you can verify a build on
any Windows machine without opening a window:

```
Grid.exe --selftest
```

It runs the engine assertions (formulas, ranges, functions, formats, dates, recalc,
reference rewriting, workbook round trip), writes `%TEMP%\grid-selftest.txt` and shows
the pass/fail summary.

## How it is put together

Grid follows the layering Excel uses:

1. **Cell store.** Each sheet holds a sparse `unordered_map` keyed by
   `(row << 20) | col`, with the full 1,048,576 × 16,384 sheet addressable but only the
   cells that exist occupying memory. Cell dimensions are stored the same way.
2. **Formula pipeline.** `=` text is lexed into tokens, parsed by a recursive-descent
   parser with Excel's operator precedence (`:` before space before `,` before unary
   minus before `^` before `*` `/` before `+` `-` before `&` before comparisons), then
   evaluated against the workbook.
3. **Calculation.** Values are pulled on demand through a computed-value cache with an
   in-flight set, so a dependency chain is evaluated once and circular references are
   detected (and reported) instead of recursing forever.
4. **Value model.** Blank / Number / Text / Bool / Error with Excel's coercion rules
   (`#DIV/0!`, `#VALUE!`, `#REF!`, `#NAME?`, `#NUM!`, `#N/A`, `#SPILL!`) and Excel's
   comparison ordering (numbers < text < FALSE < TRUE).
5. **Formatting.** A number-format interpreter supporting General, fixed decimals,
   thousands separators, parentheses for negatives, currency, percent, date/time tokens
   (`yyyy`, `mmm`, `dddd`, `hh:mm:ss`, `AM/PM`), scientific notation, text placeholders
   and multi-section formats.
6. **The grid window.** One top-level window paints a virtualised canvas — only the
   cells that intersect the viewport are laid out or drawn — together with headers,
   the toolbar ribbon, formula bar, sheet tabs and status bar. Three real `EDIT`
   controls (name box, formula field, in-cell editor) provide caret, selection and IME
   behaviour; everything else is drawn and hit-tested by hand, which is how Excel's own
   grid window works.

## What works

**Files.** New, Open, Save, Save As, Export CSV, CSV import, dirty-state tracking,
"save changes?" prompts. Workbooks are stored as `.grid`, a deterministic UTF-8 text
format (see below).

**Editing.** Click, drag-select, Shift+click, Shift+arrows, Ctrl+arrows to jump to the
edge of a block, Ctrl+Home/End, Page Up/Down, Ctrl+Space / Shift+Space for whole
columns and rows, click or drag headers, select-all, F2 and double-click to edit,
Esc to cancel, typing to start an entry, Delete to clear.

**Formula bar.** Address box (type `B7` or `A1:C20` and press Enter), live formula field,
`fx` label, in-cell editing mirrored on commit.

**Formulas.** Arithmetic, comparison, text concatenation with `&`, percent literals,
cell and area references, absolute `$` markers, cross-sheet references
(`Sheet2!A1`, `'My Sheet'!A1:B2`), string literals with doubled quotes, error literals,
and 135+ functions:

* maths — SUM, PRODUCT, SUMSQ, ABS, SIGN, INT, TRUNC, ROUND, ROUNDUP, ROUNDDOWN,
  MROUND, CEILING, FLOOR, MOD, QUOTIENT, POWER, SQRT, EXP, LN, LOG, LOG10, PI,
  SIN, COS, TAN, ASIN, ACOS, ATAN, ATAN2, DEGREES, RADIANS, RAND, RANDBETWEEN
* statistics — AVERAGE, AVERAGEA, COUNT, COUNTA, COUNTBLANK, MAX, MIN, MEDIAN,
  LARGE, SMALL, VAR(.S/.P), STDEV(.S/.P), RANK, SUMPRODUCT
* conditional aggregation — SUMIF(S), COUNTIF(S), AVERAGEIF(S), MAXIFS, MINIFS with
  Excel's criteria syntax (`">10"`, `"<>x"`, wildcards `*` `?`)
* text — LEFT, RIGHT, MID, LEN, UPPER, LOWER, PROPER, TRIM, CONCAT, CONCATENATE,
  TEXTJOIN, SUBSTITUTE, REPLACE, FIND, SEARCH, EXACT, REPT, VALUE, TEXT, CHAR, CODE
* logic — IF, IFS, IFERROR, IFNA, AND, OR, NOT, TRUE, FALSE, CHOOSE, SWITCH, LET
  (lazy forms only evaluate the branch that is taken)
* lookup — VLOOKUP, HLOOKUP, XLOOKUP, LOOKUP, INDEX, MATCH, ROW, COLUMN, ROWS, COLUMNS
* dates — TODAY, NOW, DATE, DATEVALUE, TIMEVALUE, YEAR, MONTH, DAY, HOUR, MINUTE,
  SECOND, WEEKDAY, WEEKNUM, EDATE, EOMONTH, DAYS, DATEDIF
* financial — PMT, FV, PV, NPER, NPV, IRR, SLN, SYD
* information — ISBLANK, ISNUMBER, ISTEXT, ISERROR, ISERR, ISNA, ISLOGICAL, ISEVEN,
  ISODD, NA, TYPE, N

Dates use Excel's serial calendar, including the deliberate 1900 leap-year bug, so
serial numbers match Excel exactly (2024-01-15 = 45306).

**Formatting.** Bold, italic, underline, wrap text, left/centre/right alignment, font
face and size, text and fill colours, borders, and number formats — General, Number,
Currency, Percent, Comma, Date, Time, Scientific, custom codes, increase/decrease
decimals. The Format Cells dialog (Ctrl+1) covers all of it.

**Clipboard.** Copy, cut and paste within the grid with formula reference adjustment,
plus text interop with other applications (tab-separated, newline-separated) in both
directions.

**Fill.** Fill handle drag, Ctrl+D (fill down) and Ctrl+R (fill right), with arithmetic
series detection for numeric columns and relative-reference shifting.

**Undo.** 100 levels of undo and redo, snapshot based, covering edits, styles, fills,
clipboard operations, row/column inserts and deletes, sheet operations and formatting.

**Rows, columns and sheets.** Insert/delete rows and columns, drag header edges to
resize (double-click to autofit), insert/delete/rename/reorder-by-Ctrl+PageUp sheets,
per-sheet column widths and row heights. Formulas are rewritten when rows or columns
are inserted or deleted — ranges that span an insertion grow, deleted targets become
`#REF!`, exactly as in Excel.

**Status bar.** Ready/Enter/Edit states plus Average, Count and Sum of the current
selection, and the current zoom (100%).

## What is not implemented yet

These are the honest gaps against Excel. They are feature-sized pieces of work, not
polish:

* dynamic arrays and spilling (`SEQUENCE`, `FILTER`, `SORT`, `UNIQUE`, `#` operator)
* charts, images, shapes, sparklines
* pivot tables, Power Query, data tables, scenarios
* conditional formatting, data validation, cell comments/notes
* Excel's `.xlsx` file format (Grid uses its own `.grid` text format; CSV interop is
  supported both ways)
* tables, structured references, named ranges, defined names
* macros/VBA, add-ins, multi-window views, freeze panes, split view
* printing and page setup
* iterative calculation settings, precision-as-displayed, manual calculation mode
* some 300 Excel functions (engineering, cube, most of the newer dynamic ones)

## File format

`.grid` files are UTF-8 text, one record per line, tab separated, with `\\`, `\n` and
`\t` escaped inside fields. The first line is `GRID2`.

```
GRID2
SHEET   Sheet1
W       2       150                     column width (96-dpi pixels)
H       4       42                      row height
C       0       0       0000    0       0       0       0               "Segoe UI"      10      "$#,##0.00"     =B2*C2
```

The `C` record is
`r, c, flags(bold/italic/underline/wrap), align, fill, colour, border, font, size, number format, raw text`.
Records are emitted in a deterministic order, so saved workbooks diff cleanly.
