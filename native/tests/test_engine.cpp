// Engine conformance tests. Mirrors the semantics verified in the web build.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../engine/engine.h"
#include "selftest_core.h"

using namespace grid;

static int failures = 0;
static int checks = 0;

static void checkImpl(bool ok, const std::string& what, const std::string& got, const std::string& want, int line) {
  checks++;
  if (!ok) {
    failures++;
    std::printf("FAIL line %d: %s\n      got: %s\n     want: %s\n", line, what.c_str(), got.c_str(), want.c_str());
  }
}
#define CHECK(cond) checkImpl((cond), #cond, "", "", __LINE__)
#define EQ_NUM(actual, expected)                                                                     \
  do {                                                                                               \
    double a_ = (actual), e_ = (expected);                                                           \
    checkImpl(std::fabs(a_ - e_) < 1e-9, #actual, std::to_string(a_), std::to_string(e_), __LINE__);  \
  } while (0)
#define EQ_STR(actual, expected)                                                                     \
  do {                                                                                               \
    std::string a_ = (actual), e_ = (expected);                                                      \
    checkImpl(a_ == e_, #actual, a_, e_, __LINE__);                                                  \
  } while (0)

struct Fixture {
  Workbook wb;
  void set(const char* ref, const std::string& raw) {
    int r, c;
    if (!parseA1(ref, r, c)) {
      std::printf("bad ref %s\n", ref);
      return;
    }
    wb.setRaw(0, r, c, raw);
  }
  Value v(const char* ref) {
    int r, c;
    parseA1(ref, r, c);
    return wb.value(0, r, c);
  }
  double n(const char* ref) { return v(ref).num; }
  std::string s(const char* ref) { return wb.display(0, 0, 0) == "" ? "" : generalText(v(ref)); }
  std::string shown(const char* ref) {
    int r, c;
    parseA1(ref, r, c);
    return wb.display(0, r, c);
  }
};

static void testArithmetic() {
  Fixture f;
  f.set("A1", "=1+2*3");
  EQ_NUM(f.n("A1"), 7);
  f.set("A2", "=(1+2)*3");
  EQ_NUM(f.n("A2"), 9);
  f.set("A3", "=-2^2");
  EQ_NUM(f.n("A3"), 4);
  f.set("A4", "=2^3^2");
  EQ_NUM(f.n("A4"), 64);
  f.set("A5", "=2^2%");
  EQ_NUM(f.n("A5"), std::pow(2.0, 0.02));
  f.set("A6", "=50%");
  EQ_NUM(f.n("A6"), 0.5);
  f.set("A7", "=\"a\"&\"b\"");
  EQ_STR(generalText(f.v("A7")), "ab");
  f.set("A8", "=1<2");
  CHECK(f.v("A8").type == Value::Bool && f.v("A8").b);
  f.set("A9", "=\"A\"=\"a\"");
  CHECK(f.v("A9").b);
  f.set("A10", "=7/0");
  CHECK(f.v("A10").isError() && f.v("A10").err == Err::Div0);
  f.set("A11", "=1&2");
  EQ_STR(generalText(f.v("A11")), "12");
}

static void testCoercion() {
  Fixture f;
  f.set("B1", "hello");
  f.set("B2", "42");
  f.set("B3", "TRUE");
  f.set("B4", "1,234.5");
  f.set("B5", "12%");
  EQ_STR(generalText(f.v("B1")), "hello");
  EQ_NUM(f.v("B2").num, 42);
  CHECK(f.v("B3").type == Value::Bool && f.v("B3").b);
  EQ_NUM(f.v("B4").num, 1234.5);
  EQ_NUM(f.v("B5").num, 0.12);
  f.set("C1", "=A1+5");
  EQ_NUM(f.n("C1"), 5);
}

static void testFunctions() {
  Fixture f;
  for (int i = 0; i < 4; i++) f.set((std::string("A") + std::to_string(i + 1)).c_str(), std::to_string(i + 1));
  f.set("B1", "=SUM(A1:A4)");
  EQ_NUM(f.n("B1"), 10);
  f.set("B2", "=AVERAGE(A1:A4)");
  EQ_NUM(f.n("B2"), 2.5);
  f.set("B3", "=MEDIAN(A1:A4)");
  EQ_NUM(f.n("B3"), 2.5);
  f.set("B4", "=MAX(A1:A4)");
  EQ_NUM(f.n("B4"), 4);
  f.set("B5", "=COUNT(A1:A4)");
  EQ_NUM(f.n("B5"), 4);
  f.set("B6", "=LARGE(A1:A4,2)");
  EQ_NUM(f.n("B6"), 3);
  f.set("B7", "=ROUND(2.675,2)");
  EQ_NUM(f.n("B7"), 2.68);
  f.set("B8", "=MOD(-3,2)");
  EQ_NUM(f.n("B8"), 1);
  f.set("B9", "=SQRT(16)");
  EQ_NUM(f.n("B9"), 4);
  f.set("B10", "=STDEV.S(A1:A4)");
  EQ_NUM(f.n("B10"), 1.2909944487358056);
  f.set("C1", "=SIN(PI()/2)");
  EQ_NUM(f.n("C1"), 1);
  f.set("C2", "=DEGREES(PI())");
  EQ_NUM(f.n("C2"), 180);
  f.set("C3", "=MROUND(7,3)");
  EQ_NUM(f.n("C3"), 6);
  f.set("C4", "=SUMSQ(3,4)");
  EQ_NUM(f.n("C4"), 25);
  f.set("C5", "=ISEVEN(4)");
  CHECK(f.v("C5").b);
  f.set("C6", "=RANK(4,A1:A4)");
  EQ_NUM(f.n("C6"), 1);
  f.set("C7", "=WEEKNUM(DATE(2024,1,15))");
  EQ_NUM(f.n("C7"), 3);
  f.set("C8", "=DATEVALUE(\"2024-01-15\")");
  EQ_NUM(f.n("C8"), 45306);
  f.set("C9", "=TIMEVALUE(\"14:30\")");
  EQ_NUM(f.n("C9"), (14 * 3600 + 30 * 60) / 86400.0);
  f.set("C10", "=AVERAGEA(A1:A5)");
  EQ_NUM(f.n("C10"), 2.5);
}

static void testCriteria() {
  Fixture f;
  f.set("A1", "apple");
  f.set("A2", "banana");
  f.set("A3", "apple");
  f.set("A4", "cherry");
  f.set("B1", "10");
  f.set("B2", "20");
  f.set("B3", "30");
  f.set("B4", "40");
  f.set("C1", "=SUMIF(A1:A4,\"apple\",B1:B4)");
  EQ_NUM(f.n("C1"), 40);
  f.set("C2", "=COUNTIF(B1:B4,\">15\")");
  EQ_NUM(f.n("C2"), 3);
  f.set("C3", "=SUMIFS(B1:B4,A1:A4,\"apple\")");
  EQ_NUM(f.n("C3"), 40);
  f.set("C4", "=AVERAGEIF(A1:A4,\"apple\",B1:B4)");
  EQ_NUM(f.n("C4"), 20);
  f.set("C5", "=COUNTIF(A1:A4,\"*an*\")");
  EQ_NUM(f.n("C5"), 1);
  f.set("C6", "=MAX(B1:B4)");
  EQ_NUM(f.n("C6"), 40);
}

static void testText() {
  Fixture f;
  f.set("A1", "Hello World");
  f.set("B1", "=LEFT(A1,5)");
  EQ_STR(f.v("B1").text, "Hello");
  f.set("B2", "=RIGHT(A1,5)");
  EQ_STR(f.v("B2").text, "World");
  f.set("B3", "=MID(A1,7,5)");
  EQ_STR(f.v("B3").text, "World");
  f.set("B4", "=LEN(A1)");
  EQ_NUM(f.n("B4"), 11);
  f.set("B5", "=UPPER(B1)");
  EQ_STR(f.v("B5").text, "HELLO");
  f.set("B6", "=SUBSTITUTE(A1,\"World\",\"Grid\")");
  EQ_STR(f.v("B6").text, "Hello Grid");
  f.set("B7", "=TEXTJOIN(\"-\",TRUE,\"a\",\"b\",\"c\")");
  EQ_STR(f.v("B7").text, "a-b-c");
  f.set("B8", "=TRIM(\"  a   b  \")");
  EQ_STR(f.v("B8").text, "a b");
  f.set("B9", "=FIND(\"W\",A1)");
  EQ_NUM(f.n("B9"), 7);
  f.set("B10", "=TEXT(1234.5,\"#,##0.0\")");
  EQ_STR(f.v("B10").text, "1,234.5");
}

static void testLogical() {
  Fixture f;
  f.set("A1", "5");
  f.set("B1", "=IF(A1>3,\"big\",\"small\")");
  EQ_STR(f.v("B1").text, "big");
  f.set("B2", "=IFERROR(1/0,\"caught\")");
  EQ_STR(f.v("B2").text, "caught");
  f.set("B3", "=LET(x,A1*2,y,x+1,y)");
  EQ_NUM(f.n("B3"), 11);
  f.set("B4", "=IF(A1=0,1,A1/1)");
  EQ_NUM(f.n("B4"), 5);
  f.set("B5", "=IF(A1=0,\"zero\",1/A1)");
  EQ_NUM(f.n("B5"), 0.2);
  f.set("B6", "=AND(A1>1,A1<10)");
  CHECK(f.v("B6").b);
  f.set("B7", "=OR(A1<0,A1>3)");
  CHECK(f.v("B7").b);
  f.set("B8", "=SWITCH(A1,1,\"one\",5,\"five\",\"other\")");
  EQ_STR(f.v("B8").text, "five");
  f.set("B9", "=CHOOSE(2,\"a\",\"b\",\"c\")");
  EQ_STR(f.v("B9").text, "b");
}

static void testLookup() {
  Fixture f;
  f.set("A1", "id");
  f.set("B1", "name");
  f.set("C1", "score");
  f.set("A2", "1");
  f.set("B2", "Ann");
  f.set("C2", "90");
  f.set("A3", "2");
  f.set("B3", "Bob");
  f.set("C3", "80");
  f.set("A4", "3");
  f.set("B4", "Cid");
  f.set("C4", "70");
  f.set("E1", "=VLOOKUP(2,A2:C4,2,FALSE)");
  EQ_STR(f.v("E1").text, "Bob");
  f.set("E2", "=VLOOKUP(2.5,A2:C4,3,TRUE)");
  EQ_NUM(f.n("E2"), 80);
  f.set("E3", "=INDEX(B2:B4,MATCH(3,A2:A4,0))");
  EQ_STR(f.v("E3").text, "Cid");
  f.set("E4", "=XLOOKUP(9,A2:A4,B2:B4,\"missing\")");
  EQ_STR(f.v("E4").text, "missing");
  f.set("E5", "=MATCH(\"Bob\",B2:B4,0)");
  EQ_NUM(f.n("E5"), 2);
  f.set("E6", "=ROWS(A1:C4)");
  EQ_NUM(f.n("E6"), 4);
  f.set("E7", "=COLUMNS(A1:C4)");
  EQ_NUM(f.n("E7"), 3);
}

static void testDates() {
  Fixture f;
  f.set("A1", "=DATE(2024,1,15)");
  double serial = f.n("A1");
  EQ_STR(f.shown("A1"), "45306");
  f.set("B1", "=YEAR(A1)");
  EQ_NUM(f.n("B1"), 2024);
  f.set("B2", "=MONTH(A1)");
  EQ_NUM(f.n("B2"), 1);
  f.set("B3", "=DAY(A1)");
  EQ_NUM(f.n("B3"), 15);
  f.set("A2", "=DATE(2024,1,31)");
  f.set("B4", "=EOMONTH(A2,1)");
  int y, m, d, hh, mi, ss;
  serialToDate(f.n("B4"), y, m, d, hh, mi, ss);
  CHECK(m == 2 && d == 29);
  f.set("B5", "=DATEDIF(DATE(2020,1,15),DATE(2024,3,10),\"Y\")");
  EQ_NUM(f.n("B5"), 4);
  f.set("B6", "=WEEKDAY(DATE(2024,1,15))");
  EQ_NUM(f.n("B6"), 2);
  EQ_NUM(serial, 45306);
}

static void testFinancial() {
  Fixture f;
  f.set("A1", "=PMT(0.05/12,360,-300000)");
  EQ_NUM(std::round(f.n("A1") * 100) / 100, 1610.46);
  f.set("A2", "=FV(0.05/12,12,-100)");
  EQ_NUM(std::round(f.n("A2") * 100) / 100, 1227.89);
  f.set("A3", "=NPV(0.1,100,100)");
  EQ_NUM(std::round(f.n("A3") * 100) / 100, 173.55);
}

static void testErrorsAndCircular() {
  Fixture f;
  f.set("A1", "=1/0");
  CHECK(f.v("A1").err == Err::Div0);
  f.set("A2", "=\"a\"*1");
  CHECK(f.v("A2").isError());
  f.set("A3", "=NOPE()");
  CHECK(f.v("A3").err == Err::Name);
  f.set("A4", "=SQRTT(4)");
  CHECK(f.v("A4").err == Err::Name);
  f.set("A5", "=ISERROR(1/0)");
  CHECK(f.v("A5").b);
  f.set("A6", "=IFNA(1/1,\"x\")");
  EQ_NUM(f.n("A6"), 1);
  f.set("C1", "=C2");
  f.set("C2", "=C1");
  CHECK(f.wb.computed(0, 0, 2).circular || f.wb.computed(0, 1, 2).circular);
}

static void testSheetsAndRecalc() {
  Workbook wb;
  int s2 = wb.addSheet("Data");
  wb.setRaw(s2, 0, 0, "10");
  wb.setRaw(s2, 1, 0, "20");
  wb.setRaw(0, 0, 0, "=SUM(Data!A1:A2)");
  EQ_NUM(wb.value(0, 0, 0).num, 30);

  wb.setRaw(0, 1, 0, "1");
  wb.setRaw(0, 2, 0, "=A2*2");
  wb.setRaw(0, 3, 0, "=A3+1");
  EQ_NUM(wb.value(0, 3, 0).num, 3);
  wb.setRaw(0, 1, 0, "10");
  EQ_NUM(wb.value(0, 2, 0).num, 20);
  EQ_NUM(wb.value(0, 3, 0).num, 21);

  wb.insertRows(0, 1, 1);
  CHECK(wb.raw(0, 1, 0).empty());
  EQ_STR(wb.raw(0, 2, 0), "10");
}

static void testFormatting() {
  EQ_STR(formatValue(Value::number(1234.5), "#,##0.00"), "1,234.50");
  EQ_STR(formatValue(Value::number(0.5), "0%"), "50%");
  EQ_STR(formatValue(Value::number(-1234), "#,##0;(#,##0)"), "(1,234)");
  EQ_STR(formatValue(Value::number(1234.5), "\"$\"#,##0.00"), "$1,234.50");
  double serial = dateToSerial(2024, 1, 15);
  EQ_STR(formatValue(Value::number(serial), "yyyy-mm-dd"), "2024-01-15");
  EQ_STR(formatValue(Value::number(serial), "dd/mm/yyyy"), "15/01/2024");
  double d2 = dateToSerial(2024, 3, 5);
  EQ_STR(formatValue(Value::number(d2), "mmm d, yyyy"), "Mar 5, 2024");
  EQ_STR(formatValue(Value::number(d2), "dddd, mmmm d, yyyy"), "Tuesday, March 5, 2024");
  EQ_STR(formatValue(Value::number(1234.5), "$#,##0.00"), "$1,234.50");
  EQ_STR(formatValue(Value::number(1234.5), "0.00E+00"), "1.23E+03");
  EQ_STR(formatValue(Value::number(0), "0.00E+00"), "0.00E+00");
  EQ_STR(formatValue(Value::string("abc"), "@"), "abc");
  EQ_STR(formatValue(Value::number(12.5), "0.0"), "12.5");
  EQ_STR(formatValue(Value::number(1234.5), "#,##0"), "1,235");
  EQ_STR(formatValue(Value::number(0.125), "0.0%"), "12.5%");
  EQ_STR(formatValue(Value::number(1234567), "#,##0,,"), "1");
}

static void testShiftFormula() {
  EQ_STR(shiftFormula("=A1+B2", 1, 0), "=A2+B3");
  EQ_STR(shiftFormula("=$A$1+B2", 1, 0), "=$A$1+B3");
  EQ_STR(shiftFormula("=SUM(A1:A3)", 0, 1), "=SUM(B1:B3)");
}


static void testInsertDeleteRefs() {
  {
    Workbook wb;
    wb.setRaw(0, 0, 0, "1");
    wb.setRaw(0, 1, 0, "2");
    wb.setRaw(0, 2, 0, "=SUM(A1:A2)");
    wb.insertRows(0, 0, 1);
    EQ_NUM(wb.value(0, 3, 0).num, 3);
    EQ_STR(wb.raw(0, 3, 0), "=SUM(A2:A3)");
  }
  {
    Workbook wb;
    wb.setRaw(0, 0, 0, "5");
    wb.setRaw(0, 1, 0, "=A1*2");
    wb.insertRows(0, 5, 2);  // below: untouched
    EQ_STR(wb.raw(0, 1, 0), "=A1*2");
    wb.deleteRows(0, 0, 1);
    CHECK(wb.raw(0, 0, 0) == "=#REF!*2" || wb.raw(0, 0, 0) == "=#REF!*2");
  }
  {
    Workbook wb;
    wb.addSheet("Data");
    wb.setRaw(1, 0, 0, "7");
    wb.setRaw(0, 0, 0, "=Data!A1+1");
    wb.insertRows(1, 0, 1);
    EQ_NUM(wb.value(0, 0, 0).num, 8);
  }
  {
    Workbook wb;
    wb.setRaw(0, 0, 0, "1");
    wb.setRaw(0, 0, 1, "2");
    wb.setRaw(0, 0, 2, "=A1+B1");
    wb.insertCols(0, 1, 1);
    EQ_STR(wb.raw(0, 0, 3), "=A1+C1");
    EQ_STR(wb.raw(0, 0, 2), "2");
    EQ_NUM(wb.value(0, 0, 3).num, 3);
  }
  {
    Workbook wb;
    wb.setRaw(0, 0, 0, "1");
    wb.setRaw(0, 1, 0, "2");
    wb.setRaw(0, 2, 0, "=SUM(A1:A2)");
    wb.deleteRows(0, 0, 1);
    EQ_NUM(wb.value(0, 1, 0).num, 2);
    EQ_STR(wb.raw(0, 1, 0), "=SUM(A1:A1)");
  }
}

static void testSnapshotRoundTrip() {
  Workbook wb;
  wb.addSheet("Second");
  wb.setRaw(0, 0, 0, "=1+1");
  wb.setRaw(1, 2, 3, "'0042");
  Style st;
  st.bold = true;
  st.italic = true;
  st.underline = true;
  st.wrap = true;
  st.align = 1;
  st.fill = 0x00FFCC00;
  st.color = 0x000000FF;
  st.border = 5;
  st.numFmt = "0.00%";
  st.fontName = "Georgia";
  st.fontSize = 14;
  wb.setStyle(0, 3, 1, st);
  wb.setColWidth(0, 2, 150);
  wb.setRowHeight(0, 4, 42);

  std::string snap = wb.serialize();
  Workbook back;
  CHECK(back.deserialize(snap));
  EQ_STR(back.raw(0, 0, 0), "=1+1");
  EQ_STR(back.raw(1, 2, 3), "'0042");
  Style bs = back.styleAt(0, 3, 1);
  CHECK(bs.bold && bs.italic && bs.underline && bs.wrap);
  EQ_NUM(bs.align, 1);
  EQ_NUM((double)bs.fill, (double)0x00FFCC00);
  EQ_NUM((double)bs.color, (double)0x000000FF);
  EQ_NUM(bs.border, 5);
  EQ_STR(bs.numFmt, "0.00%");
  EQ_STR(bs.fontName, "Georgia");
  EQ_NUM(bs.fontSize, 14);
  EQ_NUM(back.colWidth(0, 2), 150);
  EQ_NUM(back.rowHeight(0, 4), 42);
  EQ_NUM(back.sheetCount(), 2);
  EQ_STR(back.sheet(1).name, "Second");
  CHECK(back.serialize() == snap);
}

int main() {
  testArithmetic();
  testCoercion();
  testFunctions();
  testCriteria();
  testText();
  testLogical();
  testLookup();
  testDates();
  testFinancial();
  testErrorsAndCircular();
  testSheetsAndRecalc();
  testFormatting();
  testShiftFormula();
  testInsertDeleteRefs();
  testSnapshotRoundTrip();

  {
    std::string report;
    int failures = selftest::runEngineSelfTest(report, "/tmp/grid-selftest.grid");
    if (failures != 0) std::printf("%s\n", report.c_str());
    checkImpl(failures == 0, "embedded self-test", std::to_string(failures) + " failures", "0 failures", __LINE__);
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
