import React, { useState, useEffect } from 'react';
import { Link, Outlet } from 'react-router-dom';
import { useAppContext } from '../context/AppContext';
import '../styles/Layout.css';
import { ActionButton, VolumeSlider, renderLinkWithAnchor } from '../utils/commonUtils';
import { Sink, Source, Route } from '../api/api';

interface LayoutProps {
  children: React.ReactNode;
}

interface CollapsibleSectionProps {
  title: string;
  subtitle?: string;
  isExpanded: boolean;
  onToggle: () => void;
  children: React.ReactNode;
}

type ColorMode = 'light' | 'dark' | 'system';

const CollapsibleSection: React.FC<CollapsibleSectionProps> = ({ title, subtitle, isExpanded, onToggle, children }) => {
  return (
    <div className={`collapsible-section ${isExpanded ? 'expanded' : 'collapsed'}`}>
      <div className="section-header" onClick={onToggle}>
        <h3>{title}{subtitle && <span className="section-subtitle"> {subtitle}</span>}</h3>
        <div className="expand-toggle">▶</div>
      </div>
      {isExpanded && <div className="section-content">{children}</div>}
    </div>
  );
};

const ActiveSourceSection: React.FC<{ isExpanded: boolean; onToggle: () => void }> = ({ isExpanded, onToggle }) => {
    const {
        activeSource,
        sources,
        routes,
        toggleEnabled,
        updateVolume,
        controlSource,
        setSelectedItem,
        setShowVNCModal,
        setShowEqualizerModal,
        setSelectedItemType,
        getRoutesForSource,
      } = useAppContext();
    
      const source = sources.find(s => s.name === activeSource);
    
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
        <CollapsibleSection
          title="Active Source"
          subtitle={source ? source.name : 'None'}
          isExpanded={isExpanded}
          onToggle={onToggle}
        >
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
            <p>No Active Source</p>
          )}
        </CollapsibleSection>
      );
};

const NowPlayingSection: React.FC<{ isExpanded: boolean; onToggle: () => void }> = ({ isExpanded, onToggle }) => {
    const {
        listeningToSink,
        sinks,
        routes,
        toggleEnabled,
        updateVolume,
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
        <CollapsibleSection
          title="Now Playing"
          subtitle={sink ? sink.name : 'Not Playing'}
          isExpanded={isExpanded}
          onToggle={onToggle}
        >
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
            <div className="not-playing"><p>Not listening to a sink</p></div>
          )}
        </CollapsibleSection>
      );
};

const Layout: React.FC<LayoutProps> = ({children}) => {
  const { activeSource, listeningToSink } = useAppContext();
  const [isExpanded, setIsExpanded] = useState(false);
  const [colorMode, setColorMode] = useState<ColorMode>(() => {
    const savedMode = localStorage.getItem('colorMode');
    return (savedMode as ColorMode) || 'system';
  });

  const toggleExpanded = () => {
    setIsExpanded(!isExpanded);
  };

  const toggleColorMode = () => {
    const modes: ColorMode[] = ['light', 'dark', 'system'];
    const currentIndex = modes.indexOf(colorMode);
    const newMode = modes[(currentIndex + 1) % modes.length];
    setColorMode(newMode);
    localStorage.setItem('colorMode', newMode);
  };

  const applyColorMode = (mode: ColorMode) => {
    if (mode === 'system') {
      const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
      document.body.classList.toggle('dark-mode', prefersDark);
    } else {
      document.body.classList.toggle('dark-mode', mode === 'dark');
    }
  };

  useEffect(() => {
    applyColorMode(colorMode);

    const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
    const handleChange = () => {
      if (colorMode === 'system') {
        applyColorMode('system');
      }
    };

    mediaQuery.addListener(handleChange);
    return () => mediaQuery.removeListener(handleChange);
  }, [colorMode]);

  const getColorModeButtonText = () => {
    switch (colorMode) {
      case 'light':
        return 'Dark Mode';
      case 'dark':
        return 'System Default';
      case 'system':
        return 'Light Mode';
    }
  };

  const getCurrentColorModeText = () => {
    switch (colorMode) {
      case 'light':
        return 'Light Mode';
      case 'dark':
        return 'Dark Mode';
      case 'system':
        return 'System Default';
    }
  };

  const isDarkMode = colorMode === 'dark' || (colorMode === 'system' && window.matchMedia('(prefers-color-scheme: dark)').matches);

  return (
    <div className={`layout ${isDarkMode ? 'dark-mode' : ''}`}>
      <header className="layout-header">
        <h1>ScreamRouter</h1>
        <span className="lightdarkmode">
            <button className="color-mode-toggle" onClick={toggleColorMode}>
            {getCurrentColorModeText()}
            </button>
        </span>
        <nav>
          <ul>
            <li><Link to="/">Dashboard</Link></li>
            <li><Link to="/sources">Sources</Link></li>
            <li><Link to="/sinks">Sinks</Link></li>
            <li><Link to="/routes">Routes</Link></li>
            <li><a href="https://github.com/netham45/screamrouter/tree/master/Readme" target="_blank" rel="noopener noreferrer">Docs</a></li>
            <li><a href="https://github.com/netham45/screamrouter" target="_blank" rel="noopener noreferrer">GitHub</a></li>
          </ul>
        </nav>
      </header>
      {(activeSource || listeningToSink) && (
        <div className="status-section">
          <ActiveSourceSection isExpanded={isExpanded} onToggle={toggleExpanded} />
          <NowPlayingSection isExpanded={isExpanded} onToggle={toggleExpanded} />
        </div>
      )}
      <main className="layout-main">
        <Outlet />
        {children}
      </main>
      <footer className="layout-footer">
        <p>&copy; {new Date().getFullYear()} Netham45</p>
      </footer>
    </div>
  );
};

export default Layout;