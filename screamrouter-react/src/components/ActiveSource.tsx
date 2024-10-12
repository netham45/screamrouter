import React, { useEffect } from 'react';
import { Source, Route } from '../api/api';
import { useAppContext } from '../context/AppContext';
import { renderLinkWithAnchor } from '../utils/commonUtils';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';
import EnableButton from './controls/EnableButton';
import ActionButton from './controls/ActionButton';
import PlaybackControls from './controls/PlaybackControls';

interface ActiveSourceProps {
  isExpanded: boolean;
  onToggle: () => void;
}

const ActiveSource: React.FC<ActiveSourceProps> = ({ isExpanded, onToggle }) => {
  const {
    activeSource,
    sources,
    routes,
    toggleEnabled,
    updateVolume,
    updateTimeshift,
    controlSource,
    getRoutesForSource,
    openVNCModal,
    openEqualizerModal
  } = useAppContext();

  const source = sources.find(s => s.name === activeSource);

  useEffect(() => {
    console.log('ActiveSource component re-rendered');
  });

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

  const renderRouteLinks = (routes: Route[]) => {
    if (routes.length === 0) return 'None';
    return routes.map((route, index) => (
      <React.Fragment key={route.name}>
        {index > 0 && ', '}
        {renderLinkWithAnchor('/sinks', route.sink, 'fa-volume-up')}
      </React.Fragment>
    ));
  };

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

export default ActiveSource;
