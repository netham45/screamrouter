/**
 * @file GlobalFunctionsComponent.tsx
 * @description React component to hook up the global functions with Chakra UI's color mode.
 */

import React, { useEffect } from 'react';
import { useColorMode } from '@chakra-ui/react';
import './globalFunctions'; // Import the existing global functions

/**
 * Component that connects the global functions to Chakra UI's hooks.
 * This must be rendered within the React tree to work.
 */
const GlobalFunctionsComponent: React.FC = () => {
  const { setColorMode } = useColorMode();

  // Function to apply the color mode from localStorage
  const applyColorModeFromLocalStorage = () => {
    // Get the current color mode from localStorage
    const savedMode = localStorage.getItem('chakra-ui-color-mode');
    
    if (savedMode) {
      // Apply the saved color mode from localStorage
      if (savedMode === 'system') {
        // For system mode, check system preference
        const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
        setColorMode(prefersDark ? 'dark' : 'light');
        console.log(`Color mode set to ${prefersDark ? 'dark' : 'light'} based on system preference`);
      } else {
        // For explicit light/dark mode
        setColorMode(savedMode as 'light' | 'dark');
        console.log(`Color mode set to ${savedMode} from localStorage`);
      }
    }
  };

  useEffect(() => {
    // Apply color mode immediately
    applyColorModeFromLocalStorage();
    
    // Listen for localStorage changes
    const handleStorageChange = (event: StorageEvent) => {
      if (event.key === 'chakra-ui-color-mode') {
        console.log('chakra-ui-color-mode changed in localStorage:', event.newValue);
        applyColorModeFromLocalStorage();
      }
    };
    
    // Add event listener for storage events
    window.addEventListener('storage', handleStorageChange);
    
    // Override the DesktopMenuShow function to use Chakra's setColorMode
    const originalDesktopMenuShow = window.DesktopMenuShow;
    window.DesktopMenuShow = () => {
      // Apply color mode from localStorage
      applyColorModeFromLocalStorage();
      
      // Dispatch resize event for other functionality in the original function
      window.dispatchEvent(new Event('resize'));
    };

    return () => {
      // Clean up event listeners and restore original function on unmount
      window.removeEventListener('storage', handleStorageChange);
      window.DesktopMenuShow = originalDesktopMenuShow;
    };
  }, [setColorMode]);

  // This component doesn't render anything visible
  return null;
};

export default GlobalFunctionsComponent;