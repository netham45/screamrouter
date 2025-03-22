/**
 * React utility module for defining and creating common components and functions used across the application.
 * These utilities handle various operations such as rendering links with anchors, handling anchor flashing,
 * creating action buttons, volume sliders, sorting items, determining next sort direction, and getting stock sorted items.
 */

import React, { useEffect } from 'react';
import { Link, useLocation } from 'react-router-dom';
import { Button } from '@chakra-ui/react';

/**
 * Interface defining the configuration for sorting.
 */
export interface SortConfig {
  key: string;
  direction: 'asc' | 'desc';
}

/**
 * Renders a link with an anchor tag based on provided parameters.
 *
 * @param {string} to - The path to navigate to.
 * @param {string} name - The name of the item being linked.
 * @param {string} icon - The icon class for the link.
 * @param {'sink' | 'source'} fromType - Optional parameter indicating the type of item ('sink' or 'source').
 * @returns {JSX.Element} A Link component with an anchor tag.
 */
export const renderLinkWithAnchor = (to: string, name: string, icon: string, fromType?: 'sink' | 'source') => {
  const queryParam = fromType ? `?sort=${fromType}&direction=asc` : '';
  const anchor = `#${to.replace(/^\//,'').replace(/s$/,'')}-${encodeURIComponent(name)}`;
  return (
    <Link to={`${to}${queryParam}${anchor}`}>
      <i className={`fas ${icon}`}></i> {name}
    </Link>
  );
};

/**
 * Custom hook for handling anchor flashing when navigating to an element with a hash.
 */
export const useAnchorFlash = () => {
  const location = useLocation();

  useEffect(() => {
    if (location.hash) {
      const id = location.hash.slice(1);
      const element = document.getElementById(id);
      if (element) {
        element.scrollIntoView({ behavior: 'smooth' });
        element.classList.add('flash');
        setTimeout(() => {
          element.classList.remove('flash');
        }, 2000);
      }
    }
  }, [location]);
};

/**
 * ActionButton component for creating a button with an onClick event.
 * Uses Chakra UI Button for consistent styling.
 *
 * @param {() => void} onClick - The function to call when the button is clicked.
 * @param {string} className - Optional class name for styling.
 * @param {React.ReactNode} children - The content of the button.
 * @returns {JSX.Element} A Chakra UI Button component.
 */
export const ActionButton: React.FC<{
  onClick: () => void;
  className?: string;
  children: React.ReactNode;
}> = ({ onClick, className, children }) => (
  <Button
    onClick={onClick}
    className={className}
    size="sm"
    colorScheme="blue"
    _hover={{ transform: 'translateY(-2px)' }}
    transition="all 0.3s ease"
  >
    {children}
  </Button>
);

/**
 * VolumeSlider component for creating a volume control slider.
 *
 * @param {number} value - The current volume level (0 to 1).
 * @param {(value: number) => void} onChange - The function to call when the volume changes.
 * @returns {JSX.Element} An input range element representing the volume slider.
 */
export const VolumeSlider: React.FC<{
  value: number;
  onChange: (value: number) => void;
}> = ({ value, onChange }) => {
  const [localValue, setLocalValue] = React.useState(value);

  useEffect(() => {
    setLocalValue(value);
  }, [value]);

  const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    setLocalValue(parseFloat(e.target.value));
  };

  const handleFinalChange = () => {
    onChange(localValue);
  };

  const handleKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
      handleFinalChange();
    }
  };

  const handleWheel = async (event: React.WheelEvent<HTMLInputElement>) => {
    event.preventDefault();
    const step = 0.01; // Adjust the step size as needed
    let newValue = localValue;

    if (event.deltaY < 0) {
      newValue += step;
    } else {
      newValue -= step;
    }

    newValue = Math.max(0, Math.min(1, newValue)); // Ensure the value is between 0 and 1

    setLocalValue(newValue);
    await onChange(newValue); // Call onChange asynchronously
  };

  return (
    <input
      type="range"
      min="0"
      max="1"
      step="0.01"
      value={localValue}
      onChange={handleChange}
      onMouseUp={handleFinalChange}
      onKeyDown={handleKeyDown}
      onBlur={handleFinalChange}
      onWheel={handleWheel} // Add wheel event listener
    />
  );
};

/**
 * Sorts items based on the provided sort configuration and starred items.
 *
 * @param {T[]} items - The array of items to sort.
 * @param {SortConfig} sortConfig - The sorting configuration.
 * @param {string[]} starredItems - An array of names of starred items.
 * @returns {T[]} A new sorted array of items.
 */
export const getSortedItems = <T extends Record<string, unknown>>(
  items: T[],
  sortConfig: SortConfig,
  starredItems: string[]
): T[] => {
  const sortedItems = [...items].sort((a, b) => {
    if (sortConfig.key === 'favorite') {
      const aStarred = starredItems.includes(a.name);
      const bStarred = starredItems.includes(b.name);
      if (aStarred !== bStarred) {
        return sortConfig.direction === 'asc' ? (aStarred ? -1 : 1) : (aStarred ? 1 : -1);
      }
    }

    if (sortConfig.key === 'enabled') {
      if (a.enabled !== b.enabled) {
        return sortConfig.direction === 'asc' ? (a.enabled ? -1 : 1) : (a.enabled ? 1 : -1);
      }
    }

    if (a[sortConfig.key] < b[sortConfig.key]) return sortConfig.direction === 'asc' ? -1 : 1;
    if (a[sortConfig.key] > b[sortConfig.key]) return sortConfig.direction === 'asc' ? 1 : -1;
    return 0;
  });

  if (sortConfig.key === 'stock') {
    return sortedItems.sort((a, b) => {
      const aStarred = starredItems.includes(a.name);
      const bStarred = starredItems.includes(b.name);
      if (aStarred !== bStarred) return aStarred ? -1 : 1;
      if (a.enabled !== b.enabled) return a.enabled ? -1 : 1;
      return a.name.localeCompare(b.name);
    });
  }

  return sortedItems;
};

/**
 * Determines the next sort direction based on the current sort configuration and clicked key.
 *
 * @param {SortConfig} sortConfig - The current sorting configuration.
 * @param {string} clickedKey - The key that was clicked to change the sort direction.
 * @returns {'asc' | 'desc'} The new sort direction.
 */
export const getNextSortDirection = (sortConfig: SortConfig, clickedKey: string): 'asc' | 'desc' => {
  if (sortConfig.key === clickedKey) {
    return sortConfig.direction === 'asc' ? 'desc' : 'asc';
  }
  return 'asc';
};

/**
 * Gets items sorted by stock status and starred items.
 *
 * @param {T[]} items - The array of items to sort.
 * @param {string[]} starredItems - An array of names of starred items.
 * @returns {T[]} A new sorted array of items.
 */
export const getStockSortedItems = <T extends Record<string, unknown>>(
  items: T[],
  starredItems: string[]
): T[] => {
  return [...items].sort((a, b) => {
    const aStarred = starredItems.includes(a.name);
    const bStarred = starredItems.includes(b.name);
    if (aStarred !== bStarred) return aStarred ? -1 : 1;
    if (a.enabled !== b.enabled) return a.enabled ? -1 : 1;
    return a.name.localeCompare(b.name);
  });
};
