/**
 * React component for the Desktop Menu.
 * This component provides a user interface for navigating and managing sources, sinks, and routes.
 * It includes buttons to switch between different views and lists of items with actions like starring,
 * listening, visualizing, and opening in new windows.
 *
 * @param {React.FC} props - The properties for the component. No specific props are required.
 */
import React, { useState, useEffect, useMemo, useRef } from 'react';
import { useAppContext } from '../context/AppContext';
import SourceList from './SourceList';
import SinkList from './SinkList';
import RouteList from './RouteList';
import { createActions, Actions } from '../utils/actions';
import { SortConfig } from '../utils/commonUtils';
import { Source, Sink, Route } from '../api/api';
import '../styles/DesktopMenu.css';

/**
 * Enum for different menu levels.
 */
enum MenuLevel {
  Main,
  NowListening,
  Sources,
  Sinks,
  Routes,
  AllSources,
  AllSinks,
  AllRoutes
}

/**
 * Type definition for color modes.
 */
type ColorMode = 'light' | 'dark' | 'system';

const DesktopMenu: React.FC = () => {
  /**
   * Context variables and functions provided by AppContext.
   */
  const {
    sources,
    sinks,
    routes,
    activeSource: contextActiveSource,
    listeningToSink,
    visualizingSink,
    onListenToSink,
    onVisualizeSink,
    onToggleActiveSource,
  } = useAppContext();

  /**
   * State to keep track of the current menu level.
   */
  const [currentMenu, setCurrentMenu] = useState<MenuLevel>(() => {
    const savedMenu = localStorage.getItem('currentMenu');
    return savedMenu ? parseInt(savedMenu, 10) : MenuLevel.Main;
  });

  /**
   * State to keep track of starred sources.
   */
  const [starredSources, setStarredSources] = useState<string[]>([]);

  /**
   * State to keep track of starred sinks.
   */
  const [starredSinks, setStarredSinks] = useState<string[]>([]);

  /**
   * State to keep track of starred routes.
   */
  const [starredRoutes, setStarredRoutes] = useState<string[]>([]);

  /**
   * State to manage sorting configuration.
   */
  const [sortConfig, setSortConfig] = useState<SortConfig>({ key: '', direction: 'asc' });

  /**
   * State to handle any error messages.
   */
  const [error, setError] = useState<string | null>(null);

  /**
   * State to keep track of the color mode.
   */
  const [colorMode] = useState<ColorMode>(() => {
    const savedMode = localStorage.getItem('colorMode');
    return (savedMode as ColorMode) || 'system';
  });

  /**
   * State to keep track of the currently selected item.
   */
  const [selectedItem, setSelectedItem] = useState<string | null>(null);

  /**
   * Ref for the content div to enable smooth scrolling and highlighting.
   */
  const contentRef = useRef<HTMLDivElement>(null);

  /**
   * Effect to load starred items from local storage.
   */
  useEffect(() => {
    const starredSourcesData = JSON.parse(localStorage.getItem('starredSources') || '[]');
    const starredSinksData = JSON.parse(localStorage.getItem('starredSinks') || '[]');
    const starredRoutesData = JSON.parse(localStorage.getItem('starredRoutes') || '[]');
    setStarredSources(starredSourcesData);
    setStarredSinks(starredSinksData);
    setStarredRoutes(starredRoutesData);
  }, []);

  /**
   * Effect to save the current menu level to local storage.
   */
  useEffect(() => {
    localStorage.setItem('currentMenu', currentMenu.toString());
  }, [currentMenu]);

  /**
   * Effect to scroll to the selected item when it changes.
   */
  useEffect(() => {
    if (selectedItem && contentRef.current) {
      const element = document.getElementById(`${currentMenu.toString().toLowerCase()}-${selectedItem}`);
      if (element) {
        element.scrollIntoView({ behavior: 'smooth', block: 'center' });
      }
    }
  }, [selectedItem, currentMenu]);

  /**
   * Function to apply the color mode.
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
   * Effect to apply the color mode and listen for changes in system preferences.
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
   * Function to open a URL in a new window.
   */
  const openInNewWindow = (url: string, width: number = 800, height: number = 600) => {
    const left = (window.screen.width - width) / 2;
    const top = (window.screen.height - height) / 2;
    window.open(url, '_blank', `width=${width},height=${height},left=${left},top=${top}`);
  };

  /**
   * Function to navigate to a specific item and menu level.
   */
  const navigateToItem = (menuLevel: MenuLevel, itemName: string) => {
    setCurrentMenu(menuLevel);
    setSelectedItem(itemName);
  };

  /**
   * Memoized actions object to handle various operations on sources, sinks, and routes.
   */
  const desktopMenuActions: Actions = useMemo(() => {
    const baseActions = createActions(
      () => Promise.resolve(), // No need to fetch data as it's handled by AppContext
      setError,
      (type, setter) => {
        if (type === 'sources') setStarredSources(setter);
        else if (type === 'sinks') setStarredSinks(setter);
        else if (type === 'routes') setStarredRoutes(setter);
      },
      (show: boolean, type?: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', item?: Source | Sink | Route) => {
        if (show && type && item) {
          openInNewWindow(`/site/equalizer?type=${type}&name=${encodeURIComponent(item.name)}`);
        }
      },
      () => {}, // Set selected item function (not used here)
      () => {}, // Set selected item type function (not used here)
      (show: boolean, source: Source) => {
        if (show && source && source.vnc_ip && source.vnc_port) {
          openInNewWindow(`/site/vnc?ip=${source.vnc_ip}&port=${source.vnc_port}`);
        }
      },
      onToggleActiveSource,
      onListenToSink,
      onVisualizeSink,
      () => {}, // Edit item function (not used here)
      () => {} // Default navigate function (will be overridden)
    );

    // Override the navigate function for DesktopMenu
    return {
      ...baseActions,
      navigate: async (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', itemName: string) => {
        switch (type) {
          case 'sources':
            navigateToItem(MenuLevel.AllSources, itemName);
            onToggleActiveSource((prevActiveSource: string | null) => (prevActiveSource === itemName ? null : itemName));
            break;
          case 'sinks':
            navigateToItem(MenuLevel.AllSinks, itemName);
            break;
          case 'routes':
            navigateToItem(MenuLevel.AllRoutes, itemName);
            break;
        }
      }
    };
  }, [onListenToSink, onVisualizeSink, onToggleActiveSource]);

  /**
   * Function to open the full interface in a new window.
   */
  const openFullInterface = () => {
    window.open('/', '_blank');
  };

  /**
   * Function to handle sorting of lists.
   */
  const onSort = (key: string) => {
    setSortConfig(prevConfig => ({
      key,
      direction: prevConfig.key === key && prevConfig.direction === 'asc' ? 'desc' : 'asc',
    }));
  };

  /**
   * Function to render the content based on the current menu level.
   */
  const renderContent = () => {
    let content;
    const primarySource = sources.find(source => source.name === contextActiveSource);

    const commonProps = {
      actions: desktopMenuActions,
      sortConfig,
      onSort,
      hideSpecificButtons: true,
      hideExtraColumns: true,
      selectedItem,
      isDesktopMenu: true,
    };

    switch (currentMenu) {
      case MenuLevel.Sources:
        content = (
          <SourceList
            {...commonProps}
            sources={sources.filter(source => starredSources.includes(source.name))}
            routes={routes}
            starredSources={starredSources}
            activeSource={contextActiveSource}
          />
        );
        break;
      case MenuLevel.Sinks:
        content = (
          <SinkList
            {...commonProps}
            sinks={sinks.filter(sink => starredSinks.includes(sink.name))}
            routes={routes}
            starredSinks={starredSinks}
            listeningToSink={listeningToSink?.name}
            visualizingSink={visualizingSink?.name}
          />
        );
        break;
      case MenuLevel.Routes:
        content = (
          <RouteList
            {...commonProps}
            routes={routes.filter(route => starredRoutes.includes(route.name))}
            starredRoutes={starredRoutes}
          />
        );
        break;
      case MenuLevel.AllSources:
        content = (
          <SourceList
            {...commonProps}
            sources={sources}
            routes={routes}
            starredSources={starredSources}
            activeSource={contextActiveSource}
          />
        );
        break;
      case MenuLevel.AllSinks:
        content = (
          <SinkList
            {...commonProps}
            sinks={sinks}
            routes={routes}
            starredSinks={starredSinks}
            listeningToSink={listeningToSink?.name}
            visualizingSink={visualizingSink?.name}
          />
        );
        break;
      case MenuLevel.AllRoutes:
        content = (
          <RouteList
            {...commonProps}
            routes={routes}
            starredRoutes={starredRoutes}
          />
        );
        break;
      case MenuLevel.NowListening:
        content = (
          <SinkList
            {...commonProps}
            sinks={listeningToSink ? [listeningToSink] : []}
            routes={routes}
            starredSinks={starredSinks}
            listeningToSink={listeningToSink?.name}
            visualizingSink={visualizingSink?.name}
          />
        );
        break;
      default:
        content = (
          <SourceList
            {...commonProps}
            sources={primarySource ? [primarySource] : []}
            routes={routes}
            starredSources={starredSources}
            activeSource={contextActiveSource}
          />
        );
    }

    return content;
  };

  /**
   * Boolean to determine if the current color mode is dark.
   */
  const isDarkMode = colorMode === 'dark' || (colorMode === 'system' && window.matchMedia('(prefers-color-scheme: dark)').matches);

  /**
   * Main render function for the DesktopMenu component.
   */
  return (
    <div className={`desktop-menu ${isDarkMode ? 'dark-mode' : ''}`}>
      {error && <div className="error-message">{error}</div>}
      <div className="menu-header">
        <button
          className={`menu-button ${currentMenu === MenuLevel.Main ? 'active' : ''}`}
          onClick={() => setCurrentMenu(MenuLevel.Main)}
        >
          Primary Source
        </button>
        <button
          className={`menu-button ${currentMenu === MenuLevel.NowListening ? 'active' : ''}`}
          onClick={() => setCurrentMenu(MenuLevel.NowListening)}
        >
          Now Listening
        </button>
        <button
          className={`menu-button ${currentMenu === MenuLevel.Sources ? 'active' : ''}`}
          onClick={() => setCurrentMenu(MenuLevel.Sources)}
        >
          ★ Sources
        </button>
        <button
          className={`menu-button ${currentMenu === MenuLevel.Sinks ? 'active' : ''}`}
          onClick={() => setCurrentMenu(MenuLevel.Sinks)}
        >
          ★ Sinks
        </button>
        <button
          className={`menu-button ${currentMenu === MenuLevel.Routes ? 'active' : ''}`}
          onClick={() => setCurrentMenu(MenuLevel.Routes)}
        >
          ★ Routes
        </button>
        <button
          className={`menu-button ${currentMenu === MenuLevel.AllSources ? 'active' : ''}`}
          onClick={() => setCurrentMenu(MenuLevel.AllSources)}
        >
          All Sources
        </button>
        <button
          className={`menu-button ${currentMenu === MenuLevel.AllSinks ? 'active' : ''}`}
          onClick={() => setCurrentMenu(MenuLevel.AllSinks)}
        >
          All Sinks
        </button>
        <button
          className={`menu-button ${currentMenu === MenuLevel.AllRoutes ? 'active' : ''}`}
          onClick={() => setCurrentMenu(MenuLevel.AllRoutes)}
        >
          All Routes
        </button>
        <button className="menu-button" onClick={openFullInterface}>ScreamRouter</button>
      </div>
      <div className="menu-content" ref={contentRef}>
        {renderContent()}
      </div>
    </div>
  );
};

export default DesktopMenu;
