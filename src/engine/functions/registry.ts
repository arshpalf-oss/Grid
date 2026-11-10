import { FnContext, Val } from '../values';

export type FnImpl = (args: Val[], ctx: FnContext) => Val;

export interface FnDef {
  name: string;
  minArgs: number;
  maxArgs: number;
  fn: FnImpl;
  /** Volatile functions force a full recalculation of dependents. */
  volatile?: boolean;
  /** Receives CellError arguments instead of having them propagated. */
  toleratesErrors?: boolean;
  category: string;
}

const registry = new Map<string, FnDef>();

export function define(
  name: string,
  minArgs: number,
  maxArgs: number,
  fn: FnImpl,
  opts: { volatile?: boolean; toleratesErrors?: boolean; category?: string } = {}
): void {
  const key = name.toUpperCase();
  registry.set(key, {
    name: key,
    minArgs,
    maxArgs,
    fn,
    category: opts.category ?? 'General',
    volatile: opts.volatile,
    toleratesErrors: opts.toleratesErrors,
  });
}

/** Aliases (Excel keeps legacy names alongside the .S/.P variants). */
export function alias(from: string, to: string): void {
  const target = registry.get(to.toUpperCase());
  if (target) registry.set(from.toUpperCase(), { ...target, name: from.toUpperCase() });
}

export function getFunction(name: string): FnDef | undefined {
  const key = name.replace(/^_xlfn\./i, '').toUpperCase();
  return registry.get(key);
}

export function allFunctions(): FnDef[] {
  return [...registry.values()].sort((a, b) => a.name.localeCompare(b.name));
}

/**
 * Lazy/short-circuit constructs the evaluator must handle itself rather than
 * calling through the registry (they cannot evaluate all arguments eagerly).
 */
export const SPECIAL_FORMS = new Set([
  'IF',
  'IFS',
  'IFERROR',
  'IFNA',
  'CHOOSE',
  'SWITCH',
  'LET',
  'LAMBDA',
  'MAP',
  'REDUCE',
  'SCAN',
  'BYROW',
  'BYCOL',
  'MAKEARRAY',
]);
