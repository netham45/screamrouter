import React from 'react';
import { Source, Route } from '../api/api';
import { renderLinkWithAnchor, ActionButton, VolumeSlider } from '../utils/commonUtils';

interface SourceItemProps {
  source: Source;
  index: number;
  isStarred: boolean;
  isActive: boolean;
  onToggleSource: (name: string) => void;
  onDeleteSource: (name: string) => void;
  onUpdateVolume: (volume: number) => void;
  onToggleStar: (name: string) => void;
  onToggleActiveSource: (name: string) => void;
  onEditSource: (source: Source) => void;
  onShowEqualizer: (source: Source) => void;
  onShowVNC: (source: Source) => void;
  onControlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => void;
  sourceRefs: React.MutableRefObject<{[key: string]: HTMLTableRowElement}>;
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

const SourceItem: React.FC<SourceItemProps> = ({
  source,
  index,
  isStarred,
  isActive,
  onToggleSource,
  onDeleteSource,
  onUpdateVolume,
  onToggleStar,
  onToggleActiveSource,
  onEditSource,
  onShowEqualizer,
  onShowVNC,
  onControlSource,
  sourceRefs,
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
            {renderLinkWithAnchor('/routes', route.name, 'fa-route', 'source')}
            {index < displayedRoutes.length - 1 && ', '}
          </React.Fragment>
        ))}
        {hasMore && !isExpanded && (
          <ActionButton onClick={() => toggleExpandRoutes(source.name)} className="expand-routes">
            ...
          </ActionButton>
        )}
      </div>
    );
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
        <ActionButton onClick={() => onToggleStar(source.name)}>
          {isStarred ? '★' : '☆'}
        </ActionButton>
      </td>
      <td>
        <ActionButton 
          onClick={() => onToggleActiveSource(source.name)}
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
        <div className="source-routes">
          {renderRouteList(activeRoutes, true)}
          {renderRouteList(disabledRoutes, false)}
          {isExpanded && (
            <ActionButton onClick={() => toggleExpandRoutes(source.name)} className="collapse-routes">
              Show less
            </ActionButton>
          )}
        </div>
      </td>
      <td>{source.ip}</td>
      <td>
        <ActionButton 
          onClick={() => onToggleSource(source.name)}
          className={source.enabled ? 'enabled' : 'disabled'}
        >
          {source.enabled ? 'Enabled' : 'Disabled'}
        </ActionButton>
      </td>
      <td>
        <VolumeSlider
          value={source.volume}
          onChange={(value) => onUpdateVolume(value)}
        />
        <span>{(source.volume * 100).toFixed(0)}%</span>
      </td>
      <td>
        <ActionButton onClick={() => onEditSource(source)}>Edit</ActionButton>
        <ActionButton onClick={() => onShowEqualizer(source)}>Equalizer</ActionButton>
        {source.vnc_ip && source.vnc_port && (
          <>
            <ActionButton onClick={() => onShowVNC(source)}>VNC</ActionButton>
            <ActionButton onClick={() => onControlSource(source.name, 'prevtrack')}>⏮</ActionButton>
            <ActionButton onClick={() => onControlSource(source.name, 'play')}>⏯</ActionButton>
            <ActionButton onClick={() => onControlSource(source.name, 'nexttrack')}>⏭</ActionButton>
          </>
        )}
        <ActionButton onClick={() => onDeleteSource(source.name)} className="delete-button">Delete</ActionButton>
      </td>
    </tr>
  );
};

export default SourceItem;