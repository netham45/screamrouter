import React, { useRef } from 'react';
import { Sink, Route } from '../api/api';
import SinkItem from './SinkItem';
import { Actions } from '../utils/actions';
import { SortConfig } from '../utils/commonUtils';

interface SinkListProps {
  sinks: Sink[];
  routes: Route[];
  starredSinks: string[];
  actions: Actions;
  listeningToSink: string | undefined;
  visualizingSink: string | undefined;
  sortConfig: SortConfig;
  onSort: (key: string) => void;
  hideSpecificButtons?: boolean;
  hideExtraColumns?: boolean;
  isDesktopMenu?: boolean;
  selectedItem?: string | null;
}

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
  const sinkRefs = useRef<{[key: string]: HTMLTableRowElement}>({});

  const renderSortIcon = (key: string) => {
    if (sortConfig.key === key) {
      return sortConfig.direction === 'asc' ? ' ▲' : ' ▼';
    }
    return null;
  };

  const getSinkRoutes = (sinkName: string) => {
    const activeRoutes = routes.filter(route => route.sink === sinkName && route.enabled);
    const disabledRoutes = routes.filter(route => route.sink === sinkName && !route.enabled);
    return { activeRoutes, disabledRoutes };
  };

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
