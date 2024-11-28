/**
 * React component for displaying a single sink item.
 * This component includes controls for managing sinks such as starring, enabling/disabling,
 * adjusting volume and timeshift, and performing actions like listening to or visualizing the sink.
 *
 * @param {React.FC} props - The properties for the component.
 */
import React, { useState } from 'react';
import { Sink, Route } from '../api/api';
import { Actions } from '../utils/actions';
import { renderLinkWithAnchor } from '../utils/commonUtils';
import StarButton from './controls/StarButton';
import EnableButton from './controls/EnableButton';
import ActionButton from './controls/ActionButton';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';
import ItemRoutes from './controls/ItemRoutes';

/**
 * Interface defining the props for SinkItem component.
 */
interface SinkItemProps {
  /**
   * The sink object representing the sink item.
   */
  sink: Sink;
  /**
   * Boolean indicating if the sink is starred.
   */
  isStarred: boolean;
  /**
   * Actions object containing functions to manage sinks and routes.
   */
  actions: Actions;
  /**
   * Mutable reference object for sink table rows.
   */
  sinkRefs: React.MutableRefObject<{[key: string]: HTMLTableRowElement}>;
  /**
   * Array of active routes associated with the sink.
   */
  activeRoutes: Route[];
  /**
   * Array of disabled routes associated with the sink.
   */
  disabledRoutes: Route[];
  /**
   * Boolean indicating if the sink is currently being listened to.
   */
  isListening: boolean;
  /**
   * Optional boolean to hide specific buttons.
   */
  hideSpecificButtons?: boolean;
  /**
   * Optional boolean to hide extra columns.
   */
  hideExtraColumns?: boolean;
  /**
   * Optional boolean indicating if the component is part of a desktop menu.
   */
  isDesktopMenu?: boolean;
  /**
   * Optional boolean indicating if the sink item is selected.
   */
  isSelected?: boolean;
}

/**
 * React functional component for rendering a single sink item with controls.
 *
 * @param {SinkItemProps} props - The properties for the SinkItem component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const SinkItem: React.FC<SinkItemProps> = ({
  sink,
  isStarred,
  actions,
  sinkRefs,
  activeRoutes,
  disabledRoutes,
  isListening,
  hideSpecificButtons = false,
  hideExtraColumns = false,
  isDesktopMenu = false,
  isSelected = false,
}) => {
  /**
   * State to track if the routes are expanded.
   */
  const [isExpanded, setIsExpanded] = useState(false);

  /**
   * Function to toggle the expansion of routes.
   */
  const toggleExpandRoutes = () => {
    setIsExpanded(!isExpanded);
  };

  return (
    <tr
      ref={(el) => {
        if (el) sinkRefs.current[sink.name] = el;
      }}
      className={`draggable-row ${isSelected ? 'selected' : ''}`}
      id={`sink-${encodeURIComponent(sink.name)}`}
    >
      <td>
        {/**
         * StarButton component to toggle the starred status of the sink.
         */}
        <StarButton
          isStarred={isStarred}
          onClick={() => actions.toggleStar('sinks', sink.name)}
        />
      </td>
      <td>
        {
          /**
           * Render a span or link based on whether the component is part of a desktop menu.
           */
          isDesktopMenu ? (
            <span>{sink.name}</span>
          ) : (
            renderLinkWithAnchor('/sinks', sink.name, 'fa-volume-up')
          )
        }
        {/**
         * ItemRoutes component to display and manage routes associated with the sink.
         */}
        <ItemRoutes
          activeRoutes={activeRoutes}
          disabledRoutes={disabledRoutes}
          isExpanded={isExpanded}
          toggleExpandRoutes={toggleExpandRoutes}
          itemName={sink.name}
          isDesktopMenu={isDesktopMenu}
          onNavigate={isDesktopMenu ? actions.navigateToItem : undefined}
        />
      </td>
      {
        /**
         * Optional column to display the IP address of the sink.
         */
        !hideExtraColumns && <td>{sink.ip}</td>
      }
      <td>
        {/**
         * EnableButton component to toggle the enabled status of the sink.
         */}
        <EnableButton
          isEnabled={sink.enabled}
          onClick={() => actions.toggleEnabled('sinks', sink.name)}
        />
      </td>
      <td>
        {/**
         * VolumeSlider component to adjust the volume of the sink.
         */}
        <VolumeSlider
          value={sink.volume}
          onChange={(value) => actions.updateVolume('sinks', sink.name, value)}
        />
      </td>
      <td>
        {/**
         * TimeshiftSlider component to adjust the timeshift of the sink.
         */}
        <TimeshiftSlider
          value={sink.timeshift || 0}
          onChange={(value) => actions.updateTimeshift('sinks', sink.name, value)}
        />
      </td>
      <td>
        {/**
         * ActionButton components for various actions related to the sink.
         */}
        <>
          <ActionButton onClick={() => actions.showEqualizer(true, 'sinks', sink)}>Equalizer</ActionButton>
          <ActionButton onClick={() => actions.listenToSink(isListening ? null : sink)}>
            {isListening ? 'Stop Listening' : 'Listen'}
          </ActionButton>
          <ActionButton onClick={() => actions.visualizeSink(sink)}>
              Visualize
          </ActionButton>
          {!hideSpecificButtons && (
            <>
              <ActionButton onClick={() => actions.editItem('sinks', sink)}>Edit</ActionButton>
              <ActionButton onClick={() => actions.deleteItem('sinks', sink.name)} className="delete-button">Delete</ActionButton>
            </>
          )}
        </>
      </td>
    </tr>
  );
};

export default SinkItem;
