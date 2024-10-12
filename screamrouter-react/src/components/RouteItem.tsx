import React from 'react';
import { Route } from '../api/api';
import { Actions } from '../utils/actions';
import { renderLinkWithAnchor } from '../utils/commonUtils';
import StarButton from './controls/StarButton';
import EnableButton from './controls/EnableButton';
import ActionButton from './controls/ActionButton';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';

interface RouteItemProps {
  route: Route;
  index: number;
  isStarred: boolean;
  actions: Actions;
  routeRefs: React.MutableRefObject<{[key: string]: HTMLTableRowElement}>;
  onDragStart: (e: React.DragEvent<HTMLSpanElement>, index: number) => void;
  onDragEnter: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragLeave: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDragOver: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDrop: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragEnd: (e: React.DragEvent<HTMLSpanElement>) => void;
}

const RouteItem: React.FC<RouteItemProps> = ({
  route,
  index,
  isStarred,
  actions,
  routeRefs,
  onDragStart,
  onDragEnter,
  onDragLeave,
  onDragOver,
  onDrop,
  onDragEnd,
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
        <StarButton
          isStarred={isStarred}
          onClick={() => actions.toggleStar('routes', route.name)}
        />
      </td>
      <td>{renderLinkWithAnchor('/routes', route.name, 'fa-route')}</td>
      <td>{renderLinkWithAnchor('/sources', route.source, 'fa-music')}</td>
      <td>{renderLinkWithAnchor('/sinks', route.sink, 'fa-volume-up')}</td>
      <td>
        <EnableButton
          isEnabled={route.enabled}
          onClick={() => actions.toggleEnabled('routes', route.name)}
        />
      </td>
      <td>
        <VolumeSlider
          value={route.volume}
          onChange={(volume) => actions.updateVolume('routes', route.name, volume)}
        />
      </td>
      <td>
        <TimeshiftSlider
          value={route.timeshift || 0}
          onChange={(timeshift) => actions.updateTimeshift('routes', route.name, timeshift)}
        />
      </td>
      <td>
        <ActionButton onClick={() => actions.editItem('routes', route)}>Edit</ActionButton>
        <ActionButton onClick={() => actions.showEqualizer('routes', route)}>Equalizer</ActionButton>
        <ActionButton onClick={() => actions.deleteItem('routes', route.name)} className="delete-button">Delete</ActionButton>
      </td>
    </tr>
  );
};

export default RouteItem;
