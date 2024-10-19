import React, { useRef } from 'react';
import { Route } from '../api/api';
import RouteItem from './RouteItem';
import { Actions } from '../utils/actions';
import { SortConfig } from '../utils/commonUtils';

interface RouteListProps {
  routes: Route[];
  starredRoutes: string[];
  actions: Actions;
  sortConfig: SortConfig;
  onSort: (key: string) => void;
  hideSpecificButtons?: boolean;
  hideExtraColumns?: boolean;
}

const RouteList: React.FC<RouteListProps> = ({
  routes,
  starredRoutes,
  actions,
  sortConfig,
  onSort,
  hideSpecificButtons = false,
  hideExtraColumns = false,
}) => {
  const routeRefs = useRef<{[key: string]: HTMLTableRowElement}>({});
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
      const newRoutes = [...routes];
      const [removed] = newRoutes.splice(draggedIndex, 1);
      newRoutes.splice(targetIndex, 0, removed);
      // Update the routes order in the parent component or API
      // actions.updateRoutesOrder(newRoutes);
    }
    setDraggedIndex(null);
  };

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
              routeRefs={routeRefs}
              onDragStart={onDragStart}
              onDragEnter={() => {}}
              onDragLeave={() => {}}
              onDragOver={onDragOver}
              onDrop={onDrop}
              onDragEnd={() => setDraggedIndex(null)}
              hideSpecificButtons={hideSpecificButtons}
              hideExtraColumns={hideExtraColumns}
            />
          ))}
        </tbody>
      </table>
    </div>
  );
};

export default RouteList;
