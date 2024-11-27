/**
 * React component for displaying a list of sources.
 * It includes functionalities such as sorting, dragging and dropping sources,
 * and rendering individual source items using the SourceItem component.
 */
import React, { useRef } from 'react';
import { Source, Route } from '../api/api';
import SourceItem from './SourceItem';
import { Actions } from '../utils/actions';
import { SortConfig } from '../utils/commonUtils';

/**
 * Interface defining the props for the SourceList component.
 */
interface SourceListProps {
  /**
   * Array of source objects containing details about each source.
   */
  sources: Source[];
  /**
   * Array of route objects containing details about each route.
   */
  routes: Route[];
  /**
   * Array of names of starred sources.
   */
  starredSources: string[];
  /**
   * Name of the currently active source.
   */
  activeSource: string | null;
  /**
   * Actions object containing functions to manage sources and routes.
   */
  actions: Actions;
  /**
   * Sort configuration object defining the current sort key and direction.
   */
  sortConfig: SortConfig;
  /**
   * Function to handle sorting of sources based on a given key.
   *
   * @param {string} key - The key to sort by (e.g., 'name', 'ip').
   */
  onSort: (key: string) => void;
  /**
   * Boolean indicating if specific buttons should be hidden in source items.
   */
  hideSpecificButtons?: boolean;
  /**
   * Boolean indicating if extra columns should be hidden in the sources table.
   */
  hideExtraColumns?: boolean;
  /**
   * Boolean indicating if the component is being used in the desktop menu.
   */
  isDesktopMenu?: boolean;
  /**
   * Name of the currently selected source item.
   */
  selectedItem?: string | null;
}

/**
 * React functional component for rendering a list of sources.
 *
 * @param {SourceListProps} props - The props passed to the SourceList component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const SourceList: React.FC<SourceListProps> = ({
  sources,
  routes,
  starredSources,
  activeSource,
  actions,
  sortConfig,
  onSort,
  hideSpecificButtons = false,
  hideExtraColumns = false,
  isDesktopMenu = false,
  selectedItem = null,
}) => {
  /**
   * React ref object to store references to source table rows.
   */
  const sourceRefs = useRef<{[key: string]: HTMLTableRowElement}>({});
  /**
   * State variable to manage the index of the currently dragged source item.
   */
  const [draggedIndex, setDraggedIndex] = React.useState<number | null>(null);

  /**
   * Function to render a sort icon based on the current sort configuration.
   *
   * @param {string} key - The key being sorted by (e.g., 'name', 'ip').
   * @returns {JSX.Element | null} The rendered JSX element or null if not sorting by this key.
   */
  const renderSortIcon = (key: string) => {
    if (sortConfig.key === key) {
      return sortConfig.direction === 'asc' ? ' ▲' : ' ▼';
    }
    return null;
  };

  /**
   * Function to handle drag enter event on a source row.
   *
   * @param {React.DragEvent<HTMLTableRowElement>} e - The drag event.
   * @param {number} index - The index of the source in the list.
   */
  const onDragEnter = (e: React.DragEvent<HTMLTableRowElement>, index: number) => {
    e.preventDefault();
    console.log(`Dragged over index: ${index}`);
  };

  /**
   * Function to handle drag leave event on a source row.
   *
   * @param {React.DragEvent<HTMLTableRowElement>} e - The drag event.
   */
  const onDragLeave = (e: React.DragEvent<HTMLTableRowElement>) => {
    e.preventDefault();
  };

  /**
   * Function to handle drag over event on a source row.
   *
   * @param {React.DragEvent<HTMLTableRowElement>} e - The drag event.
   */
  const onDragOver = (e: React.DragEvent<HTMLTableRowElement>) => {
    e.preventDefault();
  };

  /**
   * Function to handle drop event on a source row.
   *
   * @param {React.DragEvent<HTMLTableRowElement>} e - The drag event.
   * @param {number} targetIndex - The index of the target source in the list.
   */
  const onDrop = (e: React.DragEvent<HTMLTableRowElement>, targetIndex: number) => {
    e.preventDefault();
    if (draggedIndex !== null && draggedIndex !== targetIndex) {
      const newSources = [...sources];
      const [removed] = newSources.splice(draggedIndex, 1);
      newSources.splice(targetIndex, 0, removed);
      // Update the sources order in the parent component or API
      // actions.updateSourcesOrder(newSources);
    }
    setDraggedIndex(null);
  };

  /**
   * Function to get active and disabled routes for a given source.
   *
   * @param {string} sourceName - The name of the source.
   * @returns {{activeRoutes: Route[], disabledRoutes: Route[]}} Object containing arrays of active and disabled routes.
   */
  const getSourceRoutes = (sourceName: string) => {
    const activeRoutes = routes.filter(route => route.source === sourceName && route.enabled);
    const disabledRoutes = routes.filter(route => route.source === sourceName && !route.enabled);
    return { activeRoutes, disabledRoutes };
  };

  /**
   * Array of sources sorted based on the current sort configuration.
   */
  const sortedSources = [...sources].sort((a, b) => {
    if (sortConfig.key === 'favorite') {
      const aStarred = starredSources.includes(a.name);
      const bStarred = starredSources.includes(b.name);
      return sortConfig.direction === 'asc' ? (aStarred === bStarred ? 0 : aStarred ? -1 : 1) : (aStarred === bStarred ? 0 : aStarred ? 1 : -1);
    }
    if (sortConfig.key === 'active') {
      const aActive = activeSource === a.name;
      const bActive = activeSource === b.name;
      return sortConfig.direction === 'asc' ? (aActive === bActive ? 0 : aActive ? -1 : 1) : (aActive === bActive ? 0 : aActive ? 1 : -1);
    }
    const aValue = a[sortConfig.key as keyof Source];
    const bValue = b[sortConfig.key as keyof Source];
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

  return (
    <div className="sources-list">
      <table className="sources-table">
        <thead>
          <tr>
            <th onClick={() => onSort('favorite')}>Favorite<br/>Primary{renderSortIcon('favorite')}</th>
            <th onClick={() => onSort('name')}>Name{renderSortIcon('name')}</th>
            {!hideExtraColumns && <th onClick={() => onSort('ip')}>IP Address{renderSortIcon('ip')}</th>}
            <th onClick={() => onSort('enabled')}>Status{renderSortIcon('enabled')}</th>
            <th onClick={() => onSort('volume')}>Volume{renderSortIcon('volume')}</th>
            <th onClick={() => onSort('timeshift')}>Timeshift{renderSortIcon('timeshift')}</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          {sortedSources.map((source, index) => {
            const { activeRoutes, disabledRoutes } = getSourceRoutes(source.name);
            return (
              <SourceItem
                key={source.name}
                source={source}
                index={index}
                isStarred={starredSources.includes(source.name)}
                isActive={activeSource === source.name}
                actions={actions}
                sourceRefs={sourceRefs}
                onDragEnter={onDragEnter}
                onDragLeave={onDragLeave}
                onDragOver={onDragOver}
                onDrop={onDrop}
                activeRoutes={activeRoutes}
                disabledRoutes={disabledRoutes}
                hideSpecificButtons={hideSpecificButtons}
                hideExtraColumns={hideExtraColumns}
                isDesktopMenu={isDesktopMenu}
                isSelected={selectedItem === source.name}
              />
            );
          })}
        </tbody>
      </table>
    </div>
  );
};

export default SourceList;
