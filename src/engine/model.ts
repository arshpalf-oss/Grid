/** Workbook data model types shared by the engine and the UI. */

import { CellError } from './errors';
import { Scalar } from './values';

export interface BorderStyle {
  color?: string;
  style?: 'thin' | 'medium' | 'thick' | 'dashed' | 'dotted' | 'double';
}

export interface CellStyle {
  bold?: boolean;
  italic?: boolean;
  underline?: boolean;
  strike?: boolean;
  align?: 'left' | 'center' | 'right';
  valign?: 'top' | 'middle' | 'bottom';
  numFmt?: string;
  fill?: string;
  color?: string;
  fontSize?: number;
  fontFamily?: string;
  wrap?: boolean;
  indent?: number;
  borderTop?: BorderStyle;
  borderRight?: BorderStyle;
  borderBottom?: BorderStyle;
  borderLeft?: BorderStyle;
}

export interface Cell {
  /** Exactly what the user typed: text, number literal or "=FORMULA". */
  raw: string;
  style?: CellStyle;
}

export interface Computed {
  value: Scalar | CellError;
  formula?: boolean;
  circular?: boolean;
  /** Set on the anchor cell of a spilled array. */
  spill?: { height: number; width: number };
  isSpillChild?: boolean;
  /** Number format inferred from the literal that was typed (e.g. dates). */
  numberFormat?: string;
}

export interface SheetData {
  id: string;
  name: string;
  cells: Map<number, Cell>;
  colWidths: Map<number, number>;
  rowHeights: Map<number, number>;
  defaultColWidth: number;
  defaultRowHeight: number;
  frozenRows: number;
  frozenCols: number;
  /** Optional tab color. */
  color?: string;
}

export interface SheetJSON {
  id: string;
  name: string;
  defaultColWidth: number;
  defaultRowHeight: number;
  frozenRows: number;
  frozenCols: number;
  color?: string;
  cells: { key: number; raw: string; style?: CellStyle }[];
  colWidths: [number, number][];
  rowHeights: [number, number][];
}

export interface WorkbookJSON {
  version: 1;
  name: string;
  sheets: SheetJSON[];
  names: { name: string; sheetId: string; formula: string }[];
  activeSheetId?: string;
}
