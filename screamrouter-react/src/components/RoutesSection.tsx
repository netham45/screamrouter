import React from 'react';
import { Route, Source, Sink } from '../api/api';
import { ActionButton, VolumeSlider, renderLinkWithAnchor } from '../utils/commonUtils';

interface RoutesSectionProps {
  title: React.ReactNode;
  routes: Route[];
  sources: Source[];
  sinks: Sink[];
  starredRoutes?: string[];
  toggleEnabled: (type: 'routes', name: string, currentStatus: boolean) => Promise<void>;
  toggleStar?: (type: 'routes', name: string) => void;
  updateVolume: (type: 'routes', name: string, volume: number) => Promise<void>;
  setSelectedItem: (item: any) => void;
  setShowEqualizerModal: (show: boolean) => void;
  setSelectedItemType: (type: 'sources' | 'sinks' | 'routes' | null) => void;
  jumpToAnchor: (name: string) => void;
  isFavoriteView: boolean;
  onDataChange: () => void;
}

const RoutesSection: React.FC<RoutesSectionProps> = ({
  title,
  routes,
  sources,
  sinks,
  starredRoutes,
  toggleEnabled,
  toggleStar,
  updateVolume,
  setSelectedItem,
  setShowEqualizerModal,
  setSelectedItemType,
  jumpToAnchor,
  isFavoriteView,
  onDataChange,
}) => {
  const renderControls = (item: Route) => {
    return (
      <>
        <ActionButton onClick={() => toggleEnabled('routes', item.name, item.enabled)}
          className={item.enabled ? 'enabled' : 'disabled'}
        >
          {item.enabled ? 'Disable' : 'Enable'}
        </ActionButton>
        <VolumeSlider
          value={item.volume}
          onChange={(value) => updateVolume('routes', item.name, value)}
        />
        <ActionButton onClick={() => {
          setSelectedItem(item);
          setSelectedItemType('routes');
          setShowEqualizerModal(true);
          onDataChange();
        }}>
          Equalizer
        </ActionButton>
        {toggleStar && (
          <ActionButton onClick={() => toggleStar('routes', item.name)}>
            {starredRoutes?.includes(item.name) ? '★' : '☆'}
          </ActionButton>
        )}
      </>
    );
  };

  const filteredRoutes = isFavoriteView
    ? routes.filter(route => starredRoutes?.includes(route.name))
    : routes.filter(route => route.enabled && 
        sources.find(s => s.name === route.source)?.enabled &&
        sinks.find(s => s.name === route.sink)?.enabled
      );

  return (
    <div id={isFavoriteView ? "favorite-routes" : "active-routes"} className={isFavoriteView ? "favorite-routes" : "active-routes"}>
      <h2>{title}</h2>
      <table>
        <thead>
          <tr>
            <th>Name</th>
            <th>Controls</th>
          </tr>
        </thead>
        <tbody>
          {filteredRoutes.map(route => (
            <tr key={route.name}>
              <td>
                {renderLinkWithAnchor('/routes', route.name, 'fa-route')}
                <div className="subtext">
                  <div>Source: {renderLinkWithAnchor('/sources', route.source, 'fa-music', 'source')}</div>
                  <div>Sink: {renderLinkWithAnchor('/sinks', route.sink, 'fa-volume-up', 'sink')}</div>
                </div>
              </td>
              <td>{renderControls(route)}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
};

export default RoutesSection;