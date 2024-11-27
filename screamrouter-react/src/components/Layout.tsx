/**
 * React component for the main layout of the application.
 * This component provides a consistent structure and navigation across different pages.
 * It includes a header with navigation links, a status section for active sources and now playing information,
 * a main content area for nested routes, and a footer with copyright information.
 *
 * @param {React.FC} props - The properties for the component.
 */
import React, { useState, useEffect } from 'react';
import { Link, Outlet } from 'react-router-dom';
import { useAppContext } from '../context/AppContext';
import '../styles/Layout.css';
import ActiveSource from './ActiveSource';
import NowPlaying from './NowPlaying';
import Equalizer from './Equalizer';

type ColorMode = 'light' | 'dark' | 'system';

/**
 * React functional component for the Layout.
 *
 * @returns {JSX.Element} The rendered JSX element.
 */
const Layout: React.FC = () => {
  /**
   * State to keep track of whether the status section is expanded or collapsed.
   */
  const [isExpanded, setIsExpanded] = useState(false);

  /**
   * State to keep track of the current color mode (light, dark, or system default).
   */
  const [colorMode, setColorMode] = useState<ColorMode>(() => {
    const savedMode = localStorage.getItem('colorMode');
    return (savedMode as ColorMode) || 'system';
  });

  /**
   * Context values from AppContext.
   */
  const { 
    activeSource, 
    listeningToSink,
    showEqualizerModal,
    selectedEqualizerItem,
    selectedEqualizerType,
    closeEqualizerModal,
    fetchSources,
    fetchSinks,
    fetchRoutes
  } = useAppContext();

  /**
   * Toggles the expanded state of the status section.
   */
  const toggleExpanded = () => {
    setIsExpanded(!isExpanded);
  };

  /**
   * Cycles through color modes (light, dark, system default).
   */
  const toggleColorMode = () => {
    const modes: ColorMode[] = ['light', 'dark', 'system'];
    const currentIndex = modes.indexOf(colorMode);
    const newMode = modes[(currentIndex + 1) % modes.length];
    setColorMode(newMode);
    localStorage.setItem('colorMode', newMode);
  };

  /**
   * Applies the color mode to the document body.
   *
   * @param {ColorMode} mode - The color mode to apply.
   */
  const applyColorMode = (mode: ColorMode) => {
    if (mode === 'system') {
      const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
      document.body.classList.toggle('dark-mode', prefersDark);
    } else {
      document.body.classList.toggle('dark-mode', mode === 'dark');
    }
  };

  /**
   * Effect to apply the color mode when it changes and listens for system color scheme changes.
   */
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

  /**
   * Returns the text representation of the current color mode.
   *
   * @returns {string} The color mode text.
   */
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

  /**
   * Determines if the current color mode is dark.
   *
   * @returns {boolean} True if dark mode, false otherwise.
   */
  const isDarkMode = colorMode === 'dark' || (colorMode === 'system' && window.matchMedia('(prefers-color-scheme: dark)').matches);

  /**
   * Fetches data for sources, sinks, and routes when the equalizer modal is closed.
   */
  const handleDataChange = async () => {
    await fetchSources();
    await fetchSinks();
    await fetchRoutes();
  };

  /**
   * Renders the Layout component.
   *
   * @returns {JSX.Element} The rendered JSX element.
   */
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
        <Outlet />
      </main>
      <footer className="layout-footer">
        <p>&copy; {new Date().getFullYear()} Netham45</p>
      </footer>
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
