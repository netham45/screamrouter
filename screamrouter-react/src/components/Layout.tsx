import React, { useState, useEffect } from 'react';
import { Link, Outlet } from 'react-router-dom';
import { useAppContext } from '../context/AppContext';
import '../styles/Layout.css';
import ActiveSource from './ActiveSource';
import NowPlaying from './NowPlaying';
import VNC from './VNC';
import Equalizer from './Equalizer';

type ColorMode = 'light' | 'dark' | 'system';

interface LayoutProps {
  children: React.ReactNode;
}

const Layout: React.FC<LayoutProps> = ({children}) => {
  const { 
    activeSource, 
    listeningToSink, 
    showVNCModal, 
    selectedVNCSource, 
    closeVNCModal,
    showEqualizerModal,
    selectedEqualizerItem,
    selectedEqualizerType,
    closeEqualizerModal,
    fetchSources,
    fetchSinks,
    fetchRoutes
  } = useAppContext();
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

  const handleDataChange = async () => {
    await fetchSources();
    await fetchSinks();
    await fetchRoutes();
  };

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
            <li><Link to="/desktopmenu">Desktop Menu</Link></li>
            <li><a href="https://github.com/netham45/screamrouter/tree/master/Readme" target="_blank" rel="noopener noreferrer">Docs</a></li>
            <li><a href="https://github.com/netham45/screamrouter" target="_blank" rel="noopener noreferrer">GitHub</a></li>
          </ul>
        </nav>
      </header>
      {(activeSource || listeningToSink) && (
        <div className="status-section">
          <ActiveSource isExpanded={isExpanded} onToggle={toggleExpanded} />
          <NowPlaying isExpanded={isExpanded} onToggle={toggleExpanded} />
        </div>
      )}
      <main className="layout-main">
        {children}
        <Outlet />
      </main>
      <footer className="layout-footer">
        <p>&copy; {new Date().getFullYear()} Netham45</p>
      </footer>
      {showVNCModal && selectedVNCSource && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={closeVNCModal}>×</button>
            <VNC source={selectedVNCSource} />
          </div>
        </div>
      )}
      {showEqualizerModal && selectedEqualizerItem && selectedEqualizerType && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={closeEqualizerModal}>×</button>
            <Equalizer
              item={selectedEqualizerItem}
              type={selectedEqualizerType}
              onClose={closeEqualizerModal}
              onDataChange={handleDataChange}
            />
          </div>
        </div>
      )}
    </div>
  );
};

export default Layout;
