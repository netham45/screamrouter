import React, { useEffect } from 'react';
import { Link, useLocation } from 'react-router-dom';

export interface SortConfig {
  key: string;
  direction: 'asc' | 'desc';
}

export const renderLinkWithAnchor = (to: string, name: string, icon: string, fromType?: 'sink' | 'source') => {
  const queryParam = fromType ? `?sort=${fromType}&direction=asc` : '';
  const anchor = `#${to.replace(/^\//,'').replace(/s$/,'')}-${encodeURIComponent(name)}`;
  return (
    <Link to={`${to}${queryParam}${anchor}`}>
      <i className={`fas ${icon}`}></i> {name}
    </Link>
  );
};

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

export const ActionButton: React.FC<{
  onClick: () => void;
  className?: string;
  children: React.ReactNode;
}> = ({ onClick, className, children }) => (
  <button onClick={onClick} className={className}>
    {children}
  </button>
);

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

export const getSortedItems = <T extends Record<string, any>>(
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

export const getNextSortDirection = (sortConfig: SortConfig, clickedKey: string): 'asc' | 'desc' => {
  if (sortConfig.key === clickedKey) {
    return sortConfig.direction === 'asc' ? 'desc' : 'asc';
  }
  return 'asc';
};

export const getStockSortedItems = <T extends Record<string, any>>(
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
