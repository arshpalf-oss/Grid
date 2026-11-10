/** Logical functions (IF/IFS/SWITCH/CHOOSE/LET live in the evaluator). */

import { ERR, FormulaError, fail } from '../errors';
import { Val } from '../values';
import { isError as isErrValue } from '../errors';
import { flattenValuesFrom, boolOr } from './helpers';
import { define } from './registry';

function logicals(args: Val[], c: Parameters<typeof flattenValuesFrom>[1]): boolean[] {
  const out: boolean[] = [];
  for (const v of flattenValuesFrom(args, c)) {
    if (isErrValue(v)) throw new FormulaError(v);
    if (v === null || v === '') continue;
    if (typeof v === 'boolean') out.push(v);
    else if (typeof v === 'number') out.push(v !== 0);
    else {
      const upper = v.trim().toUpperCase();
      if (upper === 'TRUE') out.push(true);
      else if (upper === 'FALSE') out.push(false);
      else out.push(false);
    }
  }
  return out;
}

define('TRUE', 0, 0, () => true, { category: 'Logical' });
define('FALSE', 0, 0, () => false, { category: 'Logical' });

define('AND', 1, 255, (a, c) => {
  const values = logicals(a, c);
  if (!values.length) fail(ERR.VALUE);
  return values.every(Boolean);
}, { category: 'Logical' });

define('OR', 1, 255, (a, c) => {
  const values = logicals(a, c);
  if (!values.length) fail(ERR.VALUE);
  return values.some(Boolean);
}, { category: 'Logical' });

define('XOR', 1, 255, (a, c) => {
  const values = logicals(a, c);
  if (!values.length) fail(ERR.VALUE);
  return values.filter(Boolean).length % 2 === 1;
}, { category: 'Logical' });

define('NOT', 1, 1, (a, c) => !boolOr(a, 0, c, false), { category: 'Logical' });
