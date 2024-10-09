import React from 'react';
import { Source, Route } from '../api/api';
import { ActionButton, VolumeSlider, renderLinkWithAnchor } from '../utils/commonUtils';

interface SourcesSectionProps {
  title: React.ReactNode;
  sources: Source[];
  routes: Route[];
  starredSources?: string[];
  toggleEnabled: (type: 'sources', name: string, currentStatus: boolean) => Promise<void>;
  toggleStar?: (type: 'sources', name: string) => void;
  updateVolume: (type: 'sources', name: string, volume: number) => Promise<void>;
  controlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => Promise<void>;
  setSelectedItem: (item: any) => void;
  setShowVNCModal: (show: boolean) => void;
  setShowEqualizerModal: (show: boolean) => void;
  setSelectedItemType: (type: 'sources' | 'sinks' | 'routes' | null) => void;
  getRoutesForSource: (sourceName: string) => Route[];
  getGroupMembers: (name: string, type: 'sources' | 'sinks') => string;
  jumpToAnchor: (name: string) => void;
  isFavoriteView: boolean;
  onDataChange: () => void;
}

const SourcesSection: React.FC<SourcesSectionProps> = ({
  title,
  sources,
  routes,
  starredSources,
  toggleEnabled,
  toggleStar,
  updateVolume,
  controlSource,
  setSelectedItem,
  setShowVNCModal,
  setShowEqualizerModal,
  setSelectedItemType,
  getRoutesForSource,
  getGroupMembers,
  jumpToAnchor,
  isFavoriteView,
  onDataChange,
}) => {
  const renderControls = (item: Source) => {
    return (
      <>
        <ActionButton onClick={() => toggleEnabled('sources', item.name, item.enabled)}
          className={item.enabled ? 'enabled' : 'disabled'}
        >
          {item.enabled ? 'Disable' : 'Enable'}
        </ActionButton>
        <VolumeSlider
          value={item.volume}
          onChange={(value) => updateVolume('sources', item.name, value)}
        />
        <ActionButton onClick={() => {
          setSelectedItem(item);
          setSelectedItemType('sources');
          setShowEqualizerModal(true);
          onDataChange();
        }}>
          Equalizer
        </ActionButton>
        {'vnc_ip' in item && 'vnc_port' in item && (
          <>
            <ActionButton onClick={() => {
              setSelectedItem(item);
              setShowVNCModal(true);
            }}>
              VNC
            </ActionButton>
            <ActionButton onClick={() => controlSource(item.name, 'prevtrack')}>⏮</ActionButton>
            <ActionButton onClick={() => controlSource(item.name, 'play')}>⏯</ActionButton>
            <ActionButton onClick={() => controlSource(item.name, 'nexttrack')}>⏭</ActionButton>
          </>
        )}
        {toggleStar && (
          <ActionButton onClick={() => toggleStar('sources', item.name)}>
            {starredSources?.includes(item.name) ? '★' : '☆'}
          </ActionButton>
        )}
      </>
    );
  };

  const renderRouteLinks = (routes: Route[]) => {
    if (routes.length === 0) return 'None';
    return routes.map((route, index) => (
      <React.Fragment key={route.name}>
        {index > 0 && ', '}
        {renderLinkWithAnchor('/routes', route.name, 'fa-route', 'source')}
      </React.Fragment>
    ));
  };

  const filteredSources = isFavoriteView
    ? sources.filter(source => starredSources?.includes(source.name))
    : sources.filter(source => source.enabled);

  return (
    <div id={isFavoriteView ? "favorite-sources" : "active-sources"} className={isFavoriteView ? "favorite-sources" : "active-sources"}>
      <h2>{title}</h2>
      <table>
        <thead>
          <tr>
            <th>Name</th>
            <th>Controls</th>
          </tr>
        </thead>
        <tbody>
          {filteredSources.map(source => {
            const sourceRoutes = getRoutesForSource(source.name);
            return (
              <tr key={source.name}>
                <td>
                  {renderLinkWithAnchor('/sources', source.name, 'fa-music')}
                  <div className="subtext">
                    <div>Routes to: {renderRouteLinks(sourceRoutes)}</div>
                    {source.is_group && <div>Group Members: {getGroupMembers(source.name, 'sources')}</div>}
                  </div>
                </td>
                <td>{renderControls(source)}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
};

export default SourcesSection;