// Grid — Win32 desktop application shell.
//
// This follows the shape of a classic Windows spreadsheet: a single top-level
// window owning a menu, a toolbar, a formula bar, a virtualised spreadsheet
// canvas, sheet tabs and a status bar. Only the cell editor and the two formula
// bar fields are real child controls; everything else is drawn with GDI and
// hit-tested by hand, exactly as Excel's own grid window does.
#pragma once

#include <windows.h>

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "../engine/engine.h"

namespace grid {
namespace ui {

// ------------------------------------------------------------------ commands
enum Command {
  CMD_NONE = 0,
  CMD_NEW = 100,
  CMD_OPEN,
  CMD_SAVE,
  CMD_SAVE_AS,
  CMD_EXPORT_CSV,
  CMD_EXIT,

  CMD_UNDO = 200,
  CMD_REDO,
  CMD_CUT,
  CMD_COPY,
  CMD_PASTE,
  CMD_CLEAR,
  CMD_CLEAR_FORMATS,
  CMD_SELECT_ALL,
  CMD_FIND,
  CMD_REPLACE,
  CMD_GOTO,

  CMD_BOLD = 300,
  CMD_ITALIC,
  CMD_UNDERLINE,
  CMD_ALIGN_LEFT,
  CMD_ALIGN_CENTER,
  CMD_ALIGN_RIGHT,
  CMD_FMT_GENERAL,
  CMD_FMT_NUMBER,
  CMD_FMT_CURRENCY,
  CMD_FMT_PERCENT,
  CMD_FMT_COMMA,
  CMD_FMT_DATE,
  CMD_FMT_TIME,
  CMD_FMT_TEXT,
  CMD_DEC_INC,
  CMD_DEC_DEC,
  CMD_FILL_COLOR,
  CMD_FONT_COLOR,
  CMD_FORMAT_CELLS,
  CMD_WRAP_TEXT,
  CMD_BORDERS,

  CMD_INSERT_ROWS = 400,
  CMD_INSERT_COLS,
  CMD_DELETE_ROWS,
  CMD_DELETE_COLS,
  CMD_AUTOFIT_COLS,

  CMD_INSERT_SHEET = 500,
  CMD_DELETE_SHEET,
  CMD_RENAME_SHEET,
  CMD_NEXT_SHEET,
  CMD_PREV_SHEET,
  CMD_FIRST_SHEET,
  CMD_LAST_SHEET,

  CMD_FIND_NEXT = 600,
  CMD_REPLACE_ONE,
  CMD_REPLACE_ALL,

  CMD_ABOUT = 900,
  CMD_SHORTCUTS,
};

// Shortcuts 1..999 are reserved for command ids; 1000+ are mouse-region ids.
constexpr int kFirstCommand = 1;
constexpr int kLastCommand = 999;

// ------------------------------------------------------------------- metrics
constexpr int kToolbarH = 44;
constexpr int kFormulaH = 30;
constexpr int kTabsH = 30;
constexpr int kStatusH = 26;
constexpr int kHeaderH = 24;  // column header band
constexpr int kHeaderW = 54;  // row header band

struct Point {
  int x = 0;
  int y = 0;
};

struct Range {
  int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
  int top() const { return r1 < r2 ? r1 : r2; }
  int bottom() const { return r1 < r2 ? r2 : r1; }
  int left() const { return c1 < c2 ? c1 : c2; }
  int right() const { return c1 < c2 ? c2 : c1; }
  int rows() const { return bottom() - top() + 1; }
  int cols() const { return right() - left() + 1; }
  int64_t count() const { return static_cast<int64_t>(rows()) * cols(); }
  bool contains(int r, int c) const { return r >= top() && r <= bottom() && c >= left() && c <= right(); }
  bool single() const { return rows() == 1 && cols() == 1; }
  void normalise() {
    if (r1 > r2) {
      int t = r1;
      r1 = r2;
      r2 = t;
    }
    if (c1 > c2) {
      int t = c1;
      c1 = c2;
      c2 = t;
    }
  }
};

struct ToolButton {
  int id = 0;
  std::wstring label;
  RECT rect{};
  int width = 30;
  bool toggle = false;   // draws pressed when active()
  bool isColor = false;  // paints a colour swatch under the label
  uint32_t swatch = 0;
  std::wstring tip;
};

struct TabRect {
  int index = 0;
  RECT rect{};
};

struct ClipboardCell {
  std::string raw;
  Style style;
};

struct App {
  // document
  Workbook wb;
  int sheet = 0;
  Range sel;
  std::string filePath;
  bool dirty = false;

  // view
  int scrollX = 0;  // content pixels, relative to A1's top-left
  int scrollY = 0;
  int dpi = 96;
  RECT rcToolbar{}, rcFormula{}, rcGrid{}, rcTabs{}, rcStatus{};
  RECT rcName{}, rcFxLabel{}, rcFormulaEdit{}, rcCorner{}, rcColHeader{}, rcRowHeader{}, rcSheetArea{};
  std::vector<ToolButton> buttons;
  std::vector<TabRect> tabs;
  RECT rcNewTab{};
  int hoverButton = 0;
  int pressedButton = 0;

  // interaction
  bool dragging = false;       // rubber-band selection
  bool dragAnchor = false;
  bool filling = false;        // dragging the fill handle
  bool resizingCol = false;
  bool resizingRow = false;
  int resizeIndex = -1;
  int resizeStartPos = 0;
  int resizeStartSize = 0;
  int fillToRow = 0;
  int fillToCol = 0;
  bool dragOnHeaders = false;

  // editing
  bool editing = false;
  bool editingInCell = false;
  bool committing = false;

  // clipboard
  bool hasInternalClip = false;
  bool clipIsCut = false;
  DWORD clipSeq = 0;
  Range clipSource;
  int clipSheet = 0;
  std::vector<std::vector<ClipboardCell>> clipCells;
  bool marquee = false;
  bool marqueeOn = false;

  // undo
  std::deque<std::string> undoStack;
  std::deque<std::string> redoStack;
  static constexpr size_t kUndoLimit = 100;

  // find & replace
  std::string findText;
  std::string replaceText;
  bool findMatchCase = false;
  bool findWholeCell = false;
  int findRow = 0;
  int findCol = 0;

  // windows / resources
  HWND hwnd = nullptr;
  HWND hName = nullptr;
  HWND hFormula = nullptr;
  HWND hCell = nullptr;
  HFONT fontUi = nullptr;
  HFONT fontUiBold = nullptr;
  HFONT fontHeader = nullptr;
  HFONT fontTabs = nullptr;
  HBRUSH brushWhite = nullptr;
  HBRUSH brushHeader = nullptr;
  HBRUSH brushAccent = nullptr;
  std::unordered_map<uint64_t, HFONT> fontCache;
  std::vector<HFONT> fontPool;
  int lastSmallScroll = 0;

  bool showGridlines = true;
};

extern App g;

// ------------------------------------------------------------------ helpers
int S(int v);  // scale a 96-dpi measurement to the current DPI
std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
std::string narrow(const wchar_t* s, int len);
std::string toUtf8(const wchar_t* s);
std::wstring toWide(const std::string& s);
HFONT fontFor(const Style& st, bool boldUiOverride = false);
HFONT uiFont(int sizePt, bool bold, bool italic = false, const std::string& face = "");
void invalidateAll();
void refreshChrome();
void updateScrollBars();
void setStatus(const std::wstring& text);
std::wstring statusText();

// geometry
int colWidthPx(int col);
int rowHeightPx(int row);
int colX(int col);
int rowY(int row);
int colAtX(int contentX);
int rowAtY(int contentY);
int gridContentWidth();
int gridContentHeight();
int viewportWidth();
int viewportHeight();
int firstVisibleCol();
int firstVisibleRow();
int lastVisibleCol();
int lastVisibleRow();
void ensureVisible();
void ensureVisibleCell(int r, int c);
void scrollBy(int dx, int dy);
void scrollToCell(int r, int c);

// chrome
void layoutApp();
void paintChrome(HDC dc);
void paintGrid(HDC dc);
void paintAll(HDC dc);
void buildToolbar();
void syncFormulaBar();
void syncControls();
bool chromeHitTest(int x, int y, int& id);
bool tabsHitTest(int x, int y, int& sheetIndex, bool& plus);
void paintButton(HDC dc, const ToolButton& b, bool hover, bool down);

// sheet interaction
void onLButtonDown(int x, int y, WPARAM flags);
void onLButtonUp(int x, int y, WPARAM flags);
void onLButtonDblClk(int x, int y, WPARAM flags);
void onMouseMove(int x, int y, WPARAM flags);
void onRButtonDown(int x, int y);
void onKeyDown(int vk, bool ctrl, bool shift, bool alt);
bool onMouseWheel(int delta, bool shift, int x, int y);
bool onSetCursor(int x, int y);
void selectCell(int r, int c, bool extend);
void selectRange(int r1, int c1, int r2, int c2);
void selectWholeSheet();
void moveActive(int dr, int dc, bool extend, bool jumpToEdge);
void pageMove(int dir, bool extend);
void moveToEdge(int dr, int dc, bool extend);
void beginEdit(bool inCell, bool replaceContents);
void beginEditWith(const std::string& seed, bool inCell);
void commitEdit(bool moveDown);
void cancelEdit();
void updateCellEditor();
std::string activeRaw();
std::string selectionAddress();

// commands
void runCommand(int id);
void pushUndo();
void undo();
void redo();
void newWorkbook();
void openWorkbook();
void saveWorkbook();
void saveWorkbookAs();
void exportCsv();
void clearSelection(bool formatsToo);
void clearFormats();
void copySelection(bool cut);
void pasteClipboard();
void applyStyle(const Style& patch);
void applyFullStyle(const Style& st);
void toggleStyleFlag(int flag);
void setNumberFormat(const std::string& fmt);
void changeDecimals(int delta);
void insertRows(int before, int count);
void deleteRows(int start, int count);
void insertCols(int before, int count);
void deleteCols(int start, int count);
void insertSheet();
void deleteSheet();
void renameSheet();
void switchSheet(int index);
void moveSelectionBy(int dr, int dc, bool extend);
void goToAddress();
void fillSelection(int dr, int dc);
void autoFitColumns();
void updateTitle();
bool confirmDiscard();
void findNext();
void replaceOne();
void replaceAll();
bool findInSheet(int startRow, int startCol, int dr, int dc, int& outRow, int& outCol);

// dialogs
bool showFormatCellsDialog();
bool showFindDialog();
bool showAboutDialog();
bool promptText(const std::wstring& title, const std::wstring& label, std::string& value);
int promptChoice(const std::wstring& title, const std::wstring& label, const wchar_t** options, int count);
void showError(const std::wstring& message);

// Headless engine checks; returns the number of failures (0 = success).
int runSelfTest(std::string& report);

}  // namespace ui
}  // namespace grid
