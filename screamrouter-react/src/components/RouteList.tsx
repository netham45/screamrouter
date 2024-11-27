/**
 * React component for displaying a list of routes.
 * This component includes sorting functionality, drag-and-drop reordering,
 * and rendering individual route items using the RouteItem component.
 *
 * @param {React.FC} props - The properties for the component.
 */
import React, { useState } from 'react';
import { Route } from '../api/api';
import RouteItem from './RouteItem';
import { Actions } from '../utils/actions';
import { SortConfig } from '../utils/commonUtils';

/**
 * Interface for the properties of RouteList component.
 */
interface RouteListProps {
  routes: Route[];
  starredRoutes: string[];
  actions: Actions;
  sortConfig: SortConfig;
  onSort: (key: string) => void;
  hideSpecificButtons?: boolean;
  isDesktopMenu?: boolean;
  selectedItem?: string | null;
}

/**
 * React functional component for rendering a list of routes.
 *
 * @param {RouteListProps} props - The properties for the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const RouteList: React.FC<RouteListProps> = ({
  routes,
  starredRoutes,
  actions,
  sortConfig,
  onSort,
  hideSpecificButtons = false,
  isDesktopMenu = false,
  selectedItem = null,
}) => {
  /**
   * State to keep track of the index of the currently dragged route item.
   */
  const [draggedIndex, setDraggedIndex] = useState<number | null>(null);

  /**
   * Renders a sort icon based on the current sort configuration.
   *
   * @param {string} key - The key being sorted by.
   * @returns {JSX.Element | null} The rendered JSX element or null if no icon is needed.
   */
  const renderSortIcon = (key: string) => {
    if (sortConfig.key === key) {
      return sortConfig.direction === 'asc' ? ' ▲' : ' ▼';
    }
    return null;
  };

  /**
   * Handles the drag enter event for a route item.
   *
   * @param {React.DragEvent<HTMLTableRowElement>} e - The drag event.
   * @param {number} index - The index of the route item being dragged over.
   */
  const onDragEnter = (e: React.DragEvent<HTMLTableRowElement>, index: number) => {
    e.preventDefault();
    console.log(`Dragged over index: ${index}`);
  };

  /**
   * Handles the drag leave event for a route item.
   *
   * @param {React.DragEvent<HTMLTableRowElement>} e - The drag event.
   */
  const onDragLeave = (e: React.DragEvent<HTMLTableRowElement>) => {
    e.preventDefault();
  };

  /**
   * Handles the drag over event for a route item.
   *
   * @param {React.DragEvent<HTMLTableRowElement>} e - The drag event.
   */
  const onDragOver = (e: React.DragEvent<HTMLTableRowElement>) => {
    e.preventDefault();
  };

  /**
   * Handles the drop event for a route item, reordering routes based on drag-and-drop action.
   *
   * @param {React.DragEvent<HTMLTableRowElement>} e - The drag event.
   * @param {number} targetIndex - The index where the route is being dropped.
   */
  const onDrop = (e: React.DragEvent<HTMLTableRowElement>, targetIndex: number) => {
    e.preventDefault();
    if (draggedIndex !== null && draggedIndex !== targetIndex) {
      const newRoutes = [...routes];
      const [removed] = newRoutes.splice(draggedIndex, 1);
      newRoutes.splice(targetIndex, 0, removed);
      // Update the routes order in the parent component or API
      // actions.updateRoutesOrder(newRoutes);
    }
    setDraggedIndex(null);
  };

  /**
   * Sorts the routes based on the current sort configuration.
   */
  const sortedRoutes = [...routes].sort((a, b) => {
    if (sortConfig.key === 'favorite') {
      const aStarred = starredRoutes.includes(a.name);
      const bStarred = starredRoutes.includes(b.name);
      return sortConfig.direction === 'asc' ? (aStarred === bStarred ? 0 : aStarred ? -1 : 1) : (aStarred === bStarred ? 0 : aStarred ? 1 : -1);
    }
    const aValue = a[sortConfig.key as keyof Route];
    const bValue = b[sortConfig.key as keyof Route];
    if (aValue === undefined || bValue === undefined) {
      return 0;
    }
    if (typeof aValue === 'string' && typeof bValue === 'string') {
      return sortConfig.direction === 'asc' ? aValue.localeCompare(bValue) : bValue.localeCompare(aValue);
    }
    if (aValue < bValue) return sortConfig.direction === 'asc' ? -1 : 1;
    if (aValue > bValue) return sortConfig.direction === 'asc' ? 1 : -1;
    return 0;
  });

  /**
   * Renders the RouteList component.
   *
   * @returns {JSX.Element} The rendered JSX element.
   */
  return (
    <div className="routes-list">
      <table className="routes-table">
        <thead>
          <tr>
            <th onClick={() => onSort('favorite')}>Favorite{renderSortIcon('favorite')}</th>
            <th onClick={() => onSort('name')}>Name{renderSortIcon('name')}</th>
            <th onClick={() => onSort('source')}>Source{renderSortIcon('source')}</th>
            <th onClick={() => onSort('sink')}>Sink{renderSortIcon('sink')}</th>
            <th onClick={() => onSort('enabled')}>Status{renderSortIcon('enabled')}</th>
            <th onClick={() => onSort('volume')}>Volume{renderSortIcon('volume')}</th>
            <th onClick={() => onSort('timeshift')}>Timeshift{renderSortIcon('timeshift')}</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          {sortedRoutes.map((route, index) => (
            <RouteItem
              key={route.name}
              route={route}
              index={index}
              isStarred={starredRoutes.includes(route.name)}
              actions={actions}
              onDragEnter={onDragEnter}
              onDragLeave={onDragLeave}
              onDragOver={onDragOver}
              onDrop={onDrop}
              hideSpecificButtons={hideSpecificButtons}
              isDesktopMenu={isDesktopMenu}
              isSelected={selectedItem === route.name}
            />
          ))}
        </tbody>
      </table>
    </div>
  );
};

export default RouteList;
