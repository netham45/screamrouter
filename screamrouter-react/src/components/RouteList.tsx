import React from 'react';
import { Route } from '../api/api';
import RouteItem from './RouteItem';

/**
 * Props for the RouteList component
 */
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
}

/**
 * RouteList component displays a list of routes and handles their interactions
 * @param {RouteListProps} props - The props for the RouteList component
 * @returns {React.FC} A functional component representing the list of routes
 */
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
  renderLinkWithAnchor
}) => {
  return (
    <table className="routes-table">
      <thead>
        <tr>
          <th>Reorder</th>
          <th>Favorite</th>
          <th>Name</th>
          <th>Source</th>
          <th>Sink</th>
          <th>Status</th>
          <th>Volume</th>
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