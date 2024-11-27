/**
 * React component for displaying a single route item in the routes list.
 * This component includes controls for enabling/disabling, starring, editing, deleting,
 * and adjusting volume and timeshift of the route. It also supports drag-and-drop functionality
 * for reordering routes.
 *
 * @param {React.FC} props - The properties for the component.
 */
import React from 'react';
import { Route } from '../api/api';
import { Actions } from '../utils/actions';
import { renderLinkWithAnchor } from '../utils/commonUtils';
import StarButton from './controls/StarButton';
import EnableButton from './controls/EnableButton';
import ActionButton from './controls/ActionButton';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';

/**
 * Interface for the properties of RouteItem component.
 */
interface RouteItemProps {
  route: Route;
  index: number;
  isStarred: boolean;
  actions: Actions;
  onDragEnter: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragLeave: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDragOver: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDrop: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  hideSpecificButtons?: boolean;
  isDesktopMenu?: boolean;
  isSelected?: boolean;
}

/**
 * React functional component for rendering a single route item.
 *
 * @param {RouteItemProps} props - The properties for the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const RouteItem: React.FC<RouteItemProps> = ({
  route,
  index,
  isStarred,
  actions,
  onDragEnter,
  onDragLeave,
  onDragOver,
  onDrop,
  hideSpecificButtons = false,
  isDesktopMenu = false,
  isSelected = false,
}) => {
  /**
   * Renders the RouteItem component.
   *
   * @returns {JSX.Element} The rendered JSX element.
   */
  return (
    <tr
      onDragEnter={(e) => onDragEnter(e, index)}
      onDragLeave={onDragLeave}
      onDragOver={onDragOver}
      onDrop={(e) => onDrop(e, index)}
      className={`draggable-row ${isSelected ? 'selected' : ''}`}
      id={`route-${encodeURIComponent(route.name)}`}
    >
      <td>
        <StarButton
          isStarred={isStarred}
          onClick={() => actions.toggleStar('routes', route.name)}
        />
      </td>
      <td>
        {isDesktopMenu ? (
          <span>{route.name}</span>
        ) : (
          renderLinkWithAnchor('/routes', route.name, 'fa-route')
        )}
      </td>
      <td>
        {isDesktopMenu ? (
          <span onClick={() => actions.navigateToItem('sources', route.source)} style={{ cursor: 'pointer' }}>
            {route.source}
          </span>
        ) : (
          renderLinkWithAnchor('/sources', route.source, 'fa-music')
        )}
      </td>
      <td>
        {isDesktopMenu ? (
          <span onClick={() => actions.navigateToItem('sinks', route.sink)} style={{ cursor: 'pointer' }}>
            {route.sink}
          </span>
        ) : (
          renderLinkWithAnchor('/sinks', route.sink, 'fa-volume-up')
        )}
      </td>
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
        <ActionButton onClick={() => actions.showEqualizer(true, 'routes', route)}>Equalizer</ActionButton>
        {!hideSpecificButtons && (
          <>
            <ActionButton onClick={() => actions.editItem('routes', route)}>Edit</ActionButton>
            <ActionButton onClick={() => actions.deleteItem('routes', route.name)} className="delete-button">Delete</ActionButton>
          </>
        )}
      </td>
    </tr>
  );
};

export default RouteItem;
