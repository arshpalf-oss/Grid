/** Cell display text: value + effective number format. */

import { formatValue } from './format';
import type { CellStyle, Computed } from './model';

export interface DisplaySource {
  getComputed(sheetId: string, row: number, col: number): Computed;
  getStyle(sheetId: string, row: number, col: number): CellStyle | undefined;
}

export function formattedCellValue(
  source: DisplaySource,
  sheetId: string,
  row: number,
  col: number
): string {
  const computed = source.getComputed(sheetId, row, col);
  const style = source.getStyle(sheetId, row, col);
  const fmt = style?.numFmt ?? computed.numberFormat ?? null;
  return formatValue(computed.value, fmt);
}

/** Preferred horizontal alignment, Excel-style (numbers right, text left). */
export function defaultAlignment(value: unknown): 'left' | 'right' | 'center' {
  if (typeof value === 'number') return 'right';
  if (typeof value === 'boolean') return 'center';
  if (value === null) return 'left';
  return 'left';
}
