/**
 * Excel-compatible error values.
 * Grid mirrors Excel's error literals so formulas and files interoperate.
 */

export type ErrorCode =
  | '#NULL!'
  | '#DIV/0!'
  | '#VALUE!'
  | '#REF!'
  | '#NAME?'
  | '#NUM!'
  | '#N/A'
  | '#SPILL!'
  | '#CALC!';

const CODES: ErrorCode[] = [
  '#NULL!',
  '#DIV/0!',
  '#VALUE!',
  '#REF!',
  '#NAME?',
  '#NUM!',
  '#N/A',
  '#SPILL!',
  '#CALC!',
];

/** An Excel error value. Immutable and interned so identity comparison works. */
export class CellError {
  readonly isCellError = true as const;
  private constructor(readonly code: ErrorCode) {}
  toString(): string {
    return this.code;
  }
  static readonly NULL = new CellError('#NULL!');
  static readonly DIV0 = new CellError('#DIV/0!');
  static readonly VALUE = new CellError('#VALUE!');
  static readonly REF = new CellError('#REF!');
  static readonly NAME = new CellError('#NAME?');
  static readonly NUM = new CellError('#NUM!');
  static readonly NA = new CellError('#N/A');
  static readonly SPILL = new CellError('#SPILL!');
  static readonly CALC = new CellError('#CALC!');

  static byCode(code: string): CellError {
    switch (code) {
      case '#NULL!':
        return CellError.NULL;
      case '#DIV/0!':
        return CellError.DIV0;
      case '#VALUE!':
        return CellError.VALUE;
      case '#REF!':
        return CellError.REF;
      case '#NAME?':
        return CellError.NAME;
      case '#NUM!':
        return CellError.NUM;
      case '#N/A':
        return CellError.NA;
      case '#SPILL!':
        return CellError.SPILL;
      case '#CALC!':
        return CellError.CALC;
      default:
        return CellError.VALUE;
    }
  }
}

export const ERR = CellError;

export function isError(v: unknown): v is CellError {
  return v instanceof CellError;
}

export function isErrorCode(s: string): s is ErrorCode {
  return (CODES as string[]).includes(s.toUpperCase());
}

/**
 * Thrown by formulas/coercions. Caught at the function boundary and turned
 * back into an error value, so library code can be written with plain throws.
 */
export class FormulaError extends Error {
  constructor(readonly error: CellError) {
    super(error.code);
    this.name = 'FormulaError';
  }
}

export function fail(e: CellError): never {
  throw new FormulaError(e);
}
