// Every command the application can perform: navigation, editing, formatting,
// clipboard, filling, undo/redo, worksheet management and file I/O.
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "app.h"

namespace grid {
namespace ui {

namespace {

const uint32_t kDefaultTextColor = 0;

// ---------------------------------------------------------------- file helpers
bool writeFileW(const std::wstring& path, const std::string& data) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) != 0;
  CloseHandle(h);
  return ok && written == data.size();
}

bool readFileW(const std::wstring& path, std::string& out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  GetFileSizeEx(h, &size);
  out.resize(static_cast<size_t>(size.QuadPart));
  DWORD got = 0;
  bool ok = out.empty() || ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &got, nullptr) != 0;
  CloseHandle(h);
  return ok;
}

void setClipboardText(const std::wstring& text) {
  if (!OpenClipboard(g.hwnd)) return;
  EmptyClipboard();
  size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (h) {
    void* p = GlobalLock(h);
    if (p) {
      memcpy(p, text.c_str(), bytes);
      GlobalUnlock(h);
      if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
    } else {
      GlobalFree(h);
    }
  }
  CloseClipboard();
}

bool getClipboardText(std::wstring& out) {
  if (!OpenClipboard(g.hwnd)) return false;
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  bool ok = false;
  if (h) {
    const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h));
    if (p) {
      out = p;
      GlobalUnlock(h);
      ok = true;
    }
  }
  CloseClipboard();
  return ok;
}

std::string snapshot() {
  return g.wb.serialize() + "\x1f" + std::to_string(g.sheet);
}

void restoreSnapshot(const std::string& snap) {
  size_t sep = snap.rfind('\x1f');
  std::string data = sep == std::string::npos ? snap : snap.substr(0, sep);
  int sheet = 0;
  if (sep != std::string::npos) sheet = std::atoi(snap.c_str() + sep + 1);
  g.wb.deserialize(data);
  g.sheet = std::max(0, std::min(g.wb.sheetCount() - 1, sheet));
  g.sel.normalise();
  g.sel.r1 = g.sel.r2 = std::min(g.sel.r2, MAX_ROWS - 1);
  g.sel.c1 = g.sel.c2 = std::min(g.sel.c2, MAX_COLS - 1);
  g.marquee = false;
  syncControls();
  updateScrollBars();
  invalidateAll();
}

// Applies fn to every cell of the selection. Huge selections (whole columns)
// only touch cells that actually exist, which is what Excel's sparse model does.
void applyToSelection(const std::function<void(int, int)>& fn) {
  Range r = g.sel;
  if (r.count() <= 200000) {
    for (int row = r.top(); row <= r.bottom(); row++)
      for (int col = r.left(); col <= r.right(); col++) fn(row, col);
    return;
  }
  const Sheet& sh = g.wb.sheet(g.sheet);
  std::vector<int64_t> keys;
  keys.reserve(sh.cells.size());
  for (auto& kv : sh.cells) {
    int rr = static_cast<int>(kv.first >> 20), cc = static_cast<int>(kv.first & 0xFFFFF);
    if (r.contains(rr, cc)) keys.push_back(kv.first);
  }
  for (int64_t key : keys)
    fn(static_cast<int>(key >> 20), static_cast<int>(key & 0xFFFFF));
}

void modifyStyles(const std::function<void(Style&, int, int)>& fn) {
  pushUndo();
  applyToSelection([&](int r, int c) {
    Style st = g.wb.styleAt(g.sheet, r, c);
    fn(st, r, c);
    g.wb.setStyle(g.sheet, r, c, st);
  });
  g.dirty = true;
  updateTitle();
  syncFormulaBar();
  invalidateAll();
}

std::string adjustDecimals(const std::string& fmt, int delta) {
  if (fmt.empty()) return delta > 0 ? "0." + std::string(static_cast<size_t>(delta), '0') : "0";
  size_t pct = fmt.find('%');
  std::string suffix = pct == std::string::npos ? "" : "%";
  std::string base = pct == std::string::npos ? fmt : fmt.substr(0, pct);
  size_t dot = base.find_last_of('.');
  int dec = 0;
  if (dot == std::string::npos) {
    base += ".";
  } else {
    size_t i = dot + 1;
    while (i < base.size() && (base[i] == '0' || base[i] == '#')) {
      dec++;
      i++;
    }
    base = base.substr(0, dot + 1);
  }
  dec = std::max(0, std::min(10, dec + delta));
  if (dec == 0) {
    if (!base.empty() && base.back() == '.') base.pop_back();
  } else {
    base += std::string(static_cast<size_t>(dec), '0');
  }
  return base + suffix;
}

std::wstring fileNameOf(const std::string& path) {
  std::string name = path;
  size_t slash = name.find_last_of("\\/");
  if (slash != std::string::npos) name = name.substr(slash + 1);
  return widen(name.empty() ? "Book1" : name);
}

bool endsWith(const std::string& s, const char* suffix) {
  size_t n = strlen(suffix);
  if (s.size() < n) return false;
  std::string tail = s.substr(s.size() - n);
  for (auto& ch : tail) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return tail == suffix;
}

// ---------------------------------------------------------------- CSV import
std::vector<std::vector<std::string>> parseCsv(const std::string& text) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string field;
  bool quoted = false;
  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < text.size() && text[i + 1] == '"') {
          field += '"';
          i++;
        } else {
          quoted = false;
        }
      } else {
        field += c;
      }
      continue;
    }
    if (c == '"') {
      quoted = true;
    } else if (c == ',') {
      row.push_back(field);
      field.clear();
    } else if (c == '\r') {
      // ignore
    } else if (c == '\n') {
      row.push_back(field);
      field.clear();
      rows.push_back(row);
      row.clear();
    } else {
      field += c;
    }
  }
  if (!field.empty() || !row.empty()) {
    row.push_back(field);
    rows.push_back(row);
  }
  return rows;
}

void importCsv(const std::string& raw) {
  std::string text = raw;
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
    text = text.substr(3);
  auto rows = parseCsv(text);
  g.wb = Workbook();
  g.wb.sheet(0).name = "Sheet1";
  g.sheet = 0;
  int maxRows = std::min<int>(MAX_ROWS, static_cast<int>(rows.size()));
  for (int r = 0; r < maxRows; r++) {
    int cols = std::min<int>(MAX_COLS, static_cast<int>(rows[r].size()));
    for (int c = 0; c < cols; c++) {
      if (!rows[r][c].empty()) g.wb.setRaw(0, r, c, rows[r][c]);
    }
  }
  g.sel = Range{};
  g.scrollX = g.scrollY = 0;
  g.undoStack.clear();
  g.redoStack.clear();
  syncControls();
  updateScrollBars();
  invalidateAll();
}

}  // namespace

// ------------------------------------------------------------------ undo
void pushUndo() {
  g.undoStack.push_back(snapshot());
  if (g.undoStack.size() > App::kUndoLimit) g.undoStack.pop_front();
  g.redoStack.clear();
}

void undo() {
  if (g.undoStack.empty()) {
    setStatus(L"Nothing to undo");
    return;
  }
  g.redoStack.push_back(snapshot());
  std::string prev = g.undoStack.back();
  g.undoStack.pop_back();
  restoreSnapshot(prev);
  g.dirty = true;
  updateTitle();
  setStatus(L"Undo");
}

void redo() {
  if (g.redoStack.empty()) {
    setStatus(L"Nothing to redo");
    return;
  }
  g.undoStack.push_back(snapshot());
  std::string next = g.redoStack.back();
  g.redoStack.pop_back();
  restoreSnapshot(next);
  g.dirty = true;
  updateTitle();
  setStatus(L"Redo");
}

// ------------------------------------------------------------------ navigation
void moveSelectionBy(int dr, int dc, bool extend) {
  int r = extend ? g.sel.r2 : g.sel.r1;
  int c = extend ? g.sel.c2 : g.sel.c1;
  r = std::max(0, std::min(MAX_ROWS - 1, r + dr));
  c = std::max(0, std::min(MAX_COLS - 1, c + dc));
  selectCell(r, c, extend);
  setStatus(L"");
}

void moveToEdge(int dr, int dc, bool extend) {
  int r = extend ? g.sel.r2 : g.sel.r1;
  int c = extend ? g.sel.c2 : g.sel.c1;
  auto blank = [&](int rr, int cc) { return g.wb.raw(g.sheet, rr, cc).empty(); };
  if (blank(r, c) && blank(std::min(MAX_ROWS - 1, std::max(0, r + dr)), std::min(MAX_COLS - 1, std::max(0, c + dc)))) {
    // move to the next non-empty cell in that direction
    int rr = r, cc = c;
    while (true) {
      int nr = rr + dr, nc = cc + dc;
      if (nr < 0 || nc < 0 || nr >= MAX_ROWS || nc >= MAX_COLS) break;
      rr = nr;
      cc = nc;
      if (!blank(rr, cc)) break;
    }
    selectCell(rr, cc, extend);
    return;
  }
  int rr = r, cc = c;
  while (true) {
    int nr = rr + dr, nc = cc + dc;
    if (nr < 0 || nc < 0 || nr >= MAX_ROWS || nc >= MAX_COLS) break;
    if (blank(nr, nc)) break;
    rr = nr;
    cc = nc;
  }
  if (rr == r && cc == c) {
    // current cell is the last of its block: step once into the gap
    rr = std::max(0, std::min(MAX_ROWS - 1, rr + dr));
    cc = std::max(0, std::min(MAX_COLS - 1, cc + dc));
  }
  selectCell(rr, cc, extend);
  setStatus(L"");
}

void pageMove(int dir, bool extend) {
  int visible = std::max(1, viewportHeight() / std::max(1, S(22)) - 1);
  int target = (extend ? g.sel.r2 : g.sel.r1) + dir * visible;
  target = std::max(0, std::min(MAX_ROWS - 1, target));
  selectCell(target, extend ? g.sel.c2 : g.sel.c1, extend);
}

// ------------------------------------------------------------------ editing helpers
void clearFormats() {
  pushUndo();
  applyToSelection([&](int row, int col) { g.wb.setStyle(g.sheet, row, col, Style()); });
  g.dirty = true;
  updateTitle();
  invalidateAll();
}

void clearSelection(bool formatsToo) {
  pushUndo();
  Range r = g.sel;
  applyToSelection([&](int row, int col) { g.wb.setRaw(g.sheet, row, col, ""); });
  if (formatsToo) {
    applyToSelection([&](int row, int col) { g.wb.setStyle(g.sheet, row, col, Style()); });
  }
  (void)r;
  g.dirty = true;
  updateTitle();
  syncFormulaBar();
  invalidateAll();
}

// ------------------------------------------------------------------ styles
void applyStyle(const Style& patch) {
  pushUndo();
  applyToSelection([&](int r, int c) { g.wb.patchStyle(g.sheet, r, c, patch, false); });
  g.dirty = true;
  updateTitle();
  invalidateAll();
}

void applyFullStyle(const Style& st) {
  pushUndo();
  applyToSelection([&](int r, int c) { g.wb.setStyle(g.sheet, r, c, st); });
  g.dirty = true;
  updateTitle();
  invalidateAll();
}

void toggleStyleFlag(int flag) {
  Style cur = g.wb.styleAt(g.sheet, g.sel.r1, g.sel.c1);
  bool on = false;
  switch (flag) {
    case 1: on = cur.bold; break;
    case 2: on = cur.italic; break;
    case 4: on = cur.underline; break;
    case 8: on = cur.wrap; break;
    default: break;
  }
  bool target = !on;
  modifyStyles([&](Style& st, int, int) {
    switch (flag) {
      case 1: st.bold = target; break;
      case 2: st.italic = target; break;
      case 4: st.underline = target; break;
      case 8: st.wrap = target; break;
      default: break;
    }
  });
}

void setNumberFormat(const std::string& fmt) {
  pushUndo();
  applyToSelection([&](int r, int c) {
    Style st = g.wb.styleAt(g.sheet, r, c);
    st.numFmt = fmt;
    g.wb.setStyle(g.sheet, r, c, st);
  });
  g.dirty = true;
  updateTitle();
  invalidateAll();
}

void changeDecimals(int delta) {
  std::string cur = g.wb.numberFormat(g.sheet, g.sel.r1, g.sel.c1);
  setNumberFormat(adjustDecimals(cur, delta));
}

// ------------------------------------------------------------------ clipboard
void copySelection(bool cut) {
  Range r = g.sel;
  // guard against copying an entire sheet
  Range rr = r;
  if (rr.count() > 400000) {
    int r1, c1, r2, c2;
    if (g.wb.usedBounds(g.sheet, r1, c1, r2, c2)) {
      rr.r1 = std::max(rr.top(), r1);
      rr.c1 = std::max(rr.left(), c1);
      rr.r2 = std::min(rr.bottom(), r2);
      rr.c2 = std::min(rr.right(), c2);
      rr.normalise();
      if (rr.rows() < 1 || rr.cols() < 1) rr = r = Range{};
    }
  }
  g.clipCells.clear();
  std::wstring text;
  for (int row = rr.top(); row <= rr.bottom(); row++) {
    std::vector<ClipboardCell> line;
    for (int col = rr.left(); col <= rr.right(); col++) {
      ClipboardCell cc;
      cc.raw = g.wb.raw(g.sheet, row, col);
      cc.style = g.wb.styleAt(g.sheet, row, col);
      line.push_back(cc);
      if (col > rr.left()) text += L'\t';
      text += widen(g.wb.display(g.sheet, row, col));
    }
    g.clipCells.push_back(line);
    if (row < rr.bottom()) text += L"\r\n";
  }
  setClipboardText(text);
  g.hasInternalClip = true;
  g.clipIsCut = cut;
  g.clipSeq = GetClipboardSequenceNumber();
  g.clipSource = rr;
  g.clipSheet = g.sheet;
  g.marquee = true;
  g.marqueeOn = true;
  SetTimer(g.hwnd, 7, 450, nullptr);
  invalidateAll();
  setStatus(cut ? L"Cut" : L"Copy");
}

void pasteClipboard() {
  if (g.hasInternalClip && GetClipboardSequenceNumber() == g.clipSeq) {
    pushUndo();
    Range dst = g.sel;
    int rowOff = dst.top() - g.clipSource.top();
    int colOff = dst.left() - g.clipSource.left();
    Range src = g.clipSource;
    if (g.clipIsCut) {
      for (int row = src.top(); row <= src.bottom(); row++)
        for (int col = src.left(); col <= src.right(); col++) g.wb.setRaw(g.clipSheet, row, col, "");
    }
    for (size_t i = 0; i < g.clipCells.size(); i++) {
      for (size_t j = 0; j < g.clipCells[i].size(); j++) {
        int tr = dst.top() + static_cast<int>(i);
        int tc = dst.left() + static_cast<int>(j);
        if (tr >= MAX_ROWS || tc >= MAX_COLS) continue;
        const ClipboardCell& cell = g.clipCells[i][j];
        std::string raw = cell.raw;
        if (!g.clipIsCut && !raw.empty() && raw[0] == '=') raw = shiftFormula(raw, rowOff, colOff);
        if (raw.size() > 32767) raw = raw.substr(0, 32767);
        g.wb.setRaw(g.sheet, tr, tc, raw);
        g.wb.setStyle(g.sheet, tr, tc, cell.style);
      }
    }
    int height = static_cast<int>(g.clipCells.size());
    int width = height > 0 ? static_cast<int>(g.clipCells[0].size()) : 0;
    if (height > 0 && width > 0) selectRange(dst.top(), dst.left(), dst.top() + height - 1, dst.left() + width - 1);
    g.clipIsCut = false;
    g.marquee = false;
    KillTimer(g.hwnd, 7);
    g.dirty = true;
    updateTitle();
    invalidateAll();
    setStatus(L"Paste");
    return;
  }

  std::wstring text;
  if (!getClipboardText(text) || text.empty()) {
    setStatus(L"Clipboard is empty");
    return;
  }
  std::string utf8 = narrow(text);
  pushUndo();
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string field;
  for (size_t i = 0; i < utf8.size(); i++) {
    char c = utf8[i];
    if (c == '\r') continue;
    if (c == '\n') {
      row.push_back(field);
      field.clear();
      rows.push_back(row);
      row.clear();
    } else if (c == '\t') {
      row.push_back(field);
      field.clear();
    } else {
      field += c;
    }
  }
  if (!field.empty() || !row.empty()) {
    row.push_back(field);
    rows.push_back(row);
  }
  int top = g.sel.top(), left = g.sel.left();
  int lastRow = top, lastCol = left;
  for (size_t i = 0; i < rows.size(); i++) {
    for (size_t j = 0; j < rows[i].size(); j++) {
      int tr = top + static_cast<int>(i), tc = left + static_cast<int>(j);
      if (tr >= MAX_ROWS || tc >= MAX_COLS) continue;
      g.wb.setRaw(g.sheet, tr, tc, rows[i][j]);
      lastRow = tr;
      lastCol = tc;
    }
  }
  if (rows.size() > 1 || (!rows.empty() && rows[0].size() > 1))
    selectRange(top, left, lastRow, lastCol);
  g.dirty = true;
  updateTitle();
  invalidateAll();
  setStatus(L"Paste");
}

// ------------------------------------------------------------------ fill
void fillSelection(int dr, int dc) {
  if (dr <= 0 && dc <= 0) return;
  pushUndo();
  Range start = g.sel;
  int rows = start.rows(), cols = start.cols();
  Range dst = start;
  dst.r2 = std::min(MAX_ROWS - 1, start.bottom() + std::max(0, dr));
  dst.c2 = std::min(MAX_COLS - 1, start.right() + std::max(0, dc));

  // fill right: cycle the source columns, shifting relative references
  for (int target = start.right() + 1; target <= dst.right(); target++) {
    for (int row = start.top(); row <= start.bottom(); row++) {
      int srcCol = start.left() + ((target - start.right() - 1) % cols);
      std::string raw = g.wb.raw(g.sheet, row, srcCol);
      if (!raw.empty() && raw[0] == '=') raw = shiftFormula(raw, 0, target - srcCol);
      g.wb.setRaw(g.sheet, row, target, raw);
      g.wb.setStyle(g.sheet, row, target, g.wb.styleAt(g.sheet, row, srcCol));
    }
  }

  // fill down: extrapolate arithmetic series when the source column is numeric,
  // otherwise cycle the source rows, as Excel does.
  for (int col = start.left(); col <= dst.right(); col++) {
    bool series = start.rows() >= 2;
    double first = 0, last = 0;
    if (series) {
      for (int k = 0; k < rows; k++) {
        std::string raw = g.wb.raw(g.sheet, start.top() + k, col);
        if (!raw.empty() && raw[0] == '=') {
          series = false;
          break;
        }
        Value v = g.wb.value(g.sheet, start.top() + k, col);
        if (v.type != Value::Number) {
          series = false;
          break;
        }
        if (k == 0) first = v.num;
        if (k == rows - 1) last = v.num;
      }
    }
    double step = rows > 1 ? (last - first) / static_cast<double>(rows - 1) : 0.0;
    for (int target = start.bottom() + 1; target <= dst.bottom(); target++) {
      int srcRow = start.top() + ((target - start.bottom() - 1) % rows);
      if (series) {
        double value = last + step * (target - start.bottom());
        g.wb.setRaw(g.sheet, target, col, generalText(Value::number(value)));
      } else {
        std::string raw = g.wb.raw(g.sheet, srcRow, col);
        if (!raw.empty() && raw[0] == '=') raw = shiftFormula(raw, target - srcRow, 0);
        g.wb.setRaw(g.sheet, target, col, raw);
      }
      g.wb.setStyle(g.sheet, target, col, g.wb.styleAt(g.sheet, srcRow, col));
    }
  }

  g.sel = dst;
  g.sel.normalise();
  ensureVisible();
  g.dirty = true;
  updateTitle();
  syncControls();
  invalidateAll();
}

// ------------------------------------------------------------------ rows/cols
void insertRows(int before, int count) {
  if (count <= 0) return;
  pushUndo();
  g.wb.insertRows(g.sheet, before, count);
  g.dirty = true;
  updateTitle();
  syncControls();
  invalidateAll();
}

void deleteRows(int start, int count) {
  if (count <= 0) return;
  pushUndo();
  g.wb.deleteRows(g.sheet, start, count);
  g.dirty = true;
  updateTitle();
  syncControls();
  invalidateAll();
}

void insertCols(int before, int count) {
  if (count <= 0) return;
  pushUndo();
  g.wb.insertCols(g.sheet, before, count);
  g.dirty = true;
  updateTitle();
  syncControls();
  invalidateAll();
}

void deleteCols(int start, int count) {
  if (count <= 0) return;
  pushUndo();
  g.wb.deleteCols(g.sheet, start, count);
  g.dirty = true;
  updateTitle();
  syncControls();
  invalidateAll();
}

void autoFitColumns() {
  Range r = g.sel;
  HDC dc = GetDC(g.hwnd);
  for (int c = r.left(); c <= r.right() && c < MAX_COLS && c < r.left() + 64; c++) {
    int maxW = S(50);
    int r1, c1, r2, c2;
    int last = g.wb.usedBounds(g.sheet, r1, c1, r2, c2) ? r2 : 50;
    for (int row = 0; row <= last && row < MAX_ROWS; row++) {
      if (g.wb.raw(g.sheet, row, c).empty()) continue;
      Style st = g.wb.styleAt(g.sheet, row, c);
      HGDIOBJ old = SelectObject(dc, fontFor(st));
      std::wstring text = widen(g.wb.display(g.sheet, row, c));
      SIZE sz{};
      GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
      SelectObject(dc, old);
      maxW = std::max(maxW, static_cast<int>(sz.cx) + S(12));
    }
    g.wb.setColWidth(g.sheet, c, MulDiv(std::min(maxW, S(600)), 96, g.dpi));
  }
  ReleaseDC(g.hwnd, dc);
  g.dirty = true;
  invalidateAll();
}

// ------------------------------------------------------------------ worksheets
void insertSheet() {
  pushUndo();
  int idx = g.wb.addSheet();
  switchSheet(idx);
  g.dirty = true;
  updateTitle();
}

void deleteSheet() {
  if (g.wb.sheetCount() <= 1) {
    showError(L"A workbook must contain at least one sheet.");
    return;
  }
  wchar_t msg[256];
  swprintf(msg, 256, L"Delete the sheet \"%s\"?", widen(g.wb.sheet(g.sheet).name).c_str());
  if (MessageBoxW(g.hwnd, msg, L"Delete Sheet", MB_ICONQUESTION | MB_YESNO) != IDYES) return;
  pushUndo();
  int idx = g.sheet;
  g.wb.removeSheet(idx);
  g.sheet = std::min(idx, g.wb.sheetCount() - 1);
  g.sel = Range{};
  g.scrollX = g.scrollY = 0;
  g.dirty = true;
  updateTitle();
  syncControls();
  invalidateAll();
}

void renameSheet() {
  std::string name = g.wb.sheet(g.sheet).name;
  if (!promptText(L"Rename Sheet", L"Sheet name:", name)) return;
  name = trim(name);
  if (name.empty()) return;
  if (name.size() > 31) name = name.substr(0, 31);
  for (char& c : name)
    if (c == ':' || c == '\\' || c == '/' || c == '?' || c == '*' || c == '[' || c == ']') c = '_';
  int existing = g.wb.indexOfName(name);
  if (existing >= 0 && existing != g.sheet) {
    showError(L"Another sheet already has that name.");
    return;
  }
  pushUndo();
  g.wb.sheet(g.sheet).name = name;
  g.dirty = true;
  updateTitle();
  syncControls();
  invalidateAll();
}

void switchSheet(int index) {
  if (index < 0) index = g.wb.sheetCount() - 1;
  if (index >= g.wb.sheetCount()) index = 0;
  if (index == g.sheet) return;
  g.sheet = index;
  g.sel = Range{};
  g.scrollX = g.scrollY = 0;
  g.marquee = false;
  syncControls();
  updateScrollBars();
  invalidateAll();
}

// ------------------------------------------------------------------ go to / find
void goToAddress() {
  std::string value = selectionAddress();
  if (!promptText(L"Go To", L"Cell or range reference:", value)) return;
  std::string text = trim(value);
  if (text.empty()) return;
  size_t bang = text.find('!');
  std::string sheetPart;
  if (bang != std::string::npos) {
    sheetPart = text.substr(0, bang);
    text = text.substr(bang + 1);
    if (sheetPart.size() > 1 && sheetPart.front() == '\'' && sheetPart.back() == '\'')
      sheetPart = sheetPart.substr(1, sheetPart.size() - 2);
    int idx = g.wb.indexOfName(sheetPart);
    if (idx >= 0) switchSheet(idx);
  }
  int r1, c1, r2, c2;
  if (!parseArea(text, r1, c1, r2, c2)) {
    showError(L"That is not a valid cell reference.");
    return;
  }
  selectRange(r1, c1, r2, c2);
  scrollToCell(g.sel.r1, g.sel.c1);
  setStatus(L"Go to " + widen(selectionAddress()));
}

bool findInSheet(int startRow, int startCol, int dr, int dc, int& outRow, int& outCol) {
  if (g.findText.empty()) return false;
  int r1, c1, r2, c2;
  if (!g.wb.usedBounds(g.sheet, r1, c1, r2, c2)) return false;
  std::string needle = g.findText;
  if (!g.findMatchCase) needle = toUpper(needle);
  int totalRows = r2 - r1 + 1;
  int totalCols = c2 - c1 + 1;
  long long total = static_cast<long long>(totalRows) * totalCols;
  if (total > 2000000) total = 2000000;  // keep a pathological sheet responsive
  long long start = static_cast<long long>(startRow - r1) * totalCols + (startCol - c1);
  for (long long step = 0; step < total; step++) {
    long long idx = ((start + step) % total + total) % total;
    int row = r1 + static_cast<int>(idx / totalCols);
    int col = c1 + static_cast<int>(idx % totalCols);
    std::string text = g.wb.display(g.sheet, row, col);
    if (!g.findMatchCase) text = toUpper(text);
    bool hit = g.findWholeCell ? (text == needle) : (text.find(needle) != std::string::npos);
    if (hit) {
      outRow = row;
      outCol = col;
      return true;
    }
  }
  (void)dr;
  (void)dc;
  return false;
}

void findNext() {
  if (g.findText.empty()) {
    showFindDialog();
    return;
  }
  int r = 0, c = 0;
  if (findInSheet(g.sel.r1, g.sel.c1 + 1, 0, 0, r, c) || findInSheet(0, 0, 0, 0, r, c)) {
    selectCell(r, c, false);
    scrollToCell(r, c);
    setStatus(L"Found");
  } else {
    setStatus(L"No matches");
    showError(L"Grid could not find the text you entered.");
  }
}

void replaceOne() {
  if (g.findText.empty()) {
    showFindDialog();
    return;
  }
  std::string text = g.wb.display(g.sheet, g.sel.r1, g.sel.c1);
  std::string hay = g.findMatchCase ? text : toUpper(text);
  std::string needle = g.findMatchCase ? g.findText : toUpper(g.findText);
  bool hit = g.findWholeCell ? hay == needle : hay.find(needle) != std::string::npos;
  if (hit) {
    pushUndo();
    std::string result;
    if (g.findWholeCell) {
      result = g.replaceText;
    } else {
      std::string out;
      size_t pos = 0;
      while (pos < text.size()) {
        size_t found = hay.find(needle, pos);
        if (found == std::string::npos) {
          out += text.substr(pos);
          break;
        }
        out += text.substr(pos, found - pos);
        out += g.replaceText;
        pos = found + needle.size();
        if (needle.empty()) break;
      }
      result = out;
    }
    g.wb.setRaw(g.sheet, g.sel.r1, g.sel.c1, result);
    g.dirty = true;
    updateTitle();
    invalidateAll();
  }
  findNext();
}

void replaceAll() {
  if (g.findText.empty()) {
    showFindDialog();
    return;
  }
  int r1, c1, r2, c2;
  if (!g.wb.usedBounds(g.sheet, r1, c1, r2, c2)) return;
  pushUndo();
  int count = 0;
  std::string needle = g.findMatchCase ? g.findText : toUpper(g.findText);
  for (int row = r1; row <= r2; row++) {
    for (int col = c1; col <= c2; col++) {
      std::string text = g.wb.display(g.sheet, row, col);
      std::string hay = g.findMatchCase ? text : toUpper(text);
      bool hit = g.findWholeCell ? hay == needle : hay.find(needle) != std::string::npos;
      if (!hit) continue;
      std::string result;
      if (g.findWholeCell) {
        result = g.replaceText;
      } else {
        std::string out;
        size_t pos = 0;
        while (pos <= text.size()) {
          size_t found = hay.find(needle, pos);
          if (found == std::string::npos) {
            out += text.substr(pos);
            break;
          }
          out += text.substr(pos, found - pos);
          out += g.replaceText;
          pos = found + needle.size();
          if (needle.empty()) break;
        }
        result = out;
      }
      g.wb.setRaw(g.sheet, row, col, result);
      count++;
    }
  }
  g.dirty = true;
  updateTitle();
  invalidateAll();
  wchar_t msg[128];
  swprintf(msg, 128, L"Grid made %d replacement%s.", count, count == 1 ? L"" : L"s");
  setStatus(msg);
  MessageBoxW(g.hwnd, msg, L"Replace All", MB_ICONINFORMATION | MB_OK);
}

// ------------------------------------------------------------------ files
bool confirmDiscard() {
  if (!g.dirty) return true;
  int answer = MessageBoxW(g.hwnd, L"Save changes to this workbook?", L"Grid",
                           MB_ICONWARNING | MB_YESNOCANCEL);
  if (answer == IDCANCEL) return false;
  if (answer == IDYES) {
    saveWorkbook();
    return !g.dirty;
  }
  return true;
}

void newWorkbook() {
  if (!confirmDiscard()) return;
  g.wb = Workbook();
  g.sheet = 0;
  g.sel = Range{};
  g.scrollX = g.scrollY = 0;
  g.filePath.clear();
  g.dirty = false;
  g.undoStack.clear();
  g.redoStack.clear();
  g.marquee = false;
  updateTitle();
  syncControls();
  updateScrollBars();
  invalidateAll();
  setStatus(L"New workbook");
}

void openWorkbook() {
  if (!confirmDiscard()) return;
  wchar_t path[MAX_PATH] = L"";
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g.hwnd;
  ofn.lpstrFilter = L"Grid Workbook (*.grid)\0*.grid\0CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = L"Open";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
  if (!GetOpenFileNameW(&ofn)) return;
  std::string utf8 = narrow(path);
  std::string data;
  if (!readFileW(path, data)) {
    showError(L"Grid could not open that file.");
    return;
  }
  bool csv = endsWith(utf8, ".csv");
  if (csv) {
    importCsv(data);
    g.filePath = utf8;
    g.dirty = true;
  } else {
    if (!g.wb.deserialize(data)) {
      showError(L"That file is not a Grid workbook.");
      return;
    }
    g.sheet = 0;
    g.sel = Range{};
    g.scrollX = g.scrollY = 0;
    g.filePath = utf8;
    g.dirty = false;
    g.undoStack.clear();
    g.redoStack.clear();
    syncControls();
    updateScrollBars();
    invalidateAll();
  }
  updateTitle();
  setStatus(L"Opened " + fileNameOf(g.filePath));
}

void saveWorkbook() {
  if (g.filePath.empty()) {
    saveWorkbookAs();
    return;
  }
  std::wstring path = widen(g.filePath);
  bool ok;
  if (endsWith(g.filePath, ".csv")) {
    ok = writeFileW(path, g.wb.toCSV(g.sheet));
  } else {
    ok = writeFileW(path, g.wb.serialize());
  }
  if (!ok) {
    showError(L"Grid could not save that file.");
    return;
  }
  g.dirty = false;
  updateTitle();
  setStatus(L"Saved " + fileNameOf(g.filePath));
}

void saveWorkbookAs() {
  wchar_t path[MAX_PATH] = L"";
  std::wstring initial = widen(g.filePath.empty() ? "Book1.grid" : g.filePath);
  lstrcpynW(path, initial.c_str(), MAX_PATH);
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g.hwnd;
  ofn.lpstrFilter = L"Grid Workbook (*.grid)\0*.grid\0CSV Files (*.csv)\0*.csv\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = L"Save As";
  ofn.lpstrDefExt = L"grid";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (!GetSaveFileNameW(&ofn)) return;
  g.filePath = narrow(path);
  saveWorkbook();
}

void exportCsv() {
  wchar_t path[MAX_PATH] = L"";
  std::wstring initial = widen(g.filePath.empty() ? "Book1.csv" : g.filePath + ".csv");
  lstrcpynW(path, initial.c_str(), MAX_PATH);
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g.hwnd;
  ofn.lpstrFilter = L"CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = L"Export CSV";
  ofn.lpstrDefExt = L"csv";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (!GetSaveFileNameW(&ofn)) return;
  if (!writeFileW(path, g.wb.toCSV(g.sheet))) {
    showError(L"Grid could not write that file.");
    return;
  }
  setStatus(L"Exported " + fileNameOf(narrow(path)));
}

// ------------------------------------------------------------------ dispatcher
void runCommand(int id) {
  switch (id) {
    case CMD_NEW: newWorkbook(); break;
    case CMD_OPEN: openWorkbook(); break;
    case CMD_SAVE: saveWorkbook(); break;
    case CMD_SAVE_AS: saveWorkbookAs(); break;
    case CMD_EXPORT_CSV: exportCsv(); break;
    case CMD_EXIT: PostMessageW(g.hwnd, WM_CLOSE, 0, 0); break;

    case CMD_UNDO: undo(); break;
    case CMD_REDO: redo(); break;
    case CMD_CUT: copySelection(true); break;
    case CMD_COPY: copySelection(false); break;
    case CMD_PASTE: pasteClipboard(); break;
    case CMD_CLEAR: clearSelection(false); break;
    case CMD_CLEAR_FORMATS: clearFormats(); break;
    case CMD_SELECT_ALL: selectWholeSheet(); break;
    case CMD_FIND: showFindDialog(); break;
    case CMD_REPLACE: showFindDialog(); break;
    case CMD_GOTO: goToAddress(); break;
    case CMD_FIND_NEXT: findNext(); break;
    case CMD_REPLACE_ONE: replaceOne(); break;
    case CMD_REPLACE_ALL: replaceAll(); break;

    case CMD_BOLD: toggleStyleFlag(1); break;
    case CMD_ITALIC: toggleStyleFlag(2); break;
    case CMD_UNDERLINE: toggleStyleFlag(4); break;
    case CMD_WRAP_TEXT: toggleStyleFlag(8); break;
    case CMD_ALIGN_LEFT: applyStyle(([] { Style s; s.align = 0; return s; })()); break;
    case CMD_ALIGN_CENTER: applyStyle(([] { Style s; s.align = 1; return s; })()); break;
    case CMD_ALIGN_RIGHT: applyStyle(([] { Style s; s.align = 2; return s; })()); break;
    case CMD_FMT_GENERAL: setNumberFormat(""); break;
    case CMD_FMT_NUMBER: setNumberFormat("#,##0.00"); break;
    case CMD_FMT_CURRENCY: setNumberFormat("$#,##0.00"); break;
    case CMD_FMT_PERCENT: setNumberFormat("0.00%"); break;
    case CMD_FMT_COMMA: setNumberFormat("#,##0"); break;
    case CMD_FMT_DATE: setNumberFormat("yyyy-mm-dd"); break;
    case CMD_FMT_TIME: setNumberFormat("hh:mm:ss"); break;
    case CMD_FMT_TEXT: setNumberFormat(""); break;
    case CMD_DEC_INC: changeDecimals(1); break;
    case CMD_DEC_DEC: changeDecimals(-1); break;
    case CMD_FORMAT_CELLS: showFormatCellsDialog(); break;
    case CMD_BORDERS: applyStyle(([] { Style s; s.border = 15; return s; })()); break;
    case CMD_FILL_COLOR:
    case CMD_FONT_COLOR: {
      static COLORREF custom[16] = {0};
      CHOOSECOLORW cc{};
      cc.lStructSize = sizeof(cc);
      cc.hwndOwner = g.hwnd;
      cc.lpCustColors = custom;
      Style cur = g.wb.styleAt(g.sheet, g.sel.r1, g.sel.c1);
      cc.rgbResult = id == CMD_FILL_COLOR ? static_cast<COLORREF>(cur.fill)
                                          : static_cast<COLORREF>(cur.color ? cur.color : RGB(0, 0, 0));
      cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
      if (!ChooseColorW(&cc)) break;
      Style patch;
      if (id == CMD_FILL_COLOR) patch.fill = cc.rgbResult;
      else patch.color = cc.rgbResult;
      applyStyle(patch);
      break;
    }

    case CMD_INSERT_ROWS:
      insertRows(g.sel.top(), std::max(1, g.sel.rows() > MAX_ROWS / 2 ? 1 : g.sel.rows()));
      break;
    case CMD_INSERT_COLS:
      insertCols(g.sel.left(), std::max(1, g.sel.cols() > MAX_COLS / 2 ? 1 : g.sel.cols()));
      break;
    case CMD_DELETE_ROWS:
      deleteRows(g.sel.top(), std::max(1, std::min(g.sel.rows(), MAX_ROWS - g.sel.top())));
      break;
    case CMD_DELETE_COLS:
      deleteCols(g.sel.left(), std::max(1, std::min(g.sel.cols(), MAX_COLS - g.sel.left())));
      break;
    case CMD_AUTOFIT_COLS: autoFitColumns(); break;

    case CMD_INSERT_SHEET: insertSheet(); break;
    case CMD_DELETE_SHEET: deleteSheet(); break;
    case CMD_RENAME_SHEET: renameSheet(); break;
    case CMD_NEXT_SHEET: switchSheet(g.sheet + 1); break;
    case CMD_PREV_SHEET: switchSheet(g.sheet - 1); break;
    case CMD_FIRST_SHEET: switchSheet(0); break;
    case CMD_LAST_SHEET: switchSheet(g.wb.sheetCount() - 1); break;

    case CMD_ABOUT:
      MessageBoxW(g.hwnd,
                  L"Grid 1.0\n\nA native spreadsheet for Windows, built from scratch in C++ with the "
                  L"Win32 API and GDI.\n\nEngine: formula parser, dependency recalculation, 135+ "
                  L"functions, number/date formatting.\n\nWorkbooks are stored as plain-text .grid "
                  L"files; CSV import and export are supported.",
                  L"About Grid", MB_ICONINFORMATION | MB_OK);
      break;
    case CMD_SHORTCUTS:
      MessageBoxW(g.hwnd,
                  L"Navigation\n"
                  L"  Arrow keys / Tab / Enter      move\n"
                  L"  Ctrl+Arrow                    jump to the edge of a block\n"
                  L"  Ctrl+Home / Ctrl+End          first / last used cell\n"
                  L"  Page Up / Page Down           scroll a screen\n"
                  L"  Ctrl+Page Up / Down           previous / next sheet\n"
                  L"  F5 or Ctrl+G                  go to a cell or range\n\n"
                  L"Editing\n"
                  L"  Type, F2, double-click        edit a cell\n"
                  L"  Esc                           cancel an edit\n"
                  L"  Delete / Ctrl+D / Ctrl+R      clear / fill down / fill right\n"
                  L"  Ctrl+C / X / V                copy / cut / paste\n"
                  L"  Ctrl+Z / Ctrl+Y               undo / redo\n"
                  L"  Ctrl+F / Ctrl+H               find / replace\n\n"
                  L"Formatting\n"
                  L"  Ctrl+B / I / U                bold / italic / underline\n"
                  L"  Ctrl+1                        format cells\n"
                  L"  Ctrl+Shift+4 / 5 / 3          currency / percent / date",
                  L"Keyboard Shortcuts", MB_ICONINFORMATION | MB_OK);
      break;
    default:
      break;
  }
}

}  // namespace ui
}  // namespace grid
