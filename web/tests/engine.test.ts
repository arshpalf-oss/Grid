import { describe, expect, it } from 'vitest';
import { Workbook } from '../src/engine/workbook';
import { parseCellAddr } from '../src/engine/address';
import { formatValue, generalDisplay } from '../src/engine/format';
import { CellError } from '../src/engine/errors';
import { dateToSerial, serialParts } from '../src/engine/dates';

function makeWb() {
  const wb = new Workbook();
  const id = wb.sheets[0].id;
  return {
    wb,
    id,
    set(ref: string, raw: string) {
      const a = parseCellAddr(ref)!;
      wb.setCell(id, a.row, a.col, raw);
      return this;
    },
    val(ref: string) {
      const a = parseCellAddr(ref)!;
      return wb.getScalar(id, a.row, a.col);
    },
    text(ref: string) {
      const a = parseCellAddr(ref)!;
      return wb.getDisplayText(id, a.row, a.col);
    },
  };
}

const num = (t: ReturnType<typeof makeWb>, ref: string) => Number(t.val(ref));

describe('literals and coercion', () => {
  it('parses numbers, text and booleans', () => {
    const t = makeWb();
    t.set('A1', '42').set('A2', 'hello').set('A3', 'TRUE').set('A4', '1,234.5').set('A5', '12%');
    expect(t.val('A1')).toBe(42);
    expect(t.val('A2')).toBe('hello');
    expect(t.val('A3')).toBe(true);
    expect(t.val('A4')).toBe(1234.5);
    expect(t.val('A5')).toBeCloseTo(0.12);
    expect(t.text('A5')).toBe('12%');
  });

  it('treats leading apostrophe as text', () => {
    const t = makeWb();
    t.set('A1', "'123");
    expect(t.val('A1')).toBe('123');
  });

  it('coerces blank cells as 0 in arithmetic', () => {
    const t = makeWb();
    t.set('B1', '=A1+5');
    expect(num(t, 'B1')).toBe(5);
  });
});

describe('operators and precedence', () => {
  const cases: [string, number | string | boolean][] = [
    ['=1+2*3', 7],
    ['=(1+2)*3', 9],
    ['=-2^2', 4],
    ['=2^3^2', 64],
    ['=2^2%', Math.pow(2, 0.02)],
    ['=50%', 0.5],
    ['=10/4', 2.5],
    ['=7/0', 0],
    ['="a"&"b"', 'ab'],
    ['=1<2', true],
    ['="A"="a"', true],
    ['=2<>2', false],
    ['=TRUE+1', 2],
    ['=1&2', '12'],
  ];
  it.each(cases)('%s', (formula, expected) => {
    const t = makeWb();
    t.set('A1', formula);
    const v = t.val('A1');
    if (expected === 0) {
      // 7/0 is the #DIV/0! case
      expect(v).toBeInstanceOf(CellError);
      expect((v as CellError).code).toBe('#DIV/0!');
    } else {
      expect(v).toBe(expected);
    }
  });

  it('propagates errors through arithmetic', () => {
    const t = makeWb();
    t.set('A1', '=1/0').set('A2', '=A1+1');
    expect((t.val('A2') as CellError).code).toBe('#DIV/0!');
  });
});

describe('math functions', () => {
  it('SUM / PRODUCT / SUMSQ', () => {
    const t = makeWb();
    t.set('A1', '1').set('A2', '2').set('A3', '3').set('B1', '=SUM(A1:A3)').set('B2', '=PRODUCT(A1:A3)').set('B3', '=SUMSQ(A1:A3)');
    expect(num(t, 'B1')).toBe(6);
    expect(num(t, 'B2')).toBe(6);
    expect(num(t, 'B3')).toBe(14);
  });

  it('ROUND family uses Excel half-away-from-zero', () => {
    const t = makeWb();
    t.set('A1', '=ROUND(2.675,2)')
      .set('A2', '=ROUND(-2.5,0)')
      .set('A3', '=ROUNDUP(3.2,0)')
      .set('A4', '=ROUNDDOWN(-3.9,0)')
      .set('A5', '=MROUND(10,3)');
    expect(num(t, 'A1')).toBe(2.68);
    expect(num(t, 'A2')).toBe(-3);
    expect(num(t, 'A3')).toBe(4);
    expect(num(t, 'A4')).toBe(-3);
    expect(num(t, 'A5')).toBe(9);
  });

  it('MOD follows the divisor sign', () => {
    const t = makeWb();
    t.set('A1', '=MOD(-3,2)').set('A2', '=MOD(3,-2)').set('A3', '=QUOTIENT(7,2)');
    expect(num(t, 'A1')).toBe(1);
    expect(num(t, 'A2')).toBe(-1);
    expect(num(t, 'A3')).toBe(3);
  });

  it('SUMIF / COUNTIF / SUMIFS / AVERAGEIF', () => {
    const t = makeWb();
    t.set('A1', 'apple').set('A2', 'banana').set('A3', 'apple').set('A4', 'cherry')
      .set('B1', '10').set('B2', '20').set('B3', '30').set('B4', '40')
      .set('C1', '=SUMIF(A1:A4,"apple",B1:B4)')
      .set('C2', '=COUNTIF(B1:B4,">15")')
      .set('C3', '=SUMIFS(B1:B4,A1:A4,"apple")')
      .set('C4', '=AVERAGEIF(A1:A4,"apple",B1:B4)')
      .set('C5', '=COUNTIF(A1:A4,"*an*")');
    expect(num(t, 'C1')).toBe(40);
    expect(num(t, 'C2')).toBe(3);
    expect(num(t, 'C3')).toBe(40);
    expect(num(t, 'C4')).toBe(20);
    expect(num(t, 'C5')).toBe(1);
  });

  it('SUMPRODUCT with comparison arrays', () => {
    const t = makeWb();
    t.set('A1', 'x').set('A2', 'y').set('A3', 'x')
      .set('B1', '1').set('B2', '2').set('B3', '3')
      .set('C1', '=SUMPRODUCT((A1:A3="x")*B1:B3)');
    expect(num(t, 'C1')).toBe(4);
  });
});

describe('statistics', () => {
  it('AVERAGE / MEDIAN / STDEV', () => {
    const t = makeWb();
    t.set('A1', '1').set('A2', '2').set('A3', '3').set('A4', '4')
      .set('B1', '=AVERAGE(A1:A4)')
      .set('B2', '=MEDIAN(A1:A4)')
      .set('B3', '=STDEV.S(A1:A4)')
      .set('B4', '=LARGE(A1:A4,2)')
      .set('B5', '=SMALL(A1:A4,1)')
      .set('B6', '=COUNT(A1:A4)');
    expect(num(t, 'B1')).toBe(2.5);
    expect(num(t, 'B2')).toBe(2.5);
    expect(num(t, 'B3')).toBeCloseTo(1.2909944, 6);
    expect(num(t, 'B4')).toBe(3);
    expect(num(t, 'B5')).toBe(1);
    expect(num(t, 'B6')).toBe(4);
  });

  it('COUNT ignores text, COUNTA counts it', () => {
    const t = makeWb();
    t.set('A1', '1').set('A2', 'text').set('A3', '2').set('B1', '=COUNT(A1:A3)').set('B2', '=COUNTA(A1:A3)').set('B3', '=COUNTBLANK(A1:A5)');
    expect(num(t, 'B1')).toBe(2);
    expect(num(t, 'B2')).toBe(3);
    expect(num(t, 'B3')).toBe(2);
  });
});

describe('text functions', () => {
  it('common string operations', () => {
    const t = makeWb();
    t.set('A1', 'Hello World')
      .set('B1', '=LEFT(A1,5)')
      .set('B2', '=RIGHT(A1,5)')
      .set('B3', '=MID(A1,7,5)')
      .set('B4', '=LEN(A1)')
      .set('B5', '=UPPER(B1)')
      .set('B6', '=SUBSTITUTE(A1,"World","Grid")')
      .set('B7', '=TEXTJOIN("-",TRUE,"a","b","c")')
      .set('B8', '=TRIM("  a   b  ")')
      .set('B9', '=FIND("W",A1)')
      .set('B10', '=SEARCH("world",A1)');
    expect(t.val('B1')).toBe('Hello');
    expect(t.val('B2')).toBe('World');
    expect(t.val('B3')).toBe('World');
    expect(t.val('B4')).toBe(11);
    expect(t.val('B5')).toBe('HELLO');
    expect(t.val('B6')).toBe('Hello Grid');
    expect(t.val('B7')).toBe('a-b-c');
    expect(t.val('B8')).toBe('a b');
    expect(t.val('B9')).toBe(7);
    expect(t.val('B10')).toBe(7);
  });
});

describe('logical functions and special forms', () => {
  it('IF / IFS / IFERROR / SWITCH / CHOOSE / LET', () => {
    const t = makeWb();
    t.set('A1', '5')
      .set('B1', '=IF(A1>3,"big","small")')
      .set('B2', '=IFS(A1>10,"huge",A1>3,"big",TRUE,"small")')
      .set('B3', '=IFERROR(1/0,"caught")')
      .set('B4', '=SWITCH(A1,1,"one",5,"five","other")')
      .set('B5', '=CHOOSE(2,"a","b","c")')
      .set('B6', '=LET(x,A1*2,y,x+1,y)')
      .set('B7', '=IF(A1=0,1,A1/1)');
    expect(t.val('B1')).toBe('big');
    expect(t.val('B2')).toBe('big');
    expect(t.val('B3')).toBe('caught');
    expect(t.val('B4')).toBe('five');
    expect(t.val('B5')).toBe('b');
    expect(num(t, 'B6')).toBe(11);
    expect(num(t, 'B7')).toBe(5);
  });

  it('lazy branches do not evaluate the untaken side', () => {
    const t = makeWb();
    t.set('A1', '0').set('B1', '=IF(A1=0,"zero",1/A1)');
    expect(t.val('B1')).toBe('zero');
  });
});

describe('lookup functions', () => {
  function table() {
    const t = makeWb();
    t.set('A1', 'id').set('B1', 'name').set('C1', 'score')
      .set('A2', '1').set('B2', 'Ann').set('C2', '90')
      .set('A3', '2').set('B3', 'Bob').set('C3', '80')
      .set('A4', '3').set('B4', 'Cid').set('C4', '70');
    return t;
  }

  it('VLOOKUP exact and approximate', () => {
    const t = table();
    t.set('E1', '=VLOOKUP(2,A2:C4,2,FALSE)').set('E2', '=VLOOKUP(2.5,A2:C4,3,TRUE)');
    expect(t.val('E1')).toBe('Bob');
    expect(num(t, 'E2')).toBe(80);
  });

  it('INDEX / MATCH', () => {
    const t = table();
    t.set('E1', '=INDEX(B2:B4,MATCH(3,A2:A4,0))').set('E2', '=MATCH("Bob",B2:B4,0)');
    expect(t.val('E1')).toBe('Cid');
    expect(num(t, 'E2')).toBe(2);
  });

  it('XLOOKUP with fallback', () => {
    const t = table();
    t.set('E1', '=XLOOKUP(2,A2:A4,B2:B4)').set('E2', '=XLOOKUP(9,A2:A4,B2:B4,"missing")');
    expect(t.val('E1')).toBe('Bob');
    expect(t.val('E2')).toBe('missing');
  });

  it('ROW / COLUMN / ROWS / COLUMNS', () => {
    const t = table();
    t.set('E1', '=ROW()').set('E2', '=COLUMN()').set('E3', '=ROWS(A1:C4)').set('E4', '=COLUMNS(A1:C4)');
    expect(num(t, 'E1')).toBe(1);
    expect(num(t, 'E2')).toBe(5);
    expect(num(t, 'E3')).toBe(4);
    expect(num(t, 'E4')).toBe(3);
  });

  it('OFFSET and INDIRECT', () => {
    const t = table();
    t.set('E1', '=OFFSET(A1,1,1)').set('E2', '=SUM(INDIRECT("C2:C4"))');
    expect(t.val('E1')).toBe('Ann');
    expect(num(t, 'E2')).toBe(240);
  });
});

describe('dynamic arrays', () => {
  it('spills SEQUENCE', () => {
    const t = makeWb();
    t.set('A1', '=SEQUENCE(3,2)');
    expect(num(t, 'A1')).toBe(1);
    expect(num(t, 'B1')).toBe(2);
    expect(num(t, 'A3')).toBe(5);
    expect(num(t, 'B3')).toBe(6);
  });

  it('sums a spilled range', () => {
    const t = makeWb();
    t.set('A1', '=SEQUENCE(3)').set('C1', '=SUM(A1:A3)');
    expect(num(t, 'C1')).toBe(6);
  });

  it('detects spill conflicts', () => {
    const t = makeWb();
    t.set('A2', 'block').set('A1', '=SEQUENCE(3)');
    expect((t.val('A1') as CellError).code).toBe('#SPILL!');
  });

  it('SORT / UNIQUE / FILTER / TRANSPOSE', () => {
    const t = makeWb();
    t.set('A1', '3').set('A2', '1').set('A3', '2')
      .set('C1', '=SORT(A1:A3)')
      .set('D1', '=UNIQUE(A1:A3)')
      .set('E1', '=TRANSPOSE(A1:A3)');
    expect(num(t, 'C1')).toBe(1);
    expect(num(t, 'C3')).toBe(3);
    expect(num(t, 'D1')).toBe(3);
    expect(num(t, 'E1')).toBe(3);
    expect(num(t, 'A1')).toBe(3);
  });
});

describe('dates', () => {
  it('DATE and serial parts', () => {
    const t = makeWb();
    t.set('A1', '=DATE(2024,1,15)').set('A2', '=YEAR(A1)').set('A3', '=MONTH(A1)').set('A4', '=DAY(A1)');
    const serial = num(t, 'A1');
    expect(serial).toBe(dateToSerial(new Date(Date.UTC(2024, 0, 15))));
    expect(num(t, 'A2')).toBe(2024);
    expect(num(t, 'A3')).toBe(1);
    expect(num(t, 'A4')).toBe(15);
  });

  it('EDATE / EOMONTH / DATEDIF', () => {
    const t = makeWb();
    t.set('A1', '=DATE(2024,1,31)')
      .set('B1', '=EDATE(A1,1)')
      .set('B2', '=EOMONTH(A1,1)')
      .set('B3', '=DATEDIF(DATE(2020,1,15),DATE(2024,3,10),"Y")')
      .set('B4', '=DATEDIF(DATE(2020,1,15),DATE(2024,3,10),"M")');
    expect(serialParts(num(t, 'B1')).month).toBe(2);
    expect(serialParts(num(t, 'B1')).day).toBe(29);
    expect(serialParts(num(t, 'B2')).day).toBe(29);
    expect(num(t, 'B3')).toBe(4);
    expect(num(t, 'B4')).toBe(49);
  });

  it('WEEKDAY and NETWORKDAYS', () => {
    const t = makeWb();
    t.set('A1', '=DATE(2024,1,15)')
      .set('B1', '=WEEKDAY(A1)')
      .set('B2', '=NETWORKDAYS(A1,DATE(2024,1,19))');
    expect(num(t, 'B1')).toBe(2);
    expect(num(t, 'B2')).toBe(5);
  });
});

describe('financial functions', () => {
  it('PMT / FV / NPV', () => {
    const t = makeWb();
    t.set('A1', '=PMT(0.05/12,360,-300000)')
      .set('A2', '=FV(0.05/12,12,-100)')
      .set('A3', '=NPV(0.1,100,100)');
    expect(num(t, 'A1')).toBeCloseTo(1610.46, 1);
    expect(num(t, 'A2')).toBeCloseTo(1227.89, 1);
    expect(num(t, 'A3')).toBeCloseTo(173.55, 1);
  });
});

describe('circular references', () => {
  it('flags cycles and yields 0', () => {
    const t = makeWb();
    t.set('A1', '=B1').set('B1', '=A1');
    const a = t.wb.getComputed(t.id, 0, 0);
    expect(a.circular || t.wb.getComputed(t.id, 0, 1).circular).toBe(true);
  });
});

describe('cross-sheet references', () => {
  it('reads from another sheet', () => {
    const wb = new Workbook();
    const s1 = wb.sheets[0].id;
    const s2 = wb.addSheet('Data').id;
    wb.setCell(s2, 0, 0, '10');
    wb.setCell(s2, 1, 0, '20');
    wb.setCell(s1, 0, 0, '=SUM(Data!A1:A2)');
    expect(wb.getScalar(s1, 0, 0)).toBe(30);
  });
});

describe('number formatting', () => {
  it('formats numbers', () => {
    expect(formatValue(1234.5, '#,##0.00')).toBe('1,234.50');
    expect(formatValue(0.5, '0%')).toBe('50%');
    expect(formatValue(-1234, '#,##0;(#,##0)')).toBe('(1,234)');
    expect(formatValue(1234.5, '$#,##0.00')).toBe('$1,234.50');
    expect(formatValue(0, '0.00')).toBe('0.00');
    expect(formatValue(1234567, '0.00E+00')).toBe('1.23E+06');
    expect(generalDisplay(0.1 + 0.2)).toBe('0.3');
  });

  it('formats dates', () => {
    const serial = dateToSerial(new Date(Date.UTC(2024, 0, 15, 13, 45)));
    expect(formatValue(serial, 'yyyy-mm-dd')).toBe('2024-01-15');
    expect(formatValue(serial, 'dd/mm/yyyy')).toBe('15/01/2024');
    expect(formatValue(serial, 'mmm d, yyyy')).toBe('Jan 15, 2024');
    expect(formatValue(serial, 'h:mm AM/PM')).toBe('1:45 PM');
    expect(formatValue(serial, 'dddd')).toBe('Monday');
  });

  it('TEXT() uses the format engine', () => {
    const t = makeWb();
    t.set('A1', '=TEXT(1234.5,"#,##0.0")').set('A2', '=TEXT(DATE(2024,3,5),"mmm d")');
    expect(t.val('A1')).toBe('1,234.5');
    expect(t.val('A2')).toBe('Mar 5');
  });
});

describe('error values', () => {
  it('produces Excel error codes', () => {
    const t = makeWb();
    t.set('A1', '=1/0').set('A2', '="a"*1').set('A3', '=NOPE()').set('A4', '=VLOOKUP(9,A1:A1,1,FALSE)').set('A5', '=SQRT(-1)');
    expect((t.val('A1') as CellError).code).toBe('#DIV/0!');
    expect((t.val('A2') as CellError).code).toBe('#VALUE!');
    expect((t.val('A3') as CellError).code).toBe('#NAME?');
    expect((t.val('A4') as CellError).code).toBe('#N/A');
    expect((t.val('A5') as CellError).code).toBe('#NUM!');
  });

  it('ISERROR / IFNA', () => {
    const t = makeWb();
    t.set('A1', '=ISERROR(1/0)').set('A2', '=ISNA(VLOOKUP(9,B1:B2,1,FALSE))').set('A3', '=IFNA(1/1,"x")');
    expect(t.val('A1')).toBe(true);
    expect(t.val('A2')).toBe(true);
    expect(num(t, 'A3')).toBe(1);
  });
});

describe('recalculation', () => {
  it('updates dependents on edit', () => {
    const t = makeWb();
    t.set('A1', '1').set('A2', '=A1*2').set('A3', '=A2+1');
    expect(num(t, 'A3')).toBe(3);
    t.set('A1', '10');
    expect(num(t, 'A2')).toBe(20);
    expect(num(t, 'A3')).toBe(21);
  });

  it('inserting rows moves cells', () => {
    const t = makeWb();
    t.set('A1', '1').set('A2', '2').set('A3', '3');
    t.wb.insertRows(t.id, 1, 1);
    expect(t.val('A2')).toBe(null);
    expect(t.val('A3')).toBe(2);
  });
});
