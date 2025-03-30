/**
 * ColorContext for DesktopMenu
 * Stores a base color and derived colors
 */
import React, { createContext, useContext, useState, useEffect } from 'react';

// Simple RGB color type
export interface RGBColor {
  r: number;
  g: number;
  b: number;
  a?: number;
}

// Context type definition
interface ColorContextType {
  // The base color passed to DesktopMenuShow
  baseColor: RGBColor;
  
  // Update the base color
  setBaseColor: (color: RGBColor) => void;
  
  // Helper function to get CSS color strings
  getColor: (opacity?: number) => string;
  getLighterColor: (factor?: number, opacity?: number) => string;
  getDarkerColor: (factor?: number, opacity?: number) => string;
  getDarkerColorColor: (factor?: number) => RGBColor;
  getLighterColorColor: (factor?: number) => RGBColor;
}

// Create context with a default blue color
const ColorContext = createContext<ColorContextType | null>(null);

const defaultColor: RGBColor = { r: 68, g: 77, b: 82, a: 1 };

// Global singleton to manage color outside React
class ColorContextManager {
  private listeners: Array<(color: RGBColor) => void> = [];
  private currentColor: RGBColor;

  constructor() {
    // Try to load color from localStorage
    const savedColor = localStorage.getItem('desktopMenuColor');
    if (savedColor) {
      try {
        const parsed = JSON.parse(savedColor);
        if (parsed && typeof parsed === 'object' && 
            'r' in parsed && 'g' in parsed && 'b' in parsed &&
            typeof parsed.r === 'number' && 
            typeof parsed.g === 'number' && 
            typeof parsed.b === 'number') {
          this.currentColor = parsed;
        } else {
          this.currentColor = defaultColor;
        }
      } catch {
        this.currentColor = defaultColor;
      }
    } else {
      this.currentColor = defaultColor;
    }
  }
  
  setCurrentColor(color: RGBColor = {r: 0, g: 0, b: 0, a: 0}): void {
    // Check if color is black (#000000)
    if (color.r === 0 && color.g === 0 && color.b === 0) {
      this.currentColor = defaultColor;
    } else {
      console.log(`ColorContextManager: Setting color to RGB(${color.r}, ${color.g}, ${color.b}, ${color.a})`);
      // Limit saturation by ensuring no color channel is more than 50% brighter than the average
      const avg = (color.r + color.g + color.b) / 3;
      const maxDiff = avg * 0.15;
      
      this.currentColor = {
        a: color.a,
        r: Math.round(Math.min(color.r, avg + maxDiff)),
        g: Math.round(Math.min(color.g, avg + maxDiff)),
        b: Math.round(Math.min(color.b, avg + maxDiff))
      };
    }

    const isDarkMode = window.matchMedia('(prefers-color-scheme: dark)').matches;
      if (isDarkMode)
        this.currentColor = this.getDarkerColorColor(.66);
      else
        this.currentColor = this.getLighterColorColor(1.5);
    
    // Save to localStorage
    localStorage.setItem('desktopMenuColor', JSON.stringify(this.currentColor));
    
    this.notifyListeners();
  }
  
  getCurrentColor(): RGBColor {
    return this.currentColor;
  }
  
  subscribe(listener: (color: RGBColor) => void): () => void {
    this.listeners.push(listener);
    
    // Return unsubscribe function
    return () => {
      this.listeners = this.listeners.filter(l => l !== listener);
    };
  }
  
  private notifyListeners(): void {
    this.listeners.forEach(listener => listener(this.currentColor));
  }
  
  // Utility functions for consistent color generation
  getColor(opacity = 1): string {
    return `rgba(${this.currentColor.r}, ${this.currentColor.g}, ${this.currentColor.b}, ${opacity})`;
  }
  
  getLighterColor(factor = 1.2, opacity = 1): string {
    return `rgba(${Math.min(255, Math.round(this.currentColor.r * factor))}, 
            ${Math.min(255, Math.round(this.currentColor.g * factor))}, 
            ${Math.min(255, Math.round(this.currentColor.b * factor))},
            ${opacity})`;
  }
  
  getDarkerColor(factor = 0.8, opacity = 1): string {
    return `rgba(${Math.round(this.currentColor.r * factor)}, 
            ${Math.round(this.currentColor.g * factor)}, 
            ${Math.round(this.currentColor.b * factor)},
            ${opacity})`;
  }

  getDarkerColorColor(factor = 0.8): RGBColor {
    const ret: RGBColor = {
      r: Math.round(this.currentColor.r * factor),
      g: Math.round(this.currentColor.g * factor),
      b: Math.round(this.currentColor.b * factor),
      a: this.currentColor.a};
      return ret;
  }

  getLighterColorColor(factor = 0.8): RGBColor {
    const ret: RGBColor = {
      r: Math.min(Math.round(this.currentColor.r * factor), 255),
      g: Math.min(Math.round(this.currentColor.g * factor), 255),
      b: Math.min(Math.round(this.currentColor.b * factor), 255),
      a: this.currentColor.a};
      return ret;
  }
}

// Create global singleton instance
export const colorContextInstance = new ColorContextManager();

// React provider component
export const ColorProvider: React.FC<{children: React.ReactNode}> = ({ children }) => {
  // Local state that follows the global singleton
  const [baseColor, setBaseColor] = useState<RGBColor>(colorContextInstance.getCurrentColor());
  
  // Subscribe to updates from the global singleton
  useEffect(() => {
    const unsubscribe = colorContextInstance.subscribe((color) => {
      setBaseColor(color);
    });
    
    // Cleanup subscription
    return unsubscribe;
  }, []);
  
  // Helper functions using the current state
  const getColor = (opacity = 1) => {
    return `rgba(${baseColor.r}, ${baseColor.g}, ${baseColor.b}, ${opacity})`;
  };
  
  const getLighterColor = (factor = 1.2, opacity = 1) => {
    return `rgba(${Math.min(255, Math.round(baseColor.r * factor))}, 
                ${Math.min(255, Math.round(baseColor.g * factor))}, 
                ${Math.min(255, Math.round(baseColor.b * factor))},
                ${opacity})`;
  };
  
  const getDarkerColor = (factor = 0.8, opacity = 1) => {
    return `rgba(${Math.round(baseColor.r * factor)}, 
                ${Math.round(baseColor.g * factor)}, 
                ${Math.round(baseColor.b * factor)},
                ${opacity})`;
  };

  const getDarkerColorColor = (factor = 0.8): RGBColor => {
    const ret: RGBColor = {
      r: Math.round(baseColor.r * factor),
      g: Math.round(baseColor.g * factor),
      b: Math.round(baseColor.b * factor)};
      return ret;
  };

  const  getLighterColorColor = (factor = 0.8): RGBColor => {
    const ret: RGBColor = {
      r: Math.min(Math.round(baseColor.r * factor), 255),
      g: Math.min(Math.round(baseColor.g * factor), 255),
      b: Math.min(Math.round(baseColor.b * factor), 255)};
      return ret;
  };
  
  // Update the global singleton when local state changes
  const handleSetBaseColor = (color: RGBColor) => {
    colorContextInstance.setCurrentColor(color);
  };
  
  // Context value for React components
  const contextValue: ColorContextType = {
    baseColor,
    setBaseColor: handleSetBaseColor,
    getColor,
    getLighterColor,
    getDarkerColor,
    getDarkerColorColor,
    getLighterColorColor,
  };
  
  return (
    <ColorContext.Provider value={contextValue}>
      {children}
    </ColorContext.Provider>
  );
};

// Custom hook to use the color context in React components
export const useColorContext = () => {
  const context = useContext(ColorContext);
  if (!context) {
    throw new Error('useColorContext must be used within a ColorProvider');
  }
  return context;
};
