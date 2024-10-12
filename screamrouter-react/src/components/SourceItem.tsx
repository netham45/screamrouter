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

interface SourceItemProps {
  source: Source;
  index: number;
  isStarred: boolean;
  isActive: boolean;
  actions: Actions;
  sourceRefs: React.MutableRefObject<{[key: string]: HTMLTableRowElement}>;
  onDragStart: (e: React.DragEvent<HTMLSpanElement>, index: number) => void;
  onDragEnter: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragLeave: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDragOver: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDrop: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragEnd: (e: React.DragEvent<HTMLSpanElement>) => void;
  activeRoutes: Route[];
  disabledRoutes: Route[];
}

const SourceItem: React.FC<SourceItemProps> = ({
  source,
  index,
  isStarred,
  isActive,
  actions,
  sourceRefs,
  onDragStart,
  onDragEnter,
  onDragLeave,
  onDragOver,
  onDrop,
  onDragEnd,
  activeRoutes,
  disabledRoutes
}) => {
  const [isExpanded, setIsExpanded] = useState(false);

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
      className="draggable-row"
      id={`source-${encodeURIComponent(source.name)}`}
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
          onClick={() => actions.toggleStar('sources', source.name)}
        />
      </td>
      <td>
        <ActionButton 
          onClick={() => actions.toggleActiveSource(source.name)}
          className={isActive ? 'active' : ''}
        >
          {isActive ? '⬤' : '◯'}
        </ActionButton>
      </td>
      <td>
        {renderLinkWithAnchor('/sources', source.name, 'fa-music')}
        {source.is_group && source.group_members && (
          <div className="group-members">
            <span>Group members: </span>
            {source.group_members.map((member, index) => (
              <React.Fragment key={member}>
                {renderLinkWithAnchor('/sources', member, 'fa-music')}
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
        />
      </td>
      <td>{source.ip}</td>
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
        <ActionButton onClick={() => actions.editItem('sources', source)}>Edit</ActionButton>
        <ActionButton onClick={() => actions.showEqualizer('sources', source)}>Equalizer</ActionButton>
        {source.vnc_ip && source.vnc_port && (
          <>
            <ActionButton onClick={() => actions.showVNC(source)}>VNC</ActionButton>
            <PlaybackControls
              onPrevTrack={() => actions.controlSource(source.name, 'prevtrack')}
              onPlay={() => actions.controlSource(source.name, 'play')}
              onNextTrack={() => actions.controlSource(source.name, 'nexttrack')}
            />
          </>
        )}
        <ActionButton onClick={() => actions.deleteItem('sources', source.name)} className="delete-button">Delete</ActionButton>
      </td>
    </tr>
  );
};

export default SourceItem;
