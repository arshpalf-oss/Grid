/**
 * Formula evaluator.
 *
 * Evaluation is pull-based: the workbook asks for one cell and the evaluator
 * walks the AST, reading other cells through the workbook's cache. That makes
 * cycles observable (re-entry is detected) and keeps recalculation proportional
 * to what is actually displayed or referenced.
 */

import { AreaRef, Rect, MAX_COL_INDEX, MAX_ROW_INDEX } from './address';
import { CellError, ERR, FormulaError, fail, isError } from './errors';
import { Node, parseFormula } from './parser';
import { parseAreaText } from './tokenizer';
import {
  ArrayValue,
  FnContext,
  RangeValue,
  Scalar,
  Val,
  compareScalars,
  deref,
  isArray,
  isRange,
  numberToText,
  rangeFromArea,
  toBool,
  toGrid,
  toNumber,
  toText,
} from './values';
import { getFunction, SPECIAL_FORMS } from './functions/registry';

// Register every function category exactly once.
import './functions/math';
import './functions/stat';
import './functions/text';
import './functions/logical';
import './functions/lookup';
import './functions/datefns';
import './functions/financial';
import './functions/info';

export interface EvalHost {
  sheetIdByName(name: string): string | null;
  sheetName(sheetId: string): string;
  sheetCount(): number;
  getScalar(sheetId: string, row: number, col: number): Scalar | CellError;
  usedBounds(sheetId: string): Rect | null;
  definedName(name: string): { sheetId: string; formula: string } | null;
  now(): Date;
}

const MAX_DEPTH = 256;

class Evaluator {
  private depth = 0;

  constructor(
    private readonly host: EvalHost,
    private readonly sheetId: string,
    private readonly row: number,
    private readonly col: number,
    private readonly vars: Map<string, Val> = new Map()
  ) {}

  private ctx(vars: Map<string, Val> = this.vars): FnContext {
    return {
      source: {
        sheetIdByName: (n) => this.host.sheetIdByName(n),
        getScalar: (s, r, c) => this.host.getScalar(s, r, c),
        usedBounds: (s) => this.host.usedBounds(s),
        definedName: (n) => this.host.definedName(n),
        now: () => this.host.now(),
        ...({ sheetCount: this.host.sheetCount() } as object),
      },
      sheetId: this.sheetId,
      row: this.row,
      col: this.col,
      vars,
      parseRefString: (text) => parseAreaText(text, this.host.sheetName(this.sheetId)),
      evalNode: (n) => this.eval(n),
    };
  }

  eval(node: Node, vars: Map<string, Val> = this.vars): Val {
    if (this.depth > MAX_DEPTH) fail(ERR.NUM);
    this.depth++;
    try {
      return this.evalInner(node, vars);
    } finally {
      this.depth--;
    }
  }

  private evalInner(node: Node, vars: Map<string, Val>): Val {
    switch (node.type) {
      case 'num':
        return node.value;
      case 'str':
        return node.value;
      case 'bool':
        return node.value;
      case 'err':
        return node.error;
      case 'missing':
        return null;
      case 'ref':
        return this.resolveRef(node.area, vars);
      case 'name':
        return this.resolveName(node.name, vars);
      case 'array': {
        const rows = node.rows.map((row) =>
          row.map((cell) => {
            const v = this.eval(cell, vars);
            return scalarOf(v);
          })
        );
        return new ArrayValue(rows);
      }
      case 'percent': {
        const v = this.eval(node.operand, vars);
        return this.mapUnary(v, (x) => toNumber(x ?? null, this.ctx(vars)) / 100, vars);
      }
      case 'unary': {
        const v = this.eval(node.operand, vars);
        return this.mapUnary(
          v,
          (x) => {
            const n = toNumber(x ?? null, this.ctx(vars));
            return node.op === '-' ? -n : n;
          },
          vars
        );
      }
      case 'bin':
        return this.evalBinary(node, vars);
      case 'func':
        return this.evalFunc(node, vars);
      default:
        fail(ERR.VALUE);
    }
  }

  private resolveRef(area: AreaRef, vars: Map<string, Val>): Val {
    if (area.sheetEnd) fail(ERR.REF); // 3-D references are not supported yet
    let sheetId: string = this.sheetId;
    const target = area.sheet;
    if (target) {
      const resolved = this.host.sheetIdByName(target);
      if (!resolved) fail(ERR.REF);
      sheetId = resolved as string;
    }
    return rangeFromArea(area, sheetId);
  }

  private resolveName(raw: string, vars: Map<string, Val>): Val {
    const upper = raw.toUpperCase();
    if (vars.has(upper)) return vars.get(upper)!;
    if (vars.has(raw)) return vars.get(raw)!;

    let name = raw;
    let sheetId: string = this.sheetId;
    const bang = raw.lastIndexOf('!');
    if (bang >= 0) {
      const sheetName = raw.slice(0, bang);
      name = raw.slice(bang + 1);
      const resolved = this.host.sheetIdByName(sheetName);
      if (!resolved) fail(ERR.REF);
      sheetId = resolved;
    }

    const def = this.host.definedName(name);
    if (!def) fail(ERR.NAME);

    if (def.sheetId && def.sheetId !== sheetId) sheetId = def.sheetId;
    const parsed = parseFormula(def.formula.startsWith('=') ? def.formula : `=${def.formula}`);
    if (!parsed.ast) return parsed.error ?? ERR.NAME;
    const sub = new Evaluator(this.host, sheetId, this.row, this.col, new Map());
    return sub.eval(parsed.ast);
  }

  /** Apply a scalar transform, lifting over arrays and multi-cell ranges. */
  private mapUnary(v: Val, fn: (x: Scalar | CellError) => number, vars: Map<string, Val>): Val {
    if (isError(v)) return v;
    if (isRange(v) && !v.isSingle) {
      const grid = toGrid(v, this.ctx(vars));
      return new ArrayValue(grid.map((row) => row.map((cell) => applyScalar(fn, cell, this.ctx(vars)))));
    }
    if (isArray(v) && !v.isSingle) {
      return new ArrayValue(v.rows.map((row) => row.map((cell) => applyScalar(fn, cell, this.ctx(vars)))));
    }
    return applyScalar(fn, scalarOf(v, this.ctx(vars)), this.ctx(vars));
  }

  private evalBinary(node: { op: string; left: Node; right: Node }, vars: Map<string, Val>): Val {
    const left = this.eval(node.left, vars);
    const right = this.eval(node.right, vars);

    const leftMulti = (isRange(left) && !left.isSingle) || (isArray(left) && !left.isSingle);
    const rightMulti = (isRange(right) && !right.isSingle) || (isArray(right) && !right.isSingle);

    const apply = (a: Scalar | CellError, b: Scalar | CellError): Scalar | CellError =>
      applyBinary(node.op, a, b, this.ctx(vars));

    if (!leftMulti && !rightMulti) {
      return apply(scalarOf(left, this.ctx(vars)), scalarOf(right, this.ctx(vars)));
    }

    const ctx = this.ctx(vars);
    const aGrid = toGrid(left, ctx);
    const bGrid = toGrid(right, ctx);
    const height = Math.max(aGrid.length, bGrid.length);
    const width = Math.max(aGrid[0]?.length ?? 0, bGrid[0]?.length ?? 0);
    const out: (Scalar | CellError)[][] = [];
    for (let i = 0; i < height; i++) {
      const row: (Scalar | CellError)[] = [];
      for (let j = 0; j < width; j++) {
        const a = aGrid[Math.min(i, aGrid.length - 1)]?.[Math.min(j, (aGrid[0]?.length ?? 1) - 1)] ?? null;
        const b = bGrid[Math.min(i, bGrid.length - 1)]?.[Math.min(j, (bGrid[0]?.length ?? 1) - 1)] ?? null;
        row.push(apply(a, b));
      }
      out.push(row);
    }
    return new ArrayValue(out);
  }

  private evalFunc(node: { name: string; args: Node[] }, vars: Map<string, Val>): Val {
    const name = node.name.replace(/^_XLFN\./i, '').toUpperCase();
    const ctx = this.ctx(vars);

    if (SPECIAL_FORMS.has(name)) return this.evalSpecial(name, node.args, vars, ctx);

    const def = getFunction(name);
    if (!def) fail(ERR.NAME);
    if (node.args.length < def.minArgs) fail(ERR.VALUE);
    if (node.args.length > def.maxArgs) fail(ERR.VALUE);

    const args = node.args.map((arg) => this.eval(arg, vars));

    if (!def.toleratesErrors) {
      for (const a of args) if (isError(a)) return a;
    }

    try {
      const result = def.fn(args, ctx);
      return result;
    } catch (e) {
      if (e instanceof FormulaError) return e.error;
      if (e instanceof CellError) return e;
      return ERR.VALUE;
    }
  }

  private evalSpecial(name: string, args: Node[], vars: Map<string, Val>, ctx: FnContext): Val {
    const evalArg = (i: number): Val => (i < args.length ? this.eval(args[i], vars) : null);
    const truthy = (v: Val): boolean => {
      const d = scalarOf(v, ctx);
      if (isError(d)) throw new FormulaError(d);
      return toBool(d, ctx);
    };

    switch (name) {
      case 'IF': {
        if (args.length < 1) fail(ERR.VALUE);
        const cond = evalArg(0);
        return truthy(cond) ? evalArg(1) : evalArg(2);
      }
      case 'IFS': {
        for (let i = 0; i + 1 < args.length; i += 2) {
          if (truthy(evalArg(i))) return evalArg(i + 1);
        }
        fail(ERR.NA);
      }
      case 'IFERROR': {
        const v = evalArg(0);
        const failed = isError(v) || (isRange(v) && isError(this.firstScalar(v, ctx)));
        return failed ? evalArg(1) : v;
      }
      case 'IFNA': {
        const v = evalArg(0);
        const d = scalarOf(v, ctx);
        return isError(d) && d.code === '#N/A' ? evalArg(1) : v;
      }
      case 'CHOOSE': {
        const idx = Math.trunc(toNumber(evalArg(0), ctx));
        if (idx < 1) fail(ERR.VALUE);
        if (idx >= args.length) fail(ERR.VALUE);
        return this.eval(args[idx], vars);
      }
      case 'SWITCH': {
        const subject = scalarOf(evalArg(0), ctx);
        if (isError(subject)) return subject;
        for (let i = 1; i + 1 < args.length; i += 2) {
          const candidate = scalarOf(evalArg(i), ctx);
          if (compareScalars(subject as Scalar, candidate as Scalar) === 0) return evalArg(i + 1);
        }
        // odd tail = default
        if (args.length % 2 === 0) return evalArg(args.length - 1);
        fail(ERR.NA);
      }
      case 'LET': {
        if (args.length < 3 || args.length % 2 === 0) fail(ERR.VALUE);
        const child = new Map(vars);
        for (let i = 0; i + 1 < args.length - 1; i += 2) {
          const nameNode = args[i];
          if (nameNode.type !== 'name') fail(ERR.NAME);
          child.set((nameNode as { name: string }).name.toUpperCase(), this.eval(args[i + 1], child));
        }
        return this.eval(args[args.length - 1], child);
      }
      default:
        fail(ERR.NAME);
    }
  }

  private firstScalar(v: Val, ctx: FnContext): Scalar | CellError {
    if (isRange(v)) {
      if (v.isSingle) return this.host.getScalar(v.sheetId, v.r1, v.c1);
      return toGrid(v, ctx)[0]?.[0] ?? null;
    }
    if (isArray(v)) return v.rows[0]?.[0] ?? null;
    return v as Scalar | CellError;
  }
}

function scalarOf(v: Val, ctx?: FnContext): Scalar | CellError {
  if (isRange(v)) {
    if (v.isSingle && ctx) return ctx.source.getScalar(v.sheetId, v.r1, v.c1);
    return ERR.VALUE;
  }
  if (isArray(v)) return v.rows[0]?.[0] ?? null;

  return v as Scalar | CellError;
}

function applyScalar(
  fn: (x: Scalar | CellError) => number,
  v: Scalar | CellError,
  ctx: FnContext
): Scalar | CellError {
  if (isError(v)) return v;
  try {
    return fn(v);
  } catch (e) {
    if (e instanceof FormulaError) return e.error;
    return ERR.VALUE;
  }
}

function coerceForCompare(a: Scalar, b: Scalar): [Scalar, Scalar] {
  if (a === null) {
    if (typeof b === 'number') return [0, b];
    if (typeof b === 'string') return ['', b];
    if (typeof b === 'boolean') return [false, b];
    return [null, b];
  }
  if (b === null) {
    if (typeof a === 'number') return [a, 0];
    if (typeof a === 'string') return [a, ''];
    if (typeof a === 'boolean') return [a, false];
  }
  if (typeof a === 'number' && typeof b === 'string') {
    const n = Number(b);
    if (b.trim() !== '' && Number.isFinite(n)) return [a, n];
  }
  if (typeof a === 'string' && typeof b === 'number') {
    const n = Number(a);
    if (a.trim() !== '' && Number.isFinite(n)) return [n, b];
  }
  return [a, b];
}

export function applyBinary(
  op: string,
  a: Scalar | CellError,
  b: Scalar | CellError,
  ctx: FnContext
): Scalar | CellError {
  if (isError(a)) return a;
  if (isError(b)) return b;

  switch (op) {
    case '&':
      return toText(a, ctx) + toText(b, ctx);
    case '=':
    case '<>':
    case '<':
    case '>':
    case '<=':
    case '>=': {
      const [x, y] = coerceForCompare(a as Scalar, b as Scalar);
      const cmp = compareScalars(x, y);
      switch (op) {
        case '=':
          return cmp === 0;
        case '<>':
          return cmp !== 0;
        case '<':
          return cmp < 0;
        case '>':
          return cmp > 0;
        case '<=':
          return cmp <= 0;
        case '>=':
          return cmp >= 0;
      }
      return false;
    }
    default:
      break;
  }

  const x = toNumber(a, ctx);
  const y = toNumber(b, ctx);
  let result: number;
  switch (op) {
    case '+':
      result = x + y;
      break;
    case '-':
      result = x - y;
      break;
    case '*':
      result = x * y;
      break;
    case '/':
      if (y === 0) return ERR.DIV0;
      result = x / y;
      break;
    case '^':
      if (x === 0 && y < 0) return ERR.DIV0;
      if (x < 0 && !Number.isInteger(y)) return ERR.NUM;
      result = Math.pow(x, y);
      break;
    default:
      return ERR.VALUE;
  }
  if (!Number.isFinite(result)) return ERR.NUM;
  // Excel normalizes -0 to 0
  return Object.is(result, -0) ? 0 : result;
}

/** Evaluate a formula for a specific cell. */
export function evaluateFormula(
  host: EvalHost,
  sheetId: string,
  row: number,
  col: number,
  formula: string
): { value: Val; error?: CellError } {
  const parsed = parseFormula(formula);
  if (!parsed.ast) return { value: parsed.error ?? ERR.VALUE, error: parsed.error ?? ERR.VALUE };
  const evaluator = new Evaluator(host, sheetId, row, col);
  try {
    const value = evaluator.eval(parsed.ast);
    return { value };
  } catch (e) {
    if (e instanceof FormulaError) return { value: e.error, error: e.error };
    if (e instanceof CellError) return { value: e, error: e };
    return { value: ERR.VALUE, error: ERR.VALUE };
  }
}

/** Evaluate a formula out of cell context (used for defined names). */
export function evaluateStandalone(host: EvalHost, sheetId: string, formula: string): Val {
  return evaluateFormula(host, sheetId, 0, 0, formula).value;
}

export { deref, isRange, isArray, numberToText, MAX_COL_INDEX, MAX_ROW_INDEX };
