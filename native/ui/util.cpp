// Shared helpers: UTF-8 <-> UTF-16, fonts, geometry, scrolling, status bar.
#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace grid {
namespace ui {

App g;
static std::wstring g_statusMessage;

int S(int v) { return MulDiv(v, g.dpi, 96); }

std::string toUtf8(const wchar_t* s) {
  if (!s) return std::string();
  int need = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
  if (need <= 1) return std::string();
  std::string out(static_cast<size_t>(need - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s, -1, &out[0], need, nullptr, nullptr);
  return out;
}

std::string narrow(const std::wstring& s) { return toUtf8(s.c_str()); }

std::string narrow(const wchar_t* s, int len) {
  if (!s || len <= 0) return std::string();
  int need = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(need), '\0');
  if (need > 0) WideCharToMultiByte(CP_UTF8, 0, s, len, &out[0], need, nullptr, nullptr);
  return out;
}

std::wstring widen(const std::string& s) {
  if (s.empty()) return std::wstring();
  int need = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(need), L'\0');
  if (need > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], need);
  return out;
}

std::wstring toWide(const std::string& s) { return widen(s); }

HFONT uiFont(int sizePt, bool bold, bool italic, const std::string& face) {
  std::wstring fname = face.empty() ? L"Segoe UI" : widen(face);
  LOGFONTW lf{};
  lf.lfHeight = -MulDiv(sizePt, g.dpi, 72);
  lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
  lf.lfItalic = italic ? TRUE : FALSE;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = CLEARTYPE_QUALITY;
  lstrcpynW(lf.lfFaceName, fname.c_str(), 32);
  return CreateFontIndirectW(&lf);
}

HFONT fontFor(const Style& st, bool forceBold) {
  std::string face = st.fontName.empty() ? std::string("Segoe UI") : st.fontName;
  int size = static_cast<int>(std::lround(st.fontSize > 0 ? st.fontSize : 10.0));
  bool bold = st.bold || forceBold;
  bool italic = st.italic;
  bool underline = st.underline;
  uint64_t key = (static_cast<uint64_t>(size) << 40) | (static_cast<uint64_t>(bold) << 32) |
                 (static_cast<uint64_t>(italic) << 33) | (static_cast<uint64_t>(underline) << 34);
  for (unsigned char ch : face) key = key * 131 + ch;
  auto it = g.fontCache.find(key);
  if (it != g.fontCache.end()) return it->second;
  std::wstring fname = widen(face);
  LOGFONTW lf{};
  lf.lfHeight = -MulDiv(size, g.dpi, 72);
  lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
  lf.lfItalic = italic ? TRUE : FALSE;
  lf.lfUnderline = underline ? TRUE : FALSE;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = CLEARTYPE_QUALITY;
  lstrcpynW(lf.lfFaceName, fname.c_str(), 32);
  HFONT f = CreateFontIndirectW(&lf);
  g.fontCache[key] = f;
  g.fontPool.push_back(f);
  return f;
}

void invalidateAll() {
  if (g.hwnd) InvalidateRect(g.hwnd, nullptr, FALSE);
}

void refreshChrome() {
  if (!g.hwnd) return;
  InvalidateRect(g.hwnd, &g.rcToolbar, FALSE);
  InvalidateRect(g.hwnd, &g.rcFormula, FALSE);
  InvalidateRect(g.hwnd, &g.rcTabs, FALSE);
  InvalidateRect(g.hwnd, &g.rcStatus, FALSE);
}

void setStatus(const std::wstring& text) {
  g_statusMessage = text;
  if (g.hwnd) InvalidateRect(g.hwnd, &g.rcStatus, FALSE);
}

// ------------------------------------------------------------------ geometry
static int safeSheet() {
  if (g.wb.sheetCount() <= 0) return 0;
  return std::max(0, std::min(g.wb.sheetCount() - 1, g.sheet));
}

int colWidthPx(int col) {
  return S(g.wb.colWidth(safeSheet(), col));
}

int rowHeightPx(int row) {
  return S(g.wb.rowHeight(safeSheet(), row));
}

int colX(int col) {
  const Sheet& sh = g.wb.sheet(safeSheet());
  int def = S(100);
  long long x = static_cast<long long>(col) * def;
  for (auto& kv : sh.colW) {
    if (kv.first < col) x += S(kv.second) - def;
  }
  if (x < 0) x = 0;
  if (x > 2000000000LL) x = 2000000000LL;
  return static_cast<int>(x);
}

int rowY(int row) {
  const Sheet& sh = g.wb.sheet(safeSheet());
  int def = S(22);
  long long y = static_cast<long long>(row) * def;
  for (auto& kv : sh.rowH) {
    if (kv.first < row) y += S(kv.second) - def;
  }
  if (y < 0) y = 0;
  if (y > 2000000000LL) y = 2000000000LL;
  return static_cast<int>(y);
}

int colAtX(int contentX) {
  if (contentX < 0) return 0;
  int lo = 0, hi = MAX_COLS - 1, best = 0;
  while (lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    if (colX(mid) <= contentX) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return best;
}

int rowAtY(int contentY) {
  if (contentY < 0) return 0;
  int lo = 0, hi = MAX_ROWS - 1, best = 0;
  while (lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    if (rowY(mid) <= contentY) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return best;
}

int viewportWidth() { return std::max(0, static_cast<int>(g.rcGrid.right - g.rcGrid.left) - S(kHeaderW)); }
int viewportHeight() { return std::max(0, static_cast<int>(g.rcGrid.bottom - g.rcGrid.top) - S(kHeaderH)); }

int gridContentWidth() {
  const Sheet& sh = g.wb.sheet(safeSheet());
  int def = S(100);
  long long w = static_cast<long long>(MAX_COLS) * def;
  for (auto& kv : sh.colW) w += S(kv.second) - def;
  if (w > 2000000000LL) w = 2000000000LL;
  return static_cast<int>(w);
}

int gridContentHeight() {
  const Sheet& sh = g.wb.sheet(safeSheet());
  int def = S(22);
  long long h = static_cast<long long>(MAX_ROWS) * def;
  for (auto& kv : sh.rowH) h += S(kv.second) - def;
  if (h > 2000000000LL) h = 2000000000LL;
  return static_cast<int>(h);
}

int firstVisibleCol() { return colAtX(g.scrollX); }
int firstVisibleRow() { return rowAtY(g.scrollY); }
int lastVisibleCol() { return colAtX(g.scrollX + viewportWidth()); }
int lastVisibleRow() { return rowAtY(g.scrollY + viewportHeight()); }

void scrollBy(int dx, int dy) {
  int maxX = std::max(0, gridContentWidth() - viewportWidth());
  int maxY = std::max(0, gridContentHeight() - viewportHeight());
  int nx = std::max(0, std::min(maxX, g.scrollX + dx));
  int ny = std::max(0, std::min(maxY, g.scrollY + dy));
  if (nx == g.scrollX && ny == g.scrollY) return;
  g.scrollX = nx;
  g.scrollY = ny;
  updateScrollBars();
  if (g.hwnd) InvalidateRect(g.hwnd, &g.rcGrid, FALSE);
  updateCellEditor();
}

void ensureVisibleCell(int r, int c) {
  int vw = viewportWidth(), vh = viewportHeight();
  int x1 = colX(c), x2 = x1 + colWidthPx(c);
  int y1 = rowY(r), y2 = y1 + rowHeightPx(r);
  int sx = g.scrollX, sy = g.scrollY;
  if (x1 < sx) sx = x1;
  else if (x2 > sx + vw) sx = x2 - vw;
  if (y1 < sy) sy = y1;
  else if (y2 > sy + vh) sy = y2 - vh;
  sx = std::max(0, std::min(std::max(0, gridContentWidth() - vw), sx));
  sy = std::max(0, std::min(std::max(0, gridContentHeight() - vh), sy));
  if (sx != g.scrollX || sy != g.scrollY) {
    g.scrollX = sx;
    g.scrollY = sy;
    updateScrollBars();
    if (g.hwnd) InvalidateRect(g.hwnd, &g.rcGrid, FALSE);
    updateCellEditor();
  }
}

void ensureVisible() { ensureVisibleCell(g.sel.r1, g.sel.c1); }

void scrollToCell(int r, int c) {
  int vw = viewportWidth(), vh = viewportHeight();
  int sx = colX(c) - (vw - colWidthPx(c)) / 2;
  int sy = rowY(r) - (vh - rowHeightPx(r)) / 2;
  g.scrollX = std::max(0, std::min(std::max(0, gridContentWidth() - vw), sx));
  g.scrollY = std::max(0, std::min(std::max(0, gridContentHeight() - vh), sy));
  updateScrollBars();
  if (g.hwnd) InvalidateRect(g.hwnd, &g.rcGrid, FALSE);
}

void updateScrollBars() {
  if (!g.hwnd) return;
  SCROLLINFO si{};
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMin = 0;
  si.nMax = std::max(0, gridContentWidth() - 1);
  si.nPage = static_cast<UINT>(std::max(1, viewportWidth()));
  si.nPos = g.scrollX;
  SetScrollInfo(g.hwnd, SB_HORZ, &si, TRUE);
  SCROLLINFO sv{};
  sv.cbSize = sizeof(sv);
  sv.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  sv.nMin = 0;
  sv.nMax = std::max(0, gridContentHeight() - 1);
  sv.nPage = static_cast<UINT>(std::max(1, viewportHeight()));
  sv.nPos = g.scrollY;
  SetScrollInfo(g.hwnd, SB_VERT, &sv, TRUE);
}

// ------------------------------------------------------------------ status bar
std::wstring statusText() {
  std::wstring mode = L"Ready";
  if (g.editing) mode = g.editingInCell ? L"Enter" : L"Edit";
  if (!g_statusMessage.empty()) mode = g_statusMessage;
  Range r = g.sel;
  if (r.single()) return mode;

  struct StatsCache {
    uint64_t version = 0;
    int sheet = -1;
    Range range;
    std::wstring stats;
  };
  static StatsCache cache;
  auto sameRange = [](const Range& a, const Range& b) {
    return a.r1 == b.r1 && a.c1 == b.c1 && a.r2 == b.r2 && a.c2 == b.c2;
  };

  if (cache.version != g.wb.version() || cache.sheet != g.sheet || !sameRange(cache.range, r)) {
    cache.version = g.wb.version();
    cache.sheet = g.sheet;
    cache.range = r;
    cache.stats.clear();

    double sum = 0.0;
    long long nums = 0, nonBlank = 0;
    const long long cap = 20000;
    long long scanned = 0;
    for (int row = r.top(); row <= r.bottom() && scanned < cap; row++) {
      for (int col = r.left(); col <= r.right() && scanned < cap; col++, scanned++) {
        Value v = g.wb.value(safeSheet(), row, col);
        if (v.type == Value::Blank) continue;
        nonBlank++;
        if (v.type == Value::Number) {
          sum += v.num;
          nums++;
        }
      }
    }
    if (nums > 0) {
      double avg = sum / static_cast<double>(nums);
      cache.stats = L"Average: " + widen(generalText(Value::number(avg))) +
                    L"   Count: " + std::to_wstring(nonBlank) +
                    L"   Sum: " + widen(generalText(Value::number(sum)));
    } else if (nonBlank > 0) {
      cache.stats = L"Count: " + std::to_wstring(nonBlank);
    }
  }
  if (!cache.stats.empty()) mode += L"      " + cache.stats;
  return mode;
}

void updateTitle() {
  if (!g.hwnd) return;
  std::string name = g.filePath;
  size_t slash = name.find_last_of("\\/");
  if (slash != std::string::npos) name = name.substr(slash + 1);
  if (name.empty()) name = "Book1";
  std::wstring title = widen(name);
  if (g.dirty) title += L" *";
  title += L" - Grid";
  SetWindowTextW(g.hwnd, title.c_str());
}

}  // namespace ui
}  // namespace grid
