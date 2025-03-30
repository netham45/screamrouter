/**
 * Index file for the DesktopMenu component.
 */

import DesktopMenu from './DesktopMenu';
import { colorContextInstance } from './context/ColorContext';

// Function to show the desktop menu with the given base color
export const DesktopMenuShow = (r: number, g: number, b: number, a: number) => {
  colorContextInstance.setCurrentColor({ r: r, g: g, b: b, a: a });
};

// Function to hide the desktop menu
export const DesktopMenuHide = () => {
  return true;
};

export { DesktopMenu };
export * from './types';
export * from './utils';

// Default export for backward compatibility
export default DesktopMenu;
