import React from 'react';
import { Route } from '../api/api';
import RouteItem from './RouteItem';
import { SortConfig } from '../utils/commonUtils';

interface RouteListProps {
  routes: Route[];
  starredRoutes: string[];
  onToggleRoute: (name: string) => void;
  onDeleteRoute: (name: string) => void;
  onUpdateVolume: (name: string, volume: number) => void;
  onToggleStar: (name: string) => void;
  onEditRoute: (route: Route) => void;
  onShowEqualizer: (route: Route) => void;
  routeRefs: React.MutableRefObject<{[key: string]: HTMLTableRowElement}>;
  onDragStart: (e: React.DragEvent<HTMLSpanElement>, index: number) => void;
  onDragEnter: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragLeave: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDragOver: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDrop: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragEnd: (e: React.DragEvent<HTMLSpanElement>) => void;
  jumpToAnchor: (name: string) => void;
  renderLinkWithAnchor: (to: string, name: string, icon: string) => React.ReactNode;
  sortConfig: SortConfig;
  onSort: (key: string) => void;
}

const RouteList: React.FC<RouteListProps> = ({
  routes,
  starredRoutes,
  onToggleRoute,
  onDeleteRoute,
  onUpdateVolume,
  onToggleStar,
  onEditRoute,
  onShowEqualizer,
  routeRefs,
  onDragStart,
  onDragEnter,
  onDragLeave,
  onDragOver,
  onDrop,
  onDragEnd,
  jumpToAnchor,
  renderLinkWithAnchor,
  sortConfig,
  onSort
}) => {
  const renderSortIcon = (key: string) => {
    if (sortConfig.key === key) {
      return sortConfig.direction === 'asc' ? ' ▲' : ' ▼';
    }
    return null;
  };

  return (
    <table className="routes-table">
      <thead>
        <tr>
          <th>Reorder</th>
          <th onClick={() => onSort('favorite')}>Favorite{renderSortIcon('favorite')}</th>
          <th onClick={() => onSort('name')}>Name{renderSortIcon('name')}</th>
          <th onClick={() => onSort('source')}>Source{renderSortIcon('source')}</th>
          <th onClick={() => onSort('sink')}>Sink{renderSortIcon('sink')}</th>
          <th onClick={() => onSort('enabled')}>Status{renderSortIcon('enabled')}</th>
          <th onClick={() => onSort('volume')}>Volume{renderSortIcon('volume')}</th>
          <th>Actions</th>
        </tr>
      </thead>
      <tbody>
        {routes.map((route, index) => (
          <RouteItem
            key={route.name}
            route={route}
            index={index}
            isStarred={starredRoutes.includes(route.name)}
            onToggleRoute={onToggleRoute}
            onDeleteRoute={onDeleteRoute}
            onUpdateVolume={onUpdateVolume}
            onToggleStar={onToggleStar}
            onEditRoute={onEditRoute}
            onShowEqualizer={onShowEqualizer}
            routeRefs={routeRefs}
            onDragStart={onDragStart}
            onDragEnter={onDragEnter}
            onDragLeave={onDragLeave}
            onDragOver={onDragOver}
            onDrop={onDrop}
            onDragEnd={onDragEnd}
            jumpToAnchor={jumpToAnchor}
            renderLinkWithAnchor={renderLinkWithAnchor}
          />
        ))}
      </tbody>
    </table>
  );
};

export default RouteList;