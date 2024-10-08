import React from 'react';
import { Sink, Route } from '../api/api';
import { ActionButton, VolumeSlider } from '../utils/commonUtils';
import { renderLinkWithAnchor } from '../utils/commonUtils';

interface SinksSectionProps {
  title: React.ReactNode;
  sinks: Sink[];
  routes: Route[];
  starredSinks?: string[];
  toggleEnabled: (type: 'sinks', name: string, currentStatus: boolean) => Promise<void>;
  toggleStar?: (type: 'sinks', name: string) => void;
  updateVolume: (type: 'sinks', name: string, volume: number) => Promise<void>;
  setSelectedItem: (item: any) => void;
  setShowEqualizerModal: (show: boolean) => void;
  setSelectedItemType: (type: 'sources' | 'sinks' | 'routes' | null) => void;
  getRoutesForSink: (sinkName: string) => Route[];
  getGroupMembers: (name: string, type: 'sources' | 'sinks') => string;
  jumpToAnchor: (name: string) => void;
  isFavoriteView: boolean;
  onListenToSink: (sink: Sink | null) => void;
  onVisualizeSink: (sink: Sink | null) => void;
  listeningToSink: Sink | null;
  visualizingSink: Sink | null;
  onDataChange: () => void;
}

const SinksSection: React.FC<SinksSectionProps> = ({
  title,
  sinks,
  routes,
  starredSinks,
  toggleEnabled,
  toggleStar,
  updateVolume,
  setSelectedItem,
  setShowEqualizerModal,
  setSelectedItemType,
  getRoutesForSink,
  getGroupMembers,
  jumpToAnchor,
  isFavoriteView,
  onListenToSink,
  onVisualizeSink,
  listeningToSink,
  visualizingSink,
  onDataChange,
}) => {
  const renderControls = (item: Sink) => {
    return (
      <>
        <ActionButton onClick={() => toggleEnabled('sinks', item.name, item.enabled)}
          className={item.enabled ? 'enabled' : 'disabled'}
        >
          {item.enabled ? 'Disable' : 'Enable'}
        </ActionButton>
        <VolumeSlider
          value={item.volume}
          onChange={(value) => updateVolume('sinks', item.name, value)}
        />
        <ActionButton onClick={() => {
          setSelectedItem(item);
          setSelectedItemType('sinks');
          setShowEqualizerModal(true);
          onDataChange();
        }}>
          Equalizer
        </ActionButton>
        <ActionButton
          onClick={() => onListenToSink(listeningToSink?.name === item.name ? null : item)}
          className={listeningToSink?.name === item.name ? 'listening' : ''}
        >
          {listeningToSink?.name === item.name ? 'Stop Listening' : 'Listen'}
        </ActionButton>
        <ActionButton
          onClick={() => onVisualizeSink(visualizingSink?.name === item.name ? null : item)}
          className={visualizingSink?.name === item.name ? 'visualizing' : ''}
        >
          {visualizingSink?.name === item.name ? 'Stop Visualizer' : 'Visualize'}
        </ActionButton>
        {toggleStar && (
          <ActionButton onClick={() => toggleStar('sinks', item.name)}>
            {starredSinks?.includes(item.name) ? '★' : '☆'}
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
        {renderLinkWithAnchor('/sources', route.source, 'fa-music')}
      </React.Fragment>
    ));
  };

  const filteredSinks = isFavoriteView
    ? sinks.filter(sink => starredSinks?.includes(sink.name))
    : sinks.filter(sink => sink.enabled);

  return (
    <div id={isFavoriteView ? "favorite-sinks" : "active-sinks"} className={isFavoriteView ? "favorite-sinks" : "active-sinks"}>
      <h2>{title}</h2>
      <table>
        <thead>
          <tr>
            <th>Name</th>
            <th>Controls</th>
          </tr>
        </thead>
        <tbody>
          {filteredSinks.map(sink => {
            const sinkRoutes = getRoutesForSink(sink.name);
            return (
              <tr key={sink.name}>
                <td>
                  {renderLinkWithAnchor('/sinks', sink.name, 'fa-volume-up')}
                  <div className="subtext">
                    <div>Routes from: {renderRouteLinks(sinkRoutes)}</div>
                    {sink.is_group && <div>Group Members: {getGroupMembers(sink.name, 'sinks')}</div>}
                  </div>
                </td>
                <td>{renderControls(sink)}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
};

export default SinksSection;