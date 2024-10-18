import React from 'react';
import { Sink, Route } from '../api/api';
import { useAppContext } from '../context/AppContext';
import { renderLinkWithAnchor } from '../utils/commonUtils';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';
import EnableButton from './controls/EnableButton';
import ActionButton from './controls/ActionButton';

interface NowPlayingProps {
  isExpanded: boolean;
  onToggle: () => void;
}

const NowPlaying: React.FC<NowPlayingProps> = ({ isExpanded, onToggle }) => {
  const {
    listeningToSink,
    sinks,
    toggleEnabled,
    updateVolume,
    updateTimeshift,
    setSelectedItem,
    setShowEqualizerModal,
    setSelectedItemType,
    getRoutesForSink,
    onListenToSink,
    onVisualizeSink,
    visualizingSink,
  } = useAppContext();

  const sink = listeningToSink ? sinks.find(s => s.name === listeningToSink.name) : null;

  const renderControls = (item: Sink) => {
    return (
      <>
        <EnableButton 
          isEnabled={item.enabled}
          onClick={() => toggleEnabled('sinks', item.name, item.enabled)}
        />
        <VolumeSlider
          value={item.volume}
          onChange={(value) => updateVolume('sinks', item.name, value)}
        />
        <TimeshiftSlider
          value={item.timeshift || 0}
          onChange={(value) => updateTimeshift('sinks', item.name, value)}
        />
        <ActionButton onClick={() => {
          setSelectedItem(item);
          setSelectedItemType('sinks');
          setShowEqualizerModal(true);
        }}>
          Equalizer
        </ActionButton>
        <ActionButton
          onClick={() => onListenToSink(null)}
          className="listening"
        >
          Stop Listening
        </ActionButton>
        <ActionButton
          onClick={() => onVisualizeSink(visualizingSink?.name === item.name ? null : item)}
          className={visualizingSink?.name === item.name ? 'visualizing' : ''}
        >
          {visualizingSink?.name === item.name ? 'Stop Visualizer' : 'Visualize'}
        </ActionButton>
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

  return (
    <div className={`collapsible-section ${isExpanded ? 'expanded' : 'collapsed'}`}>
      <div className="section-header" onClick={onToggle}>
        <h3>Now Playing{sink && <span className="section-subtitle"> {sink.name}</span>}</h3>
        <div className="expand-toggle">▶</div>
      </div>
      <div className="section-content">
        {sink ? (
          <>
            <div>
              {renderLinkWithAnchor('/sinks', sink.name, 'fa-volume-up')}
              <div className="subtext">
                <div>Routes from: {renderRouteLinks(getRoutesForSink(sink.name))}</div>
              </div>
            </div>
            <div>{renderControls(sink)}</div>
          </>
        ) : (
          <div className="not-playing"><p>Not listening to a stream</p></div>
        )}
      </div>
    </div>
  );
};

export default NowPlaying;
