/**
 * React component for displaying a list of sinks.
 * This component includes sorting functionality, rendering individual sink items,
 * and managing interactions with sinks such as starring, enabling/disabling,
 * adjusting volume and timeshift, and performing actions like listening to or visualizing the sink.
 *
 * @param {React.FC} props - The properties for the component.
 */
import React, { useRef } from 'react';
import { Sink, Route } from '../api/api';
import SinkItem from './SinkItem';
import { Actions } from '../utils/actions';
import { SortConfig } from '../utils/commonUtils';

/**
 * Interface defining the props for SinkList component.
 */
interface SinkListProps {
  /**
   * Array of sink objects representing the sinks to be displayed.
   */
  sinks: Sink[];
  /**
   * Array of route objects associated with the sinks.
   */
  routes: Route[];
  /**
   * Array of names of starred sinks.
   */
  starredSinks: string[];
  /**
   * Actions object containing functions to manage sinks and routes.
   */
  actions: Actions;
  /**
   * Name of the sink currently being listened to, if any.
   */
  listeningToSink: string | undefined;
  /**
   * Name of the sink currently being visualized, if any.
   */
  visualizingSink: string | undefined;
  /**
   * Configuration for sorting sinks.
   */
  sortConfig: SortConfig;
  /**
   * Function to handle sorting by a specific key.
   */
  onSort: (key: string) => void;
  /**
   * Optional boolean to hide specific buttons in sink items.
   */
  hideSpecificButtons?: boolean;
  /**
   * Optional boolean to hide extra columns in the sink list.
   */
  hideExtraColumns?: boolean;
  /**
   * Optional boolean indicating if the component is part of a desktop menu.
   */
  isDesktopMenu?: boolean;
  /**
   * Optional name of the selected sink item.
   */
  selectedItem?: string | null;
}

/**
 * React functional component for rendering a list of sinks with sorting and interaction controls.
 *
 * @param {SinkListProps} props - The properties for the SinkList component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const SinkList: React.FC<SinkListProps> = ({
  sinks,
  routes,
  starredSinks,
  actions,
  listeningToSink,
  visualizingSink,
  sortConfig,
  onSort,
  hideSpecificButtons = false,
  hideExtraColumns = false,
  isDesktopMenu = false,
  selectedItem = null,
}) => {
  /**
   * Mutable reference object for sink table rows.
   */
  const sinkRefs = useRef<{[key: string]: HTMLTableRowElement}>({});

  /**
   * Function to render a sort icon based on the current sort configuration.
   *
   * @param {string} key - The key by which sinks are sorted.
   * @returns {JSX.Element | null} The rendered JSX element or null if no sort icon is needed.
   */
  const renderSortIcon = (key: string) => {
    if (sortConfig.key === key) {
      return sortConfig.direction === 'asc' ? ' ▲' : ' ▼';
    }
    return null;
  };

  /**
   * Function to get active and disabled routes for a specific sink.
   *
   * @param {string} sinkName - The name of the sink.
   * @returns {{activeRoutes: Route[], disabledRoutes: Route[]}} Object containing arrays of active and disabled routes.
   */
  const getSinkRoutes = (sinkName: string) => {
    const activeRoutes = routes.filter(route => route.sink === sinkName && route.enabled);
    const disabledRoutes = routes.filter(route => route.sink === sinkName && !route.enabled);
    return { activeRoutes, disabledRoutes };
  };

  /**
   * Array of sinks sorted based on the current sort configuration.
   */
  const sortedSinks = [...sinks].sort((a, b) => {
    if (sortConfig.key === 'favorite') {
      const aStarred = starredSinks.includes(a.name);
      const bStarred = starredSinks.includes(b.name);
      return sortConfig.direction === 'asc' ? (aStarred === bStarred ? 0 : aStarred ? -1 : 1) : (aStarred === bStarred ? 0 : aStarred ? 1 : -1);
    }
    const aValue = a[sortConfig.key as keyof Sink];
    const bValue = b[sortConfig.key as keyof Sink];
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
    <div className="sinks-list">
      <table className="sinks-table">
        <thead>
          <tr>
            <th onClick={() => onSort('favorite')}>Favorite{renderSortIcon('favorite')}</th>
            <th onClick={() => onSort('name')}>Name{renderSortIcon('name')}</th>
            {!hideExtraColumns && <th onClick={() => onSort('ip')}>IP Address{renderSortIcon('ip')}</th>}
            <th onClick={() => onSort('enabled')}>Status{renderSortIcon('enabled')}</th>
            <th onClick={() => onSort('volume')}>Volume{renderSortIcon('volume')}</th>
            <th onClick={() => onSort('timeshift')}>Timeshift{renderSortIcon('timeshift')}</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          {sortedSinks.map((sink) => {
            const { activeRoutes, disabledRoutes } = getSinkRoutes(sink.name);
            return (
              <SinkItem
                key={sink.name}
                sink={sink}
                isStarred={starredSinks.includes(sink.name)}
                actions={actions}
                sinkRefs={sinkRefs}
                activeRoutes={activeRoutes}
                disabledRoutes={disabledRoutes}
                isListening={listeningToSink === sink.name}
                isVisualizing={visualizingSink === sink.name}
                hideSpecificButtons={hideSpecificButtons}
                hideExtraColumns={hideExtraColumns}
                isDesktopMenu={isDesktopMenu}
                isSelected={selectedItem === sink.name}
              />
            );
          })}
        </tbody>
      </table>
    </div>
  );
};

export default SinkList;
