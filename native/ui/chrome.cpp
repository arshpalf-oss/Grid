// Toolbar, formula bar, sheet tabs and status bar: layout, GDI painting and
// hit testing. These are the "chrome" bands around the spreadsheet canvas.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.h"

namespace grid {
namespace ui {

HFONT uiItalicFont();

namespace {

const COLORREF kChromeBg = RGB(249, 250, 251);
const COLORREF kChromeLine = RGB(222, 224, 227);
const COLORREF kButtonHot = RGB(231, 238, 247);
const COLORREF kButtonDown = RGB(210, 226, 243);
const COLORREF kButtonLine = RGB(196, 200, 205);
const COLORREF kChromeText = RGB(48, 52, 58);
const COLORREF kAccent = RGB(33, 115, 70);
const COLORREF kTabStripBg = RGB(240, 241, 243);

std::vector<int> g_separators;

void addSeparator(int x) { g_separators.push_back(x); }

void addButton(int id, const wchar_t* label, int width, bool toggle = false, bool color = false,
               uint32_t swatch = 0, const wchar_t* tip = L"") {
  ToolButton b;
  b.id = id;
  b.label = label ? label : L"";
  b.width = S(width);
  b.toggle = toggle;
  b.isColor = color;
  b.swatch = swatch;
  b.tip = tip ? tip : L"";
  g.buttons.push_back(b);
}

bool allSelectedBold() {
  Style st = g.wb.styleAt(g.sheet, g.sel.r1, g.sel.c1);
  return st.bold;
}
unsigned currentFill() { return g.wb.styleAt(g.sheet, g.sel.r1, g.sel.c1).fill; }
unsigned currentColor() { return g.wb.styleAt(g.sheet, g.sel.r1, g.sel.c1).color; }
int currentAlign() { return g.wb.styleAt(g.sheet, g.sel.r1, g.sel.c1).align; }
bool currentWrap() { return g.wb.styleAt(g.sheet, g.sel.r1, g.sel.c1).wrap; }

HFONT labelFont(const ToolButton& b) {
  switch (b.id) {
    case CMD_BOLD: return g.fontUiBold;
    default: return g.fontUi;
  }
}

}  // namespace

void buildToolbar() {
  g.buttons.clear();
  g_separators.clear();
  int x = S(6);
  int y = g.rcToolbar.top + S(7);
  int h = S(30);
  auto place = [&](int id, const wchar_t* label, int width, bool toggle = false, bool color = false,
                   uint32_t swatch = 0, const wchar_t* tip = L"") {
    addButton(id, label, width, toggle, color, swatch, tip);
    ToolButton& b = g.buttons.back();
    b.rect = RECT{x, y, x + b.width, y + h};
    x += b.width + S(2);
  };
  auto sep = [&]() {
    x += S(5);
    addSeparator(x);
    x += S(9);
  };

  place(CMD_UNDO, L"Undo", 50, false, false, 0, L"Undo (Ctrl+Z)");
  place(CMD_REDO, L"Redo", 50, false, false, 0, L"Redo (Ctrl+Y)");
  sep();
  place(CMD_BOLD, L"B", 30, true, false, 0, L"Bold (Ctrl+B)");
  place(CMD_ITALIC, L"I", 30, true, false, 0, L"Italic (Ctrl+I)");
  place(CMD_UNDERLINE, L"U", 30, true, false, 0, L"Underline (Ctrl+U)");
  sep();
  place(CMD_ALIGN_LEFT, L"L", 30, true, false, 0, L"Align left");
  place(CMD_ALIGN_CENTER, L"C", 30, true, false, 0, L"Centre");
  place(CMD_ALIGN_RIGHT, L"R", 30, true, false, 0, L"Align right");
  sep();
  place(CMD_FMT_CURRENCY, L"$", 30, false, false, 0, L"Currency format");
  place(CMD_FMT_PERCENT, L"%", 30, false, false, 0, L"Percent format");
  place(CMD_FMT_COMMA, L",", 30, false, false, 0, L"Comma style");
  place(CMD_DEC_INC, L".00+", 40, false, false, 0, L"Increase decimal places");
  place(CMD_DEC_DEC, L".0-", 40, false, false, 0, L"Decrease decimal places");
  sep();
  place(CMD_FILL_COLOR, L"Fill", 46, false, true, 0, L"Cell fill colour");
  place(CMD_FONT_COLOR, L"Text", 46, false, true, 0, L"Font colour");
  sep();
  place(CMD_WRAP_TEXT, L"Wrap", 50, true, false, 0, L"Wrap text");
  place(CMD_BORDERS, L"Border", 56, false, false, 0, L"Outline border");
  place(CMD_FORMAT_CELLS, L"Format...", 74, false, false, 0, L"Format Cells (Ctrl+1)");
}

void layoutApp() {
  RECT rc{};
  GetClientRect(g.hwnd, &rc);
  int w = rc.right, h = rc.bottom;
  g.rcToolbar = RECT{0, 0, w, S(kToolbarH)};
  g.rcFormula = RECT{0, g.rcToolbar.bottom, w, g.rcToolbar.bottom + S(kFormulaH)};
  g.rcStatus = RECT{0, h - S(kStatusH), w, h};
  g.rcTabs = RECT{0, g.rcStatus.top - S(kTabsH), w, g.rcStatus.top};
  g.rcGrid = RECT{0, g.rcFormula.bottom, w, g.rcTabs.top};

  int nameW = S(132);
  g.rcName = RECT{S(6), g.rcFormula.top + S(4), S(6) + nameW, g.rcFormula.bottom - S(4)};
  g.rcFxLabel = RECT{g.rcName.right + S(2), g.rcFormula.top, g.rcName.right + S(28), g.rcFormula.bottom};
  g.rcFormulaEdit = RECT{g.rcFxLabel.right + S(2), g.rcFormula.top + S(4), w - S(8), g.rcFormula.bottom - S(4)};

  g.rcCorner = RECT{g.rcGrid.left, g.rcGrid.top, g.rcGrid.left + S(kHeaderW), g.rcGrid.top + S(kHeaderH)};
  g.rcColHeader = RECT{g.rcGrid.left + S(kHeaderW), g.rcGrid.top, g.rcGrid.right, g.rcGrid.top + S(kHeaderH)};
  g.rcRowHeader = RECT{g.rcGrid.left, g.rcGrid.top + S(kHeaderH), g.rcGrid.left + S(kHeaderW), g.rcGrid.bottom};
  g.rcSheetArea = RECT{0, g.rcTabs.top, w, g.rcTabs.bottom};

  buildToolbar();

  if (g.hName) {
    MoveWindow(g.hName, g.rcName.left + 1, g.rcName.top + 1, g.rcName.right - g.rcName.left - 2,
               g.rcName.bottom - g.rcName.top - 2, TRUE);
    MoveWindow(g.hFormula, g.rcFormulaEdit.left + 1, g.rcFormulaEdit.top + 1,
               g.rcFormulaEdit.right - g.rcFormulaEdit.left - 2,
               g.rcFormulaEdit.bottom - g.rcFormulaEdit.top - 2, TRUE);
  }
  updateScrollBars();
  syncControls();
}

void paintButton(HDC dc, const ToolButton& b, bool hover, bool down) {
  bool on = false;
  switch (b.id) {
    case CMD_BOLD: on = allSelectedBold(); break;
    case CMD_ITALIC: on = g.wb.styleAt(g.sheet, g.sel.r1, g.sel.c1).italic; break;
    case CMD_UNDERLINE: on = g.wb.styleAt(g.sheet, g.sel.r1, g.sel.c1).underline; break;
    case CMD_ALIGN_LEFT: on = currentAlign() == 0; break;
    case CMD_ALIGN_CENTER: on = currentAlign() == 1; break;
    case CMD_ALIGN_RIGHT: on = currentAlign() == 2; break;
    case CMD_WRAP_TEXT: on = currentWrap(); break;
    default: break;
  }

  if (down || on) {
    HBRUSH hb = CreateSolidBrush(down ? kButtonDown : kButtonHot);
    FillRect(dc, &b.rect, hb);
    DeleteObject(hb);
    HPEN pen = CreatePen(PS_SOLID, 1, on ? kAccent : kButtonLine);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, b.rect.left, b.rect.top, b.rect.right, b.rect.bottom);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
  } else if (hover) {
    HBRUSH hb = CreateSolidBrush(kButtonHot);
    FillRect(dc, &b.rect, hb);
    DeleteObject(hb);
  }

  HFONT font = labelFont(b);
  HGDIOBJ oldFont = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, (on && !down) ? kAccent : kChromeText);
  RECT r = b.rect;
  if (b.isColor) r.bottom -= S(6);
  DrawTextW(dc, b.label.c_str(), -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  SelectObject(dc, oldFont);

  if (b.isColor) {
    RECT sw{b.rect.left + S(5), b.rect.bottom - S(9), b.rect.right - S(5), b.rect.bottom - S(4)};
    uint32_t col = b.id == CMD_FILL_COLOR ? currentFill() : currentColor();
    HBRUSH sb = CreateSolidBrush(col ? static_cast<COLORREF>(col) : RGB(255, 255, 255));
    FillRect(dc, &sw, sb);
    DeleteObject(sb);
    HPEN pen = CreatePen(PS_SOLID, 1, kButtonLine);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, sw.left, sw.top, sw.right, sw.bottom);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
  }
}

void paintChrome(HDC dc) {
  // ---- toolbar
  HBRUSH bg = CreateSolidBrush(kChromeBg);
  FillRect(dc, &g.rcToolbar, bg);
  DeleteObject(bg);
  HPEN line = CreatePen(PS_SOLID, 1, kChromeLine);
  HGDIOBJ oldPen = SelectObject(dc, line);
  MoveToEx(dc, g.rcToolbar.left, g.rcToolbar.bottom - 1, nullptr);
  LineTo(dc, g.rcToolbar.right, g.rcToolbar.bottom - 1);
  for (int sx : g_separators) {
    MoveToEx(dc, sx, g.rcToolbar.top + S(9), nullptr);
    LineTo(dc, sx, g.rcToolbar.bottom - S(10));
  }
  SelectObject(dc, oldPen);
  DeleteObject(line);

  for (const ToolButton& b : g.buttons) {
    paintButton(dc, b, b.id == g.hoverButton, b.id == g.pressedButton);
  }

  // ---- formula bar
  HBRUSH fb = CreateSolidBrush(RGB(255, 255, 255));
  FillRect(dc, &g.rcFormula, fb);
  DeleteObject(fb);
  HPEN fp = CreatePen(PS_SOLID, 1, kChromeLine);
  HGDIOBJ fop = SelectObject(dc, fp);
  MoveToEx(dc, g.rcFormula.left, g.rcFormula.bottom - 1, nullptr);
  LineTo(dc, g.rcFormula.right, g.rcFormula.bottom - 1);
  SelectObject(dc, fop);
  DeleteObject(fp);

  // borders around the two child edits
  HPEN bp = CreatePen(PS_SOLID, 1, RGB(190, 195, 200));
  HGDIOBJ bop = SelectObject(dc, bp);
  HGDIOBJ bob = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, g.rcName.left, g.rcName.top, g.rcName.right, g.rcName.bottom);
  Rectangle(dc, g.rcFormulaEdit.left, g.rcFormulaEdit.top, g.rcFormulaEdit.right, g.rcFormulaEdit.bottom);
  SelectObject(dc, bob);
  SelectObject(dc, bop);
  DeleteObject(bp);

  HFONT fx = uiItalicFont();
  HGDIOBJ of = SelectObject(dc, fx);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(110, 115, 120));
  RECT fr = g.rcFxLabel;
  DrawTextW(dc, L"fx", -1, &fr, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  SelectObject(dc, of);

  // ---- sheet tabs
  HBRUSH tb = CreateSolidBrush(kTabStripBg);
  FillRect(dc, &g.rcTabs, tb);
  DeleteObject(tb);
  HPEN tp = CreatePen(PS_SOLID, 1, kChromeLine);
  HGDIOBJ top = SelectObject(dc, tp);
  MoveToEx(dc, g.rcTabs.left, g.rcTabs.top, nullptr);
  LineTo(dc, g.rcTabs.right, g.rcTabs.top);
  SelectObject(dc, top);
  DeleteObject(tp);

  HGDIOBJ tf = SelectObject(dc, g.fontTabs);
  for (const TabRect& t : g.tabs) {
    bool active = (t.index == g.sheet);
    RECT r = t.rect;
    HBRUSH bgb = CreateSolidBrush(active ? RGB(255, 255, 255) : RGB(234, 236, 239));
    FillRect(dc, &r, bgb);
    DeleteObject(bgb);
    HPEN p = CreatePen(PS_SOLID, 1, kChromeLine);
    HGDIOBJ op = SelectObject(dc, p);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, r.left, r.top + S(3), r.right, r.bottom + S(12));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(p);
    if (active) {
      HPEN ap = CreatePen(PS_SOLID, S(2), kAccent);
      HGDIOBJ aop = SelectObject(dc, ap);
      MoveToEx(dc, r.left + S(2), r.top + S(3), nullptr);
      LineTo(dc, r.right - S(2), r.top + S(3));
      SelectObject(dc, aop);
      DeleteObject(ap);
    }
    SetTextColor(dc, active ? kAccent : kChromeText);
    RECT tr = r;
    DrawTextW(dc, widen(g.wb.sheet(t.index).name).c_str(), -1, &tr,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
  }
  // "+" new sheet button
  {
    RECT r = g.rcNewTab;
    HBRUSH bgb = CreateSolidBrush(RGB(234, 236, 239));
    FillRect(dc, &r, bgb);
    DeleteObject(bgb);
    SetTextColor(dc, kChromeText);
    DrawTextW(dc, L"+", -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  }
  SelectObject(dc, tf);

  // ---- status bar
  HBRUSH sb = CreateSolidBrush(RGB(241, 243, 245));
  FillRect(dc, &g.rcStatus, sb);
  DeleteObject(sb);
  HPEN sp = CreatePen(PS_SOLID, 1, kChromeLine);
  HGDIOBJ sop = SelectObject(dc, sp);
  MoveToEx(dc, g.rcStatus.left, g.rcStatus.top, nullptr);
  LineTo(dc, g.rcStatus.right, g.rcStatus.top);
  SelectObject(dc, sop);
  DeleteObject(sp);

  HGDIOBJ sf = SelectObject(dc, g.fontUi);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(70, 76, 84));
  RECT sr = g.rcStatus;
  sr.left += S(10);
  sr.right -= S(10);
  DrawTextW(dc, statusText().c_str(), -1, &sr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
  RECT zr = g.rcStatus;
  zr.right -= S(10);
  DrawTextW(dc, L"100%", -1, &zr, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  SelectObject(dc, sf);
}

// created lazily so the chrome module owns it
HFONT uiItalicFont() {
  static HFONT f = nullptr;
  if (!f) f = uiFont(10, false, true);
  return f;
}

void paintAll(HDC dc) {
  paintChrome(dc);
  paintGrid(dc);
}

bool chromeHitTest(int x, int y, int& id) {
  for (const ToolButton& b : g.buttons) {
    if (x >= b.rect.left && x < b.rect.right && y >= b.rect.top && y < b.rect.bottom) {
      id = b.id;
      return true;
    }
  }
  return false;
}

bool tabsHitTest(int x, int y, int& sheetIndex, bool& plus) {
  plus = false;
  if (!PtInRect(&g.rcTabs, {x, y})) return false;
  for (const TabRect& t : g.tabs) {
    if (x >= t.rect.left && x < t.rect.right && y >= t.rect.top && y < t.rect.bottom) {
      sheetIndex = t.index;
      return true;
    }
  }
  if (x >= g.rcNewTab.left && x < g.rcNewTab.right && y >= g.rcNewTab.top && y < g.rcNewTab.bottom) {
    plus = true;
    return true;
  }
  return false;
}

void syncFormulaBar() {
  if (!g.hwnd) return;
  if (g.hName && !g.editing) {
    std::wstring addr = widen(selectionAddress());
    SetWindowTextW(g.hName, addr.c_str());
  }
  if (g.hFormula && !g.editing) {
    std::wstring raw = widen(activeRaw());
    SetWindowTextW(g.hFormula, raw.c_str());
  }
  InvalidateRect(g.hwnd, &g.rcStatus, FALSE);
}

void syncControls() {
  g.tabs.clear();
  int x = S(8);
  int top = g.rcTabs.top + S(3);
  int bottom = g.rcTabs.bottom - S(1);
  if (!g.hwnd || !g.fontTabs) return;
  HDC dc = GetDC(g.hwnd);
  HGDIOBJ old = SelectObject(dc, g.fontTabs);
  for (int i = 0; i < g.wb.sheetCount(); i++) {
    std::wstring name = widen(g.wb.sheet(i).name);
    SIZE sz{};
    GetTextExtentPoint32W(dc, name.c_str(), static_cast<int>(name.size()), &sz);
    int w = std::min(S(200), std::max(S(64), static_cast<int>(sz.cx) + S(28)));
    TabRect t;
    t.index = i;
    t.rect = RECT{x, top, x + w, bottom};
    g.tabs.push_back(t);
    x += w;
  }
  SelectObject(dc, old);
  ReleaseDC(g.hwnd, dc);
  g.rcNewTab = RECT{x + S(4), top, x + S(4) + S(26), bottom};
  syncFormulaBar();
}

}  // namespace ui
}  // namespace grid
