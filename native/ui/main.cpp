// Grid — application entry point and main window.
//
// One top-level window owns the menu, the toolbar, the formula bar, the
// spreadsheet canvas, the sheet tabs and the status bar. Only the name box, the
// formula field and the in-cell editor are real child controls.
#include <windows.h>
#include <windowsx.h>

#include <commdlg.h>

#include <algorithm>
#include <string>

#include "app.h"

namespace grid {
namespace ui {

int runApp(HINSTANCE instance, int showCommand);

namespace {

WNDPROC g_oldCellProc = nullptr;
WNDPROC g_oldFormulaProc = nullptr;
WNDPROC g_oldNameProc = nullptr;

void jumpToNameBoxText() {
  std::wstring text;
  int len = GetWindowTextLengthW(g.hName);
  text.resize(static_cast<size_t>(len));
  if (len) GetWindowTextW(g.hName, &text[0], len + 1);
  std::string address = trim(narrow(text));
  int r1, c1, r2, c2;
  if (parseArea(address, r1, c1, r2, c2)) {
    selectRange(r1, c1, r2, c2);
    scrollToCell(g.sel.r1, g.sel.c1);
    setStatus(L"Go to " + widen(selectionAddress()));
  } else {
    syncFormulaBar();
    setStatus(L"Not a valid reference");
  }
  SetFocus(g.hwnd);
}

LRESULT CALLBACK cellEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_GETDLGCODE:
      return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    case WM_KEYDOWN: {
      bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      if (wp == VK_RETURN) {
        commitEdit(!shift);
        return 0;
      }
      if (wp == VK_TAB) {
        commitEdit(false);
        moveSelectionBy(0, shift ? -1 : 1, false);
        return 0;
      }
      if (wp == VK_ESCAPE) {
        cancelEdit();
        return 0;
      }
      if (wp == VK_F2) {
        std::wstring text;
        int len = GetWindowTextLengthW(hwnd);
        text.resize(static_cast<size_t>(len));
        if (len) GetWindowTextW(hwnd, &text[0], len + 1);
        SendMessageW(hwnd, EM_SETSEL, text.size(), text.size());
        return 0;
      }
      if (wp == VK_UP || wp == VK_DOWN) {
        // Excel commits and moves when the cell is edited with the pointer.
        bool up = wp == VK_UP;
        commitEdit(false);
        moveSelectionBy(up ? -1 : 1, 0, false);
        return 0;
      }
      break;
    }
    case WM_CHAR:
      if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
      break;
    case WM_KILLFOCUS:
      if (g.editing && g.editingInCell) {
        // Defer: destroying/reparenting focus inside WM_KILLFOCUS is fragile.
        PostMessageW(g.hwnd, WM_APP + 10, 0, 0);
      }
      break;
    default:
      break;
  }
  return CallWindowProcW(g_oldCellProc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK formulaEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_GETDLGCODE:
      return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    case WM_KEYDOWN: {
      bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      if (wp == VK_RETURN) {
        commitEdit(true);
        return 0;
      }
      if (wp == VK_TAB) {
        commitEdit(false);
        moveSelectionBy(0, shift ? -1 : 1, false);
        return 0;
      }
      if (wp == VK_ESCAPE) {
        cancelEdit();
        return 0;
      }
      break;
    }
    case WM_CHAR:
      if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
      break;
    case WM_SETFOCUS:
      if (!g.editing) {
        g.editing = true;
        g.editingInCell = false;
        ShowWindow(g.hCell, SW_HIDE);
        InvalidateRect(g.hwnd, &g.rcStatus, FALSE);
        InvalidateRect(g.hwnd, &g.rcGrid, FALSE);
      }
      break;
    case WM_KILLFOCUS:
      if (g.editing && !g.editingInCell) PostMessageW(g.hwnd, WM_APP + 11, 0, 0);
      break;
    default:
      break;
  }
  return CallWindowProcW(g_oldFormulaProc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK nameBoxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_KEYDOWN:
      if (wp == VK_RETURN) {
        jumpToNameBoxText();
        return 0;
      }
      if (wp == VK_ESCAPE) {
        syncFormulaBar();
        SetFocus(g.hwnd);
        return 0;
      }
      if (wp == VK_DOWN || wp == VK_UP) {
        SetFocus(g.hwnd);
        return 0;
      }
      break;
    default:
      break;
  }
  return CallWindowProcW(g_oldNameProc, hwnd, msg, wp, lp);
}

void initWindowContents() {
  g.dpi = GetDeviceCaps(GetDC(g.hwnd), LOGPIXELSX);
  g.fontUi = uiFont(10, false);
  g.fontUiBold = uiFont(10, true);
  g.fontHeader = uiFont(9, false);
  g.fontTabs = uiFont(9, false);

  g.hName = CreateWindowExW(0, L"EDIT", L"A1", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 10, 10,
                            g.hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(4001)), nullptr, nullptr);
  g.hFormula = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 10, 10,
                               g.hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(4002)), nullptr, nullptr);
  g.hCell = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 10, 10, g.hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(4003)), nullptr, nullptr);

  SendMessageW(g.hName, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont(9, false)), TRUE);
  SendMessageW(g.hFormula, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont(10, false)), TRUE);
  SendMessageW(g.hCell, WM_SETFONT, reinterpret_cast<WPARAM>(g.fontUi), TRUE);

  g_oldNameProc = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(g.hName, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(nameBoxProc)));
  g_oldFormulaProc = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(g.hFormula, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(formulaEditProc)));
  g_oldCellProc =
      reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g.hCell, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(cellEditProc)));

  g.brushWhite = CreateSolidBrush(RGB(255, 255, 255));
  g.brushHeader = CreateSolidBrush(RGB(244, 245, 247));
  layoutApp();
  g.sel = Range{};
  syncControls();
  updateScrollBars();
  updateTitle();
}

// ---------------------------------------------------------------- painting
void paintDoubleBuffered(HWND hwnd) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd, &ps);
  RECT rc;
  GetClientRect(hwnd, &rc);
  int w = rc.right, h = rc.bottom;
  if (w <= 0 || h <= 0) {
    EndPaint(hwnd, &ps);
    return;
  }
  HDC mem = CreateCompatibleDC(dc);
  HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
  HGDIOBJ oldBmp = SelectObject(mem, bmp);
  SetBkMode(mem, TRANSPARENT);
  paintAll(mem);
  BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
         ps.rcPaint.bottom - ps.rcPaint.top, mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
  SelectObject(mem, oldBmp);
  DeleteObject(bmp);
  DeleteDC(mem);
  EndPaint(hwnd, &ps);
}

void scrollVertTo(int pos) {
  int maxY = std::max(0, gridContentHeight() - viewportHeight());
  g.scrollY = std::max(0, std::min(maxY, pos));
  updateScrollBars();
  InvalidateRect(g.hwnd, &g.rcGrid, FALSE);
  updateCellEditor();
}

void scrollHorzTo(int pos) {
  int maxX = std::max(0, gridContentWidth() - viewportWidth());
  g.scrollX = std::max(0, std::min(maxX, pos));
  updateScrollBars();
  InvalidateRect(g.hwnd, &g.rcGrid, FALSE);
  updateCellEditor();
}

// ---------------------------------------------------------------- menu
HMENU buildMenuBar() {
  HMENU bar = CreateMenu();
  HMENU file = CreatePopupMenu();
  AppendMenuW(file, MF_STRING, CMD_NEW, L"&New\tCtrl+N");
  AppendMenuW(file, MF_STRING, CMD_OPEN, L"&Open...\tCtrl+O");
  AppendMenuW(file, MF_STRING, CMD_SAVE, L"&Save\tCtrl+S");
  AppendMenuW(file, MF_STRING, CMD_SAVE_AS, L"Save &As...");
  AppendMenuW(file, MF_STRING, CMD_EXPORT_CSV, L"&Export CSV...");
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, CMD_EXIT, L"E&xit");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");

  HMENU edit = CreatePopupMenu();
  AppendMenuW(edit, MF_STRING, CMD_UNDO, L"&Undo\tCtrl+Z");
  AppendMenuW(edit, MF_STRING, CMD_REDO, L"&Redo\tCtrl+Y");
  AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(edit, MF_STRING, CMD_CUT, L"Cu&t\tCtrl+X");
  AppendMenuW(edit, MF_STRING, CMD_COPY, L"&Copy\tCtrl+C");
  AppendMenuW(edit, MF_STRING, CMD_PASTE, L"&Paste\tCtrl+V");
  AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(edit, MF_STRING, CMD_CLEAR, L"Clea&r Contents\tDel");
  AppendMenuW(edit, MF_STRING, CMD_CLEAR_FORMATS, L"Clear F&ormats");
  AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(edit, MF_STRING, CMD_SELECT_ALL, L"Select &All\tCtrl+A");
  AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(edit, MF_STRING, CMD_FIND, L"&Find...\tCtrl+F");
  AppendMenuW(edit, MF_STRING, CMD_REPLACE, L"&Replace...\tCtrl+H");
  AppendMenuW(edit, MF_STRING, CMD_GOTO, L"&Go To...\tCtrl+G");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Edit");

  HMENU insert = CreatePopupMenu();
  AppendMenuW(insert, MF_STRING, CMD_INSERT_ROWS, L"&Rows");
  AppendMenuW(insert, MF_STRING, CMD_INSERT_COLS, L"&Columns");
  AppendMenuW(insert, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(insert, MF_STRING, CMD_INSERT_SHEET, L"&Sheet");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(insert), L"&Insert");

  HMENU format = CreatePopupMenu();
  AppendMenuW(format, MF_STRING, CMD_FORMAT_CELLS, L"&Cells...\tCtrl+1");
  AppendMenuW(format, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(format, MF_STRING, CMD_BOLD, L"&Bold\tCtrl+B");
  AppendMenuW(format, MF_STRING, CMD_ITALIC, L"&Italic\tCtrl+I");
  AppendMenuW(format, MF_STRING, CMD_UNDERLINE, L"&Underline\tCtrl+U");
  AppendMenuW(format, MF_STRING, CMD_WRAP_TEXT, L"&Wrap Text");
  AppendMenuW(format, MF_SEPARATOR, 0, nullptr);
  HMENU align = CreatePopupMenu();
  AppendMenuW(align, MF_STRING, CMD_ALIGN_LEFT, L"&Left");
  AppendMenuW(align, MF_STRING, CMD_ALIGN_CENTER, L"&Centre");
  AppendMenuW(align, MF_STRING, CMD_ALIGN_RIGHT, L"&Right");
  AppendMenuW(format, MF_POPUP, reinterpret_cast<UINT_PTR>(align), L"&Align");
  HMENU numbers = CreatePopupMenu();
  AppendMenuW(numbers, MF_STRING, CMD_FMT_GENERAL, L"&General");
  AppendMenuW(numbers, MF_STRING, CMD_FMT_NUMBER, L"&Number");
  AppendMenuW(numbers, MF_STRING, CMD_FMT_CURRENCY, L"&Currency");
  AppendMenuW(numbers, MF_STRING, CMD_FMT_PERCENT, L"&Percent");
  AppendMenuW(numbers, MF_STRING, CMD_FMT_COMMA, L"Co&mma");
  AppendMenuW(numbers, MF_STRING, CMD_FMT_DATE, L"&Date");
  AppendMenuW(numbers, MF_STRING, CMD_FMT_TIME, L"&Time");
  AppendMenuW(numbers, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(numbers, MF_STRING, CMD_DEC_INC, L"Increase &Decimals");
  AppendMenuW(numbers, MF_STRING, CMD_DEC_DEC, L"Decrease D&ecimals");
  AppendMenuW(format, MF_POPUP, reinterpret_cast<UINT_PTR>(numbers), L"&Number Format");
  AppendMenuW(format, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(format, MF_STRING, CMD_FILL_COLOR, L"Fill &Colour...");
  AppendMenuW(format, MF_STRING, CMD_FONT_COLOR, L"&Font Colour...");
  AppendMenuW(format, MF_STRING, CMD_BORDERS, L"&All Borders");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(format), L"F&ormat");

  HMENU sheetMenu = CreatePopupMenu();
  AppendMenuW(sheetMenu, MF_STRING, CMD_INSERT_SHEET, L"&Insert Sheet");
  AppendMenuW(sheetMenu, MF_STRING, CMD_DELETE_SHEET, L"&Delete Sheet");
  AppendMenuW(sheetMenu, MF_STRING, CMD_RENAME_SHEET, L"&Rename Sheet...");
  AppendMenuW(sheetMenu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(sheetMenu, MF_STRING, CMD_FIRST_SHEET, L"&First Sheet");
  AppendMenuW(sheetMenu, MF_STRING, CMD_PREV_SHEET, L"Pre&vious Sheet");
  AppendMenuW(sheetMenu, MF_STRING, CMD_NEXT_SHEET, L"&Next Sheet");
  AppendMenuW(sheetMenu, MF_STRING, CMD_LAST_SHEET, L"&Last Sheet");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(sheetMenu), L"&Sheet");

  HMENU rowsMenu = CreatePopupMenu();
  AppendMenuW(rowsMenu, MF_STRING, CMD_INSERT_ROWS, L"Insert Rows");
  AppendMenuW(rowsMenu, MF_STRING, CMD_DELETE_ROWS, L"Delete Rows");
  AppendMenuW(rowsMenu, MF_STRING, CMD_INSERT_COLS, L"Insert Columns");
  AppendMenuW(rowsMenu, MF_STRING, CMD_DELETE_COLS, L"Delete Columns");
  AppendMenuW(rowsMenu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(rowsMenu, MF_STRING, CMD_AUTOFIT_COLS, L"AutoFit Column Width");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(rowsMenu), L"&Rows");

  HMENU help = CreatePopupMenu();
  AppendMenuW(help, MF_STRING, CMD_SHORTCUTS, L"&Keyboard Shortcuts");
  AppendMenuW(help, MF_STRING, CMD_ABOUT, L"&About Grid");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
  return bar;
}

// ---------------------------------------------------------------- window proc
LRESULT CALLBACK mainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
      paintDoubleBuffered(hwnd);
      return 0;
    case WM_SIZE:
      layoutApp();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_SETFOCUS:
      InvalidateRect(hwnd, &g.rcGrid, FALSE);
      return 0;
    case WM_KEYDOWN: {
      bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
      onKeyDown(static_cast<int>(wp), ctrl, shift, alt);
      return 0;
    }
    case WM_CHAR: {
      wchar_t ch = static_cast<wchar_t>(wp);
      if (!g.editing && ch >= L' ') {
        beginEdit(true, true);
        std::wstring s(1, ch);
        SetWindowTextW(g.hCell, s.c_str());
        SendMessageW(g.hCell, EM_SETSEL, 1, 1);
        g.editingInCell = true;
        updateCellEditor();
      }
      return 0;
    }
    case WM_MOUSEMOVE:
      onMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp);
      return 0;
    case WM_LBUTTONDOWN: {
      int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
      if (PtInRect(&g.rcTabs, {x, y})) {
        int index = -1;
        bool plus = false;
        if (tabsHitTest(x, y, index, plus)) {
          if (plus) runCommand(CMD_INSERT_SHEET);
          else switchSheet(index);
        }
        return 0;
      }
      onLButtonDown(x, y, wp);
      return 0;
    }
    case WM_LBUTTONUP:
      onLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp);
      return 0;
    case WM_LBUTTONDBLCLK: {
      int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
      if (PtInRect(&g.rcTabs, {x, y})) {
        int index = -1;
        bool plus = false;
        if (tabsHitTest(x, y, index, plus) && !plus) {
          switchSheet(index);
          renameSheet();
          return 0;
        }
      }
      onLButtonDblClk(x, y, wp);
      return 0;
    }
    case WM_RBUTTONDOWN:
      onRButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        if (onSetCursor(pt.x, pt.y)) return TRUE;
      }
      break;
    case WM_MOUSEWHEEL: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      bool shift = (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0;
      if (onMouseWheel(GET_WHEEL_DELTA_WPARAM(wp), shift, pt.x, pt.y)) return 0;
      break;
    }
    case WM_VSCROLL: {
      SCROLLINFO si{};
      si.cbSize = sizeof(si);
      si.fMask = SIF_ALL;
      GetScrollInfo(hwnd, SB_VERT, &si);
      int pos = si.nPos;
      switch (LOWORD(wp)) {
        case SB_LINEUP: pos -= S(22); break;
        case SB_LINEDOWN: pos += S(66); break;
        case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
        case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = si.nMax; break;
        default: break;
      }
      scrollVertTo(pos);
      return 0;
    }
    case WM_HSCROLL: {
      SCROLLINFO si{};
      si.cbSize = sizeof(si);
      si.fMask = SIF_ALL;
      GetScrollInfo(hwnd, SB_HORZ, &si);
      int pos = si.nPos;
      switch (LOWORD(wp)) {
        case SB_LINELEFT: pos -= S(100); break;
        case SB_LINERIGHT: pos += S(100); break;
        case SB_PAGELEFT: pos -= static_cast<int>(si.nPage); break;
        case SB_PAGERIGHT: pos += static_cast<int>(si.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_LEFT: pos = 0; break;
        case SB_RIGHT: pos = si.nMax; break;
        default: break;
      }
      scrollHorzTo(pos);
      return 0;
    }
    case WM_TIMER:
      if (wp == 7) {
        g.marqueeOn = !g.marqueeOn;
        if (g.marquee) {
          RECT r = g.rcGrid;
          InvalidateRect(hwnd, &r, FALSE);
        }
        return 0;
      }
      break;
    case WM_COMMAND: {
      int id = LOWORD(wp);
      if (id >= kFirstCommand && id <= kLastCommand) {
        runCommand(id);
        return 0;
      }
      break;
    }
    case WM_APP + 10:  // in-cell editor lost focus
      if (g.editing && g.editingInCell) commitEdit(false);
      return 0;
    case WM_APP + 11:  // formula field lost focus
      if (g.editing && !g.editingInCell) commitEdit(false);
      return 0;
    case WM_CLOSE:
      if (!confirmDiscard()) return 0;
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, 7);
      for (HFONT f : g.fontPool) DeleteObject(f);
      g.fontPool.clear();
      if (g.fontUi) DeleteObject(g.fontUi);
      if (g.fontUiBold) DeleteObject(g.fontUiBold);
      if (g.fontHeader) DeleteObject(g.fontHeader);
      if (g.fontTabs) DeleteObject(g.fontTabs);
      if (g.brushWhite) DeleteObject(g.brushWhite);
      if (g.brushHeader) DeleteObject(g.brushHeader);
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace
}  // namespace ui
}  // namespace grid

namespace grid {
namespace ui {

int runApp(HINSTANCE instance, int showCommand) {
  SetProcessDPIAware();

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wc.lpfnWndProc = mainWndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = L"GridMainWindow";
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
  if (!RegisterClassExW(&wc)) return 1;

  g.dpi = GetDeviceCaps(GetDC(nullptr), LOGPIXELSX);
  if (g.dpi <= 0) g.dpi = 96;

  HWND hwnd = CreateWindowExW(0, L"GridMainWindow", L"Grid",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760, nullptr, nullptr, instance, nullptr);
  if (!hwnd) return 1;
  g.hwnd = hwnd;
  SetMenu(hwnd, buildMenuBar());
  initWindowContents();
  ShowWindow(hwnd, showCommand == 0 ? SW_SHOWNORMAL : showCommand);
  UpdateWindow(hwnd);
  SetFocus(hwnd);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}

}  // namespace ui
}  // namespace grid

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
  return grid::ui::runApp(instance, showCommand);
}
