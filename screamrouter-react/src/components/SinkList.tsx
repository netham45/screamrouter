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
  listeningToSink: Sink | null;
  visualizingSink: Sink | null;
  sortConfig: SortConfig;
  onSort: (key: string) => void;
  hideSpecificButtons?: boolean;
  hideExtraColumns?: boolean;
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
}) => {
  const sinkRefs = useRef<{[key: string]: HTMLTableRowElement}>({});
  const [draggedIndex, setDraggedIndex] = React.useState<number | null>(null);

  const renderSortIcon = (key: string) => {
    if (sortConfig.key === key) {
      return sortConfig.direction === 'asc' ? ' ▲' : ' ▼';
    }
    return null;
  };

  const onDragStart = (e: React.DragEvent<HTMLSpanElement>, index: number) => {
    setDraggedIndex(index);
    e.dataTransfer.setData('text/plain', index.toString());
  };

  const onDragOver = (e: React.DragEvent<HTMLTableRowElement>) => {
    e.preventDefault();
  };

  const onDrop = (e: React.DragEvent<HTMLTableRowElement>, targetIndex: number) => {
    e.preventDefault();
    if (draggedIndex !== null && draggedIndex !== targetIndex) {
      const newSinks = [...sinks];
      const [removed] = newSinks.splice(draggedIndex, 1);
      newSinks.splice(targetIndex, 0, removed);
      // Update the sinks order in the parent component or API
      // actions.updateSinksOrder(newSinks);
    }
    setDraggedIndex(null);
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
          {sortedSinks.map((sink, index) => {
            const { activeRoutes, disabledRoutes } = getSinkRoutes(sink.name);
            return (
              <SinkItem
                key={sink.name}
                sink={sink}
                index={index}
                isStarred={starredSinks.includes(sink.name)}
                actions={actions}
                sinkRefs={sinkRefs}
                onDragStart={onDragStart}
                onDragEnter={() => {}}
                onDragLeave={() => {}}
                onDragOver={onDragOver}
                onDrop={onDrop}
                onDragEnd={() => setDraggedIndex(null)}
                activeRoutes={activeRoutes}
                disabledRoutes={disabledRoutes}
                isListening={listeningToSink?.name === sink.name}
                isVisualizing={visualizingSink?.name === sink.name}
                hideSpecificButtons={hideSpecificButtons}
                hideExtraColumns={hideExtraColumns}
              />
            );
          })}
        </tbody>
      </table>
    </div>
  );
};

export default SinkList;
