/**
 * Formula tokenizer.
 *
 * Excel's grammar is small but its lexer is subtle: `A1` is a reference while
 * `LOG10` is a function, `A:A` is a range, `'My Sheet'!$B$2` is a qualified
 * reference, and `1:3` is a row range. This lexer resolves those shapes into
 * structured reference tokens so the parser never has to guess.
 */

import {
  AreaRef,
  CellAddr,
  MAX_COL_INDEX,
  MAX_ROW_INDEX,
  colToIndex,
  parseArea,
} from './address';
import { CellError, ErrorCode, isErrorCode } from './errors';

export type TokenType =
  | 'num'
  | 'str'
  | 'bool'
  | 'err'
  | 'ref'
  | 'name'
  | 'func'
  | 'op'
  | 'lparen'
  | 'rparen'
  | 'comma'
  | 'semi'
  | 'lbrace'
  | 'rbrace'
  | 'eof';

export interface Token {
  type: TokenType;
  value?: string | number | boolean | AreaRef | CellError;
  pos: number;
}

export class FormulaSyntaxError extends Error {
  constructor(message: string, readonly pos: number) {
    super(message);
    this.name = 'FormulaSyntaxError';
  }
}

const OPERATORS = ['<>', '<=', '>=', '=', '<', '>', '+', '-', '*', '/', '^', '&', '%'];

const isDigit = (c: string) => c >= '0' && c <= '9';
const isLetter = (c: string) => /[A-Za-z]/.test(c);
const isIdStart = (c: string) => /[A-Za-z_\\]/.test(c);
const isIdChar = (c: string) => /[A-Za-z0-9_.\\]/.test(c);

export function tokenize(src: string): Token[] {
  const tokens: Token[] = [];
  let i = 0;
  const n = src.length;

  const skipSpace = () => {
    while (i < n && (src[i] === ' ' || src[i] === '\t' || src[i] === '\n' || src[i] === '\r')) i++;
  };

  const push = (type: TokenType, value?: Token['value'], pos = i) => {
    tokens.push({ type, value, pos });
  };

  /** Read a cell / column / row address at the cursor, honoring `$`. */
  const readAddress = (): { kind: 'cell' | 'col' | 'row'; addr: CellAddr } | null => {
    const start = i;
    let colAbs = false;
    if (src[i] === '$') {
      colAbs = true;
      i++;
    }
    let letters = '';
    while (i < n && isLetter(src[i])) {
      letters += src[i];
      i++;
    }
    let rowAbs = false;
    const hasRowDollar = src[i] === '$';
    if (hasRowDollar) {
      rowAbs = true;
      i++;
    }
    let digits = '';
    while (i < n && isDigit(src[i])) {
      digits += src[i];
      i++;
    }
    if (letters && digits) {
      const col = colToIndex(letters);
      const row = parseInt(digits, 10) - 1;
      if (col >= 0 && col <= MAX_COL_INDEX && row >= 0 && row <= MAX_ROW_INDEX) {
        return { kind: 'cell', addr: { row, col, rowAbs, colAbs } };
      }
    } else if (letters && !digits && !hasRowDollar) {
      const col = colToIndex(letters);
      if (col >= 0 && col <= MAX_COL_INDEX) {
        return { kind: 'col', addr: { row: 0, col, rowAbs: false, colAbs } };
      }
    } else if (digits && !letters && !colAbs) {
      const row = parseInt(digits, 10) - 1;
      if (row >= 0 && row <= MAX_ROW_INDEX) {
        return { kind: 'row', addr: { row, col: 0, rowAbs, colAbs: false } };
      }
    }
    i = start;
    return null;
  };

  /** After the first half of a reference, absorb `:second-half` if present. */
  const finishArea = (first: { kind: 'cell' | 'col' | 'row'; addr: CellAddr }, sheet?: string): AreaRef | null => {
    const save = i;
    skipSpace();
    if (src[i] === ':') {
      i++;
      skipSpace();
      const second = readAddress();
      if (second) {
        if (first.kind === 'cell' && second.kind === 'cell') {
          return { sheet, start: first.addr, end: second.addr };
        }
        if (first.kind === 'col' && second.kind === 'col') {
          return {
            sheet,
            start: { ...first.addr, row: 0 },
            end: { ...second.addr, row: MAX_ROW_INDEX },
            wholeCol: true,
          };
        }
        if (first.kind === 'row' && second.kind === 'row') {
          return {
            sheet,
            start: { ...first.addr, col: 0 },
            end: { ...second.addr, col: MAX_COL_INDEX },
            wholeRow: true,
          };
        }
      }
      i = save;
      return null;
    }
    i = save;
    // A bare column ("A") or row ("1") is not a range; only A:A / 1:1 are.
    if (first.kind === 'cell') {
      return { sheet, start: first.addr, end: { ...first.addr } };
    }
    return null;
  };

  while (i < n) {
    skipSpace();
    if (i >= n) break;
    const ch = src[i];
    const pos = i;

    // String literal
    if (ch === '"') {
      i++;
      let s = '';
      let closed = false;
      while (i < n) {
        if (src[i] === '"') {
          if (src[i + 1] === '"') {
            s += '"';
            i += 2;
            continue;
          }
          i++;
          closed = true;
          break;
        }
        s += src[i];
        i++;
      }
      if (!closed) throw new FormulaSyntaxError('Unterminated string literal', pos);
      push('str', s, pos);
      continue;
    }

    // Quoted sheet name: 'Sheet 1'!A1
    if (ch === "'") {
      i++;
      let name = '';
      let closed = false;
      while (i < n) {
        if (src[i] === "'") {
          if (src[i + 1] === "'") {
            name += "'";
            i += 2;
            continue;
          }
          i++;
          closed = true;
          break;
        }
        name += src[i];
        i++;
      }
      if (!closed) throw new FormulaSyntaxError('Unterminated sheet name', pos);
      skipSpace();
      if (src[i] !== '!') throw new FormulaSyntaxError('Expected ! after sheet name', i);
      i++;
      const addr = readAddress();
      if (!addr) {
        // Quoted sheet + defined name, e.g. 'Sheet 1'!MyName
        let id = '';
        while (i < n && isIdChar(src[i])) id += src[i++];
        if (!id) throw new FormulaSyntaxError('Expected reference after sheet name', i);
        push('name', `${name}!${id}`, pos);
        continue;
      }
      const area = finishArea(addr, name);
      if (!area) throw new FormulaSyntaxError('Invalid reference', i);
      push('ref', area, pos);
      continue;
    }

    // Error literal
    if (ch === '#') {
      let code = '';
      while (i < n && /[A-Z0-9/!?_]/.test(src[i])) code += src[i++];
      if (code === '#N' && src[i] === '/') {
        code += src[i++];
      }
      if (!isErrorCode(code)) throw new FormulaSyntaxError(`Unknown error literal ${code}`, pos);
      push('err', CellError.byCode(code as ErrorCode), pos);
      continue;
    }

    // Number, or a row range like 1:3
    if (isDigit(ch) || (ch === '.' && isDigit(src[i + 1] ?? ''))) {
      const save = i;
      const rowish = readAddress();
      if (rowish && rowish.kind === 'row') {
        const area = finishArea(rowish);
        if (area && area.wholeRow) {
          push('ref', area, pos);
          continue;
        }
      }
      i = save;
      let text = '';
      while (i < n && isDigit(src[i])) text += src[i++];
      if (src[i] === '.') {
        text += '.';
        i++;
        while (i < n && isDigit(src[i])) text += src[i++];
      }
      if (src[i] === 'e' || src[i] === 'E') {
        let exp = '';
        let j = i + 1;
        if (src[j] === '+' || src[j] === '-') exp += src[j++];
        if (isDigit(src[j] ?? '')) {
          while (j < n && isDigit(src[j])) exp += src[j++];
          text += 'e' + exp;
          i = j;
        }
      }
      const value = parseFloat(text);
      if (Number.isNaN(value)) throw new FormulaSyntaxError(`Bad number ${text}`, pos);
      push('num', value, pos);
      continue;
    }

    // Sheet-qualified or bare reference / name / function
    if (isIdStart(ch) || ch === '$') {
      const addr0 = readAddress();
      if (addr0) {
        // Could be a 3-D reference: Sheet1:Sheet3!A1 -- but an address path
        // reached here without a `!`, so it is a plain reference.
        const area = finishArea(addr0);
        if (area) {
          push('ref', area, pos);
          continue;
        }
        i = pos;
      }
      if (ch === '$') {
        throw new FormulaSyntaxError('Invalid reference', pos);
      }
      let id = '';
      while (i < n && isIdChar(src[i])) id += src[i++];
      if (!id) throw new FormulaSyntaxError(`Unexpected character ${ch}`, pos);

      // Sheet prefix?
      skipSpace();
      if (src[i] === '!' && !src.slice(0, i).endsWith(' ')) {
        i++;
        const addr = readAddress();
        if (!addr) {
          let name = '';
          while (i < n && isIdChar(src[i])) name += src[i++];
          if (!name) throw new FormulaSyntaxError('Expected reference after sheet name', i);
          push('name', `${id}!${name}`, pos);
          continue;
        }
        const area = finishArea(addr, id);
        if (!area) throw new FormulaSyntaxError('Invalid reference', i);
        push('ref', area, pos);
        continue;
      }

      // 3-D reference Sheet1:Sheet3!A1
      if (src[i] === ':') {
        const save = i;
        i++;
        skipSpace();
        let id2 = '';
        while (i < n && isIdChar(src[i])) id2 += src[i++];
        if (id2 && src[i] === '!') {
          i++;
          const addr = readAddress();
          if (addr) {
            const area = finishArea(addr, id);
            if (area) {
              push('ref', { ...area, sheetEnd: id2 }, pos);
              continue;
            }
          }
        }
        i = save;
      }

      if (src[i] === '(') {
        push('func', id.toUpperCase(), pos);
        continue;
      }
      if (id.toUpperCase() === 'TRUE' || id.toUpperCase() === 'FALSE') {
        push('bool', id.toUpperCase() === 'TRUE', pos);
        continue;
      }
      push('name', id, pos);
      continue;
    }

    if (ch === '(') {
      i++;
      push('lparen', undefined, pos);
      continue;
    }
    if (ch === ')') {
      i++;
      push('rparen', undefined, pos);
      continue;
    }
    if (ch === ',') {
      i++;
      push('comma', undefined, pos);
      continue;
    }
    if (ch === ';') {
      i++;
      push('semi', undefined, pos);
      continue;
    }
    if (ch === '{') {
      i++;
      push('lbrace', undefined, pos);
      continue;
    }
    if (ch === '}') {
      i++;
      push('rbrace', undefined, pos);
      continue;
    }

    let matched = false;
    for (const op of OPERATORS) {
      if (src.startsWith(op, i)) {
        i += op.length;
        push('op', op, pos);
        matched = true;
        break;
      }
    }
    if (matched) continue;

    if (ch === ':') throw new FormulaSyntaxError('Unexpected range operator', pos);
    throw new FormulaSyntaxError(`Unexpected character ${ch}`, pos);
  }

  tokens.push({ type: 'eof', pos: n });
  return tokens;
}

/** Re-parse a plain A1/range string (used by the Name Box and INDIRECT). */
export function parseAreaText(text: string, currentSheet?: string): AreaRef | null {
  const trimmed = text.trim();
  const bang = trimmed.lastIndexOf('!');
  if (bang >= 0) {
    let sheet = trimmed.slice(0, bang).trim();
    if (sheet.startsWith("'") && sheet.endsWith("'")) sheet = sheet.slice(1, -1).replace(/''/g, "'");
    const area = parseArea(trimmed.slice(bang + 1));
    return area ? { ...area, sheet } : null;
  }
  const area = parseArea(trimmed);
  return area ? { ...area, sheet: currentSheet } : null;
}
