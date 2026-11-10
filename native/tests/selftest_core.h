// Portable engine self-test, shared by Grid.exe (--selftest) and the host test
// harness. Deliberately free of any Windows dependency.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../engine/engine.h"

namespace grid {
namespace selftest {

inline int& passCount() {
  static int n = 0;
  return n;
}
inline int& failCount() {
  static int n = 0;
  return n;
}
inline std::string& failureLog() {
  static std::string log;
  return log;
}

inline void expect(bool ok, const std::string& what, const std::string& got, const std::string& want) {
  if (ok) {
    passCount()++;
    return;
  }
  failCount()++;
  failureLog() += "FAIL " + what + "\n  got:  " + got + "\n  want: " + want + "\n";
}

inline void expectNear(double got, double want, const char* what) {
  expect(std::fabs(got - want) < 1e-9, what, std::to_string(got), std::to_string(want));
}

inline void expectText(const std::string& got, const std::string& want, const char* what) {
  expect(got == want, what, got, want);
}

// Returns 0 when every check passed.
inline int runEngineSelfTest(std::string& report, const std::string& tempPath) {
  passCount() = 0;
  failCount() = 0;
  failureLog().clear();

  Workbook wb;
  auto set = [&](int r, int c, const std::string& raw) { wb.setRaw(0, r, c, raw); };
  auto num = [&](int r, int c) { return wb.value(0, r, c).num; };
  auto text = [&](int r, int c) { return wb.display(0, r, c); };

  // arithmetic and precedence
  set(0, 0, "=1+2*3");
  expectNear(num(0, 0), 7, "1+2*3");
  set(0, 1, "=(1+2)*3");
  expectNear(num(0, 1), 9, "(1+2)*3");
  set(0, 2, "=-2^2");
  expectNear(num(0, 2), 4, "-2^2");
  set(0, 3, "=2^3^2");
  expectNear(num(0, 3), 64, "2^3^2 left-associative");
  set(0, 4, "=\"a\"&1+2");
  expectText(text(0, 4), "a3", "text concatenation with arithmetic");

  // ranges and functions
  for (int i = 0; i < 4; i++) set(1 + i, 0, std::to_string(i + 1));
  set(1, 1, "=SUM(A2:A5)");
  expectNear(num(1, 1), 10, "SUM range");
  set(2, 1, "=AVERAGE(A2:A5)");
  expectNear(num(2, 1), 2.5, "AVERAGE range");
  set(3, 1, "=COUNTIF(A2:A5,\">2\")");
  expectNear(num(3, 1), 2, "COUNTIF criteria");
  set(4, 1, "=SUMPRODUCT(A2:A5,A2:A5)");
  expectNear(num(4, 1), 30, "SUMPRODUCT");
  set(5, 0, "=IF(A2>5,\"big\",\"small\")");
  expectText(text(5, 0), "small", "IF");
  set(5, 1, "=IFERROR(1/0,\"caught\")");
  expectText(text(5, 1), "caught", "IFERROR");
  set(6, 0, "=LET(x,A2*10,y,x+5,y)");
  expectNear(num(6, 0), 15, "LET");

  // text, lookup and dates
  set(7, 0, "Hello World");
  set(7, 1, "=MID(A8,7,5)");
  expectText(text(7, 1), "World", "MID");
  set(7, 2, "=TEXTJOIN(\"-\",TRUE,\"a\",\"b\")");
  expectText(text(7, 2), "a-b", "TEXTJOIN");
  set(8, 0, "=ROUND(2.675,2)");
  expectNear(num(8, 0), 2.68, "ROUND with Excel's 15-digit rounding");
  set(8, 1, "=DATE(2024,1,15)");
  expectNear(num(8, 1), 45306, "DATE serial");
  set(8, 2, "=TEXT(B9,\"yyyy-mm-dd\")");
  expectText(text(8, 2), "2024-01-15", "TEXT date format");
  set(9, 0, "=YEAR(B9)");
  expectNear(num(9, 0), 2024, "YEAR");
  set(9, 1, "=PMT(0.05/12,360,-300000)");
  expectNear(std::round(num(9, 1) * 100) / 100, 1610.46, "PMT");

  // errors and references
  set(10, 0, "=1/0");
  expect(wb.value(0, 10, 0).isError(), "division by zero is an error", text(10, 0), "#DIV/0!");
  set(10, 1, "=NOSUCHFUNCTION(1)");
  expect(wb.value(0, 10, 1).isError(), "unknown function", text(10, 1), "#NAME?");
  set(10, 2, "=B9+1");
  expectNear(num(10, 2), 45307, "cell reference");

  // formatting
  expectText(formatValue(Value::number(1234.5), "#,##0.00"), "1,234.50", "number format");
  expectText(formatValue(Value::number(0.5), "0%"), "50%", "percent format");
  expectText(formatValue(Value::number(-1234), "#,##0;(#,##0)"), "(1,234)", "negative format");
  expectText(formatValue(Value::number(1234.5), "$#,##0.00"), "$1,234.50", "currency format");
  expectText(formatValue(Value::number(dateToSerial(2024, 3, 5)), "dddd, mmmm d, yyyy"),
             "Tuesday, March 5, 2024", "long date format");

  // recalc, structural edits and reference rewriting
  Workbook w2;
  w2.setRaw(0, 0, 0, "1");
  w2.setRaw(0, 0, 1, "=A1*2");
  expectNear(w2.value(0, 0, 1).num, 2, "formula before edit");
  w2.setRaw(0, 0, 0, "10");
  expectNear(w2.value(0, 0, 1).num, 20, "recalculation after edit");
  w2.insertRows(0, 0, 1);
  expectText(w2.raw(0, 1, 1), "=A2*2", "insert row shifts reference");
  expectNear(w2.value(0, 1, 1).num, 20, "value after insert");

  // workbook round trip
  Style st;
  st.bold = true;
  st.align = 1;
  st.fill = 0x00FFCC00;
  st.numFmt = "0.00%";
  st.fontSize = 14;
  wb.setStyle(0, 11, 0, st);
  std::string snap = wb.serialize();
  Workbook back;
  bool loaded = back.deserialize(snap);
  expect(loaded, "deserialize", "false", "true");
  Style bs = back.styleAt(0, 11, 0);
  expect(bs.bold && bs.align == 1 && bs.fontSize == 14, "style round trip", "", "");

  // save and load through the file system
  const std::string path = tempPath;
  expect(wb.save(path), "save workbook", "false", "true");
  Workbook fromDisk;
  expect(fromDisk.load(path) && fromDisk.sheetCount() >= 1, "load workbook", "false", "true");
  std::remove(path.c_str());

  report = "Grid engine self-test\n\n";
  report += std::to_string(passCount()) + " passed, " + std::to_string(failCount()) + " failed\n\n";
  report += failureLog().empty() ? "All checks passed." : failureLog();
  return failCount() == 0 ? 0 : 1;
}


}  // namespace selftest
}  // namespace grid
