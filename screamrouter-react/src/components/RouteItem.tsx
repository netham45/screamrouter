import React from 'react';
import { Route } from '../api/api';
import { ActionButton, VolumeSlider } from '../utils/commonUtils';

/**
 * Props for the RouteItem component
 */
interface RouteItemProps {
  route: Route;
  index: number;
  isStarred: boolean;
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
 * RouteItem component represents a single route in the RouteList
 * @param {RouteItemProps} props - The props for the RouteItem component
 * @returns {React.FC} A functional component representing a single route
 */
const RouteItem: React.FC<RouteItemProps> = ({
  route,
  index,
  isStarred,
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
    <tr
      ref={(el) => {
        if (el) routeRefs.current[route.name] = el;
      }}
      onDragEnter={(e) => onDragEnter(e, index)}
      onDragLeave={onDragLeave}
      onDragOver={onDragOver}
      onDrop={(e) => onDrop(e, index)}
      className="draggable-row"
      id={`route-${encodeURIComponent(route.name)}`}
    >
      <td>
        <span
          className="drag-handle"
          draggable
          onDragStart={(e) => onDragStart(e, index)}
          onDragEnd={onDragEnd}
        >
          ☰
        </span>
      </td>
      <td>
        <ActionButton onClick={() => onToggleStar(route.name)}>
          {isStarred ? '★' : '☆'}
        </ActionButton>
      </td>
      <td>
        {renderLinkWithAnchor('/routes', route.name, 'fa-route')}
      </td>
      <td>
        {renderLinkWithAnchor('/sources', route.source, 'fa-music')}
      </td>
      <td>
        {renderLinkWithAnchor('/sinks', route.sink, 'fa-volume-up')}
      </td>
      <td>
        <ActionButton 
          onClick={() => onToggleRoute(route.name)}
          className={route.enabled ? 'enabled' : 'disabled'}
        >
          {route.enabled ? 'Enabled' : 'Disabled'}
        </ActionButton>
      </td>
      <td>
        <VolumeSlider
          value={route.volume}
          onChange={(value) => onUpdateVolume(route.name, value)}
        />
        <span>{(route.volume * 100).toFixed(0)}%</span>
      </td>
      <td>
        <ActionButton onClick={() => onEditRoute(route)}>Edit</ActionButton>
        <ActionButton onClick={() => onShowEqualizer(route)}>Equalizer</ActionButton>
        <ActionButton onClick={() => onDeleteRoute(route.name)} className="delete-button">Delete</ActionButton>
      </td>
    </tr>
  );
};

export default RouteItem;