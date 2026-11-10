/** Recursive-descent parser producing an AST with Excel operator precedence. */

import { AreaRef } from './address';
import { CellError, FormulaError } from './errors';
import { FormulaSyntaxError, Token, tokenize } from './tokenizer';

export type BinOp = '=' | '<>' | '<' | '>' | '<=' | '>=' | '&' | '+' | '-' | '*' | '/' | '^';

export type Node =
  | { type: 'num'; value: number }
  | { type: 'str'; value: string }
  | { type: 'bool'; value: boolean }
  | { type: 'err'; error: CellError }
  | { type: 'ref'; area: AreaRef }
  | { type: 'name'; name: string }
  | { type: 'func'; name: string; args: Node[] }
  | { type: 'bin'; op: BinOp; left: Node; right: Node }
  | { type: 'unary'; op: '+' | '-'; operand: Node }
  | { type: 'percent'; operand: Node }
  | { type: 'array'; rows: Node[][] }
  | { type: 'missing' };

export interface ParseResult {
  ast?: Node;
  error?: CellError;
  message?: string;
}

class Parser {
  private pos = 0;
  constructor(private readonly tokens: Token[]) {}

  private peek(offset = 0): Token {
    return this.tokens[Math.min(this.pos + offset, this.tokens.length - 1)];
  }
  private next(): Token {
    const t = this.tokens[this.pos];
    if (this.pos < this.tokens.length - 1) this.pos++;
    return t;
  }
  private isOp(...ops: string[]): boolean {
    const t = this.peek();
    return t.type === 'op' && ops.includes(t.value as string);
  }

  parse(): Node {
    const node = this.parseComparison();
    if (this.peek().type !== 'eof') {
      throw new FormulaSyntaxError('Unexpected token', this.peek().pos);
    }
    return node;
  }

  /** = <> < > <= >= (left associative) */
  private parseComparison(): Node {
    let left = this.parseConcat();
    while (this.isOp('=', '<>', '<', '>', '<=', '>=')) {
      const op = this.next().value as BinOp;
      const right = this.parseConcat();
      left = { type: 'bin', op, left, right };
    }
    return left;
  }

  private parseConcat(): Node {
    let left = this.parseAdditive();
    while (this.isOp('&')) {
      this.next();
      left = { type: 'bin', op: '&', left, right: this.parseAdditive() };
    }
    return left;
  }

  private parseAdditive(): Node {
    let left = this.parseMultiplicative();
    while (this.isOp('+', '-')) {
      const op = this.next().value as BinOp;
      left = { type: 'bin', op, left, right: this.parseMultiplicative() };
    }
    return left;
  }

  private parseMultiplicative(): Node {
    let left = this.parsePower();
    while (this.isOp('*', '/')) {
      const op = this.next().value as BinOp;
      left = { type: 'bin', op, left, right: this.parsePower() };
    }
    return left;
  }

  private parsePower(): Node {
    let left = this.parseUnary();
    while (this.isOp('^')) {
      this.next();
      // Excel's ^ is left-associative: 2^3^2 = 64
      left = { type: 'bin', op: '^', left, right: this.parseUnary() };
    }
    return left;
  }

  /** Unary minus binds tighter than ^ in Excel: -2^2 = 4 */
  private parseUnary(): Node {
    if (this.isOp('-', '+')) {
      const op = this.next().value as '+' | '-';
      return { type: 'unary', op, operand: this.parseUnary() };
    }
    return this.parsePostfix();
  }

  /** Percent binds tighter than ^ : 2^2% = 2^0.02 */
  private parsePostfix(): Node {
    let node = this.parsePrimary();
    while (this.isOp('%')) {
      this.next();
      node = { type: 'percent', operand: node };
    }
    return node;
  }

  private parsePrimary(): Node {
    const t = this.peek();
    switch (t.type) {
      case 'num':
        this.next();
        return { type: 'num', value: t.value as number };
      case 'str':
        this.next();
        return { type: 'str', value: t.value as string };
      case 'bool':
        this.next();
        return { type: 'bool', value: t.value as boolean };
      case 'err':
        this.next();
        return { type: 'err', error: t.value as CellError };
      case 'ref':
        this.next();
        return { type: 'ref', area: t.value as AreaRef };
      case 'name':
        this.next();
        return { type: 'name', name: (t.value as string) };
      case 'lparen': {
        this.next();
        const inner = this.parseComparison();
        if (this.peek().type !== 'rparen') {
          throw new FormulaSyntaxError('Expected )', this.peek().pos);
        }
        this.next();
        return inner;
      }
      case 'lbrace':
        return this.parseArrayLiteral();
      case 'func': {
        this.next();
        const name = (t.value as string).toUpperCase();
        const open = this.peek();
        if (open.type !== 'lparen') throw new FormulaSyntaxError('Expected (', open.pos);
        this.next();
        const args: Node[] = [];
        if (this.peek().type === 'rparen') {
          this.next();
          return { type: 'func', name, args };
        }
        for (;;) {
          if (this.peek().type === 'comma' || this.peek().type === 'rparen') {
            args.push({ type: 'missing' });
          } else {
            args.push(this.parseComparison());
          }
          const sep = this.peek();
          if (sep.type === 'comma') {
            this.next();
            continue;
          }
          if (sep.type === 'rparen') {
            this.next();
            break;
          }
          throw new FormulaSyntaxError('Expected , or )', sep.pos);
        }
        return { type: 'func', name, args };
      }
      default:
        throw new FormulaSyntaxError('Unexpected end of formula', t.pos);
    }
  }

  private parseArrayLiteral(): Node {
    this.next(); // {
    const rows: Node[][] = [];
    let row: Node[] = [];
    for (;;) {
      row.push(this.parseComparison());
      const sep = this.peek();
      if (sep.type === 'comma') {
        this.next();
        continue;
      }
      if (sep.type === 'semi') {
        this.next();
        rows.push(row);
        row = [];
        continue;
      }
      if (sep.type === 'rbrace') {
        this.next();
        rows.push(row);
        break;
      }
      throw new FormulaSyntaxError('Expected , ; or } in array', sep.pos);
    }
    return { type: 'array', rows };
  }
}

const astCache = new Map<string, ParseResult>();
const AST_CACHE_LIMIT = 20000;

export function parseFormula(formula: string): ParseResult {
  const cached = astCache.get(formula);
  if (cached) return cached;

  let result: ParseResult;
  try {
    let text = formula.trim();
    if (text.startsWith('=')) text = text.slice(1);
    if (!text.trim()) {
      result = { error: CellError.VALUE, message: 'Empty formula' };
    } else {
      const tokens = tokenize(text);
      const ast = new Parser(tokens).parse();
      result = { ast };
    }
  } catch (e) {
    if (e instanceof FormulaSyntaxError) {
      result = { error: CellError.NAME, message: e.message };
    } else if (e instanceof FormulaError) {
      result = { error: e.error, message: e.message };
    } else {
      result = { error: CellError.VALUE, message: (e as Error).message };
    }
  }

  if (astCache.size > AST_CACHE_LIMIT) astCache.clear();
  astCache.set(formula, result);
  return result;
}

/** Collect all references in an AST (used for fill, copy and auditing). */
export function collectRefs(node: Node, out: AreaRef[] = []): AreaRef[] {
  switch (node.type) {
    case 'ref':
      out.push(node.area);
      break;
    case 'bin':
      collectRefs(node.left, out);
      collectRefs(node.right, out);
      break;
    case 'unary':
    case 'percent':
      collectRefs(node.operand, out);
      break;
    case 'func':
      node.args.forEach((a) => collectRefs(a, out));
      break;
    case 'array':
      node.rows.forEach((r) => r.forEach((c) => collectRefs(c, out)));
      break;
    default:
      break;
  }
  return out;
}
