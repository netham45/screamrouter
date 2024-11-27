/**
 * React component for displaying a single source item in a list of sources.
 * It includes functionalities such as starring, enabling/disabling, adjusting volume and timeshift,
 * and performing actions like showing equalizer or VNC controls.
 */
import React, { useState } from 'react';
import { Source, Route } from '../api/api';
import { Actions } from '../utils/actions';
import { renderLinkWithAnchor } from '../utils/commonUtils';
import StarButton from './controls/StarButton';
import EnableButton from './controls/EnableButton';
import ActionButton from './controls/ActionButton';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';
import PlaybackControls from './controls/PlaybackControls';
import ItemRoutes from './controls/ItemRoutes';

/**
 * Interface defining the props for the SourceItem component.
 */
interface SourceItemProps {
  /**
   * The source object containing details about the source.
   */
  source: Source;
  /**
   * The index of the source in the list.
   */
  index: number;
  /**
   * Boolean indicating if the source is starred.
   */
  isStarred: boolean;
  /**
   * Boolean indicating if the source is active.
   */
  isActive: boolean;
  /**
   * Actions object containing functions to manage sources.
   */
  actions: Actions;
  /**
   * React ref object to store references to source table rows.
   */
  sourceRefs: React.MutableRefObject<{[key: string]: HTMLTableRowElement}>;
  /**
   * Function to handle drag enter event on the source row.
   *
   * @param {React.DragEvent<HTMLTableRowElement>} e - The drag event.
   * @param {number} index - The index of the source in the list.
   */
  onDragEnter: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  /**
   * Function to handle drag leave event on the source row.
   *
   * @param {React.DragEvent<HTMLTableRowElement>} e - The drag event.
   */
  onDragLeave: (e: React.DragEvent<HTMLTableRowElement>) => void;
  /**
   * Function to handle drag over event on the source row.
   *
   * @param {React.DragEvent<HTMLTableRowElement>} e - The drag event.
   */
  onDragOver: (e: React.DragEvent<HTMLTableRowElement>) => void;
  /**
   * Function to handle drop event on the source row.
   *
   * @param {React.DragEvent<HTMLTableRowElement>} e - The drag event.
   * @param {number} index - The index of the source in the list.
   */
  onDrop: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  /**
   * Array of active routes associated with the source.
   */
  activeRoutes: Route[];
  /**
   * Array of disabled routes associated with the source.
   */
  disabledRoutes: Route[];
  /**
   * Boolean indicating if specific buttons should be hidden.
   */
  hideSpecificButtons?: boolean;
  /**
   * Boolean indicating if extra columns should be hidden.
   */
  hideExtraColumns?: boolean;
  /**
   * Boolean indicating if the component is being used in the desktop menu.
   */
  isDesktopMenu?: boolean;
  /**
   * Boolean indicating if the source row is selected.
   */
  isSelected?: boolean;
}

/**
 * React functional component for rendering a single source item.
 *
 * @param {SourceItemProps} props - The props passed to the SourceItem component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const SourceItem: React.FC<SourceItemProps> = ({
  source,
  index,
  isStarred,
  isActive,
  actions,
  sourceRefs,
  onDragEnter,
  onDragLeave,
  onDragOver,
  onDrop,
  activeRoutes,
  disabledRoutes,
  hideSpecificButtons = false,
  hideExtraColumns = false,
  isDesktopMenu = false,
  isSelected = false
}) => {
  /**
   * State variable to manage the expanded state of routes.
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
        if (el) sourceRefs.current[source.name] = el;
      }}
      onDragEnter={(e) => onDragEnter(e, index)}
      onDragLeave={onDragLeave}
      onDragOver={onDragOver}
      onDrop={(e) => onDrop(e, index)}
      className={`draggable-row ${isSelected ? 'selected' : ''}`}
      id={`source-${encodeURIComponent(source.name)}`}
    >
      <td>
        <StarButton
          isStarred={isStarred}
          onClick={() => actions.toggleStar('sources', source.name)}
        />
        <br />
        <ActionButton 
          onClick={() => actions.toggleActiveSource(source.name)}
          className={isActive ? 'active' : ''}
        >
          {isActive ? '⬤' : '◯'}
        </ActionButton>
      </td>
      <td>
        {isDesktopMenu ? (
          <span>{source.name}</span>
        ) : (
          renderLinkWithAnchor('/sources', source.name, 'fa-music')
        )}
        {source.is_group && source.group_members && (
          <div className="group-members">
            <span>Group members: </span>
            {source.group_members.map((member, index) => (
              <React.Fragment key={member}>
                {isDesktopMenu ? (
                  <span>{member}</span>
                ) : (
                  renderLinkWithAnchor('/sources', member, 'fa-music')
                )}
                {index < source.group_members.length - 1 && ', '}
              </React.Fragment>
            ))}
          </div>
        )}
        <ItemRoutes
          activeRoutes={activeRoutes}
          disabledRoutes={disabledRoutes}
          isExpanded={isExpanded}
          toggleExpandRoutes={toggleExpandRoutes}
          itemName={source.name}
          isDesktopMenu={isDesktopMenu}
          onNavigate={isDesktopMenu ? actions.navigateToItem : undefined}
        />
      </td>
      {!hideExtraColumns && <td>{source.ip}</td>}
      <td>
        <EnableButton
          isEnabled={source.enabled}
          onClick={() => actions.toggleEnabled('sources', source.name)}
        />
      </td>
      <td>
        <VolumeSlider
          value={source.volume}
          onChange={(value) => actions.updateVolume('sources', source.name, value)}
        />
      </td>
      <td>
        <TimeshiftSlider
          value={source.timeshift || 0}
          onChange={(value) => actions.updateTimeshift('sources', source.name, value)}
        />
      </td>
      <td>
        <ActionButton onClick={() => actions.showEqualizer(true, 'sources', source)}>Equalizer</ActionButton>
        {source.vnc_ip && source.vnc_port && (
          <ActionButton onClick={() => actions.showVNC(true, source)}>VNC</ActionButton>
        )}
        {source.vnc_ip && source.vnc_port && (
          <PlaybackControls
            onPrevTrack={() => actions.controlSource(source.name, 'prevtrack')}
            onPlay={() => actions.controlSource(source.name, 'play')}
            onNextTrack={() => actions.controlSource(source.name, 'nexttrack')}
          />
        )}
        {!hideSpecificButtons && (
          <>
            <ActionButton onClick={() => actions.editItem('sources', source)}>Edit</ActionButton>
            <ActionButton onClick={() => actions.deleteItem('sources', source.name)} className="delete-button">Delete</ActionButton>
          </>
        )}
      </td>
    </tr>
  );
};

export default SourceItem;
