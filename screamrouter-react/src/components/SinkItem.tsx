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

interface SinkItemProps {
  sink: Sink;
  index: number;
  isStarred: boolean;
  actions: Actions;
  sinkRefs: React.MutableRefObject<{[key: string]: HTMLTableRowElement}>;
  onDragStart: (e: React.DragEvent<HTMLSpanElement>, index: number) => void;
  onDragEnter: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragLeave: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDragOver: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDrop: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragEnd: (e: React.DragEvent<HTMLSpanElement>) => void;
  activeRoutes: Route[];
  disabledRoutes: Route[];
  isListening: boolean;
  isVisualizing: boolean;
  hideSpecificButtons?: boolean;
  hideExtraColumns?: boolean;
}

const SinkItem: React.FC<SinkItemProps> = ({
  sink,
  index,
  isStarred,
  actions,
  sinkRefs,
  onDragStart,
  onDragEnter,
  onDragLeave,
  onDragOver,
  onDrop,
  onDragEnd,
  activeRoutes,
  disabledRoutes,
  isListening,
  isVisualizing,
  hideSpecificButtons = false,
  hideExtraColumns = false,
}) => {
  const [isExpanded, setIsExpanded] = useState(false);

  const toggleExpandRoutes = () => {
    setIsExpanded(!isExpanded);
  };

  return (
    <tr
      ref={(el) => {
        if (el) sinkRefs.current[sink.name] = el;
      }}
      onDragEnter={(e) => onDragEnter(e, index)}
      onDragLeave={onDragLeave}
      onDragOver={onDragOver}
      onDrop={(e) => onDrop(e, index)}
      className="draggable-row"
      id={`sink-${encodeURIComponent(sink.name)}`}
    >
      <td>
        <StarButton
          isStarred={isStarred}
          onClick={() => actions.toggleStar('sinks', sink.name)}
        />
      </td>
      <td>
        {renderLinkWithAnchor('/sinks', sink.name, 'fa-volume-up')}
        <ItemRoutes
          activeRoutes={activeRoutes}
          disabledRoutes={disabledRoutes}
          isExpanded={isExpanded}
          toggleExpandRoutes={toggleExpandRoutes}
          itemName={sink.name}
        />
      </td>
      {!hideExtraColumns && <td>{sink.ip}</td>}
      <td>
        <EnableButton
          isEnabled={sink.enabled}
          onClick={() => actions.toggleEnabled('sinks', sink.name)}
        />
      </td>
      <td>
        <VolumeSlider
          value={sink.volume}
          onChange={(value) => actions.updateVolume('sinks', sink.name, value)}
        />
      </td>
      <td>
        <TimeshiftSlider
          value={sink.timeshift || 0}
          onChange={(value) => actions.updateTimeshift('sinks', sink.name, value)}
        />
      </td>
      <td>
        <ActionButton onClick={() => actions.showEqualizer(true, 'sinks', sink)}>Equalizer</ActionButton>
        {!hideSpecificButtons && (
          <>
            <ActionButton onClick={() => actions.editItem('sinks', sink)}>Edit</ActionButton>
            <ActionButton onClick={() => actions.listenToSink(isListening ? null : sink)}>
              {isListening ? 'Stop Listening' : 'Listen'}
            </ActionButton>
            <ActionButton onClick={() => actions.visualizeSink(isVisualizing ? null : sink)}>
              {isVisualizing ? 'Stop Visualizing' : 'Visualize'}
            </ActionButton>
            <ActionButton onClick={() => actions.deleteItem('sinks', sink.name)} className="delete-button">Delete</ActionButton>
          </>
        )}
      </td>
    </tr>
  );
};

export default SinkItem;
