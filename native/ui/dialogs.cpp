// Modal dialogs: Format Cells, Find & Replace, text prompt, and small errors.
// They are plain popup windows driven by a local message loop, which gives us
// pixel-accurate layout without the awkwardness of in-memory DLGTEMPLATEs.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "app.h"

namespace grid {
namespace ui {

namespace {

enum : int {
  ID_FMT_CATEGORY = 2001,
  ID_FMT_CODE,
  ID_FMT_FACE,
  ID_FMT_SIZE,
  ID_FMT_BOLD,
  ID_FMT_ITALIC,
  ID_FMT_UNDERLINE,
  ID_FMT_ALIGN_L,
  ID_FMT_ALIGN_C,
  ID_FMT_ALIGN_R,
  ID_FMT_WRAP,
  ID_FMT_FILL,
  ID_FMT_COLOR,
  ID_FMT_FILL_SWATCH,
  ID_FMT_COLOR_SWATCH,
  ID_FIND_TEXT,
  ID_FIND_REPLACE,
  ID_FIND_CASE,
  ID_FIND_WHOLE,
  ID_FIND_NEXT,
  ID_FIND_REPLACE_ONE,
  ID_FIND_REPLACE_ALL,
  ID_PROMPT_EDIT,
};

struct ModalState {
  bool done = false;
  int result = 0;
  std::string textValue;
};

ModalState* g_modal = nullptr;
HBRUSH g_fillSwatchBrush = nullptr;
HBRUSH g_textSwatchBrush = nullptr;


void centreOnOwner(HWND dlg, int w, int h) {
  RECT pr{};
  GetWindowRect(g.hwnd, &pr);
  int x = pr.left + ((pr.right - pr.left) - w) / 2;
  int y = pr.top + ((pr.bottom - pr.top) - h) / 3;
  SetWindowPos(dlg, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

HWND makeControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h,
                 int id, DWORD ex = 0) {
  HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  if (c) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g.fontUi), TRUE);
  return c;
}

void setComboItems(HWND combo, const wchar_t* const* items, int count, const wchar_t* initial) {
  for (int i = 0; i < count; i++) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i]));
  if (initial) {
    int idx = static_cast<int>(SendMessageW(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                           reinterpret_cast<LPARAM>(initial)));
    if (idx >= 0) SendMessageW(combo, CB_SETCURSEL, idx, 0);
    else SetWindowTextW(combo, initial);
  }
}

std::wstring getText(HWND h) {
  int len = GetWindowTextLengthW(h);
  std::wstring out(static_cast<size_t>(len), L'\0');
  if (len) GetWindowTextW(h, &out[0], len + 1);
  return out;
}

std::wstring getComboText(HWND h) {
  int len = GetWindowTextLengthW(h);
  std::wstring out(static_cast<size_t>(len), L'\0');
  if (len) GetWindowTextW(h, &out[0], len + 1);
  return out;
}

int runModal(HWND dlg, ModalState& state) {
  g_modal = &state;
  EnableWindow(g.hwnd, FALSE);
  SetActiveWindow(dlg);
  HWND focused = GetFocus();
  if (!focused || GetAncestor(focused, GA_PARENT) != dlg) {
    HWND first = GetNextDlgTabItem(dlg, nullptr, FALSE);
    if (first) SetFocus(first);
  }
  MSG msg;
  while (!state.done && GetMessageW(&msg, nullptr, 0, 0)) {
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
      state.result = IDCANCEL;
      state.done = true;
      break;
    }
    if (!IsDialogMessageW(dlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  if (IsWindow(dlg)) DestroyWindow(dlg);
  EnableWindow(g.hwnd, TRUE);
  SetActiveWindow(g.hwnd);
  SetFocus(g.hwnd);
  g_modal = nullptr;
  invalidateAll();
  return state.result;
}

// ------------------------------------------------------------------ format cells
struct FmtCtx {
  HWND hwnd = nullptr;
  HWND category = nullptr, code = nullptr, face = nullptr, size = nullptr;
  HWND bold = nullptr, italic = nullptr, underline = nullptr;
  HWND alignL = nullptr, alignC = nullptr, alignR = nullptr, wrap = nullptr;
  HWND fillSwatch = nullptr, colorSwatch = nullptr;
  Style work;
  ModalState* state = nullptr;
  COLORREF fill = 0, text = 0;
};

FmtCtx g_fmt;

void syncSwatches() {
  if (g_fillSwatchBrush) DeleteObject(g_fillSwatchBrush);
  if (g_textSwatchBrush) DeleteObject(g_textSwatchBrush);
  g_fillSwatchBrush = CreateSolidBrush(g_fmt.fill ? g_fmt.fill : RGB(255, 255, 255));
  g_textSwatchBrush = CreateSolidBrush(g_fmt.text ? g_fmt.text : RGB(0, 0, 0));
  if (g_fmt.fillSwatch) InvalidateRect(g_fmt.fillSwatch, nullptr, TRUE);
  if (g_fmt.colorSwatch) InvalidateRect(g_fmt.colorSwatch, nullptr, TRUE);
}

const wchar_t* kCategories[] = {L"General", L"Number", L"Currency", L"Percent", L"Comma",
                                L"Date",    L"Long Date", L"Time",  L"Scientific", L"Custom"};
const char* const kCategoryFormats[] = {"",        "#,##0.00", "$#,##0.00", "0.00%",       "#,##0",
                                     "yyyy-mm-dd", "dddd, mmmm d, yyyy", "hh:mm:ss", "0.00E+00", ""};

void applyFormatDialog(HWND hwnd) {
  Style st = g_fmt.work;
  st.bold = SendMessageW(g_fmt.bold, BM_GETCHECK, 0, 0) == BST_CHECKED;
  st.italic = SendMessageW(g_fmt.italic, BM_GETCHECK, 0, 0) == BST_CHECKED;
  st.underline = SendMessageW(g_fmt.underline, BM_GETCHECK, 0, 0) == BST_CHECKED;
  st.wrap = SendMessageW(g_fmt.wrap, BM_GETCHECK, 0, 0) == BST_CHECKED;
  st.align = SendMessageW(g_fmt.alignC, BM_GETCHECK, 0, 0) == BST_CHECKED
                 ? 1
                 : (SendMessageW(g_fmt.alignR, BM_GETCHECK, 0, 0) == BST_CHECKED ? 2 : 0);
  st.fontName = narrow(getComboText(g_fmt.face));
  int sizePt = std::atoi(narrow(getComboText(g_fmt.size)).c_str());
  st.fontSize = sizePt > 0 ? sizePt : 11;
  st.numFmt = narrow(getText(g_fmt.code));
  st.fill = g_fmt.fill;
  st.color = g_fmt.text;
  applyFullStyle(st);
  (void)hwnd;
}

LRESULT CALLBACK formatProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      g_fmt.hwnd = hwnd;
      int pad = 12;
      makeControl(hwnd, L"BUTTON", L"Number", BS_GROUPBOX, pad, 8, 400, 100, 0);
      makeControl(hwnd, L"STATIC", L"Category:", SS_LEFT, pad + 10, 28, 70, 18, 0);
      g_fmt.category = makeControl(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, pad + 84, 26, 200, 200,
                                   ID_FMT_CATEGORY);
      setComboItems(g_fmt.category, kCategories, 10, nullptr);
      SendMessageW(g_fmt.category, CB_SETCURSEL, 0, 0);
      makeControl(hwnd, L"STATIC", L"Format code:", SS_LEFT, pad + 10, 58, 70, 18, 0);
      g_fmt.code = makeControl(hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, pad + 84, 56, 296, 22, ID_FMT_CODE);
      SetWindowTextW(g_fmt.code, widen(g_fmt.work.numFmt).c_str());

      makeControl(hwnd, L"BUTTON", L"Font", BS_GROUPBOX, pad, 116, 400, 84, 0);
      makeControl(hwnd, L"STATIC", L"Font:", SS_LEFT, pad + 10, 136, 34, 18, 0);
      g_fmt.face = makeControl(hwnd, L"COMBOBOX", L"", CBS_DROPDOWN | WS_VSCROLL, pad + 48, 134, 180, 200,
                               ID_FMT_FACE);
      const wchar_t* faces[] = {L"Segoe UI", L"Calibri",  L"Arial",   L"Times New Roman", L"Consolas",
                                L"Georgia",  L"Verdana", L"Tahoma",  L"Courier New",     L"Trebuchet MS"};
      std::wstring initialFace = widen(g_fmt.work.fontName.empty() ? "Segoe UI" : g_fmt.work.fontName);
      setComboItems(g_fmt.face, faces, 10, initialFace.c_str());
      makeControl(hwnd, L"STATIC", L"Size:", SS_LEFT, pad + 240, 136, 34, 18, 0);
      g_fmt.size = makeControl(hwnd, L"COMBOBOX", L"", CBS_DROPDOWN | WS_VSCROLL, pad + 278, 134, 60, 200,
                               ID_FMT_SIZE);
      const wchar_t* sizes[] = {L"8", L"9", L"10", L"11", L"12", L"14", L"16", L"18", L"20", L"24", L"28", L"36"};
      wchar_t sizeInit[16];
      swprintf(sizeInit, 16, L"%d", static_cast<int>(g_fmt.work.fontSize > 0 ? g_fmt.work.fontSize : 11));
      setComboItems(g_fmt.size, sizes, 12, sizeInit);

      g_fmt.bold = makeControl(hwnd, L"BUTTON", L"Bold", BS_AUTOCHECKBOX, pad + 10, 172, 66, 20, ID_FMT_BOLD);
      g_fmt.italic = makeControl(hwnd, L"BUTTON", L"Italic", BS_AUTOCHECKBOX, pad + 84, 172, 66, 20, ID_FMT_ITALIC);
      g_fmt.underline =
          makeControl(hwnd, L"BUTTON", L"Underline", BS_AUTOCHECKBOX, pad + 158, 172, 90, 20, ID_FMT_UNDERLINE);
      SendMessageW(g_fmt.bold, BM_SETCHECK, g_fmt.work.bold ? BST_CHECKED : BST_UNCHECKED, 0);
      SendMessageW(g_fmt.italic, BM_SETCHECK, g_fmt.work.italic ? BST_CHECKED : BST_UNCHECKED, 0);
      SendMessageW(g_fmt.underline, BM_SETCHECK, g_fmt.work.underline ? BST_CHECKED : BST_UNCHECKED, 0);

      makeControl(hwnd, L"BUTTON", L"Alignment", BS_GROUPBOX, pad, 208, 240, 66, 0);
      g_fmt.alignL = makeControl(hwnd, L"BUTTON", L"Left", BS_AUTORADIOBUTTON | WS_GROUP, pad + 10, 228, 56, 20,
                                 ID_FMT_ALIGN_L);
      g_fmt.alignC = makeControl(hwnd, L"BUTTON", L"Centre", BS_AUTORADIOBUTTON, pad + 74, 228, 66, 20,
                                 ID_FMT_ALIGN_C);
      g_fmt.alignR = makeControl(hwnd, L"BUTTON", L"Right", BS_AUTORADIOBUTTON, pad + 146, 228, 60, 20,
                                 ID_FMT_ALIGN_R);
      g_fmt.wrap = makeControl(hwnd, L"BUTTON", L"Wrap text", BS_AUTOCHECKBOX, pad + 10, 250, 110, 20, ID_FMT_WRAP);
      SendMessageW(g_fmt.wrap, BM_SETCHECK, g_fmt.work.wrap ? BST_CHECKED : BST_UNCHECKED, 0);
      int align = g_fmt.work.align;
      HWND chosen = align == 1 ? g_fmt.alignC : (align == 2 ? g_fmt.alignR : g_fmt.alignL);
      SendMessageW(chosen, BM_SETCHECK, BST_CHECKED, 0);

      makeControl(hwnd, L"BUTTON", L"Colour", BS_GROUPBOX, pad + 252, 208, 148, 66, 0);
      makeControl(hwnd, L"BUTTON", L"Fill", BS_PUSHBUTTON, pad + 260, 226, 50, 22, ID_FMT_FILL);
      g_fmt.fillSwatch = makeControl(hwnd, L"STATIC", L"", SS_LEFT | WS_BORDER, pad + 316, 226, 40, 22,
                                     ID_FMT_FILL_SWATCH);
      makeControl(hwnd, L"BUTTON", L"Text", BS_PUSHBUTTON, pad + 260, 250, 50, 22, ID_FMT_COLOR);
      g_fmt.colorSwatch = makeControl(hwnd, L"STATIC", L"", SS_LEFT | WS_BORDER, pad + 316, 250, 40, 22,
                                      ID_FMT_COLOR_SWATCH);
      syncSwatches();

      makeControl(hwnd, L"BUTTON", L"OK", BS_DEFPUSHBUTTON, 250, 286, 74, 26, IDOK);
      makeControl(hwnd, L"BUTTON", L"Cancel", BS_PUSHBUTTON, 332, 286, 74, 26, IDCANCEL);
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(wp);
      int code = HIWORD(wp);
      if (id == IDOK) {
        applyFormatDialog(hwnd);
        if (g_fmt.state) {
          g_fmt.state->done = true;
          g_fmt.state->result = IDOK;
        }
        return 0;
      }
      if (id == IDCANCEL) {
        if (g_fmt.state) {
          g_fmt.state->done = true;
          g_fmt.state->result = IDCANCEL;
        }
        return 0;
      }
      if (id == ID_FMT_CATEGORY && code == CBN_SELCHANGE) {
        int sel = static_cast<int>(SendMessageW(g_fmt.category, CB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < 10) SetWindowTextW(g_fmt.code, widen(kCategoryFormats[sel]).c_str());
        return 0;
      }
      if (id == ID_FMT_FILL) {
        static COLORREF custom[16] = {0};
        CHOOSECOLORW cc{};
        cc.lStructSize = sizeof(cc);
        cc.hwndOwner = hwnd;
        cc.lpCustColors = custom;
        cc.rgbResult = g_fmt.fill;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
        if (ChooseColorW(&cc)) {
          g_fmt.fill = cc.rgbResult;
          syncSwatches();
        }
        return 0;
      }
      if (id == ID_FMT_COLOR) {
        static COLORREF custom[16] = {0};
        CHOOSECOLORW cc{};
        cc.lStructSize = sizeof(cc);
        cc.hwndOwner = hwnd;
        cc.lpCustColors = custom;
        cc.rgbResult = g_fmt.text;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
        if (ChooseColorW(&cc)) {
          g_fmt.text = cc.rgbResult;
          syncSwatches();
        }
        return 0;
      }
      return 0;
    }
    case WM_CTLCOLORSTATIC: {
      HWND child = reinterpret_cast<HWND>(lp);
      if (child == g_fmt.fillSwatch && g_fillSwatchBrush) {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetBkColor(dc, g_fmt.fill ? static_cast<COLORREF>(g_fmt.fill) : RGB(255, 255, 255));
        SetTextColor(dc, RGB(0, 0, 0));
        return reinterpret_cast<LRESULT>(g_fillSwatchBrush);
      }
      if (child == g_fmt.colorSwatch && g_textSwatchBrush) {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetBkColor(dc, g_fmt.text ? static_cast<COLORREF>(g_fmt.text) : RGB(0, 0, 0));
        SetTextColor(dc, RGB(0, 0, 0));
        return reinterpret_cast<LRESULT>(g_textSwatchBrush);
      }
      break;
    }
    case WM_CLOSE:
      if (g_modal) {
        g_modal->done = true;
        g_modal->result = IDCANCEL;
      }
      return 0;
    case WM_DESTROY:
      if (g_fillSwatchBrush) {
        DeleteObject(g_fillSwatchBrush);
        g_fillSwatchBrush = nullptr;
      }
      if (g_textSwatchBrush) {
        DeleteObject(g_textSwatchBrush);
        g_textSwatchBrush = nullptr;
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------ find dialog
struct FindCtx {
  HWND find = nullptr, replace = nullptr, matchCase = nullptr, wholeCell = nullptr;
  ModalState* state = nullptr;
};

FindCtx g_find;

LRESULT CALLBACK findProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      int pad = 12;
      makeControl(hwnd, L"STATIC", L"Find what:", SS_LEFT, pad, 18, 80, 18, 0);
      g_find.find = makeControl(hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, pad + 88, 14, 260, 24,
                                ID_FIND_TEXT);
      SetWindowTextW(g_find.find, widen(g.findText).c_str());
      makeControl(hwnd, L"STATIC", L"Replace with:", SS_LEFT, pad, 50, 80, 18, 0);
      g_find.replace = makeControl(hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, pad + 88, 46, 260, 24,
                                   ID_FIND_REPLACE);
      SetWindowTextW(g_find.replace, widen(g.replaceText).c_str());
      g_find.matchCase = makeControl(hwnd, L"BUTTON", L"Match case", BS_AUTOCHECKBOX, pad, 80, 120, 20,
                                     ID_FIND_CASE);
      SendMessageW(g_find.matchCase, BM_SETCHECK, g.findMatchCase ? BST_CHECKED : BST_UNCHECKED, 0);
      g_find.wholeCell = makeControl(hwnd, L"BUTTON", L"Match entire cell contents", BS_AUTOCHECKBOX, pad, 102, 200,
                                     20, ID_FIND_WHOLE);
      SendMessageW(g_find.wholeCell, BM_SETCHECK, g.findWholeCell ? BST_CHECKED : BST_UNCHECKED, 0);
      makeControl(hwnd, L"BUTTON", L"Find Next", BS_DEFPUSHBUTTON, pad, 136, 88, 26, ID_FIND_NEXT);
      makeControl(hwnd, L"BUTTON", L"Replace", BS_PUSHBUTTON, pad + 96, 136, 78, 26, ID_FIND_REPLACE_ONE);
      makeControl(hwnd, L"BUTTON", L"Replace All", BS_PUSHBUTTON, pad + 182, 136, 90, 26, ID_FIND_REPLACE_ALL);
      makeControl(hwnd, L"BUTTON", L"Close", BS_PUSHBUTTON, pad + 280, 136, 70, 26, IDCANCEL);
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(wp);
      if (id == IDCANCEL || id == IDOK) {
        if (g_find.state) {
          g_find.state->done = true;
          g_find.state->result = IDCANCEL;
        }
        return 0;
      }
      g.findText = narrow(getText(g_find.find));
      g.replaceText = narrow(getText(g_find.replace));
      g.findMatchCase = SendMessageW(g_find.matchCase, BM_GETCHECK, 0, 0) == BST_CHECKED;
      g.findWholeCell = SendMessageW(g_find.wholeCell, BM_GETCHECK, 0, 0) == BST_CHECKED;
      if (id == ID_FIND_NEXT) findNext();
      else if (id == ID_FIND_REPLACE_ONE) replaceOne();
      else if (id == ID_FIND_REPLACE_ALL) replaceAll();
      return 0;
    }
    case WM_CLOSE:
      if (g_modal) {
        g_modal->done = true;
        g_modal->result = IDCANCEL;
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------ prompt
struct PromptCtx {
  HWND edit = nullptr;
  ModalState* state = nullptr;
};

PromptCtx g_prompt;
std::wstring g_promptLabel;

LRESULT CALLBACK promptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      makeControl(hwnd, L"STATIC", g_promptLabel.c_str(), SS_LEFT, 12, 14, 320, 18, 0);
      g_prompt.edit = makeControl(hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 12, 36, 320, 24, ID_PROMPT_EDIT);
      SetWindowTextW(g_prompt.edit, widen(g_prompt.state->textValue).c_str());
      SendMessageW(g_prompt.edit, EM_SETSEL, 0, -1);
      makeControl(hwnd, L"BUTTON", L"OK", BS_DEFPUSHBUTTON, 176, 74, 74, 26, IDOK);
      makeControl(hwnd, L"BUTTON", L"Cancel", BS_PUSHBUTTON, 258, 74, 74, 26, IDCANCEL);
      SetFocus(g_prompt.edit);
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(wp);
      if (id == IDOK) {
        if (g_prompt.state) {
          g_prompt.state->textValue = narrow(getText(g_prompt.edit));
          g_prompt.state->done = true;
          g_prompt.state->result = IDOK;
        }
        return 0;
      }
      if (id == IDCANCEL) {
        if (g_prompt.state) {
          g_prompt.state->done = true;
          g_prompt.state->result = IDCANCEL;
        }
        return 0;
      }
      return 0;
    }
    case WM_CLOSE:
      if (g_modal) {
        g_modal->done = true;
        g_modal->result = IDCANCEL;
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void registerDialogClass(const wchar_t* name, WNDPROC proc) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = name;
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  RegisterClassExW(&wc);
}

HWND createDialogWindow(const wchar_t* cls, const wchar_t* title, int w, int h) {
  HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, cls, title,
                             WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, S(w),
                             S(h), g.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
  centreOnOwner(dlg, S(w), S(h));
  return dlg;
}

}  // namespace

bool showFormatCellsDialog() {
  registerDialogClass(L"GridFormatDialog", formatProc);
  g_fmt = FmtCtx();
  g_fmt.work = g.wb.styleAt(g.sheet, g.sel.r1, g.sel.c1);
  g_fmt.fill = static_cast<COLORREF>(g_fmt.work.fill);
  g_fmt.text = static_cast<COLORREF>(g_fmt.work.color ? g_fmt.work.color : RGB(0, 0, 0));
  ModalState state;
  g_fmt.state = &state;
  HWND dlg = createDialogWindow(L"GridFormatDialog", L"Format Cells", 432, 330);
  if (!dlg) return false;
  int result = runModal(dlg, state);
  g_fmt.state = nullptr;
  return result == IDOK;
}

bool showFindDialog() {
  registerDialogClass(L"GridFindDialog", findProc);
  ModalState state;
  g_find = FindCtx();
  g_find.state = &state;
  HWND dlg = createDialogWindow(L"GridFindDialog", L"Find and Replace", 384, 182);
  if (!dlg) return false;
  int result = runModal(dlg, state);
  g_find.state = nullptr;
  return result == IDOK;
}

bool promptText(const std::wstring& title, const std::wstring& label, std::string& value) {
  registerDialogClass(L"GridPromptDialog", promptProc);
  ModalState state;
  state.textValue = value;
  g_promptLabel = label;
  g_prompt = PromptCtx();
  g_prompt.state = &state;
  HWND dlg = createDialogWindow(L"GridPromptDialog", title.c_str(), 356, 140);
  if (!dlg) return false;
  SetWindowTextW(g_prompt.edit, widen(value).c_str());
  SendMessageW(g_prompt.edit, EM_SETSEL, 0, -1);
  SetFocus(g_prompt.edit);
  int result = runModal(dlg, state);
  g_prompt.state = nullptr;
  if (result == IDOK) {
    value = state.textValue;
    return true;
  }
  return false;
}

int promptChoice(const std::wstring& title, const std::wstring& label, const wchar_t** options, int count) {
  std::wstring text = label;
  for (int i = 0; i < count; i++) {
    text += L"\n";
    text += options[i];
  }
  MessageBoxW(g.hwnd, text.c_str(), title.c_str(), MB_ICONINFORMATION | MB_OK);
  return 0;
}

bool showAboutDialog() {
  runCommand(CMD_ABOUT);
  return true;
}

void showError(const std::wstring& message) {
  MessageBoxW(g.hwnd, message.c_str(), L"Grid", MB_ICONWARNING | MB_OK);
}

}  // namespace ui
}  // namespace grid
