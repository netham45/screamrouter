/**
 * React component for displaying the currently playing audio sink and its controls.
 * This component shows detailed information about the sink being listened to, including routes from sources,
 * volume control, timeshift control, equalizer settings, and options to stop listening or visualize the stream.
 *
 * @param {React.FC} props - The properties for the component.
 * @param {boolean} props.isExpanded - Indicates whether the section is expanded or collapsed.
 * @param {() => void} props.onToggle - Function to toggle the expanded state of the section.
 */
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

/**
 * React functional component for the NowPlaying section.
 *
 * @param {NowPlayingProps} props - The properties for the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const NowPlaying: React.FC<NowPlayingProps> = ({ isExpanded, onToggle }) => {
  /**
   * Context values from AppContext.
   */
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

  /**
   * The sink that is currently being listened to.
   */
  const sink = listeningToSink ? sinks.find(s => s.name === listeningToSink.name) : null;

  /**
   * Renders the controls for a given sink, including enable button, volume slider, timeshift slider,
   * equalizer button, stop listening button, and visualize button.
   *
   * @param {Sink} item - The sink item to render controls for.
   * @returns {JSX.Element} The rendered JSX element containing the controls.
   */
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
          onClick={() => onVisualizeSink(item)}
          className={visualizingSink?.name === item.name ? 'visualizing' : ''}
        >
          Visualize
        </ActionButton>
      </>
    );
  };

  /**
   * Renders links to the sources that have routes to the given sink.
   *
   * @param {Route[]} routes - The list of routes for the sink.
   * @returns {JSX.Element} The rendered JSX element containing the route links.
   */
  const renderRouteLinks = (routes: Route[]) => {
    if (routes.length === 0) return 'None';
    return routes.map((route, index) => (
      <React.Fragment key={route.name}>
        {index > 0 && ', '}
        {renderLinkWithAnchor('/sources', route.source, 'fa-music')}
      </React.Fragment>
    ));
  };

  /**
   * Renders the NowPlaying component.
   *
   * @returns {JSX.Element} The rendered JSX element.
   */
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
