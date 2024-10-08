import React from 'react';
import { Source, Sink, Route } from '../api/api';
import { renderLinkWithAnchor } from '../utils/commonUtils';

interface FavoriteSectionProps {
  title: React.ReactNode;
  items: any[];
  type: 'sources' | 'sinks' | 'routes';
  starredItems: string[];
  toggleEnabled: (type: 'sources' | 'sinks' | 'routes', name: string, enabled: boolean) => void;
  toggleStar: (type: 'sources' | 'sinks' | 'routes', name: string) => void;
  setSelectedItem: (item: any) => void;
  setSelectedItemType: (type: 'sources' | 'sinks' | 'routes') => void;
  setShowEditModal: (show: boolean) => void;
  toggleActive?: (name: string) => void;
  activeSource?: string | null;
  controlSource?: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => void;
  setShowVNCModal?: (show: boolean) => void;
  updateVolume: (type: 'sources' | 'sinks' | 'routes', name: string, volume: number) => void;
  renderControls: (item: any, type: 'sources' | 'sinks' | 'routes') => React.ReactNode;
  getRoutesForSource: (sourceName: string) => Route[];
  getRoutesForSink: (sinkName: string) => Route[];
  getGroupMembers: (name: string, type: 'sources' | 'sinks') => string;
  jumpToAnchor: (name: string) => void;
}

const FavoriteSection: React.FC<FavoriteSectionProps> = ({
  title,
  items,
  type,
  starredItems,
  renderControls,
  getRoutesForSource,
  getRoutesForSink,
  getGroupMembers,
  jumpToAnchor
}) => {
  const filteredItems = items.filter(item => starredItems.includes(item.name));

  const getIconClass = (itemType: 'sources' | 'sinks' | 'routes') => {
    switch (itemType) {
      case 'sources':
        return 'fa-music';
      case 'sinks':
        return 'fa-volume-up';
      case 'routes':
        return 'fa-route';
      default:
        return '';
    }
  };

  const renderRouteLinks = (routes: Route[], routeType: 'to' | 'from') => {
    if (routes.length === 0) return 'None';
    return routes.map((route, index) => (
      <React.Fragment key={route.name}>
        {index > 0 && ', '}
        {renderLinkWithAnchor(
          routeType === 'to' ? '/sinks' : '/sources',
          routeType === 'to' ? route.sink : route.source,
          routeType === 'to' ? 'fa-volume-up' : 'fa-music'
        )}
      </React.Fragment>
    ));
  };

  const renderGroupMembers = (item: Source | Sink) => {
    if (!item.is_group || !item.group_members) return null;
    return (
      <div className="group-members">
        <span>Group members: </span>
        {item.group_members.map((member, index) => (
          <React.Fragment key={member}>
            {index > 0 && ', '}
            <a href={`#${type.slice(0, -1)}-${encodeURIComponent(member)}`} onClick={(e) => { e.preventDefault(); jumpToAnchor(member); }}>
              {member}
            </a>
          </React.Fragment>
        ))}
      </div>
    );
  };

  return (
    <div className="favorite-section">
      <h2>{title}</h2>
      <table>
        <thead>
          <tr>
            <th>Name</th>
            <th>Controls</th>
          </tr>
        </thead>
        <tbody>
          {filteredItems.map(item => (
            <tr key={item.name}>
              <td>
                {renderLinkWithAnchor(`/${type}`, item.name, getIconClass(type))}
                <div className="subtext">
                  {type !== 'routes' && (
                    <>
                      <div>Routes {type === 'sources' ? 'to' : 'from'}: {renderRouteLinks(type === 'sources' ? getRoutesForSource(item.name) : getRoutesForSink(item.name), type === 'sources' ? 'to' : 'from')}</div>
                      {renderGroupMembers(item)}
                    </>
                  )}
                  {type === 'routes' && (
                    <>
                      <div>Source: {renderLinkWithAnchor('/sources', item.source, 'fa-music')}</div>
                      <div>Sink: {renderLinkWithAnchor('/sinks', item.sink, 'fa-volume-up')}</div>
                    </>
                  )}
                </div>
              </td>
              <td>
                {renderControls(item, type)}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
};

export default FavoriteSection;