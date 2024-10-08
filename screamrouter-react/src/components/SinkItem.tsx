import React from 'react';
import { Sink, Route } from '../api/api';
import { renderLinkWithAnchor, ActionButton, VolumeSlider } from '../utils/commonUtils';

interface SinkItemProps {
  sink: Sink;
  index: number;
  isStarred: boolean;
  onToggleSink: (name: string) => void;
  onDeleteSink: (name: string) => void;
  onUpdateVolume: (name: string, volume: number) => void;
  onToggleStar: (name: string) => void;
  onEditSink: (sink: Sink) => void;
  onShowEqualizer: (sink: Sink) => void;
  onListenToSink: (sink: Sink | null) => Promise<void>;
  onVisualizeSink: (sink: Sink | null) => void;
  isListening: boolean;
  isVisualizing: boolean;
  sinkRefs: React.MutableRefObject<{[key: string]: HTMLTableRowElement}>;
  onDragStart: (e: React.DragEvent<HTMLSpanElement>, index: number) => void;
  onDragEnter: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragLeave: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDragOver: (e: React.DragEvent<HTMLTableRowElement>) => void;
  onDrop: (e: React.DragEvent<HTMLTableRowElement>, index: number) => void;
  onDragEnd: (e: React.DragEvent<HTMLSpanElement>) => void;
  jumpToAnchor: (name: string) => void;
  activeRoutes: Route[];
  disabledRoutes: Route[];
  isExpanded: boolean;
  toggleExpandRoutes: (name: string) => void;
}

const SinkItem: React.FC<SinkItemProps> = ({
  sink,
  index,
  isStarred,
  onToggleSink,
  onDeleteSink,
  onUpdateVolume,
  onToggleStar,
  onEditSink,
  onShowEqualizer,
  onListenToSink,
  onVisualizeSink,
  isListening,
  isVisualizing,
  sinkRefs,
  onDragStart,
  onDragEnter,
  onDragLeave,
  onDragOver,
  onDrop,
  onDragEnd,
  jumpToAnchor,
  activeRoutes,
  disabledRoutes,
  isExpanded,
  toggleExpandRoutes
}) => {
  const renderRouteList = (routes: Route[], isEnabled: boolean) => {
    if (routes.length === 0) return null;

    const displayedRoutes = isExpanded ? routes : routes.slice(0, 3);
    const hasMore = routes.length > 3;

    return (
      <div className={`route-list ${isEnabled ? 'enabled' : 'disabled'}`}>
        <span className="route-list-label">{isEnabled ? 'Enabled routes:' : 'Disabled routes:'}</span>
        {displayedRoutes.map((route, index) => (
          <React.Fragment key={route.name}>
            {renderLinkWithAnchor('/routes', route.name, 'fa-route')}
            {index < displayedRoutes.length - 1 && ', '}
          </React.Fragment>
        ))}
        {hasMore && !isExpanded && (
          <ActionButton onClick={() => toggleExpandRoutes(sink.name)} className="expand-routes">
            ...
          </ActionButton>
        )}
      </div>
    );
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
        <ActionButton onClick={() => onToggleStar(sink.name)}>
          {isStarred ? '★' : '☆'}
        </ActionButton>
      </td>
      <td>
        {renderLinkWithAnchor('/sinks', sink.name, 'fa-volume-up')}
        {sink.is_group && sink.group_members && (
          <div className="group-members">
            <span>Group members: </span>
            {sink.group_members.map((member, index) => (
              <React.Fragment key={member}>
                {renderLinkWithAnchor('/sinks', member, 'fa-volume-up')}
                {index < sink.group_members.length - 1 && ', '}
              </React.Fragment>
            ))}
          </div>
        )}
        <div className="sink-routes">
          {renderRouteList(activeRoutes, true)}
          {renderRouteList(disabledRoutes, false)}
          {isExpanded && (
            <ActionButton onClick={() => toggleExpandRoutes(sink.name)} className="collapse-routes">
              Show less
            </ActionButton>
          )}
        </div>
      </td>
      <td>{sink.ip}</td>
      <td>{sink.port}</td>
      <td>
        <ActionButton 
          onClick={() => onToggleSink(sink.name)}
          className={sink.enabled ? 'enabled' : 'disabled'}
        >
          {sink.enabled ? 'Enabled' : 'Disabled'}
        </ActionButton>
      </td>
      <td>
        <VolumeSlider
          value={sink.volume}
          onChange={(value) => onUpdateVolume(sink.name, value)}
        />
        <span>{(sink.volume * 100).toFixed(0)}%</span>
      </td>
      <td>
        <ActionButton onClick={() => onEditSink(sink)}>Edit</ActionButton>
        <ActionButton onClick={() => onShowEqualizer(sink)}>Equalizer</ActionButton>
        <ActionButton 
          onClick={() => onListenToSink(isListening ? null : sink)}
          className={isListening ? 'listening' : ''}
        >
          {isListening ? 'Stop Listening' : 'Listen'}
        </ActionButton>
        <ActionButton 
          onClick={() => onVisualizeSink(isVisualizing ? null : sink)}
          className={isVisualizing ? 'visualizing' : ''}
        >
          {isVisualizing ? 'Stop Visualizer' : 'Visualize'}
        </ActionButton>
        <ActionButton onClick={() => onDeleteSink(sink.name)} className="delete-button">Delete</ActionButton>
      </td>
    </tr>
  );
};

export default SinkItem;