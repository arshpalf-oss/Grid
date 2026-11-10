// Grid engine — public API.
//
// Mirrors the architecture of Excel's calculation engine: a formula string is
// lexed, parsed into an AST, and evaluated by a calc engine that pulls values
// from a workbook through a computed-value cache with circular-reference
// detection. Values follow Excel's type and coercion rules.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace grid {

enum class Err { None, Null, Div0, Value, Ref, Name, Num, NA, Spill };
const char* errText(Err e);

struct Value {
  enum Type { Blank, Number, Text, Bool, Error };
  Type type = Blank;
  double num = 0.0;
  std::string text;
  bool b = false;
  Err err = Err::None;

  static Value number(double d);
  static Value string(const std::string& s);
  static Value boolean(bool v);
  static Value error(Err e);
  bool isError() const { return type == Error; }
};

struct Style {
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool wrap = false;
  int align = -1;               // -1 auto (Excel's General), 0 left, 1 centre, 2 right
  uint32_t fill = 0;            // 0 = no fill, else 0x00BBGGRR as stored by Win32
  uint32_t color = 0;           // 0 = automatic, else 0x00BBGGRR (Win32 COLORREF)
  std::string numFmt;           // empty = General
  std::string fontName;         // empty = workbook default font
  double fontSize = 0;          // 0 = default (11pt)
  int border = 0;               // bit 1 left, 2 right, 4 top, 8 bottom
  bool isDefault() const {
    return !bold && !italic && !underline && !wrap && align < 0 && fill == 0 && color == 0 &&
           numFmt.empty() && fontName.empty() && fontSize == 0 && border == 0;
  }
};

struct Cell {
  std::string raw;
  Style style;
};

struct Computed {
  Value value;
  bool formula = false;
  bool circular = false;
  std::string numFmt;           // format inferred from the literal typed
};

// 1,048,576 rows x 16,384 columns, as in Excel.
constexpr int MAX_ROWS = 1048576;
constexpr int MAX_COLS = 16384;
inline int64_t cellKey(int r, int c) { return (static_cast<int64_t>(r) << 20) | c; }

struct Sheet {
  std::string name;
  std::unordered_map<int64_t, Cell> cells;   // sparse storage, like Excel's
  std::unordered_map<int, int> colW;
  std::unordered_map<int, int> rowH;
};

class Workbook {
 public:
  Workbook();
  Workbook clone() const;

  int sheetCount() const { return static_cast<int>(sheets_.size()); }
  Sheet& sheet(int i) { return sheets_[i]; }
  const Sheet& sheet(int i) const { return sheets_[i]; }
  int addSheet(const std::string& name = "");
  bool removeSheet(int index);
  int indexOfName(const std::string& name) const;

  std::string raw(int s, int r, int c) const;
  void setRaw(int s, int r, int c, const std::string& raw);
  const Cell* cell(int s, int r, int c) const;
  void setStyle(int s, int r, int c, const Style& st);
  void patchStyle(int s, int r, int c, const Style& patch, bool clear);
  void clearRange(int s, int r1, int c1, int r2, int c2);
  void insertRows(int s, int before, int count);
  void deleteRows(int s, int start, int count);
  void insertCols(int s, int before, int count);
  void deleteCols(int s, int start, int count);
  void setColWidth(int s, int col, int w);
  void setRowHeight(int s, int row, int h);
  int colWidth(int s, int col) const;
  int rowHeight(int s, int row) const;

  Value value(int s, int r, int c) const;            // computed value
  Computed computed(int s, int r, int c) const;
  std::string display(int s, int r, int c) const;    // formatted text
  std::string numberFormat(int s, int r, int c) const;
  bool usedBounds(int s, int& r1, int& c1, int& r2, int& c2) const;
  void invalidate();
  uint64_t version() const { return version_; }

  bool save(const std::string& path) const;
  bool load(const std::string& path);
  // In-memory snapshot in the same textual form as save(); used for undo.
  std::string serialize() const;
  bool deserialize(const std::string& data);
  Style styleAt(int s, int r, int c) const;
  std::string toCSV(int s) const;

  int nowProvider = 0;  // 0 = real clock
  void setNow(double serial) { nowOverride_ = serial; hasNow_ = true; }
  double now() const;

 private:
  std::vector<Sheet> sheets_;
  mutable std::unordered_map<int64_t, Computed> cache_;   // key: sheet<<44 | cellKey
  mutable std::unordered_set<int64_t> inFlight_;
  mutable std::unordered_set<int64_t> circular_;
  uint64_t version_ = 1;
  double nowOverride_ = 0;
  bool hasNow_ = false;

  Computed compute(int s, int r, int c) const;
  Computed computeFormula(int s, int r, int c, const std::string& raw) const;
  Computed computeLiteral(const std::string& raw) const;
  friend struct Evaluator;
};

// Formatting / dates ---------------------------------------------------------
std::string formatValue(const Value& v, const std::string& fmt);
std::string generalText(const Value& v);
double dateToSerial(int y, int m, int d, int hh = 0, int mm = 0, int ss = 0);
void serialToDate(double serial, int& y, int& m, int& d, int& hh, int& mm, int& ss);
bool parseDateText(const std::string& text, double& out);

// Utilities -----------------------------------------------------------------
std::string toUpper(std::string s);
std::string trim(const std::string& s);
int colToIndex(const std::string& letters);
std::string indexToCol(int index);
std::string a1(int row, int col);
bool parseA1(const std::string& text, int& row, int& col);
bool parseArea(const std::string& text, int& r1, int& c1, int& r2, int& c2);
std::string shiftFormula(const std::string& formula, int dRow, int dCol);

}  // namespace grid
