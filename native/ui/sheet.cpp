// The spreadsheet canvas: GDI painting of a virtualised cell grid, headers,
// selection, in-cell editing and all grid mouse/keyboard interaction.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.h"

namespace grid {
namespace ui {

namespace {

const COLORREF kGridLine = RGB(217, 217, 217);
const COLORREF kHeaderBg = RGB(244, 245, 247);
const COLORREF kHeaderHot = RGB(222, 234, 246);
const COLORREF kHeaderText = RGB(64, 64, 64);
const COLORREF kHeaderLine = RGB(200, 200, 200);
const COLORREF kSelFill = RGB(233, 241, 252);
const COLORREF kAccent = RGB(33, 115, 70);
const COLORREF kText = RGB(20, 20, 20);
const COLORREF kErrorText = RGB(176, 0, 32);

int cellLeft(int c) { return g.rcGrid.left + S(kHeaderW) + colX(c) - g.scrollX; }
int cellTop(int r) { return g.rcGrid.top + S(kHeaderH) + rowY(r) - g.scrollY; }

bool inClip(int r, int c) {
  return g.marquee && g.clipSheet == g.sheet && g.clipSource.contains(r, c);
}

}  // namespace

// ------------------------------------------------------------------ selection
void selectCell(int r, int c, bool extend) {
  r = std::max(0, std::min(MAX_ROWS - 1, r));
  c = std::max(0, std::min(MAX_COLS - 1, c));
  if (extend) {
    g.sel.r2 = r;
    g.sel.c2 = c;
  } else {
    g.sel.r1 = g.sel.r2 = r;
    g.sel.c1 = g.sel.c2 = c;
  }
  syncFormulaBar();
  ensureVisible();
  invalidateAll();
}

void selectRange(int r1, int c1, int r2, int c2) {
  g.sel.r1 = std::max(0, std::min(MAX_ROWS - 1, r1));
  g.sel.c1 = std::max(0, std::min(MAX_COLS - 1, c1));
  g.sel.r2 = std::max(0, std::min(MAX_ROWS - 1, r2));
  g.sel.c2 = std::max(0, std::min(MAX_COLS - 1, c2));
  syncFormulaBar();
  invalidateAll();
}

void selectWholeSheet() {
  selectRange(0, 0, MAX_ROWS - 1, MAX_COLS - 1);
  ensureVisibleCell(0, 0);
}

std::string selectionAddress() {
  Range r = g.sel;
  if (r.single()) return a1(r.r1, r.c1);
  return a1(r.top(), r.left()) + ":" + a1(r.bottom(), r.right());
}

std::string activeRaw() { return g.wb.raw(g.sheet, g.sel.r1, g.sel.c1); }

// ------------------------------------------------------------------ painting
namespace {

void drawCellText(HDC dc, int r, int c, int x, int y, int w, int h, const Computed& cc, const Style& st) {
  if (cc.value.type == Value::Blank) return;
  std::string text = formatValue(cc.value, g.wb.numberFormat(g.sheet, r, c));
  if (text.empty()) return;
  std::wstring ws = widen(text);

  HFONT font = fontFor(st);
  HGDIOBJ oldFont = SelectObject(dc, font);
  COLORREF color = st.color ? static_cast<COLORREF>(st.color) : (cc.value.type == Value::Error ? kErrorText : kText);
  SetTextColor(dc, color);
  SetBkMode(dc, TRANSPARENT);

  int pad = S(4);
  SIZE sz{};
  GetTextExtentPoint32W(dc, ws.c_str(), static_cast<int>(ws.size()), &sz);
  int avail = w - pad * 2;

  int align = st.align;
  if (align < 0) {
    if (cc.value.type == Value::Number) align = 2;
    else if (cc.value.type == Value::Bool || cc.value.type == Value::Error) align = 1;
    else align = 0;
  }

  bool numeric = cc.value.type == Value::Number;
  if (numeric && sz.cx > avail && !st.wrap) {
    int room = w - pad;
    int hash = 0;
    while (hash * sz.cx / 3 + sz.cx / 3 <= room && hash < 24) hash++;
    std::wstring fill(static_cast<size_t>(std::max(1, hash)), L'#');
    ExtTextOutW(dc, x + w - pad - static_cast<int>(fill.size()) * (sz.cx / 3), y + (h - sz.cy) / 2,
                ETO_CLIPPED, nullptr, fill.c_str(), static_cast<UINT>(fill.size()), nullptr);
    SelectObject(dc, oldFont);
    return;
  }

  RECT box{x, y, x + w, y + h};
  if (st.wrap) {
    RECT inner{x + pad, y + S(2), x + w - pad, y + h - S(1)};
    DrawTextW(dc, ws.c_str(), -1, &inner, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(dc, oldFont);
    return;
  }

  int tx;
  if (align == 1) tx = x + (w - sz.cx) / 2;
  else if (align == 2) tx = x + w - pad - sz.cx;
  else tx = x + pad;

  // Excel lets left/centre aligned text spill into empty neighbours.
  int clipLeft = x, clipRight = x + w;
  if (align != 2 && sz.cx > avail) {
    int extra = 0;
    for (int cc2 = c + 1; cc2 < c + 40 && cc2 < MAX_COLS; cc2++) {
      if (!g.wb.raw(g.sheet, r, cc2).empty()) break;
      extra += colWidthPx(cc2);
      if (tx + sz.cx <= clipRight + extra) break;
    }
    if (extra > 0) clipRight = std::min<int>(g.rcGrid.right - S(kHeaderW), x + w + extra);
    if (align == 1) tx = x + pad + ((w - pad * 2) - sz.cx) / 2;
  }
  if (align == 1) tx = x + (w - sz.cx) / 2;

  SaveDC(dc);
  IntersectClipRect(dc, clipLeft, y, clipRight, y + h);
  ExtTextOutW(dc, tx, y + (h - sz.cy) / 2, ETO_CLIPPED, nullptr, ws.c_str(), static_cast<UINT>(ws.size()), nullptr);
  RestoreDC(dc, -1);
  SelectObject(dc, oldFont);
  (void)box;
}

void paintColumnHeaders(HDC dc, int first, int last) {
  int top = g.rcGrid.top;
  int h = S(kHeaderH);
  int x0 = g.rcGrid.left + S(kHeaderW);
  RECT band{x0, top, g.rcGrid.right, top + h};
  HBRUSH bg = CreateSolidBrush(kHeaderBg);
  FillRect(dc, &band, bg);
  DeleteObject(bg);

  Range sel = g.sel;
  bool wholeCols = (sel.top() == 0 && sel.bottom() >= MAX_ROWS - 1);
  HFONT font = g.fontHeader ? g.fontHeader : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  HGDIOBJ oldFont = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  for (int c = first; c <= last && c < MAX_COLS; c++) {
    int x = cellLeft(c);
    int w = colWidthPx(c);
    if (x + w < x0) continue;
    if (x > g.rcGrid.right) break;
    bool hot = wholeCols && c >= sel.left() && c <= sel.right();
    if (hot) {
      RECT hr{x, top, x + w, top + h};
      HBRUSH hb = CreateSolidBrush(kHeaderHot);
      FillRect(dc, &hr, hb);
      DeleteObject(hb);
    }
    SetTextColor(dc, hot ? kAccent : kHeaderText);
    std::wstring label = widen(indexToCol(c));
    RECT tr{x, top, x + w, top + h - S(2)};
    DrawTextW(dc, label.c_str(), -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    HPEN pen = CreatePen(PS_SOLID, 1, kHeaderLine);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, x + w, top, nullptr);
    LineTo(dc, x + w, top + h);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    if (hot) {
      HPEN ap = CreatePen(PS_SOLID, S(2), kAccent);
      HGDIOBJ op = SelectObject(dc, ap);
      MoveToEx(dc, x, top + h - 1, nullptr);
      LineTo(dc, x + w, top + h - 1);
      SelectObject(dc, op);
      DeleteObject(ap);
    }
  }
  SelectObject(dc, oldFont);
  // bottom separator of the header band
  HPEN pen = CreatePen(PS_SOLID, 1, kHeaderLine);
  HGDIOBJ oldPen = SelectObject(dc, pen);
  MoveToEx(dc, g.rcGrid.left, top + h - 1, nullptr);
  LineTo(dc, g.rcGrid.right, top + h - 1);
  SelectObject(dc, oldPen);
  DeleteObject(pen);
}

void paintRowHeaders(HDC dc, int first, int last) {
  int left = g.rcGrid.left;
  int w = S(kHeaderW);
  int x0 = left, y0 = g.rcGrid.top + S(kHeaderH);
  RECT band{x0, y0, x0 + w, g.rcGrid.bottom};
  HBRUSH bg = CreateSolidBrush(kHeaderBg);
  FillRect(dc, &band, bg);
  DeleteObject(bg);

  Range sel = g.sel;
  bool wholeRows = (sel.left() == 0 && sel.right() >= MAX_COLS - 1);
  HFONT font = g.fontHeader ? g.fontHeader : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  HGDIOBJ oldFont = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  for (int r = first; r <= last && r < MAX_ROWS; r++) {
    int y = cellTop(r);
    int h = rowHeightPx(r);
    if (y + h < y0) continue;
    if (y > g.rcGrid.bottom) break;
    bool hot = wholeRows && r >= sel.top() && r <= sel.bottom();
    if (hot) {
      RECT hr{x0, y, x0 + w, y + h};
      HBRUSH hb = CreateSolidBrush(kHeaderHot);
      FillRect(dc, &hr, hb);
      DeleteObject(hb);
    }
    SetTextColor(dc, hot ? kAccent : kHeaderText);
    std::wstring label = std::to_wstring(r + 1);
    RECT tr{x0, y, x0 + w - S(4), y + h};
    DrawTextW(dc, label.c_str(), -1, &tr, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    HPEN pen = CreatePen(PS_SOLID, 1, kHeaderLine);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, x0, y + h, nullptr);
    LineTo(dc, x0 + w, y + h);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
  }
  SelectObject(dc, oldFont);
  HPEN pen = CreatePen(PS_SOLID, 1, kHeaderLine);
  HGDIOBJ oldPen = SelectObject(dc, pen);
  MoveToEx(dc, x0 + w - 1, g.rcGrid.top, nullptr);
  LineTo(dc, x0 + w - 1, g.rcGrid.bottom);
  SelectObject(dc, oldPen);
  DeleteObject(pen);

  // corner box
  RECT corner{g.rcGrid.left, g.rcGrid.top, g.rcGrid.left + w, g.rcGrid.top + S(kHeaderH)};
  HBRUSH cb = CreateSolidBrush(kHeaderBg);
  FillRect(dc, &corner, cb);
  DeleteObject(cb);
  HPEN cp = CreatePen(PS_SOLID, 1, kHeaderLine);
  HGDIOBJ cop = SelectObject(dc, cp);
  MoveToEx(dc, corner.left, corner.bottom - 1, nullptr);
  LineTo(dc, corner.right, corner.bottom - 1);
  MoveToEx(dc, corner.right - 1, corner.top, nullptr);
  LineTo(dc, corner.right - 1, corner.bottom);
  // little select-all wedge, as in Excel's corner
  POINT pts[3] = {{corner.left + S(6), corner.bottom - S(6)},
                  {corner.right - S(6), corner.bottom - S(6)},
                  {corner.right - S(6), corner.top + S(6)}};
  HBRUSH wb = CreateSolidBrush(RGB(180, 190, 200));
  HGDIOBJ ob = SelectObject(dc, wb);
  Polygon(dc, pts, 3);
  SelectObject(dc, ob);
  DeleteObject(wb);
  SelectObject(dc, cop);
  DeleteObject(cp);
}

}  // namespace

void paintGrid(HDC dc) {
  if (g.wb.sheetCount() <= 0) return;
  RECT area = g.rcGrid;
  HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
  FillRect(dc, &area, white);
  DeleteObject(white);

  int cw = area.right - (area.left + S(kHeaderW));
  int chh = area.bottom - (area.top + S(kHeaderH));
  if (cw <= 0 || chh <= 0) return;

  int firstCol = firstVisibleCol(), lastCol = lastVisibleCol();
  int firstRow = firstVisibleRow(), lastRow = lastVisibleRow();

  SaveDC(dc);
  IntersectClipRect(dc, area.left + S(kHeaderW), area.top + S(kHeaderH), area.right, area.bottom);

  Range sel = g.sel;
  int activeR = sel.r1, activeC = sel.c1;

  // pass 1 — cell backgrounds and fills
  for (int r = firstRow; r <= lastRow && r < MAX_ROWS; r++) {
    int y = cellTop(r), h = rowHeightPx(r);
    if (y + h < area.top) continue;
    if (y > area.bottom) break;
    for (int c = firstCol; c <= lastCol && c < MAX_COLS; c++) {
      int x = cellLeft(c), w = colWidthPx(c);
      if (x + w < area.left) continue;
      if (x > area.right) break;
      Style st = g.wb.styleAt(g.sheet, r, c);
      bool selCell = sel.contains(r, c);
      bool active = (r == activeR && c == activeC);
      COLORREF bg;
      bool paint = false;
      if (st.fill) {
        bg = static_cast<COLORREF>(st.fill);
        paint = true;
      } else if (selCell && !active) {
        bg = kSelFill;
        paint = true;
      }
      if (paint) {
        RECT cr{x, y, x + w, y + h};
        HBRUSH b = CreateSolidBrush(bg);
        FillRect(dc, &cr, b);
        DeleteObject(b);
      }
      (void)inClip(r, c);
    }
  }

  // pass 2 — grid lines
  if (g.showGridlines) {
    HPEN pen = CreatePen(PS_SOLID, 1, kGridLine);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    int yTop = area.top + S(kHeaderH), yBottom = area.bottom;
    int xLeft = area.left + S(kHeaderW), xRight = area.right;
    for (int r = firstRow; r <= lastRow && r < MAX_ROWS; r++) {
      int y = cellTop(r) + rowHeightPx(r);
      if (y < yTop || y > yBottom) continue;
      MoveToEx(dc, xLeft, y, nullptr);
      LineTo(dc, xRight, y);
    }
    for (int c = firstCol; c <= lastCol && c < MAX_COLS; c++) {
      int x = cellLeft(c) + colWidthPx(c);
      if (x < xLeft || x > xRight) continue;
      MoveToEx(dc, x, yTop, nullptr);
      LineTo(dc, x, yBottom);
    }
    SelectObject(dc, oldPen);
    DeleteObject(pen);
  }

  // pass 3 — cell text
  for (int r = firstRow; r <= lastRow && r < MAX_ROWS; r++) {
    int y = cellTop(r), h = rowHeightPx(r);
    if (y + h < area.top) continue;
    if (y > area.bottom) break;
    for (int c = firstCol; c <= lastCol && c < MAX_COLS; c++) {
      int x = cellLeft(c), w = colWidthPx(c);
      if (x + w < area.left) continue;
      if (x > area.right) break;
      bool active = (r == activeR && c == activeC);
      if (active && g.editing && g.editingInCell) continue;  // the edit control covers it
      Computed cc = g.wb.computed(g.sheet, r, c);
      if (cc.value.type == Value::Blank) continue;
      Style st = g.wb.styleAt(g.sheet, r, c);
      drawCellText(dc, r, c, x, y, w, h, cc, st);
    }
  }

  // pass 4 — custom cell borders
  for (int r = firstRow; r <= lastRow && r < MAX_ROWS; r++) {
    int y = cellTop(r), h = rowHeightPx(r);
    for (int c = firstCol; c <= lastCol && c < MAX_COLS; c++) {
      Style st = g.wb.styleAt(g.sheet, r, c);
      if (!st.border) continue;
      int x = cellLeft(c), w = colWidthPx(c);
      HPEN pen = CreatePen(PS_SOLID, std::max(1, S(1)), RGB(60, 60, 60));
      HGDIOBJ op = SelectObject(dc, pen);
      if (st.border & 1) {
        MoveToEx(dc, x, y, nullptr);
        LineTo(dc, x, y + h);
      }
      if (st.border & 2) {
        MoveToEx(dc, x + w - 1, y, nullptr);
        LineTo(dc, x + w - 1, y + h);
      }
      if (st.border & 4) {
        MoveToEx(dc, x, y, nullptr);
        LineTo(dc, x + w, y);
      }
      if (st.border & 8) {
        MoveToEx(dc, x, y + h - 1, nullptr);
        LineTo(dc, x + w, y + h - 1);
      }
      SelectObject(dc, op);
      DeleteObject(pen);
    }
  }

  // pass 5 — selection outline, active cell, fill handle, copy marquee
  {
    int x1 = cellLeft(sel.left()), y1 = cellTop(sel.top());
    int x2 = cellLeft(sel.right()) + colWidthPx(sel.right());
    int y2 = cellTop(sel.bottom()) + rowHeightPx(sel.bottom());
    int lw = std::max(1, S(2));
    HPEN pen = CreatePen(PS_SOLID, lw, kAccent);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, x1, y1, x2 + 1, y2 + 1);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);

    int ax = cellLeft(activeC), ay = cellTop(activeR);
    int aw = colWidthPx(activeC), ah = rowHeightPx(activeR);
    if (!sel.single()) {
      HPEN thin = CreatePen(PS_SOLID, 1, RGB(130, 170, 210));
      HGDIOBJ tp = SelectObject(dc, thin);
      HGDIOBJ tb = SelectObject(dc, GetStockObject(NULL_BRUSH));
      Rectangle(dc, ax, ay, ax + aw + 1, ay + ah + 1);
      SelectObject(dc, tb);
      SelectObject(dc, tp);
      DeleteObject(thin);
    }

    int hs = std::max(5, S(6));
    RECT handle{x2 - hs / 2, y2 - hs / 2, x2 + hs / 2, y2 + hs / 2};
    HBRUSH hb = CreateSolidBrush(kAccent);
    FillRect(dc, &handle, hb);
    DeleteObject(hb);

    if (g.filling) {
      int fr = std::max(g.fillToRow, sel.bottom());
      int fc = std::max(g.fillToCol, sel.right());
      if (fr > sel.bottom() || fc > sel.right()) {
        int px2 = cellLeft(fc) + colWidthPx(fc);
        int py2 = cellTop(fr) + rowHeightPx(fr);
        HPEN dp = CreatePen(PS_DOT, 1, RGB(90, 90, 90));
        HGDIOBJ dop = SelectObject(dc, dp);
        HGDIOBJ dob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, x1, y1, std::max(px2, x2) + 1, std::max(py2, y2) + 1);
        SelectObject(dc, dob);
        SelectObject(dc, dop);
        DeleteObject(dp);
      }
    }
  }

  if (g.marquee && g.clipSheet == g.sheet) {
    int x1 = cellLeft(g.clipSource.left()), y1 = cellTop(g.clipSource.top());
    int x2 = cellLeft(g.clipSource.right()) + colWidthPx(g.clipSource.right());
    int y2 = cellTop(g.clipSource.bottom()) + rowHeightPx(g.clipSource.bottom());
    if (g.marqueeOn) {
      HPEN dp = CreatePen(PS_DOT, 1, RGB(40, 40, 40));
      HGDIOBJ dop = SelectObject(dc, dp);
      HGDIOBJ dob = SelectObject(dc, GetStockObject(NULL_BRUSH));
      Rectangle(dc, x1, y1, x2 + 1, y2 + 1);
      SelectObject(dc, dob);
      SelectObject(dc, dop);
      DeleteObject(dp);
    }
  }

  RestoreDC(dc, -1);

  paintColumnHeaders(dc, firstCol, lastCol);
  paintRowHeaders(dc, firstRow, lastRow);

  // scroll shadow under the headers so content does not bleed through
  HPEN pen = CreatePen(PS_SOLID, 1, kHeaderLine);
  HGDIOBJ op = SelectObject(dc, pen);
  MoveToEx(dc, area.left, area.top, nullptr);
  LineTo(dc, area.right, area.top);
  SelectObject(dc, op);
  DeleteObject(pen);
}

// ------------------------------------------------------------------ editing
void beginEdit(bool inCell, bool replaceContents) {
  if (g.editing) return;
  std::string raw = activeRaw();
  std::wstring text = replaceContents ? L"" : widen(raw);
  g.editing = true;
  g.editingInCell = inCell;
  if (inCell) {
    updateCellEditor();
    SetWindowTextW(g.hCell, text.c_str());
    SendMessage(g.hCell, EM_SETSEL, replaceContents ? 0 : (LPARAM)text.size(),
                replaceContents ? -1 : (LPARAM)text.size());
    SetFocus(g.hCell);
  } else {
    ShowWindow(g.hCell, SW_HIDE);
    SetWindowTextW(g.hFormula, text.c_str());
    SendMessage(g.hFormula, EM_SETSEL, text.size(), text.size());
    SetFocus(g.hFormula);
  }
  syncFormulaBar();
  InvalidateRect(g.hwnd, &g.rcStatus, FALSE);
}

void beginEditWith(const std::string& seed, bool inCell) {
  beginEdit(inCell, true);
  HWND target = inCell ? g.hCell : g.hFormula;
  std::wstring w = widen(seed);
  SetWindowTextW(target, w.c_str());
  SendMessage(target, EM_SETSEL, w.size(), w.size());
  SetFocus(target);
}

void updateCellEditor() {
  if (!g.editing || !g.editingInCell || !g.hCell) {
    if (g.hCell) ShowWindow(g.hCell, SW_HIDE);
    return;
  }
  Range r = g.sel;
  int x = cellLeft(r.r1);
  int y = cellTop(r.r1);
  int w = colWidthPx(r.c1);
  int h = rowHeightPx(r.r1);
  // keep the editor inside the grid viewport
  int vx = g.rcGrid.left + S(kHeaderW);
  int vy = g.rcGrid.top + S(kHeaderH);
  int vright = g.rcGrid.right;
  int vbottom = g.rcGrid.bottom;
  if (x + w < vx || x > vright || y + h < vy || y > vbottom) {
    ShowWindow(g.hCell, SW_HIDE);  // the edited cell scrolled out of view
    return;
  }
  if (x < vx) {
    w -= (vx - x);
    x = vx;
  }
  if (y < vy) {
    h -= (vy - y);
    y = vy;
  }
  if (x + w > vright) w = vright - x;
  if (y + h > vbottom) h = vbottom - y;
  if (w < S(24)) w = S(24);
  if (h < S(18)) h = S(18);
  Style st = g.wb.styleAt(g.sheet, r.r1, r.c1);
  SendMessage(g.hCell, WM_SETFONT, reinterpret_cast<WPARAM>(fontFor(st)), TRUE);
  SetWindowPos(g.hCell, HWND_TOP, x - 1, y - 1, w + 2, h + 2, SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void commitEdit(bool moveDown) {
  if (!g.editing || g.committing) return;
  g.committing = true;
  HWND focused = GetFocus();
  bool keepFocus = (focused == g.hName || focused == g.hFormula) && focused != nullptr;
  HWND src = g.editingInCell ? g.hCell : g.hFormula;
  int len = GetWindowTextLengthW(src);
  std::wstring text(static_cast<size_t>(len), L'\0');
  if (len > 0) GetWindowTextW(src, &text[0], len + 1);
  std::string raw = narrow(text);

  Range r = g.sel;
  std::string before = g.wb.raw(g.sheet, r.r1, r.c1);
  if (before != raw) {
    pushUndo();
    g.wb.setRaw(g.sheet, r.r1, r.c1, raw);
    g.dirty = true;
    updateTitle();
  }
  g.editing = false;
  g.editingInCell = false;
  ShowWindow(g.hCell, SW_HIDE);
  SendMessage(g.hFormula, EM_SETSEL, 0, 0);
  syncFormulaBar();
  if (!keepFocus) SetFocus(g.hwnd);
  if (moveDown) {
    int nr = std::min(MAX_ROWS - 1, r.r1 + 1);
    selectCell(nr, r.c1, false);
  }
  g.committing = false;
  invalidateAll();
}

void cancelEdit() {
  if (!g.editing) return;
  g.editing = false;
  g.editingInCell = false;
  ShowWindow(g.hCell, SW_HIDE);
  syncFormulaBar();
  SetFocus(g.hwnd);
  invalidateAll();
}

// ------------------------------------------------------------------ cursor hit test
namespace {

enum Zone { Z_NONE, Z_GRID, Z_COL_HEADER, Z_ROW_HEADER, Z_CORNER, Z_COL_RESIZE, Z_ROW_RESIZE, Z_FILL_HANDLE,
            Z_TOOLBAR, Z_FORMULA, Z_TABS, Z_STATUS };

Zone hitTest(int x, int y, int* outRow, int* outCol) {
  int row = 0, col = 0;
  Zone z = Z_NONE;
  if (PtInRect(&g.rcToolbar, {x, y})) return Z_TOOLBAR;
  if (PtInRect(&g.rcFormula, {x, y})) return Z_FORMULA;
  if (PtInRect(&g.rcTabs, {x, y})) return Z_TABS;
  if (PtInRect(&g.rcStatus, {x, y})) return Z_STATUS;
  if (!PtInRect(&g.rcGrid, {x, y})) return Z_NONE;

  int gx = g.rcGrid.left + S(kHeaderW), gy = g.rcGrid.top + S(kHeaderH);
  if (x < gx && y < gy) z = Z_CORNER;
  else if (y < gy) {
    z = Z_COL_HEADER;
    int c = colAtX(x - gx + g.scrollX);
    col = c;
    int right = cellLeft(c) + colWidthPx(c);
    if (std::abs(x - right) <= std::max(2, S(3))) z = Z_COL_RESIZE;
  } else if (x < gx) {
    z = Z_ROW_HEADER;
    int r = rowAtY(y - gy + g.scrollY);
    row = r;
    int bottom = cellTop(r) + rowHeightPx(r);
    if (std::abs(y - bottom) <= std::max(2, S(3))) z = Z_ROW_RESIZE;
  } else {
    z = Z_GRID;
    int r = rowAtY(y - gy + g.scrollY);
    int c = colAtX(x - gx + g.scrollX);
    row = r;
    col = c;
    if (g.sel.single() || true) {
      int x2 = cellLeft(g.sel.right()) + colWidthPx(g.sel.right());
      int y2 = cellTop(g.sel.bottom()) + rowHeightPx(g.sel.bottom());
      int tol = std::max(3, S(4));
      if (std::abs(x - x2) <= tol && std::abs(y - y2) <= tol) z = Z_FILL_HANDLE;
    }
  }
  if (outRow) *outRow = row;
  if (outCol) *outCol = col;
  return z;
}

}  // namespace

bool onSetCursor(int x, int y) {
  int r = 0, c = 0;
  Zone z = hitTest(x, y, &r, &c);
  LPCWSTR id = IDC_ARROW;
  if (z == Z_COL_RESIZE) id = IDC_SIZEWE;
  else if (z == Z_ROW_RESIZE) id = IDC_SIZENS;
  else if (z == Z_FILL_HANDLE) id = IDC_CROSS;
  else if (z == Z_GRID) id = IDC_ARROW;
  SetCursor(LoadCursorW(nullptr, id));
  return true;
}

// ------------------------------------------------------------------ mouse
void onLButtonDown(int x, int y, WPARAM flags) {
  SetFocus(g.hwnd);
  int r = 0, c = 0;
  Zone z = hitTest(x, y, &r, &c);
  if (z == Z_TOOLBAR) {
    int id = 0;
    if (chromeHitTest(x, y, id)) {
      g.pressedButton = id;
      invalidateAll();
    }
    return;
  }
  if (z == Z_TABS) {
    return;  // handled by the tab strip
  }
  if (z == Z_CORNER) {
    selectWholeSheet();
    g.dragging = true;
    g.dragAnchor = true;
    return;
  }
  if (z == Z_COL_RESIZE) {
    g.resizingCol = true;
    g.resizeIndex = c;
    g.resizeStartPos = x;
    g.resizeStartSize = g.wb.colWidth(g.sheet, c);
    SetCapture(g.hwnd);
    return;
  }
  if (z == Z_ROW_RESIZE) {
    g.resizingRow = true;
    g.resizeIndex = r;
    g.resizeStartPos = y;
    g.resizeStartSize = g.wb.rowHeight(g.sheet, r);
    SetCapture(g.hwnd);
    return;
  }
  if (z == Z_COL_HEADER) {
    if (flags & MK_SHIFT) {
      g.sel.c1 = std::min(g.sel.c1, c);
      g.sel.c2 = std::max(g.sel.c2, c);
      g.sel.r1 = 0;
      g.sel.r2 = MAX_ROWS - 1;
    } else {
      g.sel.r1 = 0;
      g.sel.r2 = MAX_ROWS - 1;
      g.sel.c1 = g.sel.c2 = c;
    }
    g.dragging = true;
    g.dragOnHeaders = true;
    SetCapture(g.hwnd);
    syncFormulaBar();
    invalidateAll();
    return;
  }
  if (z == Z_ROW_HEADER) {
    if (flags & MK_SHIFT) {
      g.sel.r1 = std::min(g.sel.r1, r);
      g.sel.r2 = std::max(g.sel.r2, r);
      g.sel.c1 = 0;
      g.sel.c2 = MAX_COLS - 1;
    } else {
      g.sel.c1 = 0;
      g.sel.c2 = MAX_COLS - 1;
      g.sel.r1 = g.sel.r2 = r;
    }
    g.dragging = true;
    g.dragOnHeaders = true;
    SetCapture(g.hwnd);
    syncFormulaBar();
    invalidateAll();
    return;
  }
  if (z == Z_FILL_HANDLE) {
    g.filling = true;
    g.fillToRow = g.sel.bottom();
    g.fillToCol = g.sel.right();
    SetCapture(g.hwnd);
    invalidateAll();
    return;
  }
  if (z == Z_GRID) {
    if (g.editing) {
      commitEdit(true);
    }
    if (flags & MK_SHIFT) {
      selectCell(r, c, true);
    } else {
      selectCell(r, c, false);
      g.dragging = true;
      g.dragAnchor = true;
      SetCapture(g.hwnd);
    }
  }
}

void onMouseMove(int x, int y, WPARAM flags) {
  int r = 0, c = 0;
  if (g.resizingCol) {
    int delta = x - g.resizeStartPos;
    int w = g.resizeStartSize + MulDiv(delta, 96, g.dpi);
    w = std::max(24, std::min(600, w));
    g.wb.setColWidth(g.sheet, g.resizeIndex, w);
    invalidateAll();
    g.dirty = true;
    return;
  }
  if (g.resizingRow) {
    int delta = y - g.resizeStartPos;
    int h = g.resizeStartSize + MulDiv(delta, 96, g.dpi);
    h = std::max(14, std::min(300, h));
    g.wb.setRowHeight(g.sheet, g.resizeIndex, h);
    invalidateAll();
    g.dirty = true;
    return;
  }
  if (g.filling) {
    int gx = g.rcGrid.left + S(kHeaderW), gy = g.rcGrid.top + S(kHeaderH);
    int rr = rowAtY(y - gy + g.scrollY);
    int cc = colAtX(x - gx + g.scrollX);
    g.fillToRow = std::max(g.sel.bottom(), std::min(MAX_ROWS - 1, std::max(rr, g.sel.bottom())));
    g.fillToCol = std::max(g.sel.right(), std::min(MAX_COLS - 1, std::max(cc, g.sel.right())));
    invalidateAll();
    return;
  }
  if (g.dragging) {
    Zone z = hitTest(x, y, &r, &c);
    if (z == Z_GRID || z == Z_COL_HEADER || z == Z_ROW_HEADER) {
      int newX = x, newY = y;
      // auto-scroll when dragging past the viewport edges
      if (newY < g.rcGrid.top + S(kHeaderH)) scrollBy(0, -S(22));
      else if (newY > g.rcGrid.bottom - 2) scrollBy(0, S(22));
      if (newX < g.rcGrid.left + S(kHeaderW)) scrollBy(-S(100), 0);
      else if (newX > g.rcGrid.right - 2) scrollBy(S(100), 0);
      int gx = g.rcGrid.left + S(kHeaderW), gy = g.rcGrid.top + S(kHeaderH);
      int rr = std::max(0, std::min(MAX_ROWS - 1, rowAtY(y - gy + g.scrollY)));
      int cc = std::max(0, std::min(MAX_COLS - 1, colAtX(x - gx + g.scrollX)));
      if (g.dragOnHeaders) {
        if (g.sel.r2 - g.sel.r1 >= MAX_ROWS - 1) g.sel.c2 = cc;
        else g.sel.r2 = rr;
      } else {
        g.sel.r2 = rr;
        g.sel.c2 = cc;
      }
      syncFormulaBar();
      invalidateAll();
    }
    return;
  }
  // hover feedback in the toolbar
  int id = 0;
  int hover = chromeHitTest(x, y, id) ? id : 0;
  if (hover != g.hoverButton) {
    g.hoverButton = hover;
    refreshChrome();
  }
}

void onLButtonUp(int x, int y, WPARAM flags) {
  if (g.resizingCol || g.resizingRow) {
    g.resizingCol = g.resizingRow = false;
    ReleaseCapture();
    g.dirty = true;
    updateTitle();
    invalidateAll();
    return;
  }
  if (g.pressedButton) {
    int id = g.pressedButton;
    g.pressedButton = 0;
    int hit = 0;
    if (chromeHitTest(x, y, hit) && hit == id) runCommand(id);
    refreshChrome();
    return;
  }
  if (g.filling) {
    int dr = g.fillToRow - g.sel.bottom();
    int dc = g.fillToCol - g.sel.right();
    g.filling = false;
    ReleaseCapture();
    if (dr > 0 || dc > 0) fillSelection(dr, dc);
    invalidateAll();
    return;
  }
  if (g.dragging) {
    g.dragging = false;
    g.dragAnchor = false;
    g.dragOnHeaders = false;
    ReleaseCapture();
    invalidateAll();
  }
  (void)flags;
}

void onLButtonDblClk(int x, int y, WPARAM flags) {
  int r = 0, c = 0;
  Zone z = hitTest(x, y, &r, &c);
  if (z == Z_COL_RESIZE) {  // autofit the column to its widest visible value
    int maxW = S(60);
    int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
    int last = g.wb.usedBounds(g.sheet, r1, c1, r2, c2) ? r2 : 100;
    HDC dc = GetDC(g.hwnd);
    HFONT font = fontFor(g.wb.styleAt(g.sheet, r, c));
    HGDIOBJ oldFont = SelectObject(dc, font);
    for (int rr = 0; rr <= last && rr < MAX_ROWS; rr++) {
      std::string raw = g.wb.raw(g.sheet, rr, c);
      if (raw.empty()) continue;
      std::wstring text = widen(g.wb.display(g.sheet, rr, c));
      SIZE sz{};
      GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
      maxW = std::max(maxW, static_cast<int>(sz.cx) + S(12));
    }
    SelectObject(dc, oldFont);
    ReleaseDC(g.hwnd, dc);
    g.wb.setColWidth(g.sheet, c, MulDiv(std::min(maxW, S(600)), 96, g.dpi));
    g.dirty = true;
    invalidateAll();
    return;
  }
  if (z == Z_ROW_RESIZE) {
    g.wb.setRowHeight(g.sheet, r, 22);
    invalidateAll();
    return;
  }
  if (z == Z_GRID) {
    selectCell(r, c, false);
    beginEdit(true, false);
  }
  (void)flags;
}

void onRButtonDown(int x, int y) {
  int r = 0, c = 0;
  Zone z = hitTest(x, y, &r, &c);
  if (z == Z_TABS) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CMD_INSERT_SHEET, L"&Insert Sheet");
    AppendMenuW(m, MF_STRING, CMD_DELETE_SHEET, L"&Delete Sheet");
    AppendMenuW(m, MF_STRING, CMD_RENAME_SHEET, L"&Rename Sheet...");
    POINT pt{x, y};
    ClientToScreen(g.hwnd, &pt);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(m);
    if (cmd) runCommand(cmd);
    return;
  }
  if (z == Z_GRID || z == Z_COL_HEADER || z == Z_ROW_HEADER) {
    if (z == Z_GRID && !(g.sel.contains(r, c) && !g.sel.single())) selectCell(r, c, false);
    if (z == Z_COL_HEADER) selectRange(0, c, MAX_ROWS - 1, c);
    if (z == Z_ROW_HEADER) selectRange(r, 0, r, MAX_COLS - 1);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CMD_CUT, L"Cu&t\tCtrl+X");
    AppendMenuW(m, MF_STRING, CMD_COPY, L"&Copy\tCtrl+C");
    AppendMenuW(m, MF_STRING, CMD_PASTE, L"&Paste\tCtrl+V");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_FORMAT_CELLS, L"F&ormat Cells...\tCtrl+1");
    AppendMenuW(m, MF_STRING, CMD_CLEAR, L"Clea&r Contents\tDel");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_INSERT_ROWS, L"Insert Rows");
    AppendMenuW(m, MF_STRING, CMD_INSERT_COLS, L"Insert Columns");
    AppendMenuW(m, MF_STRING, CMD_DELETE_ROWS, L"Delete Rows");
    AppendMenuW(m, MF_STRING, CMD_DELETE_COLS, L"Delete Columns");
    POINT pt{x, y};
    ClientToScreen(g.hwnd, &pt);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(m);
    if (cmd) runCommand(cmd);
    return;
  }
  (void)z;
}

bool onMouseWheel(int delta, bool shift, int x, int y) {
  if (PtInRect(&g.rcGrid, {x, y}) || PtInRect(&g.rcTabs, {x, y})) {
    int step = -delta / WHEEL_DELTA;
    if (shift) {
      int dir = step > 0 ? 1 : -1;
      int target = colAtX(g.scrollX + viewportWidth());
      for (int i = 0; i < std::abs(step); i++) scrollBy(dir * S(100), 0);
      (void)target;
    } else {
      scrollBy(0, step * S(66));
    }
    return true;
  }
  return false;
}

// ------------------------------------------------------------------ keyboard
namespace {

void applyFormatShortcut(int id) { runCommand(id); }

}  // namespace

void onKeyDown(int vk, bool ctrl, bool shift, bool alt) {
  Range sel = g.sel;
  switch (vk) {
    case VK_UP:
      if (ctrl) moveToEdge(-1, 0, shift);
      else moveSelectionBy(-1, 0, shift);
      return;
    case VK_DOWN:
      if (ctrl) moveToEdge(1, 0, shift);
      else moveSelectionBy(1, 0, shift);
      return;
    case VK_LEFT:
      if (ctrl) moveToEdge(0, -1, shift);
      else moveSelectionBy(0, -1, shift);
      return;
    case VK_RIGHT:
      if (ctrl) moveToEdge(0, 1, shift);
      else moveSelectionBy(0, 1, shift);
      return;
    case VK_HOME:
      if (ctrl) {
        selectCell(0, 0, shift);
        scrollToCell(0, 0);
      } else {
        selectCell(sel.r1, 0, shift);
      }
      return;
    case VK_END:
      if (ctrl) {
        int r1, c1, r2, c2;
        if (g.wb.usedBounds(g.sheet, r1, c1, r2, c2)) selectCell(r2, c2, false);
        else selectCell(0, 0, false);
        scrollToCell(g.sel.r1, g.sel.c1);
      } else {
        selectCell(sel.r1, sel.c1, shift);
      }
      return;
    case VK_PRIOR:
      if (ctrl) switchSheet(g.sheet - 1);
      else pageMove(-1, shift);
      return;
    case VK_NEXT:
      if (ctrl) switchSheet(g.sheet + 1);
      else pageMove(1, shift);
      return;
    case VK_TAB:
      moveSelectionBy(0, shift ? -1 : 1, false);
      return;
    case VK_RETURN:
      moveSelectionBy(shift ? -1 : 1, 0, false);
      return;
    case VK_DELETE:
      runCommand(CMD_CLEAR);
      return;
    case VK_BACK:
      runCommand(CMD_CLEAR);
      return;
    case VK_F2:
      beginEdit(true, false);
      return;
    case VK_F5:
      runCommand(CMD_GOTO);
      return;
    case VK_F4:
      if (ctrl) runCommand(CMD_FIND_NEXT);
      return;
    case VK_ESCAPE:
      if (g.marquee) {
        g.marquee = false;
        invalidateAll();
      }
      return;
    case VK_SPACE:
      if (ctrl) {
        selectRange(0, sel.left(), MAX_ROWS - 1, sel.right());
        return;
      }
      if (shift) {
        selectRange(sel.top(), 0, sel.bottom(), MAX_COLS - 1);
        return;
      }
      break;
    default:
      break;
  }

  if (ctrl) {
    switch (vk) {
      case 'A': runCommand(CMD_SELECT_ALL); return;
      case 'B': runCommand(CMD_BOLD); return;
      case 'I': runCommand(CMD_ITALIC); return;
      case 'U': runCommand(CMD_UNDERLINE); return;
      case 'C': runCommand(CMD_COPY); return;
      case 'X': runCommand(CMD_CUT); return;
      case 'V': runCommand(CMD_PASTE); return;
      case 'Z': shift ? redo() : undo(); return;
      case 'Y': redo(); return;
      case 'S': saveWorkbook(); return;
      case 'O': openWorkbook(); return;
      case 'N': newWorkbook(); return;
      case 'F': runCommand(CMD_FIND); return;
      case 'H': runCommand(CMD_REPLACE); return;
      case 'G': runCommand(CMD_GOTO); return;
      case 'D': fillSelection(1, 0); return;
      case 'R': fillSelection(0, 1); return;
      case '1': runCommand(CMD_FORMAT_CELLS); return;
      case '5': applyFormatShortcut(CMD_FMT_PERCENT); return;
      case '4': applyFormatShortcut(CMD_FMT_CURRENCY); return;
      case '3': applyFormatShortcut(CMD_FMT_DATE); return;
      case '2': applyFormatShortcut(CMD_FMT_TIME); return;
      case VK_OEM_1:  // Ctrl+;
        if (!shift) {
          double serial = g.wb.now();
          int y, m, d, hh, mi, ss;
          serialToDate(std::floor(serial), y, m, d, hh, mi, ss);
          char buf[16];
          snprintf(buf, sizeof buf, "%04d-%02d-%02d", y, m, d);
          beginEditWith(buf, true);
        }
        return;
      default: break;
    }
  }
}

}  // namespace ui
}  // namespace grid
