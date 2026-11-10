#include "engine.h"

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace grid {
namespace {

struct FErr {
  Err code;
};
[[noreturn]] void fail(Err e) { throw FErr{e}; }

// ---------------------------------------------------------------- utilities
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool isIdStart(char c) { return isAlpha(c) || c == '_' || c == '\\'; }
bool isIdChar(char c) { return isAlpha(c) || isDigit(c) || c == '_' || c == '.' || c == '\\'; }

}  // namespace

const char* errText(Err e) {
  switch (e) {
    case Err::Null: return "#NULL!";
    case Err::Div0: return "#DIV/0!";
    case Err::Value: return "#VALUE!";
    case Err::Ref: return "#REF!";
    case Err::Name: return "#NAME?";
    case Err::Num: return "#NUM!";
    case Err::NA: return "#N/A";
    case Err::Spill: return "#SPILL!";
    default: return "";
  }
}

Value Value::number(double d) {
  Value v;
  v.type = Number;
  v.num = d;
  return v;
}
Value Value::string(const std::string& s) {
  Value v;
  v.type = Text;
  v.text = s;
  return v;
}
Value Value::boolean(bool b) {
  Value v;
  v.type = Bool;
  v.b = b;
  return v;
}
Value Value::error(Err e) {
  Value v;
  v.type = Error;
  v.err = e;
  return v;
}

std::string toUpper(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

int colToIndex(const std::string& letters) {
  int n = 0;
  for (char ch : letters) {
    int c = std::toupper(static_cast<unsigned char>(ch)) - 'A' + 1;
    if (c < 1 || c > 26) return -1;
    n = n * 26 + c;
  }
  return n - 1;
}
std::string indexToCol(int index) {
  std::string s;
  int n = index + 1;
  while (n > 0) {
    int rem = (n - 1) % 26;
    s.insert(s.begin(), static_cast<char>('A' + rem));
    n = (n - 1) / 26;
  }
  return s;
}
std::string a1(int row, int col) { return indexToCol(col) + std::to_string(row + 1); }

bool parseA1(const std::string& text, int& row, int& col) {
  std::string s = trim(text);
  if (!s.empty() && s[0] == '$') s.erase(0, 1);
  size_t i = 0;
  std::string letters;
  while (i < s.size() && isAlpha(s[i])) letters += s[i++];
  if (i < s.size() && s[i] == '$') i++;
  std::string digits;
  while (i < s.size() && isDigit(s[i])) digits += s[i++];
  if (i != s.size() || letters.empty() || digits.empty() || letters.size() > 3) return false;
  col = colToIndex(letters);
  row = std::atoi(digits.c_str()) - 1;
  return col >= 0 && col < MAX_COLS && row >= 0 && row < MAX_ROWS;
}

bool parseArea(const std::string& text, int& r1, int& c1, int& r2, int& c2) {
  auto pos = text.find(':');
  if (pos == std::string::npos) {
    if (!parseA1(text, r1, c1)) return false;
    r2 = r1;
    c2 = c1;
    return true;
  }
  if (!parseA1(text.substr(0, pos), r1, c1)) return false;
  if (!parseA1(text.substr(pos + 1), r2, c2)) return false;
  return true;
}

// Rewrite relative references when a formula is copied or filled.
std::string shiftFormula(const std::string& formula, int dRow, int dCol) {
  if (formula.empty() || formula[0] != '=' || (dRow == 0 && dCol == 0)) return formula;
  std::string out;
  size_t i = 0;
  while (i < formula.size()) {
    char ch = formula[i];
    if (ch == '"') {
      out += ch;
      i++;
      while (i < formula.size()) {
        out += formula[i];
        if (formula[i] == '"' && i + 1 < formula.size() && formula[i + 1] == '"') {
          out += formula[i + 1];
          i += 2;
          continue;
        }
        if (formula[i] == '"') {
          i++;
          break;
        }
        i++;
      }
      continue;
    }
    if (isAlpha(ch) || ch == '$') {
      size_t start = i;
      while (i < formula.size() && (isIdChar(formula[i]) || formula[i] == '$')) i++;
      std::string word = formula.substr(start, i - start);
      char prev = start > 0 ? formula[start - 1] : 0;
      char next = i < formula.size() ? formula[i] : 0;
      int r, c;
      if (prev != '!' && next != '(' && next != '!' && parseA1(word, r, c)) {
        bool rowAbs = word.find('$') != std::string::npos && word.find('$', 1) != std::string::npos;
        bool colAbs = !word.empty() && word[0] == '$';
        int nr = rowAbs ? r : std::min(MAX_ROWS - 1, std::max(0, r + dRow));
        int nc = colAbs ? c : std::min(MAX_COLS - 1, std::max(0, c + dCol));
        out += (colAbs ? "$" : "") + indexToCol(nc) + (rowAbs ? "$" : "") + std::to_string(nr + 1);
      } else {
        out += word;
      }
      continue;
    }
    out += ch;
    i++;
  }
  return out;
}

// ---------------------------------------------------------------- dates
namespace {
long long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<long long>(era) * 146097 + static_cast<long long>(doe) - 719468;
}
void civilFromDays(long long z, int& y, int& m, int& d) {
  z += 719468;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp + (mp < 10 ? 3 : -9));
  y += (m <= 2);
}
}  // namespace

namespace {
// daysFromCivil(1899,12,30) is 25569 days before 1970-01-01.
constexpr long long kSerialBase = 25569;      // 1899-12-30
constexpr long long kSerialBaseEarly = 25568;  // 1899-12-31 (pre Mar-1900)

long long serialOfDate(int y, int m, int d) {
  long long own = daysFromCivil(y, m, d);
  bool beforeFakeLeapDay = (y < 1900) || (y == 1900 && m < 3);
  return own + (beforeFakeLeapDay ? kSerialBaseEarly : kSerialBase);
}
}  // namespace

double dateToSerial(int y, int m, int d, int hh, int mm, int ss) {
  if (y >= 0 && y < 1900) y += 1900;
  double serial = static_cast<double>(serialOfDate(y, m, d));
  return serial + (hh * 3600 + mm * 60 + ss) / 86400.0;
}

void serialToDate(double serial, int& y, int& m, int& d, int& hh, int& mm, int& ss) {
  long long whole = static_cast<long long>(std::floor(serial));
  double frac = serial - static_cast<double>(whole);
  long long days = whole - (whole < 61 ? kSerialBaseEarly : kSerialBase);
  civilFromDays(days, y, m, d);
  int secs = static_cast<int>(std::llround(frac * 86400.0)) % 86400;
  if (secs < 0) secs += 86400;
  if (secs >= 86400) secs -= 86400;
  hh = secs / 3600;
  mm = (secs % 3600) / 60;
  ss = secs % 60;
}

bool parseDateText(const std::string& text, double& out) {
  std::string s = trim(text);
  if (s.empty()) return false;
  int y = 0, m = 0, d = 0, hh = 0, mm = 0, ss = 0;
  if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y, &m, &d, &hh, &mm, &ss) >= 3 ||
      std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &m, &d, &hh, &mm, &ss) >= 3) {
    if (m < 1 || m > 12 || d < 1 || d > 31) return false;
    out = dateToSerial(y, m, d, hh, mm, ss);
    return true;
  }
  if (std::sscanf(s.c_str(), "%d/%d/%d %d:%d:%d", &m, &d, &y, &hh, &mm, &ss) >= 3) {
    if (m < 1 || m > 12 || d < 1 || d > 31) return false;
    out = dateToSerial(y, m, d, hh, mm, ss);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- text
std::string generalText(const Value& v) {
  switch (v.type) {
    case Value::Blank: return "";
    case Value::Error: return errText(v.err);
    case Value::Bool: return v.b ? "TRUE" : "FALSE";
    case Value::Text: return v.text;
    case Value::Number: {
      double n = v.num;
      if (!std::isfinite(n)) return "#NUM!";
      if (n == 0) return "0";
      double a = std::fabs(n);
      if (a >= 1e11) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.5E", n);
        std::string s(buf);
        auto e = s.find('E');
        std::string mant = s.substr(0, e);
        while (mant.size() > 1 && mant.back() == '0') mant.pop_back();
        if (!mant.empty() && mant.back() == '.') mant.pop_back();
        int exp = std::atoi(s.c_str() + e + 1);
        std::snprintf(buf, sizeof buf, "%sE%c%02d", mant.c_str(), exp < 0 ? '-' : '+', std::abs(exp));
        return buf;
      }
      char buf[40];
      std::snprintf(buf, sizeof buf, "%.15g", n);
      return buf;
    }
  }
  return "";
}

// ---------------------------------------------------------------- format
namespace {
std::vector<std::string> splitSections(const std::string& fmt) {
  std::vector<std::string> out;
  std::string cur;
  bool quote = false;
  for (size_t i = 0; i < fmt.size(); i++) {
    char c = fmt[i];
    if (c == '"') quote = !quote;
    if (c == '\\' && !quote && i + 1 < fmt.size()) {
      cur += fmt[++i];
      continue;
    }
    if (c == ';' && !quote) {
      out.push_back(cur);
      cur.clear();
      continue;
    }
    cur += c;
  }
  out.push_back(cur);
  return out;
}
std::string groupThousands(const std::string& digits) {
  std::string out;
  int count = 0;
  for (int i = static_cast<int>(digits.size()) - 1; i >= 0; i--) {
    out.insert(out.begin(), digits[i]);
    if (++count % 3 == 0 && i > 0) out.insert(out.begin(), ',');
  }
  return out;
}
// Excel rounds the value as it appears with 15 significant decimal digits, so
// 2.675 (stored as 2.67499999999999982) rounds to 2.68. We reproduce that by
// decomposing the magnitude into 15 decimal digits and rounding on the decimal
// string rather than on binary fractions.
bool decomposeDigits(double a, int mant[15], int& exp10) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.14e", a);
  std::string s(buf);
  size_t e = s.find_first_of("eE");
  if (e == std::string::npos) return false;
  exp10 = std::atoi(s.c_str() + e + 1);
  int idx = 0;
  for (size_t i = 0; i < e && idx < 15; i++) {
    char c = s[i];
    if (c == '.' || c == '-' || c == '+') continue;
    if (!isDigit(c)) return false;
    mant[idx++] = c - '0';
  }
  while (idx < 15) mant[idx++] = 0;
  return true;
}
double recomposeDigits(const int mant[15], int exp10, double sign) {
  long long n = 0;
  for (int i = 0; i < 15; i++) n = n * 10 + mant[i];
  return sign * static_cast<double>(static_cast<long double>(n) * std::pow(10.0L, exp10 - 14));
}
// mode: 0 = half away from zero (ROUND), 1 = away from zero (ROUNDUP),
//       2 = toward zero (ROUNDDOWN / TRUNC).
double roundDecimal(double x, int digits, int mode) {
  if (!std::isfinite(x) || x == 0) return x;
  if (digits > 20) return x;
  if (digits < -300) return 0;
  double sign = x < 0 ? -1 : 1;
  int mant[15];
  int exp10 = 0;
  if (!decomposeDigits(std::fabs(x), mant, exp10)) return x;
  long long k = static_cast<long long>(exp10) + digits;  // leading digits 0..k survive
  if (k < 0) {
    // The value is smaller than the rounding unit.
    double p = std::pow(10.0, digits);
    double scaled = std::fabs(x) * p;
    double r;
    if (mode == 0) r = std::floor(scaled + 0.5);
    else if (mode == 1) r = std::ceil(scaled);
    else r = std::floor(scaled);
    return sign * r / p;
  }
  if (k > 14) return x;
  long long n = 0;
  for (int i = 0; i < 15; i++) n = n * 10 + mant[i];
  long long div = 1;
  for (int i = 0; i < 14 - static_cast<int>(k); i++) div *= 10;
  long long q = n / div, rem = n % div;
  bool up = false;
  if (rem != 0) {
    if (mode == 0) up = rem * 2 >= div;
    else up = (mode == 1);
  }
  if (up) q += 1;
  for (int i = 0; i < 15; i++) mant[i] = 0;
  long long nn = q;
  for (int i = static_cast<int>(k); i >= 0 && nn > 0; i--) {
    mant[i] = static_cast<int>(nn % 10);
    nn /= 10;
  }
  return recomposeDigits(mant, exp10, sign);
}
double roundHalfAway(double x, int digits) { return roundDecimal(x, digits, 0); }
const char* MONTH_NAMES[] = {"January", "February", "March", "April", "May", "June",
                             "July", "August", "September", "October", "November", "December"};
const char* DAY_NAMES[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
// 0 = Sunday. Serial 1 (1900-01-01) is a Sunday in Excel's calendar, and the
// fake 1900-02-29 keeps the rest of the range aligned.
int weekdayOf(double serial) {
  long long d = static_cast<long long>(std::floor(serial));
  return static_cast<int>(((d + 6) % 7 + 7) % 7);
}
}  // namespace

std::string formatValue(const Value& v, const std::string& fmt) {
  if (v.type == Value::Error) return errText(v.err);
  if (v.type == Value::Blank) return "";
  if (v.type == Value::Bool) return v.b ? "TRUE" : "FALSE";
  if (!fmt.empty() && fmt != "General") {
    // A format made only of a text placeholder (optionally with literals) just
    // renders the value as text, as Excel's "@" format does.
    bool hasPlaceholder = fmt.find('0') != std::string::npos || fmt.find('#') != std::string::npos;
    if (fmt.find('@') != std::string::npos && !hasPlaceholder) {
      std::string text = v.type == Value::Text ? v.text : generalText(v);
      std::string out;
      for (char c : fmt) {
        if (c == '@') out += text;
        else if (c != '"') out += c;
      }
      return out;
    }
    if (v.type == Value::Text) {
      auto secs = splitSections(fmt);
      if (secs.size() >= 4) {
        std::string out;
        for (char c : secs[3])
          if (c == '@') out += v.text;
          else if (c != '"') out += c;
        return out;
      }
      return v.text;
    }
  }
  if (fmt.empty() || fmt == "General") return generalText(v);
  if (v.type != Value::Number) return generalText(v);

  auto secs = splitSections(fmt);
  double n = v.num;
  size_t idx = 0;
  bool useAbs = false;
  if (n < 0 && secs.size() >= 2) {
    idx = 1;
    useAbs = true;
  } else if (n == 0 && secs.size() >= 3) {
    idx = 2;
    useAbs = true;
  }
  std::string sec = secs[std::min(idx, secs.size() - 1)];
  if (trim(sec).empty()) return generalText(v);
  bool negative = n < 0 && !useAbs;

  bool isDate = sec.find('y') != std::string::npos || sec.find('d') != std::string::npos ||
                sec.find("hh") != std::string::npos || sec.find("mm:") != std::string::npos ||
                sec.find("AM/PM") != std::string::npos || sec.find("h:") != std::string::npos;

  if (isDate) {
    int y, mo, d, hh, mi, ss;
    serialToDate(n, y, mo, d, hh, mi, ss);
    std::string out;
    bool ampm = sec.find("AM/PM") != std::string::npos || sec.find("A/P") != std::string::npos;
    int dow = weekdayOf(static_cast<double>(static_cast<long long>(std::floor(n))));
    size_t i = 0;
    while (i < sec.size()) {
      char c = sec[i];
      char up = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      if (up == '"') {
        i++;
        while (i < sec.size() && sec[i] != '"') out += sec[i++];
        i++;
        continue;
      }
      auto run = [&](char want) {
        size_t j = i;
        while (j < sec.size() && std::toupper(static_cast<unsigned char>(sec[j])) == want) j++;
        size_t n2 = j - i;
        i = j;
        return n2;
      };
      char buf[64];
      if (up == 'Y') {
        size_t len = run('Y');
        if (len <= 2) std::snprintf(buf, sizeof buf, "%02d", y % 100);
        else std::snprintf(buf, sizeof buf, "%d", y);
        out += buf;
        continue;
      }
      if (up == 'M') {
        size_t len = run('M');
        // month vs minute: minutes if an hour token precedes
        bool minute = out.find(':') != std::string::npos && (sec.find("h") < i || sec.find("h:") != std::string::npos);
        if (minute) {
          std::snprintf(buf, sizeof buf, len >= 2 ? "%02d" : "%d", mi);
          out += buf;
        } else if (len == 1) out += std::to_string(mo);
        else if (len == 2) {
          std::snprintf(buf, sizeof buf, "%02d", mo);
          out += buf;
        } else if (len == 3) out += std::string(MONTH_NAMES[mo - 1]).substr(0, 3);
        else if (len == 4) out += MONTH_NAMES[mo - 1];
        else out += std::string(1, MONTH_NAMES[mo - 1][0]);
        continue;
      }
      if (up == 'D') {
        size_t len = run('D');
        if (len == 1) out += std::to_string(d);
        else if (len == 2) {
          std::snprintf(buf, sizeof buf, "%02d", d);
          out += buf;
        } else if (len == 3) out += std::string(DAY_NAMES[dow]).substr(0, 3);
        else out += DAY_NAMES[dow];
        continue;
      }
      if (up == 'H') {
        size_t len = run('H');
        int h = hh;
        if (ampm) h = (h % 12 == 0) ? 12 : h % 12;
        std::snprintf(buf, sizeof buf, len >= 2 ? "%02d" : "%d", h);
        out += buf;
        continue;
      }
      if (up == 'S') {
        size_t len = run('S');
        std::snprintf(buf, sizeof buf, len >= 2 ? "%02d" : "%d", ss);
        out += buf;
        continue;
      }
      if (up == 'A' && sec.compare(i, 5, "AM/PM") == 0) {
        out += hh < 12 ? "AM" : "PM";
        i += 5;
        continue;
      }
      if (c == ':') {
        out += ':';
        i++;
        continue;
      }
      out += c;
      i++;
    }
    return out;
  }

  int pct = 0;
  for (char c : sec)
    if (c == '%') pct++;
  double val = std::fabs(n) * std::pow(100.0, pct);

  // scientific notation, e.g. 0.00E+00
  size_t epos = std::string::npos;
  for (size_t i = 0; i < sec.size(); i++) {
    if ((sec[i] == 'E' || sec[i] == 'e') && i + 1 < sec.size() && (sec[i + 1] == '+' || sec[i + 1] == '-')) {
      epos = i;
      break;
    }
  }
  if (epos != std::string::npos) {
    size_t dotPos = sec.find('.');
    int dec = 0;
    for (size_t i = (dotPos == std::string::npos ? epos : dotPos + 1); i < epos; i++)
      if (sec[i] == '0' || sec[i] == '#') dec++;
    int expDigits = 0;
    for (size_t i = epos + 2; i < sec.size(); i++)
      if (sec[i] == '0' || sec[i] == '#') expDigits++;
    expDigits = std::max(1, std::min(6, expDigits));
    char signChar = sec[epos + 1];
    double x = val;
    int exp10 = 0;
    if (x > 0) {
      exp10 = static_cast<int>(std::floor(std::log10(x)));
      double mant = x / std::pow(10.0, exp10);
      double limit = 10.0 - 0.5 * std::pow(10.0, -dec);
      if (mant >= limit) {
        exp10 += 1;
        mant /= 10.0;
      }
      if (mant < 1.0 && mant > 0.999999999) mant = 1.0;
      char mbuf[64];
      std::snprintf(mbuf, sizeof mbuf, "%.*f", dec, mant);
      char ebuf[32];
      std::snprintf(ebuf, sizeof ebuf, "%0*d", expDigits, std::abs(exp10));
      std::string prefix, suffix;
      size_t firstPlace = sec.find_first_of("0#?");
      for (size_t i = 0; i < firstPlace && i < sec.size(); i++) {
        if (sec[i] == '"') {
          i++;
          while (i < sec.size() && sec[i] != '"') prefix += sec[i++];
          continue;
        }
        prefix += sec[i];
      }
      bool expDone = false;
      for (size_t i = epos + 2; i < sec.size(); i++) {
        if (sec[i] == '0' || sec[i] == '#') {
          if (!expDone) {
            expDone = true;
            suffix.clear();
          }
          continue;
        }
        if (expDone) suffix += sec[i];
      }
      std::string out = prefix + mbuf;
      out += 'E';
      out += (exp10 < 0 ? '-' : signChar);
      out += ebuf;
      out += suffix;
      return (negative ? "-" : "") + out;
    }
    std::string zeros(static_cast<size_t>(1 + (dec > 0 ? dec + 1 : 0)), '0');
    char ebuf[32];
    std::snprintf(ebuf, sizeof ebuf, "%0*d", expDigits, 0);
    std::string mant = dec > 0 ? "0." + std::string(static_cast<size_t>(dec), '0') : "0";
    return (negative ? "-" : "") + mant + "E" + signChar + ebuf;
  }

  // decimal places and grouping from the pattern
  size_t dot = sec.find('.');
  int decimals = 0;
  int minInt = 0;
  bool grouping = false;
  for (size_t i = 0; i < sec.size(); i++) {
    char c = sec[i];
    if (c == '0' || c == '#' || c == '?') {
      if (dot == std::string::npos || i < dot) {
        if (c == '0') minInt++;
      } else
        decimals++;
    }
    if (c == ',' && (dot == std::string::npos || i < dot)) grouping = true;
  }
  // trailing commas scale by thousands
  int lastDigit = -1;
  for (size_t i = 0; i < sec.size(); i++)
    if (sec[i] == '0' || sec[i] == '#' || sec[i] == '?') lastDigit = static_cast<int>(i);
  int scale = 0;
  for (size_t i = 0; i < sec.size(); i++)
    if (sec[i] == ',' && static_cast<int>(i) > lastDigit) scale++;
  if (scale) val /= std::pow(1000.0, scale);

  double rounded = roundHalfAway(val, decimals);
  char buf[128];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, rounded);
  std::string fixed(buf);
  std::string ip = fixed, fp;
  auto fpPos = fixed.find('.');
  if (fpPos != std::string::npos) {
    ip = fixed.substr(0, fpPos);
    fp = fixed.substr(fpPos + 1);
  }
  while (static_cast<int>(ip.size()) < minInt) ip = "0" + ip;
  if (ip == "0" && minInt == 0) ip.clear();
  if (grouping) ip = groupThousands(ip);

  std::string out;
  bool wroteInt = false, wroteFrac = false;
  for (size_t i = 0; i < sec.size(); i++) {
    char c = sec[i];
    if (c == '"') {
      i++;
      while (i < sec.size() && sec[i] != '"') out += sec[i++];
      continue;
    }
    if (c == '0' || c == '#' || c == '?') {
      bool inInt = dot == std::string::npos || i < dot;
      if (inInt) {
        if (!wroteInt) {
          out += ip;
          wroteInt = true;
        }
      } else if (!wroteFrac) {
        out += fp;
        wroteFrac = true;
      }
      continue;
    }
    if (c == '.') {
      if (decimals > 0) out += '.';
      continue;
    }
    if (c == ',') continue;
    out += c;
  }
  if (!wroteInt) out += ip;
  return (negative ? "-" : "") + out;
}

// ---------------------------------------------------------------- lexer
namespace {

struct Area {
  std::string sheet;
  int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
};

enum class TT { Num, Str, Bool, ErrLit, Ref, Name, Func, Op, LParen, RParen, Comma, Semi, LBrace, RBrace, End };

struct Tok {
  TT t = TT::End;
  double num = 0;
  std::string s;
  bool b = false;
  Err err = Err::None;
  Area area;
};

struct Lexer {
  const std::string& src;
  size_t i = 0;
  std::vector<Tok> out;

  explicit Lexer(const std::string& s) : src(s) {}

  void skip() {
    while (i < src.size() && std::isspace(static_cast<unsigned char>(src[i]))) i++;
  }

  bool readAddr(int& r, int& c, int& kind) {  // kind 1 cell, 2 col, 3 row
    size_t save = i;
    if (i < src.size() && src[i] == '$') i++;
    std::string letters;
    while (i < src.size() && isAlpha(src[i])) letters += src[i++];
    bool rowAbs = false;
    if (i < src.size() && src[i] == '$') {
      rowAbs = true;
      i++;
    }
    std::string digits;
    while (i < src.size() && isDigit(src[i])) digits += src[i++];
    if (!letters.empty() && !digits.empty()) {
      int col = colToIndex(letters);
      int row = std::atoi(digits.c_str()) - 1;
      if (col >= 0 && col < MAX_COLS && row >= 0 && row < MAX_ROWS) {
        r = row;
        c = col;
        kind = 1;
        return true;
      }
    } else if (!letters.empty() && digits.empty() && !rowAbs) {
      int col = colToIndex(letters);
      if (col >= 0 && col < MAX_COLS) {
        r = 0;
        c = col;
        kind = 2;
        return true;
      }
    } else if (letters.empty() && !digits.empty()) {
      int row = std::atoi(digits.c_str()) - 1;
      if (row >= 0 && row < MAX_ROWS) {
        r = row;
        c = 0;
        kind = 3;
        return true;
      }
    }
    i = save;
    return false;
  }

  bool finishArea(int r, int c, int kind, const std::string& sheet, Area& out) {
    size_t save = i;
    skip();
    if (i < src.size() && src[i] == ':') {
      i++;
      skip();
      int r2, c2, k2;
      if (readAddr(r2, c2, k2)) {
        if (kind == 1 && k2 == 1) {
          out.sheet = sheet;
          out.r1 = r;
          out.c1 = c;
          out.r2 = r2;
          out.c2 = c2;
          return true;
        }
        if (kind == 2 && k2 == 2) {
          out.sheet = sheet;
          out.r1 = 0;
          out.c1 = c;
          out.r2 = MAX_ROWS - 1;
          out.c2 = c2;
          return true;
        }
        if (kind == 3 && k2 == 3) {
          out.sheet = sheet;
          out.r1 = r;
          out.c1 = 0;
          out.r2 = r2;
          out.c2 = MAX_COLS - 1;
          return true;
        }
      }
      i = save;
      return false;
    }
    i = save;
    if (kind != 1) return false;
    out.sheet = sheet;
    out.r1 = r;
    out.c1 = c;
    out.r2 = r;
    out.c2 = c;
    return true;
  }

  std::string readQuoted(char quote, bool& ok) {
    std::string s;
    ok = false;
    i++;
    while (i < src.size()) {
      if (src[i] == quote) {
        if (i + 1 < src.size() && src[i + 1] == quote) {
          s += quote;
          i += 2;
          continue;
        }
        i++;
        ok = true;
        return s;
      }
      s += src[i++];
    }
    return s;
  }

  void run() {
    while (i < src.size()) {
      skip();
      if (i >= src.size()) break;
      char ch = src[i];
      if (ch == '"') {
        bool ok;
        std::string s = readQuoted('"', ok);
        if (!ok) fail(Err::Name);
        Tok t;
        t.t = TT::Str;
        t.s = s;
        out.push_back(t);
        continue;
      }
      if (ch == '\'') {
        bool ok;
        std::string sheet = readQuoted('\'', ok);
        if (!ok) fail(Err::Name);
        skip();
        if (i >= src.size() || src[i] != '!') fail(Err::Name);
        i++;
        int r, c, kind;
        Area area;
        if (readAddr(r, c, kind) && finishArea(r, c, kind, sheet, area)) {
          Tok t;
          t.t = TT::Ref;
          t.area = area;
          out.push_back(t);
          continue;
        }
        std::string name;
        while (i < src.size() && isIdChar(src[i])) name += src[i++];
        if (name.empty()) fail(Err::Name);
        Tok t;
        t.t = TT::Name;
        t.s = sheet + "!" + name;
        out.push_back(t);
        continue;
      }
      if (ch == '#') {
        std::string code;
        while (i < src.size() && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '!' ||
                                  src[i] == '/' || src[i] == '?' || src[i] == '_'))
          code += src[i++];
        Tok t;
        t.t = TT::ErrLit;
        auto up = toUpper(code);
        t.err = up == "#DIV/0!" ? Err::Div0 : up == "#VALUE!" ? Err::Value : up == "#REF!" ? Err::Ref
               : up == "#NAME?" ? Err::Name : up == "#NUM!" ? Err::Num : up == "#N/A" ? Err::NA
               : up == "#NULL!" ? Err::Null : up == "#SPILL!" ? Err::Spill : Err::Value;
        out.push_back(t);
        continue;
      }
      if (isDigit(ch) || (ch == '.' && i + 1 < src.size() && isDigit(src[i + 1]))) {
        size_t save = i;
        int r, c, kind;
        if (isdigit(static_cast<unsigned char>(ch)) && readAddr(r, c, kind) && kind == 3) {
          Area area;
          if (finishArea(r, c, 3, "", area) && area.r2 != area.r1) {
            Tok t;
            t.t = TT::Ref;
            t.area = area;
            out.push_back(t);
            continue;
          }
        }
        i = save;
        size_t j = i;
        while (j < src.size() && (isDigit(src[j]) || src[j] == '.')) j++;
        if (j < src.size() && (src[j] == 'e' || src[j] == 'E')) {
          size_t k = j + 1;
          if (k < src.size() && (src[k] == '+' || src[k] == '-')) k++;
          if (k < src.size() && isDigit(src[k])) {
            while (k < src.size() && isDigit(src[k])) k++;
            j = k;
          }
        }
        Tok t;
        t.t = TT::Num;
        t.num = std::atof(src.substr(i, j - i).c_str());
        i = j;
        out.push_back(t);
        continue;
      }
      if (isIdStart(ch) || ch == '$') {
        size_t save = i;
        int r, c, kind;
        if (readAddr(r, c, kind)) {
          Area area;
          if (finishArea(r, c, kind, "", area)) {
            Tok t;
            t.t = TT::Ref;
            t.area = area;
            out.push_back(t);
            continue;
          }
        }
        i = save;
        if (ch == '$') fail(Err::Name);
        std::string id;
        while (i < src.size() && isIdChar(src[i])) id += src[i++];
        if (id.empty()) fail(Err::Name);
        if (i < src.size() && src[i] == '!') {
          i++;
          int r2, c2, k2;
          Area area;
          if (readAddr(r2, c2, k2) && finishArea(r2, c2, k2, id, area)) {
            Tok t;
            t.t = TT::Ref;
            t.area = area;
            out.push_back(t);
            continue;
          }
          std::string name;
          while (i < src.size() && isIdChar(src[i])) name += src[i++];
          Tok t;
          t.t = TT::Name;
          t.s = id + "!" + name;
          out.push_back(t);
          continue;
        }
        if (i < src.size() && src[i] == '(') {
          Tok t;
          t.t = TT::Func;
          t.s = toUpper(id);
          out.push_back(t);
          continue;
        }
        auto up = toUpper(id);
        Tok t;
        if (up == "TRUE" || up == "FALSE") {
          t.t = TT::Bool;
          t.b = up == "TRUE";
        } else {
          t.t = TT::Name;
          t.s = id;
        }
        out.push_back(t);
        continue;
      }
      auto simple = [&](TT tt) {
        Tok t;
        t.t = tt;
        t.s = std::string(1, ch);
        out.push_back(t);
        i++;
      };
      switch (ch) {
        case '(': simple(TT::LParen); continue;
        case ')': simple(TT::RParen); continue;
        case ',': simple(TT::Comma); continue;
        case ';': simple(TT::Semi); continue;
        case '{': simple(TT::LBrace); continue;
        case '}': simple(TT::RBrace); continue;
        default: break;
      }
      static const char* ops[] = {"<>", "<=", ">=", "=", "<", ">", "+", "-", "*", "/", "^", "&", "%"};
      bool matched = false;
      for (const char* op : ops) {
        size_t len = std::strlen(op);
        if (src.compare(i, len, op) == 0) {
          Tok t;
          t.t = TT::Op;
          t.s = op;
          out.push_back(t);
          i += len;
          matched = true;
          break;
        }
      }
      if (matched) continue;
      fail(Err::Name);
    }
    Tok end;
    end.t = TT::End;
    out.push_back(end);
  }
};

// ---------------------------------------------------------------- parser
enum class NK { Num, Str, Bool, ErrL, Ref, Name, Func, Bin, Un, Pct, Missing };

struct Node {
  NK k = NK::Missing;
  double num = 0;
  std::string s;
  bool b = false;
  Err e = Err::None;
  Area area;
  std::vector<Node> kids;
};

struct Parser {
  const std::vector<Tok>& toks;
  size_t p = 0;
  explicit Parser(const std::vector<Tok>& t) : toks(t) {}
  const Tok& peek(size_t n = 0) const { return toks[std::min(p + n, toks.size() - 1)]; }
  bool isOp(std::initializer_list<const char*> ops) {
    if (peek().t != TT::Op) return false;
    for (auto o : ops)
      if (peek().s == o) return true;
    return false;
  }

  Node parseExpr() { return parseCompare(); }

  Node parseCompare() {
    Node left = parseConcat();
    while (isOp({"<", ">", "<=", ">=", "=", "<>"})) {
      Node n;
      n.k = NK::Bin;
      n.s = toks[p++].s;
      n.kids.push_back(left);
      n.kids.push_back(parseConcat());
      left = n;
    }
    return left;
  }
  Node parseConcat() {
    Node left = parseAdd();
    while (isOp({"&"})) {
      Node n;
      n.k = NK::Bin;
      n.s = toks[p++].s;
      n.kids.push_back(left);
      n.kids.push_back(parseAdd());
      left = n;
    }
    return left;
  }
  Node parseAdd() {
    Node left = parseMul();
    while (isOp({"+", "-"})) {
      Node n;
      n.k = NK::Bin;
      n.s = toks[p++].s;
      n.kids.push_back(left);
      n.kids.push_back(parseMul());
      left = n;
    }
    return left;
  }
  Node parseMul() {
    Node left = parsePow();
    while (isOp({"*", "/"})) {
      Node n;
      n.k = NK::Bin;
      n.s = toks[p++].s;
      n.kids.push_back(left);
      n.kids.push_back(parsePow());
      left = n;
    }
    return left;
  }
  Node parsePow() {
    Node left = parseUnary();
    while (isOp({"^"})) {
      Node n;
      n.k = NK::Bin;
      n.s = toks[p++].s;  // Excel's ^ is left-associative
      n.kids.push_back(left);
      n.kids.push_back(parseUnary());
      left = n;
    }
    return left;
  }
  Node parseUnary() {
    if (isOp({"-", "+"})) {
      Node n;
      n.k = NK::Un;
      n.s = toks[p++].s;
      n.kids.push_back(parseUnary());
      return n;
    }
    return parsePostfix();
  }
  Node parsePostfix() {
    Node n = parsePrimary();
    while (isOp({"%"})) {
      p++;
      Node pct;
      pct.k = NK::Pct;
      pct.kids.push_back(n);
      n = pct;
    }
    return n;
  }
  Node parsePrimary() {
    const Tok& t = peek();
    Node n;
    switch (t.t) {
      case TT::Num:
        n.k = NK::Num;
        n.num = t.num;
        p++;
        return n;
      case TT::Str:
        n.k = NK::Str;
        n.s = t.s;
        p++;
        return n;
      case TT::Bool:
        n.k = NK::Bool;
        n.b = t.b;
        p++;
        return n;
      case TT::ErrLit:
        n.k = NK::ErrL;
        n.e = t.err;
        p++;
        return n;
      case TT::Ref:
        n.k = NK::Ref;
        n.area = t.area;
        p++;
        return n;
      case TT::Name:
        n.k = NK::Name;
        n.s = t.s;
        p++;
        return n;
      case TT::LParen: {
        p++;
        Node inner = parseExpr();
        if (peek().t != TT::RParen) fail(Err::Name);
        p++;
        return inner;
      }
      case TT::Func: {
        n.k = NK::Func;
        n.s = t.s;
        p++;
        if (peek().t != TT::LParen) fail(Err::Name);
        p++;
        if (peek().t == TT::RParen) {
          p++;
          return n;
        }
        for (;;) {
          if (peek().t == TT::Comma || peek().t == TT::RParen) {
            Node miss;
            n.kids.push_back(miss);
          } else {
            n.kids.push_back(parseExpr());
          }
          if (peek().t == TT::Comma) {
            p++;
            continue;
          }
          if (peek().t == TT::RParen) {
            p++;
            break;
          }
          fail(Err::Name);
        }
        return n;
      }
      case TT::LBrace: {
        // array literal: {1,2;3,4} -> kept as a name-less construct
        p++;
        std::vector<Node> rows;
        for (;;) {
          rows.push_back(parseExpr());
          if (peek().t == TT::Comma) {
            p++;
            continue;
          }
          if (peek().t == TT::Semi) {
            Node sep;
            sep.k = NK::Missing;
            sep.s = ";";
            rows.push_back(sep);
            p++;
            continue;
          }
          if (peek().t == TT::RBrace) {
            p++;
            break;
          }
          fail(Err::Name);
        }
        n.k = NK::Func;
        n.s = "__ARRAY";
        n.kids = rows;
        return n;
      }
      default:
        fail(Err::Name);
    }
  }
};

// ---------------------------------------------------------------- evaluator
struct Arg {
  bool isRange = false;
  Area area;
  Value v;
};

struct Ctx {
  const Workbook* wb;
  int sheet;
  int row;
  int col;
  std::unordered_map<std::string, Arg> vars;
};

Arg evalNode(const Node& n, Ctx& ctx);

using Fn = Value (*)(std::vector<Arg>&, Ctx&);
std::unordered_map<std::string, Fn>& registry() {
  static std::unordered_map<std::string, Fn> r;
  return r;
}
void reg(const char* name, Fn fn) { registry()[name] = fn; }

// ---- coercion helpers
int sheetIndexFor(const Ctx& ctx, const Arg& a) {
  if (a.area.sheet.empty()) return ctx.sheet;
  int idx = ctx.wb->indexOfName(a.area.sheet);
  if (idx < 0) fail(Err::Ref);
  return idx;
}

Value scalarOf(const Arg& a, Ctx& ctx) {
  if (!a.isRange) {
    if (a.v.isError()) fail(a.v.err);
    return a.v;
  }
  int s = sheetIndexFor(ctx, a);
  Value v = ctx.wb->value(s, a.area.r1, a.area.c1);
  if (v.isError()) fail(v.err);
  return v;
}

double numOf(const Arg& a, Ctx& ctx) {
  Value v = scalarOf(a, ctx);
  switch (v.type) {
    case Value::Blank: return 0;
    case Value::Number: return v.num;
    case Value::Bool: return v.b ? 1 : 0;
    case Value::Text: {
      std::string s = trim(v.text);
      if (s.empty()) fail(Err::Value);
      char* end = nullptr;
      double d = std::strtod(s.c_str(), &end);
      if (end && *end == '\0') return d;
      fail(Err::Value);
    }
    default: fail(v.err == Err::None ? Err::Value : v.err);
  }
}

std::string textOf(const Arg& a, Ctx& ctx) {
  Value v = scalarOf(a, ctx);
  if (v.type == Value::Text) return v.text;
  return generalText(v);
}

bool boolOf(const Arg& a, Ctx& ctx) {
  Value v = scalarOf(a, ctx);
  if (v.type == Value::Bool) return v.b;
  if (v.type == Value::Number) return v.num != 0;
  if (v.type == Value::Blank) return false;
  if (v.type == Value::Text) {
    auto up = toUpper(trim(v.text));
    if (up == "TRUE") return true;
    if (up == "FALSE") return false;
    fail(Err::Value);
  }
  fail(Err::Value);
}

template <typename F>
void forEachCell(const Arg& a, Ctx& ctx, F cb) {
  if (!a.isRange) {
    cb(a.v);
    return;
  }
  int s = sheetIndexFor(ctx, a);
  for (int r = a.area.r1; r <= a.area.r2; r++)
    for (int c = a.area.c1; c <= a.area.c2; c++) cb(ctx.wb->value(s, r, c));
}

std::vector<double> numbersOf(const std::vector<Arg>& args, Ctx& ctx) {
  std::vector<double> out;
  for (const auto& a : args) {
    if (a.isRange) {
      forEachCell(a, ctx, [&](const Value& v) {
        if (v.type == Value::Error) fail(v.err);
        if (v.type == Value::Number) out.push_back(v.num);
      });
    } else {
      Value v = a.v;
      if (v.type == Value::Error) fail(v.err);
      if (v.type == Value::Number) out.push_back(v.num);
      else if (v.type == Value::Bool) out.push_back(v.b ? 1 : 0);
      else if (v.type == Value::Text) {
        std::string s = trim(v.text);
        if (s.empty()) continue;
        char* end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (end && *end == '\0') out.push_back(d);
        else fail(Err::Value);
      }
    }
  }
  return out;
}

std::vector<Value> valuesOf(std::vector<Arg>& args, Ctx& ctx) {
  std::vector<Value> out;
  for (auto& a : args)
    forEachCell(a, ctx, [&](const Value& v) {
      if (v.type == Value::Error) fail(v.err);
      out.push_back(v);
    });
  return out;
}

// ---- criteria (SUMIF/COUNTIF style)
struct Criteria {
  std::string op;
  bool numeric = false;
  double num = 0;
  std::string text;
  bool blank = false;
  bool isBool = false;
  bool boolVal = false;

  bool wildcardMatch(const std::string& s) const {
    // translate * and ? into a simple matcher
    std::function<bool(size_t, size_t)> m = [&](size_t i, size_t j) -> bool {
      while (i < text.size()) {
        if (text[i] == '*') {
          for (size_t k = j; k <= s.size(); k++)
            if (m(i + 1, k)) return true;
          return false;
        }
        if (j >= s.size()) return false;
        if (text[i] != '?' &&
            std::tolower(static_cast<unsigned char>(text[i])) != std::tolower(static_cast<unsigned char>(s[j])))
          return false;
        i++;
        j++;
      }
      return j == s.size();
    };
    return m(0, 0);
  }
};

Criteria makeCriteria(const Arg& a, Ctx& ctx) {
  Value v = scalarOf(a, ctx);
  Criteria c;
  c.op = "=";
  if (v.type == Value::Blank) {
    c.blank = true;
    return c;
  }
  if (v.type == Value::Number) {
    c.numeric = true;
    c.num = v.num;
    return c;
  }
  if (v.type == Value::Bool) {
    c.isBool = true;
    c.boolVal = v.b;
    return c;
  }
  std::string s = v.text;
  for (const char* op : {">=", "<=", "<>", ">", "<", "="}) {
    if (s.rfind(op, 0) == 0) {
      c.op = op;
      s = s.substr(std::strlen(op));
      break;
    }
  }
  if (s.empty()) {
    c.blank = true;
    return c;
  }
  char* end = nullptr;
  double d = std::strtod(s.c_str(), &end);
  if (end && *end == '\0' && trim(s) != "") {
    c.numeric = true;
    c.num = d;
  } else {
    c.text = s;
  }
  return c;
}

int cmpValue(const Value& v, const Criteria& c) {
  if (c.numeric) {
    if (v.type == Value::Number) return v.num < c.num ? -1 : (v.num > c.num ? 1 : 0);
    if (v.type == Value::Text) {
      char* end = nullptr;
      double d = std::strtod(v.text.c_str(), &end);
      if (end && *end == '\0' && !v.text.empty()) return d < c.num ? -1 : (d > c.num ? 1 : 0);
    }
    return 2;
  }
  if (c.isBool) {
    if (v.type == Value::Bool) return v.b == c.boolVal ? 0 : 1;
    return 2;
  }
  if (v.type != Value::Text) return 2;
  int r = 0;
  if (toUpper(v.text) < toUpper(c.text)) r = -1;
  else if (toUpper(v.text) > toUpper(c.text)) r = 1;
  return r;
}

bool testValue(const Value& v, const Criteria& c) {
  if (c.blank) return v.type == Value::Blank || (v.type == Value::Text && v.text.empty());
  if (c.numeric) {
    int r = cmpValue(v, c);
    if (r == 2) return c.op == "<>";
    if (c.op == "=") return r == 0;
    if (c.op == "<>") return r != 0;
    if (c.op == "<") return r < 0;
    if (c.op == ">") return r > 0;
    if (c.op == "<=") return r <= 0;
    return r >= 0;
  }
  if (c.isBool) {
    bool match = (v.type == Value::Bool && v.b == c.boolVal);
    return c.op == "<>" ? !match : match;
  }
  if (v.type != Value::Text) {
    if (c.op == "<>") return true;
    return false;
  }
  bool match = (c.text.find('*') != std::string::npos || c.text.find('?') != std::string::npos)
                   ? c.wildcardMatch(v.text)
                   : toUpper(v.text) == toUpper(c.text);
  return c.op == "<>" ? !match : match;
}

// ---- binary operators
Value applyBin(const std::string& op, const Value& a, const Value& b, Ctx& ctx) {
  if (a.type == Value::Error) return a;
  if (b.type == Value::Error) return b;
  auto toNum = [&](const Value& v) -> double {
    Arg ar;
    ar.v = v;
    return numOf(ar, ctx);
  };
  if (op == "&") {
    Arg x, y;
    x.v = a;
    y.v = b;
    return Value::string(textOf(x, ctx) + textOf(y, ctx));
  }
  if (op == "=" || op == "<>" || op == "<" || op == ">" || op == "<=" || op == ">=") {
    Value x = a, y = b;
    if (x.type == Value::Blank) x = (y.type == Value::Text) ? Value::string("") : Value::number(0);
    if (y.type == Value::Blank) y = (x.type == Value::Text) ? Value::string("") : Value::number(0);
    int cmp;
    if (x.type == Value::Number && y.type == Value::Text) {
      char* e = nullptr;
      double d = std::strtod(y.text.c_str(), &e);
      if (e && *e == '\0' && !y.text.empty()) {
        y = Value::number(d);
      }
    }
    if (x.type == Value::Text && y.type == Value::Number) {
      char* e = nullptr;
      double d = std::strtod(x.text.c_str(), &e);
      if (e && *e == '\0' && !x.text.empty()) x = Value::number(d);
    }
    if (x.type == Value::Number && y.type == Value::Number) cmp = x.num < y.num ? -1 : (x.num > y.num ? 1 : 0);
    else if (x.type == Value::Text && y.type == Value::Text)
      cmp = toUpper(x.text) < toUpper(y.text) ? -1 : (toUpper(x.text) > toUpper(y.text) ? 1 : 0);
    else if (x.type == Value::Bool && y.type == Value::Bool) cmp = x.b == y.b ? 0 : (x.b ? 1 : -1);
    else {
      auto rank = [](const Value& v) { return v.type == Value::Number ? 1 : v.type == Value::Text ? 2 : 3; };
      cmp = rank(x) < rank(y) ? -1 : (rank(x) > rank(y) ? 1 : 0);
    }
    if (op == "=") return Value::boolean(cmp == 0);
    if (op == "<>") return Value::boolean(cmp != 0);
    if (op == "<") return Value::boolean(cmp < 0);
    if (op == ">") return Value::boolean(cmp > 0);
    if (op == "<=") return Value::boolean(cmp <= 0);
    return Value::boolean(cmp >= 0);
  }
  double x = toNum(a);
  double y = toNum(b);
  double r = 0;
  if (op == "+") r = x + y;
  else if (op == "-") r = x - y;
  else if (op == "*") r = x * y;
  else if (op == "/") {
    if (y == 0) return Value::error(Err::Div0);
    r = x / y;
  } else if (op == "^") {
    if (x == 0 && y < 0) return Value::error(Err::Div0);
    if (x < 0 && y != std::floor(y)) return Value::error(Err::Num);
    r = std::pow(x, y);
  }
  if (!std::isfinite(r)) return Value::error(Err::Num);
  return Value::number(r);
}

// ---- SUMIF family core
Value conditionalAggregate(std::vector<Arg>& args, Ctx& ctx, int mode) {
  // mode 0 sum, 1 count, 2 average, 3 max, 4 min
  if (args.size() < 2) fail(Err::Value);
  Criteria crit = makeCriteria(args[1], ctx);
  int critSheet = sheetIndexFor(ctx, args[0]);
  int sumSheet = mode == 1 ? critSheet : sheetIndexFor(ctx, args.size() > 2 && mode == 0 ? args[2] : args[0]);
  int rows = args[0].isRange ? args[0].area.r2 - args[0].area.r1 + 1 : 1;
  int cols = args[0].isRange ? args[0].area.c2 - args[0].area.c1 + 1 : 1;
  int cr1 = args[0].isRange ? args[0].area.r1 : ctx.row;
  int cc1 = args[0].isRange ? args[0].area.c1 : ctx.col;
  int sr1 = cr1, sc1 = cc1;
  if (mode != 1 && args.size() > 2 && mode == 0) {
    sr1 = args[2].isRange ? args[2].area.r1 : 0;
    sc1 = args[2].isRange ? args[2].area.c1 : 0;
  }
  if (mode != 1) {
    sumSheet = sheetIndexFor(ctx, args.size() > 2 ? args[2] : args[0]);
    if (args.size() > 2) {
      sr1 = args[2].isRange ? args[2].area.r1 : 0;
      sc1 = args[2].isRange ? args[2].area.c1 : 0;
    }
  }
  double total = 0;
  int count = 0;
  double best = 0;
  for (int i = 0; i < rows; i++)
    for (int j = 0; j < cols; j++) {
      Value cv = ctx.wb->value(critSheet, cr1 + i, cc1 + j);
      if (!testValue(cv, crit)) continue;
      if (mode == 1) {
        count++;
        continue;
      }
      Value v = ctx.wb->value(sumSheet, sr1 + i, sc1 + j);
      if (v.type == Value::Error) fail(v.err);
      if (v.type == Value::Number) {
        total += v.num;
        if (count == 0 || v.num > best) best = v.num;
        if (mode == 4 && (count == 0 || v.num < best)) best = v.num;
        if (mode == 4) best = (count == 0) ? v.num : std::min(best, v.num);
        count++;
      }
    }
  switch (mode) {
    case 0: return Value::number(total);
    case 1: return Value::number(count);
    case 2:
      if (!count) return Value::error(Err::Div0);
      return Value::number(total / count);
    case 3: return Value::number(count ? best : 0);
    case 4: return Value::number(count ? best : 0);
  }
  return Value::number(0);
}

double pmtCalc(double rate, double nper, double pv, double fv, double type) {
  if (nper == 0) fail(Err::Num);
  if (rate == 0) return -(pv + fv) / nper;
  double pvif = std::pow(1 + rate, nper);
  double factor = ((1 + rate * type) * (pvif - 1)) / rate;
  return -(pv * pvif + fv) / factor;
}

Value addMonths(double serial, int months, bool endOfMonth) {
  int y, m, d, hh, mi, ss;
  serialToDate(serial, y, m, d, hh, mi, ss);
  int total = y * 12 + (m - 1) + months;
  int ny = total / 12;
  int nm = total % 12 + 1;
  static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int dim = mdays[nm - 1];
  if (nm == 2 && ((ny % 4 == 0 && ny % 100 != 0) || ny % 400 == 0)) dim = 29;
  int nd = endOfMonth ? dim : std::min(d, dim);
  return Value::number(dateToSerial(ny, nm, nd, hh, mi, ss));
}

// ---- function library
#define A(i) (i < (int)args.size() ? args[i] : Arg())
#define NUM(i) numOf(A(i), ctx)
#define TXT(i) textOf(A(i), ctx)

Value fnSum(std::vector<Arg>& args, Ctx& ctx) {
  double t = 0;
  for (double n : numbersOf(args, ctx)) t += n;
  return Value::number(t);
}
Value fnProduct(std::vector<Arg>& args, Ctx& ctx) {
  auto v = numbersOf(args, ctx);
  if (v.empty()) return Value::number(0);
  double p = 1;
  for (double n : v) p *= n;
  return Value::number(p);
}
Value fnAbs(std::vector<Arg>& args, Ctx& ctx) { return Value::number(std::fabs(NUM(0))); }
Value fnSign(std::vector<Arg>& args, Ctx& ctx) { double x = NUM(0); return Value::number(x > 0 ? 1 : x < 0 ? -1 : 0); }
Value fnInt(std::vector<Arg>& args, Ctx& ctx) { return Value::number(std::floor(NUM(0))); }
Value fnSqrt(std::vector<Arg>& args, Ctx& ctx) { double x = NUM(0); if (x < 0) fail(Err::Num); return Value::number(std::sqrt(x)); }
Value fnPower(std::vector<Arg>& args, Ctx& ctx) { return Value::number(std::pow(NUM(0), NUM(1))); }
Value fnExp(std::vector<Arg>& args, Ctx& ctx) { return Value::number(std::exp(NUM(0))); }
Value fnLn(std::vector<Arg>& args, Ctx& ctx) { double x = NUM(0); if (x <= 0) fail(Err::Num); return Value::number(std::log(x)); }
Value fnLog10(std::vector<Arg>& args, Ctx& ctx) { double x = NUM(0); if (x <= 0) fail(Err::Num); return Value::number(std::log10(x)); }
Value fnLog(std::vector<Arg>& args, Ctx& ctx) {
  double x = NUM(0);
  double b = args.size() > 1 ? NUM(1) : 10.0;
  if (x <= 0 || b <= 0 || b == 1) fail(Err::Num);
  return Value::number(std::log(x) / std::log(b));
}
Value fnPi(std::vector<Arg>&, Ctx&) { return Value::number(M_PI); }
Value fnMod(std::vector<Arg>& args, Ctx& ctx) {
  double n = NUM(0), d = NUM(1);
  if (d == 0) fail(Err::Div0);
  return Value::number(n - d * std::floor(n / d));
}
Value fnQuotient(std::vector<Arg>& args, Ctx& ctx) { double d = NUM(1); if (d == 0) fail(Err::Div0); return Value::number(std::trunc(NUM(0) / d)); }
Value fnRound(std::vector<Arg>& args, Ctx& ctx) {
  int digits = args.size() > 1 ? (int)NUM(1) : 0;
  return Value::number(roundHalfAway(NUM(0), digits));
}
Value fnRoundUp(std::vector<Arg>& args, Ctx& ctx) {
  int digits = args.size() > 1 ? (int)NUM(1) : 0;
  return Value::number(roundDecimal(NUM(0), digits, 1));
}
Value fnRoundDown(std::vector<Arg>& args, Ctx& ctx) {
  int digits = args.size() > 1 ? (int)NUM(1) : 0;
  return Value::number(roundDecimal(NUM(0), digits, 2));
}
Value fnTrunc(std::vector<Arg>& args, Ctx& ctx) {
  int digits = args.size() > 1 ? (int)NUM(1) : 0;
  return Value::number(roundDecimal(NUM(0), digits, 2));
}
Value fnCeiling(std::vector<Arg>& args, Ctx& ctx) {
  double x = NUM(0);
  double s = args.size() > 1 ? NUM(1) : 1;
  if (s == 0) return Value::number(0);
  return Value::number(std::ceil(x / s) * s);
}
Value fnFloor(std::vector<Arg>& args, Ctx& ctx) {
  double x = NUM(0);
  double s = args.size() > 1 ? NUM(1) : 1;
  if (s == 0) return Value::number(0);
  return Value::number(std::floor(x / s) * s);
}
Value fnSumIf(std::vector<Arg>& args, Ctx& ctx) { return conditionalAggregate(args, ctx, 0); }
Value fnCountIf(std::vector<Arg>& args, Ctx& ctx) { return conditionalAggregate(args, ctx, 1); }
Value fnAverageIf(std::vector<Arg>& args, Ctx& ctx) { return conditionalAggregate(args, ctx, 2); }
Value fnMaxIfs(std::vector<Arg>& args, Ctx& ctx) { return conditionalAggregate(args, ctx, 3); }
Value fnMinIfs(std::vector<Arg>& args, Ctx& ctx) { return conditionalAggregate(args, ctx, 4); }

Value fnSumIfs(std::vector<Arg>& args, Ctx& ctx) {
  if (args.size() < 3) fail(Err::Value);
  int rows = args[1].isRange ? args[1].area.r2 - args[1].area.r1 + 1 : 1;
  int cols = args[1].isRange ? args[1].area.c2 - args[1].area.c1 + 1 : 1;
  int sumSheet = sheetIndexFor(ctx, args[0]);
  int sr = args[0].isRange ? args[0].area.r1 : 0;
  int sc = args[0].isRange ? args[0].area.c1 : 0;
  std::vector<Criteria> crits;
  std::vector<int> crSheet, cr1, cc1;
  for (size_t i = 1; i + 1 < args.size(); i += 2) {
    crits.push_back(makeCriteria(args[i + 1], ctx));
    crSheet.push_back(sheetIndexFor(ctx, args[i]));
    cr1.push_back(args[i].isRange ? args[i].area.r1 : 0);
    cc1.push_back(args[i].isRange ? args[i].area.c1 : 0);
  }
  double total = 0;
  int count = 0;
  for (int i = 0; i < rows; i++)
    for (int j = 0; j < cols; j++) {
      bool ok = true;
      for (size_t k = 0; k < crits.size(); k++) {
        if (!testValue(ctx.wb->value(crSheet[k], cr1[k] + i, cc1[k] + j), crits[k])) {
          ok = false;
          break;
        }
      }
      if (!ok) continue;
      Value v = ctx.wb->value(sumSheet, sr + i, sc + j);
      if (v.type == Value::Number) {
        total += v.num;
        count++;
      }
    }
  return Value::number(total);
}

Value fnCountIfs(std::vector<Arg>& args, Ctx& ctx) {
  if (args.size() < 2) fail(Err::Value);
  int rows = args[0].isRange ? args[0].area.r2 - args[0].area.r1 + 1 : 1;
  int cols = args[0].isRange ? args[0].area.c2 - args[0].area.c1 + 1 : 1;
  std::vector<Criteria> crits;
  std::vector<int> sh, r1s, c1s;
  for (size_t i = 0; i + 1 < args.size(); i += 2) {
    crits.push_back(makeCriteria(args[i + 1], ctx));
    sh.push_back(sheetIndexFor(ctx, args[i]));
    r1s.push_back(args[i].isRange ? args[i].area.r1 : 0);
    c1s.push_back(args[i].isRange ? args[i].area.c1 : 0);
  }
  int count = 0;
  for (int i = 0; i < rows; i++)
    for (int j = 0; j < cols; j++) {
      bool ok = true;
      for (size_t k = 0; k < crits.size(); k++)
        if (!testValue(ctx.wb->value(sh[k], r1s[k] + i, c1s[k] + j), crits[k])) {
          ok = false;
          break;
        }
      if (ok) count++;
    }
  return Value::number(count);
}

Value fnAverageIfs(std::vector<Arg>& args, Ctx& ctx) {
  if (args.size() < 3) fail(Err::Value);
  Value sum = fnSumIfs(args, ctx);
  // average = sum / matching count of numeric cells in the average range
  int rows = args[1].isRange ? args[1].area.r2 - args[1].area.r1 + 1 : 1;
  int cols = args[1].isRange ? args[1].area.c2 - args[1].area.c1 + 1 : 1;
  int sumSheet = sheetIndexFor(ctx, args[0]);
  int sr = args[0].isRange ? args[0].area.r1 : 0;
  int sc = args[0].isRange ? args[0].area.c1 : 0;
  std::vector<Criteria> crits;
  std::vector<int> sh, r1s, c1s;
  for (size_t i = 1; i + 1 < args.size(); i += 2) {
    crits.push_back(makeCriteria(args[i + 1], ctx));
    sh.push_back(sheetIndexFor(ctx, args[i]));
    r1s.push_back(args[i].isRange ? args[i].area.r1 : 0);
    c1s.push_back(args[i].isRange ? args[i].area.c1 : 0);
  }
  int count = 0;
  for (int i = 0; i < rows; i++)
    for (int j = 0; j < cols; j++) {
      bool ok = true;
      for (size_t k = 0; k < crits.size(); k++)
        if (!testValue(ctx.wb->value(sh[k], r1s[k] + i, c1s[k] + j), crits[k])) {
          ok = false;
          break;
        }
      if (ok && ctx.wb->value(sumSheet, sr + i, sc + j).type == Value::Number) count++;
    }
  if (!count) fail(Err::Div0);
  return Value::number(sum.num / count);
}

Value fnSumProduct(std::vector<Arg>& args, Ctx& ctx) {
  if (args.empty()) fail(Err::Value);
  int rows = args[0].isRange ? args[0].area.r2 - args[0].area.r1 + 1 : 1;
  int cols = args[0].isRange ? args[0].area.c2 - args[0].area.c1 + 1 : 1;
  for (auto& a : args)
    if (a.isRange) {
      rows = std::min(rows, a.area.r2 - a.area.r1 + 1);
      cols = std::min(cols, a.area.c2 - a.area.c1 + 1);
    }
  double total = 0;
  for (int i = 0; i < rows; i++)
    for (int j = 0; j < cols; j++) {
      double prod = 1;
      for (auto& a : args) {
        Value v;
        if (a.isRange) v = ctx.wb->value(sheetIndexFor(ctx, a), a.area.r1 + i, a.area.c1 + j);
        else v = a.v;
        if (v.type == Value::Error) fail(v.err);
        prod *= (v.type == Value::Number) ? v.num : (v.type == Value::Bool ? (v.b ? 1 : 0) : 0);
      }
      total += prod;
    }
  return Value::number(total);
}

Value fnAverage(std::vector<Arg>& args, Ctx& ctx) {
  auto v = numbersOf(args, ctx);
  if (v.empty()) fail(Err::Div0);
  double t = 0;
  for (double n : v) t += n;
  return Value::number(t / (double)v.size());
}
Value fnCount(std::vector<Arg>& args, Ctx& ctx) {
  int n = 0;
  for (const auto& a : args) {
    if (a.isRange) {
      forEachCell(a, ctx, [&](const Value& v) {
        if (v.type == Value::Number) n++;
      });
    } else if (a.v.type == Value::Number) n++;
    else if (a.v.type == Value::Text && !trim(a.v.text).empty()) {
      char* e = nullptr;
      std::strtod(a.v.text.c_str(), &e);
      if (e && *e == '\0') n++;
    }
  }
  return Value::number(n);
}
Value fnCountA(std::vector<Arg>& args, Ctx& ctx) {
  int n = 0;
  for (auto& a : args)
    forEachCell(a, ctx, [&](const Value& v) {
      if (v.type != Value::Blank && !(v.type == Value::Text && v.text.empty())) n++;
    });
  return Value::number(n);
}
Value fnCountBlank(std::vector<Arg>& args, Ctx& ctx) {
  int n = 0;
  for (auto& a : args)
    forEachCell(a, ctx, [&](const Value& v) {
      if (v.type == Value::Blank || (v.type == Value::Text && v.text.empty())) n++;
    });
  return Value::number(n);
}
Value fnMax(std::vector<Arg>& args, Ctx& ctx) {
  auto v = numbersOf(args, ctx);
  if (v.empty()) return Value::number(0);
  return Value::number(*std::max_element(v.begin(), v.end()));
}
Value fnMin(std::vector<Arg>& args, Ctx& ctx) {
  auto v = numbersOf(args, ctx);
  if (v.empty()) return Value::number(0);
  return Value::number(*std::min_element(v.begin(), v.end()));
}
Value fnMedian(std::vector<Arg>& args, Ctx& ctx) {
  auto v = numbersOf(args, ctx);
  if (v.empty()) fail(Err::Num);
  std::sort(v.begin(), v.end());
  size_t n = v.size();
  return Value::number(n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2);
}
Value fnLarge(std::vector<Arg>& args, Ctx& ctx) {
  auto v = numbersOf(args, ctx);
  int k = (int)NUM(1);
  if (k < 1 || k > (int)v.size()) fail(Err::Num);
  std::sort(v.begin(), v.end(), std::greater<double>());
  return Value::number(v[k - 1]);
}
Value fnSmall(std::vector<Arg>& args, Ctx& ctx) {
  auto v = numbersOf(args, ctx);
  int k = (int)NUM(1);
  if (k < 1 || k > (int)v.size()) fail(Err::Num);
  std::sort(v.begin(), v.end());
  return Value::number(v[k - 1]);
}
Value fnVarS(std::vector<Arg>& args, Ctx& ctx) {
  auto v = numbersOf(args, ctx);
  if (v.size() < 2) fail(Err::Div0);
  double m = 0;
  for (double n : v) m += n;
  m /= (double)v.size();
  double s = 0;
  for (double n : v) s += (n - m) * (n - m);
  return Value::number(s / (double)(v.size() - 1));
}
Value fnVarP(std::vector<Arg>& args, Ctx& ctx) {
  auto v = numbersOf(args, ctx);
  if (v.empty()) fail(Err::Div0);
  double m = 0;
  for (double n : v) m += n;
  m /= (double)v.size();
  double s = 0;
  for (double n : v) s += (n - m) * (n - m);
  return Value::number(s / (double)v.size());
}
Value fnStdevS(std::vector<Arg>& args, Ctx& ctx) { return Value::number(std::sqrt(fnVarS(args, ctx).num)); }
Value fnStdevP(std::vector<Arg>& args, Ctx& ctx) { return Value::number(std::sqrt(fnVarP(args, ctx).num)); }
Value fnRand(std::vector<Arg>&, Ctx&) { return Value::number((double)std::rand() / (RAND_MAX + 1.0)); }
Value fnRandBetween(std::vector<Arg>& args, Ctx& ctx) {
  int lo = (int)std::ceil(NUM(0)), hi = (int)std::floor(NUM(1));
  if (lo > hi) fail(Err::Num);
  return Value::number(lo + std::rand() % (hi - lo + 1));
}

// text
Value fnLeft(std::vector<Arg>& args, Ctx& ctx) {
  std::string s = TXT(0);
  int n = args.size() > 1 ? (int)NUM(1) : 1;
  if (n < 0) fail(Err::Value);
  return Value::string(s.substr(0, std::min((size_t)n, s.size())));
}
Value fnRight(std::vector<Arg>& args, Ctx& ctx) {
  std::string s = TXT(0);
  int n = args.size() > 1 ? (int)NUM(1) : 1;
  if (n < 0) fail(Err::Value);
  n = std::min((size_t)n, s.size());
  return Value::string(s.substr(s.size() - n));
}
Value fnMid(std::vector<Arg>& args, Ctx& ctx) {
  std::string s = TXT(0);
  int start = (int)NUM(1), len = (int)NUM(2);
  if (start < 1 || len < 0) fail(Err::Value);
  if ((size_t)start > s.size()) return Value::string("");
  return Value::string(s.substr(start - 1, len));
}
Value fnLen(std::vector<Arg>& args, Ctx& ctx) { return Value::number((double)TXT(0).size()); }
Value fnUpper(std::vector<Arg>& args, Ctx& ctx) { return Value::string(toUpper(TXT(0))); }
Value fnLower(std::vector<Arg>& args, Ctx& ctx) {
  std::string s = TXT(0);
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return Value::string(s);
}
Value fnProper(std::vector<Arg>& args, Ctx& ctx) {
  std::string s = TXT(0);
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  bool start = true;
  for (auto& c : s) {
    if (start && isAlpha(c)) c = (char)std::toupper((unsigned char)c);
    start = !isAlpha(c) && c != '\'';
  }
  return Value::string(s);
}
Value fnTrim(std::vector<Arg>& args, Ctx& ctx) {
  std::string s = TXT(0);
  std::string out;
  bool space = false;
  for (char c : s) {
    if (std::isspace((unsigned char)c)) {
      space = true;
    } else {
      if (space && !out.empty()) out += ' ';
      space = false;
      out += c;
    }
  }
  return Value::string(out);
}
Value fnConcat(std::vector<Arg>& args, Ctx& ctx) {
  std::string out;
  for (auto& a : args) {
    forEachCell(a, ctx, [&](const Value& v) {
      if (v.type == Value::Error) fail(v.err);
      out += (v.type == Value::Text) ? v.text : generalText(v);
    });
  }
  return Value::string(out);
}
Value fnTextJoin(std::vector<Arg>& args, Ctx& ctx) {
  if (args.size() < 3) fail(Err::Value);
  std::string delim = TXT(0);
  bool ignoreEmpty = boolOf(A(1), ctx);
  std::string out;
  bool first = true;
  for (size_t i = 2; i < args.size(); i++)
    forEachCell(args[i], ctx, [&](const Value& v) {
      if (v.type == Value::Error) fail(v.err);
      std::string t = (v.type == Value::Text) ? v.text : generalText(v);
      if (ignoreEmpty && t.empty()) return;
      if (!first) out += delim;
      out += t;
      first = false;
    });
  return Value::string(out);
}
Value fnSubstitute(std::vector<Arg>& args, Ctx& ctx) {
  std::string s = TXT(0), from = TXT(1), to = TXT(2);
  if (from.empty()) return Value::string(s);
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    if (s.compare(i, from.size(), from) == 0) {
      out += to;
      i += from.size();
    } else {
      out += s[i++];
    }
  }
  return Value::string(out);
}
Value fnReplace(std::vector<Arg>& args, Ctx& ctx) {
  std::string s = TXT(0);
  int start = (int)NUM(1), len = (int)NUM(2);
  std::string rep = TXT(3);
  if (start < 1 || len < 0) fail(Err::Value);
  std::string out = s.substr(0, std::min((size_t)(start - 1), s.size()));
  out += rep;
  if ((size_t)(start - 1 + len) < s.size()) out += s.substr(start - 1 + len);
  return Value::string(out);
}
Value fnFind(std::vector<Arg>& args, Ctx& ctx) {
  std::string needle = TXT(0), hay = TXT(1);
  int start = args.size() > 2 ? (int)NUM(2) : 1;
  if (start < 1) fail(Err::Value);
  size_t pos = hay.find(needle, start - 1);
  if (pos == std::string::npos) fail(Err::Value);
  return Value::number((double)(pos + 1));
}
Value fnSearch(std::vector<Arg>& args, Ctx& ctx) {
  std::string needle = TXT(0), hay = TXT(1);
  auto upHay = toUpper(hay), upNeedle = toUpper(needle);
  size_t pos = upHay.find(upNeedle);
  if (pos == std::string::npos) fail(Err::Value);
  return Value::number((double)(pos + 1));
}
Value fnExact(std::vector<Arg>& args, Ctx& ctx) { return Value::boolean(TXT(0) == TXT(1)); }
Value fnRept(std::vector<Arg>& args, Ctx& ctx) {
  std::string s = TXT(0);
  int n = (int)NUM(1);
  if (n < 0) fail(Err::Value);
  std::string out;
  for (int i = 0; i < n; i++) out += s;
  return Value::string(out);
}
Value fnValue(std::vector<Arg>& args, Ctx& ctx) {
  Value v = scalarOf(A(0), ctx);
  if (v.type == Value::Number) return v;
  if (v.type == Value::Bool) return Value::number(v.b ? 1 : 0);
  std::string s = trim(v.text);
  char* end = nullptr;
  double d = std::strtod(s.c_str(), &end);
  if (end && *end == '\0' && !s.empty()) return Value::number(d);
  double serial;
  if (parseDateText(s, serial)) return Value::number(serial);
  fail(Err::Value);
}
Value fnText(std::vector<Arg>& args, Ctx& ctx) {
  Value v = scalarOf(A(0), ctx);
  if (v.type == Value::Text && args.size() < 2) return v;
  std::string fmt = args.size() > 1 ? TXT(1) : "";
  return Value::string(formatValue(v, fmt));
}
Value fnChar(std::vector<Arg>& args, Ctx& ctx) {
  int n = (int)NUM(0);
  if (n < 1 || n > 255) fail(Err::Value);
  return Value::string(std::string(1, (char)n));
}
Value fnCode(std::vector<Arg>& args, Ctx& ctx) {
  std::string s = TXT(0);
  if (s.empty()) fail(Err::Value);
  return Value::number((unsigned char)s[0]);
}

// logical
Value fnAnd(std::vector<Arg>& args, Ctx& ctx) {
  bool any = false;
  for (auto& a : args)
    forEachCell(a, ctx, [&](const Value& v) {
      if (v.type == Value::Error) fail(v.err);
      if (v.type == Value::Blank || v.type == Value::Text) return;
      any = true;
      if (v.type == Value::Bool ? !v.b : v.num == 0) {
        Arg ar;
        ar.v = Value::boolean(false);
        boolOf(ar, ctx);
        fail(Err::Value);
      }
    });
  if (!any) fail(Err::Value);
  // recompute cleanly
  bool all = true;
  for (auto& a : args)
    forEachCell(a, ctx, [&](const Value& v) {
      if (v.type == Value::Error) fail(v.err);
      if (v.type == Value::Blank || v.type == Value::Text) return;
      if (v.type == Value::Bool && !v.b) all = false;
      if (v.type == Value::Number && v.num == 0) all = false;
    });
  return Value::boolean(all);
}
Value fnOr(std::vector<Arg>& args, Ctx& ctx) {
  bool any = false, found = false;
  for (auto& a : args)
    forEachCell(a, ctx, [&](const Value& v) {
      if (v.type == Value::Error) fail(v.err);
      if (v.type == Value::Blank || v.type == Value::Text) return;
      found = true;
      if (v.type == Value::Bool ? v.b : v.num != 0) any = true;
    });
  if (!found) fail(Err::Value);
  return Value::boolean(any);
}
Value fnNot(std::vector<Arg>& args, Ctx& ctx) { return Value::boolean(!boolOf(A(0), ctx)); }
Value fnTrue(std::vector<Arg>&, Ctx&) { return Value::boolean(true); }
Value fnFalse(std::vector<Arg>&, Ctx&) { return Value::boolean(false); }

// lookup
Value fnRow(std::vector<Arg>& args, Ctx& ctx) {
  if (args.empty() || (!args[0].isRange && args[0].v.type == Value::Blank)) return Value::number(ctx.row + 1);
  if (!args[0].isRange) fail(Err::Value);
  return Value::number(args[0].area.r1 + 1);
}
Value fnColumn(std::vector<Arg>& args, Ctx& ctx) {
  if (args.empty() || (!args[0].isRange && args[0].v.type == Value::Blank)) return Value::number(ctx.col + 1);
  if (!args[0].isRange) fail(Err::Value);
  return Value::number(args[0].area.c1 + 1);
}
Value fnRows(std::vector<Arg>& args, Ctx& ctx) {
  if (!args[0].isRange) return Value::number(1);
  return Value::number(args[0].area.r2 - args[0].area.r1 + 1);
}
Value fnColumns(std::vector<Arg>& args, Ctx& ctx) {
  if (!args[0].isRange) return Value::number(1);
  return Value::number(args[0].area.c2 - args[0].area.c1 + 1);
}

Value fnVLookup(std::vector<Arg>& args, Ctx& ctx) {
  if (args.size() < 3) fail(Err::Value);
  Value lookup = scalarOf(A(0), ctx);
  if (!args[1].isRange) fail(Err::Value);
  int s = sheetIndexFor(ctx, args[1]);
  int colIdx = (int)NUM(2);
  bool approx = args.size() > 3 ? boolOf(A(3), ctx) : true;
  int rows = args[1].area.r2 - args[1].area.r1 + 1;
  int width = args[1].area.c2 - args[1].area.c1 + 1;
  if (colIdx < 1 || colIdx > width) fail(Err::Ref);
  int found = -1;
  for (int i = 0; i < rows; i++) {
    Value v = ctx.wb->value(s, args[1].area.r1 + i, args[1].area.c1);
    if (v.type == Value::Error) fail(v.err);
    if (v.type == Value::Blank) continue;
    int cmp;
    if (v.type == Value::Number && lookup.type == Value::Number) cmp = v.num < lookup.num ? -1 : (v.num > lookup.num ? 1 : 0);
    else if (v.type == Value::Text && lookup.type == Value::Text)
      cmp = toUpper(v.text) < toUpper(lookup.text) ? -1 : (toUpper(v.text) > toUpper(lookup.text) ? 1 : 0);
    else cmp = 1;
    if (cmp == 0) {
      found = i;
      if (!approx) break;
    }
    if (approx && cmp <= 0) found = i;
    if (approx && cmp > 0) break;
  }
  if (found < 0) fail(Err::NA);
  return ctx.wb->value(s, args[1].area.r1 + found, args[1].area.c1 + colIdx - 1);
}

Value fnHLookup(std::vector<Arg>& args, Ctx& ctx) {
  if (args.size() < 3) fail(Err::Value);
  Value lookup = scalarOf(A(0), ctx);
  if (!args[1].isRange) fail(Err::Value);
  int s = sheetIndexFor(ctx, args[1]);
  int rowIdx = (int)NUM(2);
  int rows = args[1].area.r2 - args[1].area.r1 + 1;
  int width = args[1].area.c2 - args[1].area.c1 + 1;
  if (rowIdx < 1 || rowIdx > rows) fail(Err::Ref);
  int found = -1;
  for (int j = 0; j < width; j++) {
    Value v = ctx.wb->value(s, args[1].area.r1, args[1].area.c1 + j);
    if (v.type == Value::Error) fail(v.err);
    if (v.type == Value::Blank) continue;
    if (v.type == Value::Number && lookup.type == Value::Number) {
      if (v.num == lookup.num) found = j;
    } else if (v.type == Value::Text && lookup.type == Value::Text) {
      if (toUpper(v.text) == toUpper(lookup.text)) found = j;
    }
    if (found >= 0) break;
  }
  if (found < 0) fail(Err::NA);
  return ctx.wb->value(s, args[1].area.r1 + rowIdx - 1, args[1].area.c1 + found);
}

Value fnIndex(std::vector<Arg>& args, Ctx& ctx) {
  if (args.size() < 2) fail(Err::Value);
  if (!args[0].isRange) fail(Err::Value);
  int s = sheetIndexFor(ctx, args[0]);
  int rowNum = (int)NUM(1);
  int rows = args[0].area.r2 - args[0].area.r1 + 1;
  int cols = args[0].area.c2 - args[0].area.c1 + 1;
  int colNum = args.size() > 2 ? (int)NUM(2) : 1;
  if (rowNum < 1 || rowNum > rows) fail(Err::Ref);
  if (colNum < 1 || colNum > cols) fail(Err::Ref);
  return ctx.wb->value(s, args[0].area.r1 + rowNum - 1, args[0].area.c1 + colNum - 1);
}

Value fnMatch(std::vector<Arg>& args, Ctx& ctx) {
  if (args.size() < 2 || !args[1].isRange) fail(Err::Value);
  Value lookup = scalarOf(A(0), ctx);
  int s = sheetIndexFor(ctx, args[1]);
  int matchType = args.size() > 2 ? (int)NUM(2) : 1;
  bool vertical = args[1].area.r2 > args[1].area.r1;
  int n = vertical ? args[1].area.r2 - args[1].area.r1 + 1 : args[1].area.c2 - args[1].area.c1 + 1;
  int best = -1;
  for (int i = 0; i < n; i++) {
    Value v = vertical ? ctx.wb->value(s, args[1].area.r1 + i, args[1].area.c1)
                       : ctx.wb->value(s, args[1].area.r1, args[1].area.c1 + i);
    if (v.type == Value::Error) fail(v.err);
    if (v.type == Value::Blank) continue;
    int cmp = 2;
    if (v.type == Value::Number && lookup.type == Value::Number) cmp = v.num < lookup.num ? -1 : (v.num > lookup.num ? 1 : 0);
    else if (v.type == Value::Text && lookup.type == Value::Text)
      cmp = toUpper(v.text) < toUpper(lookup.text) ? -1 : (toUpper(v.text) > toUpper(lookup.text) ? 1 : 0);
    if (matchType == 0) {
      if (cmp == 0) return Value::number(i + 1);
    } else if (matchType > 0) {
      if (cmp <= 0) best = i;
      else break;
    } else {
      if (cmp >= 0) best = i;
      else break;
    }
  }
  if (matchType == 0 || best < 0) fail(Err::NA);
  return Value::number(best + 1);
}

Value fnXLookup(std::vector<Arg>& args, Ctx& ctx) {
  if (args.size() < 3 || !args[1].isRange || !args[2].isRange) fail(Err::Value);
  Value lookup = scalarOf(A(0), ctx);
  int ls = sheetIndexFor(ctx, args[1]);
  int rs = sheetIndexFor(ctx, args[2]);
  int n = args[1].area.r2 - args[1].area.r1 + 1;
  for (int i = 0; i < n; i++) {
    Value v = ctx.wb->value(ls, args[1].area.r1 + i, args[1].area.c1);
    bool match = false;
    if (v.type == Value::Number && lookup.type == Value::Number) match = v.num == lookup.num;
    else if (v.type == Value::Text && lookup.type == Value::Text) match = toUpper(v.text) == toUpper(lookup.text);
    else if (v.type == Value::Bool && lookup.type == Value::Bool) match = v.b == lookup.b;
    if (match) {
      int rr = args[2].area.r1 + i;
      if (rr > args[2].area.r2) fail(Err::Ref);
      return ctx.wb->value(rs, rr, args[2].area.c1);
    }
  }
  if (args.size() > 3) return scalarOf(A(3), ctx);
  fail(Err::NA);
}

Value fnLookup(std::vector<Arg>& args, Ctx& ctx) {
  if (args.size() < 2 || !args[1].isRange) fail(Err::Value);
  Value lookup = scalarOf(A(0), ctx);
  int s = sheetIndexFor(ctx, args[1]);
  int n = args[1].area.r2 - args[1].area.r1 + 1;
  int best = -1;
  for (int i = 0; i < n; i++) {
    Value v = ctx.wb->value(s, args[1].area.r1 + i, args[1].area.c1);
    if (v.type == Value::Number && lookup.type == Value::Number && v.num <= lookup.num) best = i;
  }
  if (best < 0) fail(Err::NA);
  if (args.size() > 2 && args[2].isRange) {
    int rs = sheetIndexFor(ctx, args[2]);
    return ctx.wb->value(rs, args[2].area.r1 + best, args[2].area.c1);
  }
  return ctx.wb->value(s, args[1].area.r1 + best, args[1].area.c1);
}

// dates
Value fnToday(std::vector<Arg>&, Ctx& ctx) { return Value::number(std::floor(ctx.wb->now())); }
Value fnNow(std::vector<Arg>&, Ctx& ctx) { return Value::number(ctx.wb->now()); }
Value fnDate(std::vector<Arg>& args, Ctx& ctx) {
  int y = (int)NUM(0), m = (int)NUM(1), d = (int)NUM(2);
  if (y >= 0 && y < 1900) y += 1900;
  // allow month/day overflow, like Excel
  int yAdj = y + (m - 1) / 12;
  int mAdj = (m - 1) % 12 + 1;
  if (mAdj <= 0) {
    mAdj += 12;
    yAdj--;
  }
  return Value::number(dateToSerial(yAdj, mAdj, 1) + d - 1);
}
Value fnYear(std::vector<Arg>& args, Ctx& ctx) {
  int y, m, d, hh, mi, ss;
  serialToDate(NUM(0), y, m, d, hh, mi, ss);
  return Value::number(y);
}
Value fnMonth(std::vector<Arg>& args, Ctx& ctx) {
  int y, m, d, hh, mi, ss;
  serialToDate(NUM(0), y, m, d, hh, mi, ss);
  return Value::number(m);
}
Value fnDay(std::vector<Arg>& args, Ctx& ctx) {
  int y, m, d, hh, mi, ss;
  serialToDate(NUM(0), y, m, d, hh, mi, ss);
  return Value::number(d);
}
Value fnHour(std::vector<Arg>& args, Ctx& ctx) {
  int y, m, d, hh, mi, ss;
  serialToDate(NUM(0), y, m, d, hh, mi, ss);
  return Value::number(hh);
}
Value fnMinute(std::vector<Arg>& args, Ctx& ctx) {
  int y, m, d, hh, mi, ss;
  serialToDate(NUM(0), y, m, d, hh, mi, ss);
  return Value::number(mi);
}
Value fnSecond(std::vector<Arg>& args, Ctx& ctx) {
  int y, m, d, hh, mi, ss;
  serialToDate(NUM(0), y, m, d, hh, mi, ss);
  return Value::number(ss);
}
Value fnWeekday(std::vector<Arg>& args, Ctx& ctx) {
  int dow = weekdayOf(NUM(0));  // 0 = Sunday
  int type = args.size() > 1 ? (int)NUM(1) : 1;
  if (type == 2) return Value::number(((dow + 6) % 7) + 1);
  if (type == 3) return Value::number((dow + 6) % 7);
  return Value::number(dow + 1);
}
Value fnEdate(std::vector<Arg>& args, Ctx& ctx) { return addMonths(NUM(0), (int)NUM(1), false); }
Value fnEomonth(std::vector<Arg>& args, Ctx& ctx) { return addMonths(NUM(0), (int)NUM(1), true); }
Value fnDays(std::vector<Arg>& args, Ctx& ctx) {
  return Value::number(std::floor(NUM(0)) - std::floor(NUM(1)));
}
Value fnDatedif(std::vector<Arg>& args, Ctx& ctx) {
  double start = std::floor(NUM(0)), end = std::floor(NUM(1));
  std::string unit = toUpper(TXT(2));
  if (end < start) fail(Err::Num);
  int y1, m1, d1, h1, mi1, s1, y2, m2, d2, h2, mi2, s2;
  serialToDate(start, y1, m1, d1, h1, mi1, s1);
  serialToDate(end, y2, m2, d2, h2, mi2, s2);
  int md = (y2 - y1) * 12 + (m2 - m1);
  if (d2 < d1) md--;
  if (unit == "D") return Value::number(end - start);
  if (unit == "M") return Value::number(md);
  if (unit == "Y") return Value::number(md / 12);
  if (unit == "YM") return Value::number(md % 12);
  if (unit == "MD") {
    int dd = d2 - d1;
    if (dd < 0) {
      int pm = m2 == 1 ? 12 : m2 - 1;
      int py = m2 == 1 ? y2 - 1 : y2;
      static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      dd += mdays[pm - 1];
      (void)py;
    }
    return Value::number(dd);
  }
  fail(Err::Num);
}

// financial
Value fnPmt(std::vector<Arg>& args, Ctx& ctx) {
  return Value::number(pmtCalc(NUM(0), NUM(1), NUM(2), args.size() > 3 ? NUM(3) : 0,
                               args.size() > 4 ? NUM(4) : 0));
}
Value fnFv(std::vector<Arg>& args, Ctx& ctx) {
  double rate = NUM(0), nper = NUM(1), p = NUM(2);
  double pv = args.size() > 3 ? NUM(3) : 0, type = args.size() > 4 ? NUM(4) : 0;
  if (rate == 0) return Value::number(-(pv + p * nper));
  double pvif = std::pow(1 + rate, nper);
  double factor = ((1 + rate * type) * (pvif - 1)) / rate;
  return Value::number(-(pv * pvif + p * factor));
}
Value fnPv(std::vector<Arg>& args, Ctx& ctx) {
  double rate = NUM(0), nper = NUM(1), p = NUM(2);
  double fv = args.size() > 3 ? NUM(3) : 0, type = args.size() > 4 ? NUM(4) : 0;
  if (rate == 0) return Value::number(-(fv + p * nper));
  double pvif = std::pow(1 + rate, nper);
  double factor = ((1 + rate * type) * (pvif - 1)) / rate;
  return Value::number(-(fv + p * factor) / pvif);
}
Value fnNper(std::vector<Arg>& args, Ctx& ctx) {
  double rate = NUM(0), p = NUM(1), pv = NUM(2);
  double fv = args.size() > 3 ? NUM(3) : 0, type = args.size() > 4 ? NUM(4) : 0;
  if (rate == 0) {
    if (p == 0) fail(Err::Div0);
    return Value::number(-(pv + fv) / p);
  }
  double z = p * (1 + rate * type) / rate;
  return Value::number(std::log((z - fv) / (z + pv)) / std::log(1 + rate));
}
Value fnNpv(std::vector<Arg>& args, Ctx& ctx) {
  double rate = NUM(0);
  std::vector<double> flows;
  for (size_t i = 1; i < args.size(); i++)
    for (double v : numbersOf({args[i]}, ctx)) flows.push_back(v);
  double total = 0;
  for (size_t i = 0; i < flows.size(); i++) total += flows[i] / std::pow(1 + rate, (double)(i + 1));
  return Value::number(total);
}
Value fnIrr(std::vector<Arg>& args, Ctx& ctx) {
  std::vector<double> flows;
  for (double v : numbersOf({args[0]}, ctx)) flows.push_back(v);
  if (flows.size() < 2) fail(Err::Num);
  auto npvAt = [&](double r) {
    double t = 0;
    for (size_t i = 0; i < flows.size(); i++) t += flows[i] / std::pow(1 + r, (double)i);
    return t;
  };
  double lo = -0.9999, hi = 10.0, flo = npvAt(lo);
  for (int i = 0; i < 200; i++) {
    double mid = (lo + hi) / 2;
    double f = npvAt(mid);
    if (std::fabs(f) < 1e-9) return Value::number(mid);
    if (flo * f < 0)
      hi = mid;
    else {
      lo = mid;
      flo = f;
    }
  }
  fail(Err::Num);
}
Value fnSln(std::vector<Arg>& args, Ctx& ctx) {
  double cost = NUM(0), salvage = NUM(1), life = NUM(2);
  if (life == 0) fail(Err::Div0);
  return Value::number((cost - salvage) / life);
}
Value fnSyd(std::vector<Arg>& args, Ctx& ctx) {
  double cost = NUM(0), salvage = NUM(1), life = NUM(2), per = NUM(3);
  return Value::number((cost - salvage) * (life - per + 1) * 2 / (life * (life + 1)));
}

// information
Value fnIsBlank(std::vector<Arg>& args, Ctx& ctx) {
  if (args.empty()) return Value::boolean(true);
  if (args[0].isRange) {
    Value v = ctx.wb->value(sheetIndexFor(ctx, args[0]), args[0].area.r1, args[0].area.c1);
    return Value::boolean(v.type == Value::Blank);
  }
  return Value::boolean(args[0].v.type == Value::Blank);
}
Value fnIsNumber(std::vector<Arg>& args, Ctx& ctx) {
  if (args.empty()) return Value::boolean(false);
  if (args[0].isRange) return Value::boolean(ctx.wb->value(sheetIndexFor(ctx, args[0]), args[0].area.r1, args[0].area.c1).type == Value::Number);
  return Value::boolean(args[0].v.type == Value::Number);
}
Value fnIsText(std::vector<Arg>& args, Ctx& ctx) {
  if (args.empty()) return Value::boolean(false);
  if (args[0].isRange) return Value::boolean(ctx.wb->value(sheetIndexFor(ctx, args[0]), args[0].area.r1, args[0].area.c1).type == Value::Text);
  return Value::boolean(args[0].v.type == Value::Text);
}
Value fnIsError(std::vector<Arg>& args, Ctx& ctx) {
  if (args.empty()) return Value::boolean(false);
  if (args[0].isRange) return Value::boolean(ctx.wb->value(sheetIndexFor(ctx, args[0]), args[0].area.r1, args[0].area.c1).isError());
  return Value::boolean(args[0].v.isError());
}
Value fnIsErr(std::vector<Arg>& args, Ctx& ctx) {
  Value v = args.empty() ? Value() : (args[0].isRange ? ctx.wb->value(sheetIndexFor(ctx, args[0]), args[0].area.r1, args[0].area.c1) : args[0].v);
  return Value::boolean(v.isError() && v.err != Err::NA);
}
Value fnIsNa(std::vector<Arg>& args, Ctx& ctx) {
  Value v = args.empty() ? Value() : (args[0].isRange ? ctx.wb->value(sheetIndexFor(ctx, args[0]), args[0].area.r1, args[0].area.c1) : args[0].v);
  return Value::boolean(v.isError() && v.err == Err::NA);
}
Value fnIsLogical(std::vector<Arg>& args, Ctx& ctx) {
  Value v = args.empty() ? Value() : (args[0].isRange ? ctx.wb->value(sheetIndexFor(ctx, args[0]), args[0].area.r1, args[0].area.c1) : args[0].v);
  return Value::boolean(v.type == Value::Bool);
}
Value fnNa(std::vector<Arg>&, Ctx&) { return Value::error(Err::NA); }
Value fnType(std::vector<Arg>& args, Ctx&) {
  if (args.empty()) return Value::number(1);
  switch (args[0].v.type) {
    case Value::Number: return Value::number(1);
    case Value::Text: return Value::number(2);
    case Value::Bool: return Value::number(4);
    case Value::Error: return Value::number(16);
    default: return Value::number(1);
  }
}

// ---- evaluation of a node
Arg evalFunc(const Node& n, Ctx& ctx) {
  const std::string& name = n.s;
  Arg out;

  auto evalArg = [&](size_t i) -> Arg {
    if (i >= n.kids.size()) {
      Arg a;
      return a;
    }
    return evalNode(n.kids[i], ctx);
  };
  auto truthy = [&](const Arg& a) { return boolOf(a, ctx); };

  // Lazy special forms (Excel evaluates these selectively)
  if (name == "IF") {
    if (n.kids.empty()) fail(Err::Value);
    out = truthy(evalArg(0)) ? evalArg(1) : evalArg(2);
    return out;
  }
  if (name == "IFS") {
    for (size_t i = 0; i + 1 < n.kids.size(); i += 2)
      if (truthy(evalArg(i))) return evalArg(i + 1);
    fail(Err::NA);
  }
  if (name == "IFERROR" || name == "IFNA") {
    Arg a = evalArg(0);
    bool isErr = a.isRange ? ctx.wb->value(sheetIndexFor(ctx, a), a.area.r1, a.area.c1).isError() : a.v.isError();
    if (name == "IFNA") {
      Value v = a.isRange ? ctx.wb->value(sheetIndexFor(ctx, a), a.area.r1, a.area.c1) : a.v;
      isErr = v.isError() && v.err == Err::NA;
    }
    return isErr ? evalArg(1) : a;
  }
  if (name == "CHOOSE") {
    int idx = (int)numOf(evalArg(0), ctx);
    if (idx < 1 || idx >= (int)n.kids.size()) fail(Err::Value);
    return evalArg(idx);
  }
  if (name == "SWITCH") {
    Arg subject = evalArg(0);
    Value sv = subject.isRange ? ctx.wb->value(sheetIndexFor(ctx, subject), subject.area.r1, subject.area.c1) : subject.v;
    for (size_t i = 1; i + 1 < n.kids.size(); i += 2) {
      Arg cand = evalArg(i);
      Value cv = cand.isRange ? ctx.wb->value(sheetIndexFor(ctx, cand), cand.area.r1, cand.area.c1) : cand.v;
      bool eq = false;
      if (sv.type == Value::Number && cv.type == Value::Number) eq = sv.num == cv.num;
      else if (sv.type == Value::Text && cv.type == Value::Text) eq = toUpper(sv.text) == toUpper(cv.text);
      else if (sv.type == Value::Bool && cv.type == Value::Bool) eq = sv.b == cv.b;
      if (eq) return evalArg(i + 1);
    }
    if (n.kids.size() % 2 == 0) return evalArg(n.kids.size() - 1);
    fail(Err::NA);
  }
  if (name == "LET") {
    if (n.kids.size() < 3 || n.kids.size() % 2 == 0) fail(Err::Value);
    Ctx child = ctx;
    for (size_t i = 0; i + 1 < n.kids.size() - 1; i += 2) {
      if (n.kids[i].k != NK::Name) fail(Err::Name);
      child.vars[toUpper(n.kids[i].s)] = evalNode(n.kids[i + 1], child);
    }
    return evalNode(n.kids[n.kids.size() - 1], child);
  }
  if (name == "__ARRAY") {
    // Only the first element is used (no dynamic arrays in the native build yet)
    if (!n.kids.empty()) return evalNode(n.kids[0], ctx);
    return out;
  }

  auto it = registry().find(name);
  if (it == registry().end()) fail(Err::Name);
  std::vector<Arg> args;
  args.reserve(n.kids.size());
  for (auto& kid : n.kids) args.push_back(evalNode(kid, ctx));
  return out.isRange = false, out.v = it->second(args, ctx), out;
}

Arg evalNode(const Node& n, Ctx& ctx) {
  Arg a;
  switch (n.k) {
    case NK::Num:
      a.v = Value::number(n.num);
      return a;
    case NK::Str:
      a.v = Value::string(n.s);
      return a;
    case NK::Bool:
      a.v = Value::boolean(n.b);
      return a;
    case NK::ErrL:
      a.v = Value::error(n.e);
      return a;
    case NK::Missing:
      a.v = Value();
      return a;
    case NK::Ref: {
      int sheet = ctx.sheet;
      if (!n.area.sheet.empty()) {
        sheet = ctx.wb->indexOfName(n.area.sheet);
        if (sheet < 0) fail(Err::Ref);
      }
      a.isRange = true;
      a.area = n.area;
      a.area.sheet = ctx.wb->sheet(sheet).name;
      if (a.area.r1 == a.area.r2 && a.area.c1 == a.area.c2) a.v = ctx.wb->value(sheet, a.area.r1, a.area.c1);
      return a;
    }
    case NK::Name: {
      std::string key = toUpper(n.s);
      auto v = ctx.vars.find(key);
      if (v != ctx.vars.end()) return v->second;
      fail(Err::Name);
    }
    case NK::Bin: {
      Arg l = evalNode(n.kids[0], ctx);
      Arg r = evalNode(n.kids[1], ctx);
      Value lv = scalarOf(l, ctx), rv = scalarOf(r, ctx);
      a.v = applyBin(n.s, lv, rv, ctx);
      return a;
    }
    case NK::Un: {
      Arg v = evalNode(n.kids[0], ctx);
      double d = numOf(v, ctx);
      a.v = Value::number(n.s == "-" ? -d : d);
      return a;
    }
    case NK::Pct: {
      Arg v = evalNode(n.kids[0], ctx);
      a.v = Value::number(numOf(v, ctx) / 100.0);
      return a;
    }
    case NK::Func:
      return evalFunc(n, ctx);
  }
  return a;
}

// ---------------------------------------------------------------- extra maths
namespace {
double rad(double d) { return d * M_PI / 180.0; }
double deg(double r) { return r * 180.0 / M_PI; }
}  // namespace
Value fnSin(std::vector<Arg>& args, Ctx& ctx) { return Value::number(std::sin(NUM(0))); }
Value fnCos(std::vector<Arg>& args, Ctx& ctx) { return Value::number(std::cos(NUM(0))); }
Value fnTan(std::vector<Arg>& args, Ctx& ctx) { return Value::number(std::tan(NUM(0))); }
Value fnAsin(std::vector<Arg>& args, Ctx& ctx) {
  double x = NUM(0);
  if (x < -1 || x > 1) fail(Err::Num);
  return Value::number(std::asin(x));
}
Value fnAcos(std::vector<Arg>& args, Ctx& ctx) {
  double x = NUM(0);
  if (x < -1 || x > 1) fail(Err::Num);
  return Value::number(std::acos(x));
}
Value fnAtan(std::vector<Arg>& args, Ctx& ctx) { return Value::number(std::atan(NUM(0))); }
Value fnAtan2(std::vector<Arg>& args, Ctx& ctx) {
  double x = NUM(0), y = NUM(1);
  if (x == 0 && y == 0) fail(Err::Div0);
  return Value::number(std::atan2(y, x));
}
Value fnDegrees(std::vector<Arg>& args, Ctx& ctx) { return Value::number(deg(NUM(0))); }
Value fnRadians(std::vector<Arg>& args, Ctx& ctx) { return Value::number(rad(NUM(0))); }
Value fnSumSq(std::vector<Arg>& args, Ctx& ctx) {
  double total = 0;
  for (double v : numbersOf(args, ctx)) total += v * v;
  return Value::number(total);
}
Value fnMRound(std::vector<Arg>& args, Ctx& ctx) {
  double x = NUM(0), m = NUM(1);
  if (m == 0) return Value::number(0);
  if ((x < 0) != (m < 0)) fail(Err::Num);
  return Value::number(roundDecimal(x / m, 0, 0) * m);
}
Value fnIsEven(std::vector<Arg>& args, Ctx& ctx) {
  double x = std::trunc(numOf(args[0], ctx));
  return Value::boolean(std::fmod(std::fabs(x), 2.0) < 0.5);
}
Value fnIsOdd(std::vector<Arg>& args, Ctx& ctx) {
  double x = std::trunc(numOf(args[0], ctx));
  return Value::boolean(std::fmod(std::fabs(x), 2.0) > 0.5);
}
Value fnAverageA(std::vector<Arg>& args, Ctx& ctx) {
  double sum = 0;
  long long count = 0;
  auto take = [&](const Value& v) {
    switch (v.type) {
      case Value::Blank: return;
      case Value::Number: sum += v.num; break;
      case Value::Bool: sum += v.b ? 1 : 0; break;
      case Value::Text: sum += 0; break;
      case Value::Error: fail(v.err); break;
    }
    count++;
  };
  for (auto& a : args) forEachCell(a, ctx, take);
  if (count == 0) fail(Err::Div0);
  return Value::number(sum / static_cast<double>(count));
}
Value fnRank(std::vector<Arg>& args, Ctx& ctx) {
  double x = NUM(0);
  std::vector<double> pool = numbersOf({args[1]}, ctx);
  bool ascending = args.size() > 2 ? numOf(args[2], ctx) != 0 : false;
  int rank = 1;
  for (double v : pool) {
    if (ascending ? (v < x) : (v > x)) rank++;
  }
  bool found = false;
  for (double v : pool)
    if (v == x) found = true;
  if (!found) fail(Err::NA);
  return Value::number(rank);
}
Value fnDateValue(std::vector<Arg>& args, Ctx& ctx) {
  std::string text = textOf(args[0], ctx);
  double serial = 0;
  if (!parseDateText(text, serial)) fail(Err::Value);
  return Value::number(std::floor(serial));
}
Value fnTimeValue(std::vector<Arg>& args, Ctx& ctx) {
  std::string text = trim(textOf(args[0], ctx));
  int hh = 0, mm = 0, ss = 0;
  double serial = 0;
  if (parseDateText(text, serial)) return Value::number(serial - std::floor(serial));
  if (std::sscanf(text.c_str(), "%d:%d:%d", &hh, &mm, &ss) >= 2) {
    if (hh < 0 || mm < 0 || mm > 59 || ss < 0 || ss > 59) fail(Err::Value);
    return Value::number((hh * 3600 + mm * 60 + ss) / 86400.0);
  }
  fail(Err::Value);
}
Value fnWeekNum(std::vector<Arg>& args, Ctx& ctx) {
  double serial = std::floor(NUM(0));
  int y, m, d, hh, mi, ss;
  serialToDate(serial, y, m, d, hh, mi, ss);
  double jan1 = dateToSerial(y, 1, 1);
  int dow = weekdayOf(jan1);  // 0 = Sunday
  int offset = dow;           // Excel's default: weeks start on Sunday
  int dayOfYear = static_cast<int>(serial - jan1);
  return Value::number((dayOfYear + offset) / 7 + 1);
}

void registerFunctions() {
  static bool done = false;
  if (done) return;
  done = true;
  reg("SIN", fnSin);
  reg("COS", fnCos);
  reg("TAN", fnTan);
  reg("ASIN", fnAsin);
  reg("ACOS", fnAcos);
  reg("ATAN", fnAtan);
  reg("ATAN2", fnAtan2);
  reg("DEGREES", fnDegrees);
  reg("RADIANS", fnRadians);
  reg("SUMSQ", fnSumSq);
  reg("MROUND", fnMRound);
  reg("ISEVEN", fnIsEven);
  reg("ISODD", fnIsOdd);
  reg("AVERAGEA", fnAverageA);
  reg("RANK", fnRank);
  reg("DATEVALUE", fnDateValue);
  reg("TIMEVALUE", fnTimeValue);
  reg("WEEKNUM", fnWeekNum);
  reg("SUM", fnSum);
  reg("PRODUCT", fnProduct);
  reg("ABS", fnAbs);
  reg("SIGN", fnSign);
  reg("INT", fnInt);
  reg("SQRT", fnSqrt);
  reg("POWER", fnPower);
  reg("EXP", fnExp);
  reg("LN", fnLn);
  reg("LOG", fnLog);
  reg("LOG10", fnLog10);
  reg("PI", fnPi);
  reg("MOD", fnMod);
  reg("QUOTIENT", fnQuotient);
  reg("ROUND", fnRound);
  reg("ROUNDUP", fnRoundUp);
  reg("ROUNDDOWN", fnRoundDown);
  reg("TRUNC", fnTrunc);
  reg("CEILING", fnCeiling);
  reg("FLOOR", fnFloor);
  reg("SUMIF", fnSumIf);
  reg("SUMIFS", fnSumIfs);
  reg("COUNTIF", fnCountIf);
  reg("COUNTIFS", fnCountIfs);
  reg("AVERAGEIF", fnAverageIf);
  reg("AVERAGEIFS", fnAverageIfs);
  reg("MAXIFS", fnMaxIfs);
  reg("MINIFS", fnMinIfs);
  reg("SUMPRODUCT", fnSumProduct);
  reg("AVERAGE", fnAverage);
  reg("COUNT", fnCount);
  reg("COUNTA", fnCountA);
  reg("COUNTBLANK", fnCountBlank);
  reg("MAX", fnMax);
  reg("MIN", fnMin);
  reg("MEDIAN", fnMedian);
  reg("LARGE", fnLarge);
  reg("SMALL", fnSmall);
  reg("VAR.S", fnVarS);
  reg("VAR", fnVarS);
  reg("VAR.P", fnVarP);
  reg("STDEV.S", fnStdevS);
  reg("STDEV", fnStdevS);
  reg("STDEV.P", fnStdevP);
  reg("RAND", fnRand);
  reg("RANDBETWEEN", fnRandBetween);
  reg("LEFT", fnLeft);
  reg("RIGHT", fnRight);
  reg("MID", fnMid);
  reg("LEN", fnLen);
  reg("UPPER", fnUpper);
  reg("LOWER", fnLower);
  reg("PROPER", fnProper);
  reg("TRIM", fnTrim);
  reg("CONCAT", fnConcat);
  reg("CONCATENATE", fnConcat);
  reg("TEXTJOIN", fnTextJoin);
  reg("SUBSTITUTE", fnSubstitute);
  reg("REPLACE", fnReplace);
  reg("FIND", fnFind);
  reg("SEARCH", fnSearch);
  reg("EXACT", fnExact);
  reg("REPT", fnRept);
  reg("VALUE", fnValue);
  reg("TEXT", fnText);
  reg("CHAR", fnChar);
  reg("CODE", fnCode);
  reg("AND", fnAnd);
  reg("OR", fnOr);
  reg("NOT", fnNot);
  reg("TRUE", fnTrue);
  reg("FALSE", fnFalse);
  reg("ROW", fnRow);
  reg("COLUMN", fnColumn);
  reg("ROWS", fnRows);
  reg("COLUMNS", fnColumns);
  reg("VLOOKUP", fnVLookup);
  reg("HLOOKUP", fnHLookup);
  reg("INDEX", fnIndex);
  reg("MATCH", fnMatch);
  reg("XLOOKUP", fnXLookup);
  reg("LOOKUP", fnLookup);
  reg("TODAY", fnToday);
  reg("NOW", fnNow);
  reg("DATE", fnDate);
  reg("YEAR", fnYear);
  reg("MONTH", fnMonth);
  reg("DAY", fnDay);
  reg("HOUR", fnHour);
  reg("MINUTE", fnMinute);
  reg("SECOND", fnSecond);
  reg("WEEKDAY", fnWeekday);
  reg("EDATE", fnEdate);
  reg("EOMONTH", fnEomonth);
  reg("DAYS", fnDays);
  reg("DATEDIF", fnDatedif);
  reg("PMT", fnPmt);
  reg("FV", fnFv);
  reg("PV", fnPv);
  reg("NPER", fnNper);
  reg("NPV", fnNpv);
  reg("IRR", fnIrr);
  reg("SLN", fnSln);
  reg("SYD", fnSyd);
  reg("ISBLANK", fnIsBlank);
  reg("ISNUMBER", fnIsNumber);
  reg("ISTEXT", fnIsText);
  reg("ISERROR", fnIsError);
  reg("ISERR", fnIsErr);
  reg("ISNA", fnIsNa);
  reg("ISLOGICAL", fnIsLogical);
  reg("NA", fnNa);
  reg("TYPE", fnType);
}

}  // namespace

// ---------------------------------------------------------------- workbook
Workbook::Workbook() { addSheet("Sheet1"); }

Workbook Workbook::clone() const {
  Workbook w;
  w.sheets_ = sheets_;
  w.version_ = version_;
  w.nowOverride_ = nowOverride_;
  w.hasNow_ = hasNow_;
  return w;
}

int Workbook::addSheet(const std::string& nameIn) {
  std::string name = nameIn;
  if (name.empty()) {
    int n = static_cast<int>(sheets_.size()) + 1;
    do {
      name = "Sheet" + std::to_string(n++);
    } while (indexOfName(name) >= 0);
  } else if (indexOfName(name) >= 0) {
    name += "2";
  }
  Sheet s;
  s.name = name;
  s.colW[0] = 110;
  s.colW[1] = 100;
  s.colW[2] = 100;
  s.colW[3] = 100;
  s.colW[4] = 120;
  s.colW[5] = 120;
  sheets_.push_back(s);
  invalidate();
  return static_cast<int>(sheets_.size()) - 1;
}

bool Workbook::removeSheet(int index) {
  if (sheets_.size() <= 1 || index < 0 || index >= (int)sheets_.size()) return false;
  sheets_.erase(sheets_.begin() + index);
  invalidate();
  return true;
}

int Workbook::indexOfName(const std::string& name) const {
  for (size_t i = 0; i < sheets_.size(); i++)
    if (toUpper(sheets_[i].name) == toUpper(name)) return static_cast<int>(i);
  return -1;
}

const Cell* Workbook::cell(int s, int r, int c) const {
  const Sheet& sh = sheets_[s];
  auto it = sh.cells.find(cellKey(r, c));
  return it == sh.cells.end() ? nullptr : &it->second;
}

std::string Workbook::raw(int s, int r, int c) const {
  const Cell* cp = cell(s, r, c);
  return cp ? cp->raw : std::string();
}

void Workbook::setRaw(int s, int r, int c, const std::string& rawIn) {
  if (r < 0 || c < 0 || r >= MAX_ROWS || c >= MAX_COLS) return;
  Sheet& sh = sheets_[s];
  int64_t key = cellKey(r, c);
  auto it = sh.cells.find(key);
  if (rawIn.empty()) {
    if (it != sh.cells.end()) {
      it->second.raw.clear();
      if (!it->second.style.bold && !it->second.style.italic && !it->second.style.underline &&
          it->second.style.fill == 0 && it->second.style.numFmt.empty() && it->second.style.align < 0)
        sh.cells.erase(it);
    }
  } else {
    if (it == sh.cells.end()) {
      Cell cell;
      cell.raw = rawIn;
      sh.cells[key] = cell;
    } else {
      it->second.raw = rawIn;
    }
  }
  invalidate();
}

void Workbook::setStyle(int s, int r, int c, const Style& st) {
  Sheet& sh = sheets_[s];
  int64_t key = cellKey(r, c);
  auto it = sh.cells.find(key);
  if (it == sh.cells.end()) {
    Cell cell;
    cell.style = st;
    sh.cells[key] = cell;
  } else {
    it->second.style = st;
  }
  version_++;
}

void Workbook::patchStyle(int s, int r, int c, const Style& patch, bool clear) {
  Style st;
  const Cell* cp = cell(s, r, c);
  if (cp && !clear) st = cp->style;
  if (!clear) {
    if (patch.bold) st.bold = true;
    if (patch.italic) st.italic = true;
    if (patch.underline) st.underline = true;
    if (patch.wrap) st.wrap = true;
    if (patch.align >= 0) st.align = patch.align;
    if (patch.fill) st.fill = patch.fill;
    if (patch.color) st.color = patch.color;
    if (patch.border) st.border |= patch.border;
    if (!patch.fontName.empty()) st.fontName = patch.fontName;
    if (patch.fontSize > 0) st.fontSize = patch.fontSize;
    if (patch.numFmt.size()) st.numFmt = patch.numFmt;
  }
  setStyle(s, r, c, st);
}

void Workbook::clearRange(int s, int r1, int c1, int r2, int c2) {
  Sheet& sh = sheets_[s];
  for (int r = std::min(r1, r2); r <= std::max(r1, r2); r++)
    for (int c = std::min(c1, c2); c <= std::max(c1, c2); c++) {
      auto it = sh.cells.find(cellKey(r, c));
      if (it != sh.cells.end()) it->second.raw.clear();
    }
  invalidate();
}

// ---------------------------------------------------------------- ref adjust
namespace {

struct RefTok {
  bool valid = false;
  bool absCol = false;
  bool absRow = false;
  int col = 0;
  int row = 0;
  size_t end = 0;
  std::string sheet;  // optional "Sheet!" prefix without quoting
};

bool idChar(char c, bool first) {
  if (c >= 'A' && c <= 'Z') return true;
  if (c >= 'a' && c <= 'z') return true;
  if (!first && c >= '0' && c <= '9') return true;
  return false;
}

// Parses A1 / $A$1 at text[i]. Fails on things that are not references, such as
// function names (LOG10 followed by "(") or defined names (A1B).
bool parseRefAt(const std::string& t, size_t i, RefTok& out) {
  out = RefTok();
  size_t p = i;
  if (p < t.size() && t[p] == '$') {
    out.absCol = true;
    p++;
  }
  size_t colStart = p;
  while (p < t.size() && std::isalpha(static_cast<unsigned char>(t[p])) && p - colStart < 3) p++;
  if (p == colStart) return false;
  std::string letters = t.substr(colStart, p - colStart);
  if (p < t.size() && std::isalpha(static_cast<unsigned char>(t[p]))) return false;  // too long
  bool absRow = false;
  size_t q = p;
  if (q < t.size() && t[q] == '$') {
    absRow = true;
    q++;
  }
  size_t digStart = q;
  while (q < t.size() && isDigit(t[q]) && q - digStart < 7) q++;
  if (q == digStart) return false;
  if (q < t.size() && (isDigit(t[q]) || std::isalpha(static_cast<unsigned char>(t[q])) || t[q] == '_'))
    return false;
  if (q < t.size() && t[q] == '(') return false;  // function name
  int col = colToIndex(toUpper(letters));
  int row = std::atoi(t.substr(digStart, q - digStart).c_str()) - 1;
  if (col < 0 || col >= MAX_COLS || row < 0 || row >= MAX_ROWS) return false;
  out.valid = true;
  out.absRow = absRow;
  out.col = col;
  out.row = row;
  out.end = q;
  return true;
}

std::string makeRef(const RefTok& r, int row, int col) {
  std::string out;
  if (!r.sheet.empty()) out += r.sheet + "!";
  if (r.absCol) out += "$";
  out += indexToCol(col);
  if (r.absRow) out += "$";
  out += std::to_string(row + 1);
  return out;
}

struct ShiftSpec {
  bool rows = true;
  bool insert = true;
  int at = 0;
  int count = 0;
};

// Shifts a lone reference. false means it pointed at now-deleted cells.
bool shiftSingle(int value, const ShiftSpec& sp, int& out) {
  if (sp.insert) {
    out = value >= sp.at ? value + sp.count : value;
    return true;
  }
  if (value >= sp.at + sp.count) {
    out = value - sp.count;
    return true;
  }
  if (value >= sp.at) return false;
  out = value;
  return true;
}

// Shifts an area. A range that merely spans an insertion grows, as in Excel:
// inserting a row inside A1:A10 gives A1:A11, not A2:A11.
bool shiftRange(int a, int b, const ShiftSpec& sp, int& outA, int& outB) {
  if (b < a) std::swap(a, b);
  if (sp.insert) {
    if (b < sp.at) {
      outA = a;
      outB = b;
    } else if (a >= sp.at) {
      outA = a + sp.count;
      outB = b + sp.count;
    } else {
      outA = a;
      outB = b + sp.count;
    }
    return true;
  }
  int na = (a >= sp.at + sp.count) ? a - sp.count : (a >= sp.at ? sp.at : a);
  int nb = (b >= sp.at + sp.count) ? b - sp.count : (b >= sp.at ? sp.at - 1 : b);
  outA = na;
  outB = nb;
  return na <= nb;
}

std::string shiftOneOrRange(const RefTok& first, const RefTok& second, bool isRange, const ShiftSpec& sp) {
  if (!isRange) {
    int v = 0;
    if (!shiftSingle(sp.rows ? first.row : first.col, sp, v)) return "#REF!";
    return sp.rows ? makeRef(first, v, first.col) : makeRef(first, first.row, v);
  }
  int a = 0, b = 0;
  bool ok = sp.rows ? shiftRange(first.row, second.row, sp, a, b)
                    : shiftRange(first.col, second.col, sp, a, b);
  if (!ok) return "#REF!";
  std::string out = sp.rows ? makeRef(first, a, first.col) : makeRef(first, first.row, a);
  out += ":";
  out += sp.rows ? makeRef(second, b, second.col) : makeRef(second, second.row, b);
  return out;
}

// Rewrites A1-style references in a formula after rows/columns were inserted or
// deleted. Mirrors Excel: absolute markers do not stop shifting, deleted targets
// become #REF!, and string literals are left alone.
std::string adjustFormulaText(const std::string& f, const ShiftSpec& sp, const std::string& targetSheetUpper,
                              bool onTargetSheet) {
  std::string out;
  out.reserve(f.size() + 8);
  size_t i = 0;
  auto boundaryOk = [&](size_t p) {
    if (p == 0) return true;
    char pc = f[p - 1];
    return !(std::isalnum(static_cast<unsigned char>(pc)) || pc == '_' || pc == '.' || pc == '$');
  };
  while (i < f.size()) {
    char c = f[i];
    if (c == '"') {  // string literal: copy verbatim
      out += c;
      i++;
      while (i < f.size()) {
        out += f[i];
        if (f[i] == '"') {
          if (i + 1 < f.size() && f[i + 1] == '"') {
            out += f[i + 1];
            i += 2;
            continue;
          }
          i++;
          break;
        }
        i++;
      }
      continue;
    }
    bool startOk = boundaryOk(i);
    // sheet-qualified reference: Name!A1 or 'Name'!A1
    if (startOk && (c == '\'' || std::isalpha(static_cast<unsigned char>(c)))) {
      size_t bang = std::string::npos;
      std::string sheetName;
      if (c == '\'') {
        size_t close = f.find('\'', i + 1);
        if (close != std::string::npos && close + 1 < f.size() && f[close + 1] == '!') {
          bang = close + 1;
          sheetName = f.substr(i + 1, close - i - 1);
        }
      } else {
        size_t p = i;
        while (p < f.size() && (std::isalnum(static_cast<unsigned char>(f[p])) || f[p] == '_' || f[p] == '.'))
          p++;
        if (p < f.size() && f[p] == '!' && p - i < 40) {
          bang = p;
          sheetName = f.substr(i, p - i);
        }
      }
      if (bang != std::string::npos) {
        RefTok first;
        if (parseRefAt(f, bang + 1, first)) {
          RefTok second;
          bool isRange = false;
          if (first.end < f.size() && f[first.end] == ':') {
            RefTok tmp;
            if (parseRefAt(f, first.end + 1, tmp)) {
              second = tmp;
              isRange = true;
            }
          }
          if (toUpper(sheetName) == targetSheetUpper) {
            first.sheet = f.substr(i, bang - i);  // keep the prefix exactly
            second.sheet = first.sheet;
            out += shiftOneOrRange(first, second, isRange, sp);
            i = isRange ? second.end : first.end;
            continue;
          }
          out += f.substr(i, (isRange ? second.end : first.end) - i);
          i = isRange ? second.end : first.end;
          continue;
        }
      }
    }
    if (startOk && (c == '$' || std::isalpha(static_cast<unsigned char>(c)))) {
      RefTok first;
      if (parseRefAt(f, i, first)) {
        RefTok second;
        bool isRange = false;
        if (first.end < f.size() && f[first.end] == ':') {
          RefTok tmp;
          if (parseRefAt(f, first.end + 1, tmp)) {
            second = tmp;
            isRange = true;
          }
        }
        if (onTargetSheet) {
          out += shiftOneOrRange(first, second, isRange, sp);
          i = isRange ? second.end : first.end;
          continue;
        }
        out += f.substr(i, (isRange ? second.end : first.end) - i);
        i = isRange ? second.end : first.end;
        continue;
      }
    }
    out += c;
    i++;
  }
  return out;
}

void remapRows(std::unordered_map<int, int>& m, int at, int count, bool insert) {
  std::unordered_map<int, int> next;
  for (auto& kv : m) {
    int r = kv.first;
    if (insert) {
      next[r >= at ? r + count : r] = kv.second;
    } else {
      if (r >= at && r < at + count) continue;
      next[r >= at + count ? r - count : r] = kv.second;
    }
  }
  m.swap(next);
}
void remapCols(std::unordered_map<int, int>& m, int at, int count, bool insert) {
  std::unordered_map<int, int> next;
  for (auto& kv : m) {
    int c = kv.first;
    if (insert) {
      next[c >= at ? c + count : c] = kv.second;
    } else {
      if (c >= at && c < at + count) continue;
      next[c >= at + count ? c - count : c] = kv.second;
    }
  }
  m.swap(next);
}

void shiftAllFormulas(std::vector<Sheet>& sheets, int targetSheet, const ShiftSpec& sp) {
  std::string targetUpper = toUpper(sheets[targetSheet].name);
  for (size_t si = 0; si < sheets.size(); si++) {
    bool onTarget = (static_cast<int>(si) == targetSheet);
    for (auto& kv : sheets[si].cells) {
      std::string& raw = kv.second.raw;
      if (raw.size() > 1 && raw[0] == '=' && raw.find('!') == std::string::npos && !onTarget) continue;
      if (raw.size() > 1 && raw[0] == '=') {
        raw = adjustFormulaText(raw, sp, targetUpper, onTarget);
      }
    }
  }
}

}  // namespace

void Workbook::insertRows(int s, int before, int count) {
  if (count <= 0) return;
  before = std::max(0, std::min(before, MAX_ROWS - 1));
  count = std::min(count, MAX_ROWS - before);
  Sheet& sh = sheets_[s];
  std::unordered_map<int64_t, Cell> next;
  for (auto& kv : sh.cells) {
    int r = static_cast<int>(kv.first >> 20), c = static_cast<int>(kv.first & 0xFFFFF);
    int nr = r >= before ? r + count : r;
    if (nr < MAX_ROWS) next[cellKey(nr, c)] = kv.second;
  }
  sh.cells.swap(next);
  remapRows(sh.rowH, before, count, true);
  ShiftSpec sp;
  sp.rows = true;
  sp.insert = true;
  sp.at = before;
  sp.count = count;
  shiftAllFormulas(sheets_, s, sp);
  invalidate();
}

void Workbook::deleteRows(int s, int start, int count) {
  if (count <= 0) return;
  start = std::max(0, std::min(start, MAX_ROWS - 1));
  count = std::min(count, MAX_ROWS - start);
  Sheet& sh = sheets_[s];
  std::unordered_map<int64_t, Cell> next;
  for (auto& kv : sh.cells) {
    int r = static_cast<int>(kv.first >> 20), c = static_cast<int>(kv.first & 0xFFFFF);
    if (r >= start && r < start + count) continue;
    int nr = r >= start + count ? r - count : r;
    next[cellKey(nr, c)] = kv.second;
  }
  sh.cells.swap(next);
  remapRows(sh.rowH, start, count, false);
  ShiftSpec sp;
  sp.rows = true;
  sp.insert = false;
  sp.at = start;
  sp.count = count;
  shiftAllFormulas(sheets_, s, sp);
  invalidate();
}

void Workbook::insertCols(int s, int before, int count) {
  if (count <= 0) return;
  before = std::max(0, std::min(before, MAX_COLS - 1));
  count = std::min(count, MAX_COLS - before);
  Sheet& sh = sheets_[s];
  std::unordered_map<int64_t, Cell> next;
  for (auto& kv : sh.cells) {
    int r = static_cast<int>(kv.first >> 20), c = static_cast<int>(kv.first & 0xFFFFF);
    int nc = c >= before ? c + count : c;
    if (nc < MAX_COLS) next[cellKey(r, nc)] = kv.second;
  }
  sh.cells.swap(next);
  remapCols(sh.colW, before, count, true);
  ShiftSpec sp;
  sp.rows = false;
  sp.insert = true;
  sp.at = before;
  sp.count = count;
  shiftAllFormulas(sheets_, s, sp);
  invalidate();
}

void Workbook::deleteCols(int s, int start, int count) {
  if (count <= 0) return;
  start = std::max(0, std::min(start, MAX_COLS - 1));
  count = std::min(count, MAX_COLS - start);
  Sheet& sh = sheets_[s];
  std::unordered_map<int64_t, Cell> next;
  for (auto& kv : sh.cells) {
    int r = static_cast<int>(kv.first >> 20), c = static_cast<int>(kv.first & 0xFFFFF);
    if (c >= start && c < start + count) continue;
    int nc = c >= start + count ? c - count : c;
    next[cellKey(r, nc)] = kv.second;
  }
  sh.cells.swap(next);
  remapCols(sh.colW, start, count, false);
  ShiftSpec sp;
  sp.rows = false;
  sp.insert = false;
  sp.at = start;
  sp.count = count;
  shiftAllFormulas(sheets_, s, sp);
  invalidate();
}

void Workbook::setColWidth(int s, int col, int w) {
  sheets_[s].colW[col] = std::max(24, std::min(600, w));
  version_++;
}
void Workbook::setRowHeight(int s, int row, int h) {
  sheets_[s].rowH[row] = std::max(14, std::min(200, h));
  version_++;
}
int Workbook::colWidth(int s, int col) const {
  auto& m = sheets_[s].colW;
  auto it = m.find(col);
  return it == m.end() ? 100 : it->second;
}
int Workbook::rowHeight(int s, int row) const {
  auto& m = sheets_[s].rowH;
  auto it = m.find(row);
  return it == m.end() ? 22 : it->second;
}

void Workbook::invalidate() {
  cache_.clear();
  inFlight_.clear();
  circular_.clear();
  version_++;
}

double Workbook::now() const {
  if (hasNow_) return nowOverride_;
  time_t t = std::time(nullptr);
  struct tm tmv;
#ifdef _WIN32
  gmtime_s(&tmv, &t);
#else
  gmtime_r(&t, &tmv);
#endif
  return dateToSerial(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

Value Workbook::value(int s, int r, int c) const { return computed(s, r, c).value; }

Computed Workbook::computed(int s, int r, int c) const {
  if (s < 0 || s >= (int)sheets_.size() || r < 0 || c < 0 || r >= MAX_ROWS || c >= MAX_COLS) {
    Computed out;
    out.value = Value::error(Err::Ref);
    return out;
  }
  int64_t key = (static_cast<int64_t>(s) << 44) | cellKey(r, c);
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;

  const Cell* cp = cell(s, r, c);
  if (!cp || cp->raw.empty()) {
    Computed blank;
    return blank;
  }
  if (inFlight_.count(key)) {
    circular_.insert(key);
    Computed cyc;
    cyc.value = Value::number(0);
    cyc.circular = true;
    return cyc;
  }
  inFlight_.insert(key);
  Computed out;
  try {
    if (cp->raw[0] == '=') out = computeFormula(s, r, c, cp->raw);
    else out = computeLiteral(cp->raw);
  } catch (FErr& e) {
    out.value = Value::error(e.code);
  } catch (...) {
    out.value = Value::error(Err::Value);
  }
  inFlight_.erase(key);
  if (circular_.count(key)) out.circular = true;
  cache_[key] = out;
  return out;
}

Computed Workbook::computeFormula(int s, int r, int c, const std::string& raw) const {
  Computed out;
  out.formula = true;
  registerFunctions();
  try {
    std::string body = raw.substr(1);
    Lexer lex(body);
    lex.run();
    Parser parser(lex.out);
    Node ast = parser.parseExpr();
    if (parser.peek().t != TT::End) fail(Err::Name);
    Ctx ctx{this, s, r, c, {}};
    Arg arg = evalNode(ast, ctx);
    out.value = scalarOf(arg, ctx);
  } catch (FErr& e) {
    out.value = Value::error(e.code);
  } catch (...) {
    out.value = Value::error(Err::Value);
  }
  return out;
}

Computed Workbook::computeLiteral(const std::string& raw) const {
  Computed out;
  if (raw.empty()) return out;
  if (raw[0] == '\'') {
    out.value = Value::string(raw.substr(1));
    return out;
  }
  std::string up = toUpper(trim(raw));
  if (up == "TRUE" || up == "FALSE") {
    out.value = Value::boolean(up == "TRUE");
    return out;
  }
  if (!up.empty() && up[0] == '#') {
    if (up == "#N/A") out.value = Value::error(Err::NA);
    else if (up == "#DIV/0!") out.value = Value::error(Err::Div0);
    else if (up == "#VALUE!") out.value = Value::error(Err::Value);
    else if (up == "#REF!") out.value = Value::error(Err::Ref);
    else if (up == "#NAME?") out.value = Value::error(Err::Name);
    else if (up == "#NUM!") out.value = Value::error(Err::Num);
    else out.value = Value::string(raw);
    return out;
  }
  // numbers, percentages, currency and thousands separators
  std::string s = trim(raw);
  bool percent = !s.empty() && s.back() == '%';
  if (percent) s.pop_back();
  std::string cleaned;
  for (char ch : s)
    if (ch != ',' && ch != '$' && ch != ' ') cleaned += ch;
  if (!cleaned.empty()) {
    char* end = nullptr;
    double d = std::strtod(cleaned.c_str(), &end);
    if (end && *end == '\0') {
      bool hasCents = cleaned.find('.') != std::string::npos;
      bool hasComma = s.find(',') != std::string::npos;
      bool hasCurrency = s.find('$') != std::string::npos;
      if (percent) {
        d /= 100;
        out.numFmt = "0%";
      } else if (hasCurrency) {
        out.numFmt = hasCents ? "$#,##0.00" : "$#,##0";
      } else if (hasComma) {
        out.numFmt = hasCents ? "#,##0.00" : "#,##0";
      }
      out.value = Value::number(d);
      return out;
    }
  }
  double serial;
  if (parseDateText(raw, serial)) {
    out.value = Value::number(serial);
    out.numFmt = raw.find(':') != std::string::npos ? "yyyy-mm-dd hh:mm" : "yyyy-mm-dd";
    return out;
  }
  out.value = Value::string(raw);
  return out;
}

std::string Workbook::numberFormat(int s, int r, int c) const {
  const Cell* cp = cell(s, r, c);
  if (cp && !cp->style.numFmt.empty()) return cp->style.numFmt;
  return computed(s, r, c).numFmt;
}

std::string Workbook::display(int s, int r, int c) const {
  Computed cc = computed(s, r, c);
  return formatValue(cc.value, numberFormat(s, r, c));
}

bool Workbook::usedBounds(int s, int& r1, int& c1, int& r2, int& c2) const {
  if (s < 0 || s >= (int)sheets_.size()) return false;
  bool any = false;
  for (auto& kv : sheets_[s].cells) {
    if (kv.second.raw.empty()) continue;
    int r = (int)(kv.first >> 20), c = (int)(kv.first & 0xFFFFF);
    if (!any) {
      r1 = r2 = r;
      c1 = c2 = c;
      any = true;
    } else {
      r1 = std::min(r1, r);
      c1 = std::min(c1, c);
      r2 = std::max(r2, r);
      c2 = std::max(c2, c);
    }
  }
  return any;
}

// ---------------------------------------------------------------- IO
namespace {
std::string escapeField(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\t') out += "\\t";
    else out += c;
  }
  return out;
}
std::string unescapeField(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char n = s[++i];
      out += n == 'n' ? '\n' : n == 't' ? '\t' : n;
    } else
      out += s[i];
  }
  return out;
}
}  // namespace

namespace {
std::vector<std::string> splitFields(const std::string& line) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (true) {
    size_t tab = line.find('\t', pos);
    if (tab == std::string::npos) {
      out.push_back(line.substr(pos));
      break;
    }
    out.push_back(line.substr(pos, tab - pos));
    pos = tab + 1;
  }
  return out;
}
std::string fmtDouble(double d) {
  char buf[40];
  std::snprintf(buf, sizeof buf, "%g", d);
  return buf;
}
}  // namespace

std::string Workbook::serialize() const {
  std::string out = "GRID2\n";
  for (auto& sh : sheets_) {
    out += "SHEET\t" + escapeField(sh.name) + "\n";
    std::vector<int> cols(sh.colW.size()), rows(sh.rowH.size());
    {
      size_t k = 0;
      for (auto& kv : sh.colW) cols[k++] = kv.first;
      k = 0;
      for (auto& kv : sh.rowH) rows[k++] = kv.first;
    }
    std::sort(cols.begin(), cols.end());
    std::sort(rows.begin(), rows.end());
    for (int c : cols) out += "W\t" + std::to_string(c) + "\t" + std::to_string(sh.colW.at(c)) + "\n";
    for (int r : rows) out += "H\t" + std::to_string(r) + "\t" + std::to_string(sh.rowH.at(r)) + "\n";
    std::vector<int64_t> keys;
    keys.reserve(sh.cells.size());
    for (auto& kv : sh.cells) {
      if (kv.second.raw.empty() && kv.second.style.isDefault()) continue;
      keys.push_back(kv.first);
    }
    std::sort(keys.begin(), keys.end());
    for (int64_t key : keys) {
      const Cell& cell = sh.cells.at(key);
      int r = static_cast<int>(key >> 20), c = static_cast<int>(key & 0xFFFFF);
      const Style& st = cell.style;
      out += "C\t" + std::to_string(r) + "\t" + std::to_string(c) + "\t";
      out += std::string(st.bold ? "1" : "0") + (st.italic ? "1" : "0") + (st.underline ? "1" : "0") +
             (st.wrap ? "1" : "0");
      out += "\t" + std::to_string(st.align);
      out += "\t" + std::to_string(st.fill);
      out += "\t" + std::to_string(st.color);
      out += "\t" + std::to_string(st.border);
      out += "\t" + escapeField(st.fontName);
      out += "\t" + fmtDouble(st.fontSize);
      out += "\t" + escapeField(st.numFmt);
      out += "\t" + escapeField(cell.raw) + "\n";
    }
  }
  return out;
}

bool Workbook::deserialize(const std::string& data) {
  size_t pos = 0;
  auto nextLine = [&](std::string& line) -> bool {
    if (pos >= data.size()) return false;
    size_t nl = data.find('\n', pos);
    if (nl == std::string::npos) {
      line = data.substr(pos);
      pos = data.size();
    } else {
      line = data.substr(pos, nl - pos);
      pos = nl + 1;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
  };
  std::string line;
  if (!nextLine(line) || line.rfind("GRID", 0) != 0) return false;
  bool legacy = (line == "GRID1");
  std::vector<Sheet> sheets;
  while (nextLine(line)) {
    if (line.empty()) continue;
    std::vector<std::string> f = splitFields(line);
    const std::string& tag = f[0];
    if (tag == "SHEET") {
      Sheet sh;
      if (f.size() > 1) sh.name = unescapeField(f[1]);
      sheets.push_back(sh);
    } else if (tag == "W" && !sheets.empty() && f.size() >= 3) {
      sheets.back().colW[std::atoi(f[1].c_str())] = std::atoi(f[2].c_str());
    } else if (tag == "H" && !sheets.empty() && f.size() >= 3) {
      sheets.back().rowH[std::atoi(f[1].c_str())] = std::atoi(f[2].c_str());
    } else if (tag == "C" && !sheets.empty() && f.size() >= 4) {
      Cell cell;
      int r = std::atoi(f[1].c_str()), c = std::atoi(f[2].c_str());
      if (legacy) {
        const std::string& flags = f[3];
        cell.style.bold = !flags.empty() && flags[0] == '1';
        cell.style.italic = flags.size() > 1 && flags[1] == '1';
        cell.style.underline = flags.size() > 2 && flags[2] == '1';
        if (f.size() > 4) cell.style.align = std::atoi(f[4].c_str());
        if (f.size() > 5) cell.style.fill = static_cast<uint32_t>(std::strtoul(f[5].c_str(), nullptr, 10));
        if (f.size() > 6) cell.style.numFmt = unescapeField(f[6]);
        if (f.size() > 7) cell.raw = unescapeField(f[7]);
      } else {
        const std::string& flags = f[3];
        cell.style.bold = !flags.empty() && flags[0] == '1';
        cell.style.italic = flags.size() > 1 && flags[1] == '1';
        cell.style.underline = flags.size() > 2 && flags[2] == '1';
        cell.style.wrap = flags.size() > 3 && flags[3] == '1';
        if (f.size() > 4) cell.style.align = std::atoi(f[4].c_str());
        if (f.size() > 5) cell.style.fill = static_cast<uint32_t>(std::strtoul(f[5].c_str(), nullptr, 10));
        if (f.size() > 6) cell.style.color = static_cast<uint32_t>(std::strtoul(f[6].c_str(), nullptr, 10));
        if (f.size() > 7) cell.style.border = std::atoi(f[7].c_str());
        if (f.size() > 8) cell.style.fontName = unescapeField(f[8]);
        if (f.size() > 9) cell.style.fontSize = std::atof(f[9].c_str());
        if (f.size() > 10) cell.style.numFmt = unescapeField(f[10]);
        if (f.size() > 11) cell.raw = unescapeField(f[11]);
      }
      if (r >= 0 && r < MAX_ROWS && c >= 0 && c < MAX_COLS)
        sheets.back().cells[cellKey(r, c)] = cell;
    }
  }
  if (sheets.empty()) {
    Sheet sh;
    sh.name = "Sheet1";
    sheets.push_back(sh);
  }
  sheets_ = sheets;
  cache_.clear();
  inFlight_.clear();
  circular_.clear();
  version_++;
  return true;
}

bool Workbook::save(const std::string& path) const {
  std::string data = serialize();
  std::ofstream f(path, std::ios::binary);
  bool ok = static_cast<bool>(f);
  if (ok) f.write(data.data(), static_cast<std::streamsize>(data.size()));
  return ok;
}

bool Workbook::load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return deserialize(data);
}

Style Workbook::styleAt(int s, int r, int c) const {
  const Cell* cp = cell(s, r, c);
  return cp ? cp->style : Style();
}

std::string Workbook::toCSV(int s) const {
  int r1, c1, r2, c2;
  if (!usedBounds(s, r1, c1, r2, c2)) return "";
  std::string out;
  for (int r = r1; r <= r2; r++) {
    for (int c = c1; c <= c2; c++) {
      std::string t = display(s, r, c);
      bool needsQuote = t.find(',') != std::string::npos || t.find('"') != std::string::npos ||
                        t.find('\n') != std::string::npos;
      if (needsQuote) {
        std::string q = "\"";
        for (char ch : t) {
          if (ch == '"') q += "\"\"";
          else q += ch;
        }
        q += "\"";
        out += q;
      } else
        out += t;
      if (c < c2) out += ",";
    }
    out += "\r\n";
  }
  return out;
}

}  // namespace grid
