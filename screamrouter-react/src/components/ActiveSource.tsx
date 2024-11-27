/**
 * This file contains the ActiveSource component, which displays and controls the primary audio source.
 * It includes functionality for enabling/disabling the source, adjusting volume and timeshift,
 * opening equalizer and VNC modals, and controlling playback (prev track, play, next track).
 */

import React, { useEffect } from 'react';
import { Source, Route } from '../api/api';
import { useAppContext } from '../context/AppContext';
import { renderLinkWithAnchor } from '../utils/commonUtils';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';
import EnableButton from './controls/EnableButton';
import ActionButton from './controls/ActionButton';
import PlaybackControls from './controls/PlaybackControls';

/**
 * Interface defining the props for the ActiveSource component.
 */
interface ActiveSourceProps {
  isExpanded: boolean;
  onToggle: () => void;
}

/**
 * React functional component that displays and controls the primary audio source.
 * @param {ActiveSourceProps} props - Component properties including expansion state and toggle function.
 * @returns {JSX.Element} Rendered ActiveSource component.
 */
const ActiveSource: React.FC<ActiveSourceProps> = ({ isExpanded, onToggle }) => {
  const {
    activeSource,
    sources,
    toggleEnabled,
    updateVolume,
    updateTimeshift,
    controlSource,
    getRoutesForSource,
    openVNCModal,
    openEqualizerModal
  } = useAppContext();

  /**
   * The currently active source object.
   */
  const source = Array.isArray(sources) ? sources.find(s => s.name === activeSource) : undefined;

  /**
   * Effect hook that logs when the ActiveSource component re-renders.
   */
  useEffect(() => {
    console.log('ActiveSource component re-rendered');
  });

  /**
   * Renders control elements for a given source item.
   * @param {Source} item - The source object to render controls for.
   * @returns {JSX.Element} Rendered control elements.
   */
  const renderControls = (item: Source) => {
    return (
      <>
        <EnableButton 
          isEnabled={item.enabled}
          onClick={() => toggleEnabled('sources', item.name, item.enabled)}
        />
        <VolumeSlider
          value={item.volume}
          onChange={(value) => updateVolume('sources', item.name, value)}
        />
        <TimeshiftSlider
          value={item.timeshift || 0}
          onChange={(value) => updateTimeshift('sources', item.name, value)}
        />
        <ActionButton onClick={() => openEqualizerModal(item, 'sources')}>
          Equalizer
        </ActionButton>
        {(item.vnc_ip || item.vnc_port) && (
          <>
            <ActionButton onClick={() => openVNCModal(item)}>
              VNC
            </ActionButton>
            <PlaybackControls
              onPrevTrack={() => controlSource(item.name, 'prevtrack')}
              onPlay={() => controlSource(item.name, 'play')}
              onNextTrack={() => controlSource(item.name, 'nexttrack')}
            />
          </>
        )}
      </>
    );
  };

  /**
   * Renders links to routes associated with a given source.
   * @param {Route[]} routes - Array of route objects.
   * @returns {JSX.Element} Rendered route links or "None" if no routes are present.
   */
  const renderRouteLinks = (routes: Route[]) => {
    if (routes.length === 0) return 'None';
    return routes.map((route, index) => (
      <React.Fragment key={route.name}>
        {index > 0 && ', '}
        {renderLinkWithAnchor('/sinks', route.sink, 'fa-volume-up')}
      </React.Fragment>
    ));
  };

  /**
   * Main render method for the ActiveSource component.
   * @returns {JSX.Element} Rendered ActiveSource component structure.
   */
  return (
    <div className={`collapsible-section ${isExpanded ? 'expanded' : 'collapsed'}`}>
      <div className="section-header" onClick={onToggle}>
        <h3>Primary Source{source && <span className="section-subtitle"> {source.name}</span>}</h3>
        <div className="expand-toggle">▶</div>
      </div>
      <div className="section-content">
        {source ? (
          <>
            <div>
              {renderLinkWithAnchor('/sources', source.name, 'fa-music')}
              <div className="subtext">
                <div>Routes to: {renderRouteLinks(getRoutesForSource(source.name))}</div>
              </div>
            </div>
            <div>{renderControls(source)}</div>
          </>
        ) : (
          <p>No Primary Source</p>
        )}
      </div>
    </div>
  );
};

/**
 * Exports the ActiveSource component as the default export.
 */
export default ActiveSource;
