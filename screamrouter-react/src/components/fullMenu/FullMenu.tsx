import React, { useState, useEffect, useMemo } from 'react';
import { useColorMode } from '@chakra-ui/react';
import { useAppContext } from '../../context/AppContext';
import { createActions } from '../../utils/actions';
import { SortConfig, ContentCategory, ViewMode } from './types';
import { openInNewWindow } from './utils';
import { Source, Sink, Route } from '../../api/api';
import HeaderBar from './layout/HeaderBar';
import Sidebar from './layout/Sidebar';
import ContentPanel from './layout/ContentPanel';
import '../../styles/FullMenu.css';

/**
 * FullMenu component.
 * This component provides a user interface for navigating and managing sources, sinks, and routes.
 * It uses a modern, responsive layout with improved navigation, better space utilization, and a more cohesive visual design.
 */
const FullMenu: React.FC = () => {
  /**
   * Context variables and functions provided by AppContext.
   */
  const {
    sources,
    sinks,
    routes,
    activeSource: contextActiveSource,
    listeningToSink,
    onListenToSink,
    onVisualizeSink,
    onToggleActiveSource,
    controlSource
  } = useAppContext();

  /**
   * State to keep track of the current content category.
   */
  const [currentCategory, setCurrentCategory] = useState<ContentCategory>(() => {
    const savedCategory = localStorage.getItem('fullMenuCurrentCategory');
    return (savedCategory as ContentCategory) || 'dashboard';
  });

  /**
   * State to keep track of the current view mode (grid or list).
   */
  const [viewMode, setViewMode] = useState<ViewMode>(() => {
    const savedMode = localStorage.getItem('fullMenuViewMode');
    return (savedMode as ViewMode) || 'grid';
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
  const [sortConfig, setSortConfig] = useState<SortConfig>({ key: 'name', direction: 'asc' });

  /**
   * State to handle any error messages.
   */
  const [error, setError] = useState<string | null>(null);

  /**
   * Use Chakra UI's color mode hook.
   */
  const { colorMode } = useColorMode();

  /**
   * State to manage the sidebar visibility on mobile.
   */
  const [sidebarOpen, setSidebarOpen] = useState(false);

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
   * Effect to save the current content category to local storage.
   */
  useEffect(() => {
    localStorage.setItem('fullMenuCurrentCategory', currentCategory);
  }, [currentCategory]);

  /**
   * Effect to save the current view mode to local storage.
   */
  useEffect(() => {
    localStorage.setItem('fullMenuViewMode', viewMode);
  }, [viewMode]);

  /**
   * Effect to add the dark-mode class to the body for CSS compatibility.
   */
  useEffect(() => {
    document.body.classList.toggle('dark-mode', colorMode === 'dark');
  }, [colorMode]);

  /**
   * Function to open the desktop menu in a new window.
   */
  const openDesktopMenu = () => {
    window.open('/site/desktopmenu', '_blank');
  };

  /**
   * Function to open the visualizer in a new window.
   * @param sink - The sink to visualize
   */
  const handleOpenVisualizer = (sink: Sink) => {
    if (sink) {
      openInNewWindow(`/site/visualizer?ip=${sink.ip}`, 800, 600);
      onVisualizeSink(sink);
    }
  };

  /**
   * Function to toggle the sidebar on mobile.
   */
  const toggleSidebar = () => {
    setSidebarOpen(prev => !prev);
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
   * Memoized actions object to handle various operations on sources, sinks, and routes.
   */
  const fullMenuActions = useMemo(() => {
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
      () => {}, // Set selected item function
      () => {}, // Set selected item type function (not used here)
      (show: boolean, source: Source) => {
        if (show && source && source.vnc_ip && source.vnc_port) {
          openInNewWindow(`/site/vnc?ip=${source.vnc_ip}&port=${source.vnc_port}`);
        }
      },
      // Wrap onToggleActiveSource to match the expected function signature
      (setter) => {
        const newValue = setter(contextActiveSource);
        if (newValue !== null) {
          onToggleActiveSource(newValue);
        } else if (contextActiveSource) {
          onToggleActiveSource(contextActiveSource); // Toggle off the current Primary Source
        }
      },
      onListenToSink,
      onVisualizeSink,
      () => {}, // Edit item function (not used here)
      () => {} // Default navigate function (will be overridden)
    );

    // Override the navigate function for FullMenu
    return {
      ...baseActions,
      navigate: async (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', itemName: string) => {
        // Set the current category and item name for detailed view
        
        switch (type) {
          case 'sources':
          case 'group-source':
            // Set the current category to 'source' for detailed view
            setCurrentCategory('source');
            // Set the sourceName in localStorage for the SourceContent component
            localStorage.setItem('currentSourceName', itemName);
            break;
          case 'sinks':
          case 'group-sink':
            // Set the current category to 'sink' for detailed view
            setCurrentCategory('sink');
            // Set the sinkName in localStorage for the SinkContent component
            localStorage.setItem('currentSinkName', itemName);
            break;
          case 'routes':
            // Set the current category to 'route' for detailed view
            setCurrentCategory('route');
            // Set the routeName in localStorage for the RouteContent component
            localStorage.setItem('currentRouteName', itemName);
            break;
        }
      }
    };
  }, [onListenToSink, onVisualizeSink, onToggleActiveSource, contextActiveSource]);

  /**
   * Function to handle starring/unstarring an item.
   */
  const handleStar = (type: 'sources' | 'sinks' | 'routes', itemName: string) => {
    if (type === 'sources') {
      setStarredSources(prev => {
        const newStarred = prev.includes(itemName)
          ? prev.filter(name => name !== itemName)
          : [...prev, itemName];
        localStorage.setItem('starredSources', JSON.stringify(newStarred));
        return newStarred;
      });
    } else if (type === 'sinks') {
      setStarredSinks(prev => {
        const newStarred = prev.includes(itemName)
          ? prev.filter(name => name !== itemName)
          : [...prev, itemName];
        localStorage.setItem('starredSinks', JSON.stringify(newStarred));
        return newStarred;
      });
    } else if (type === 'routes') {
      setStarredRoutes(prev => {
        const newStarred = prev.includes(itemName)
          ? prev.filter(name => name !== itemName)
          : [...prev, itemName];
        localStorage.setItem('starredRoutes', JSON.stringify(newStarred));
        return newStarred;
      });
    }
  };

  /**
   * Function to handle activating/deactivating a source.
   */
  const handleToggleSource = (sourceName: string) => {
    const source = sources.find(s => s.name === sourceName);
    if (source) {
      fullMenuActions.toggleEnabled('sources', sourceName);
    }
  };

  /**
   * Function to handle activating/deactivating a sink.
   */
  const handleToggleSink = (sinkName: string) => {
    const sink = sinks.find(s => s.name === sinkName);
    if (sink) {
      fullMenuActions.toggleEnabled('sinks', sinkName);
    }
  };

  /**
   * Function to handle activating/deactivating a route.
   */
  const handleToggleRoute = (routeName: string) => {
    const route = routes.find(r => r.name === routeName);
    if (route) {
      fullMenuActions.toggleEnabled('routes', routeName);
    }
  };
  
  /**
   * Function to handle updating the volume of a source.
   */
  const handleUpdateSourceVolume = (sourceName: string, volume: number) => {
    fullMenuActions.updateVolume('sources', sourceName, volume);
  };
  
  /**
   * Function to handle updating the volume of a sink.
   */
  const handleUpdateSinkVolume = (sinkName: string, volume: number) => {
    fullMenuActions.updateVolume('sinks', sinkName, volume);
  };
  
  /**
   * Function to handle updating the timeshift of a sink.
   */
  const handleUpdateSinkTimeshift = (sinkName: string, timeshift: number) => {
    fullMenuActions.updateTimeshift('sinks', sinkName, timeshift);
  };
  
  /**
   * Function to handle updating the timeshift of a source.
   */
  const handleUpdateSourceTimeshift = (sourceName: string, timeshift: number) => {
    fullMenuActions.updateTimeshift('sources', sourceName, timeshift);
  };
  
  /**
   * Function to handle updating the volume of a route.
   */
  const handleUpdateRouteVolume = (routeName: string, volume: number) => {
    fullMenuActions.updateVolume('routes', routeName, volume);
  };
  
  /**
   * Function to handle updating the timeshift of a route.
   */
  const handleUpdateRouteTimeshift = (routeName: string, timeshift: number) => {
    fullMenuActions.updateTimeshift('routes', routeName, timeshift);
  };

  /**
   * Function to open the equalizer for a source.
   */
  const handleOpenSourceEqualizer = (sourceName: string) => {
    const source = sources.find(s => s.name === sourceName);
    if (source) {
      fullMenuActions.showEqualizer(true, 'sources', source);
    }
  };

  /**
   * Function to open the equalizer for a sink.
   */
  const handleOpenSinkEqualizer = (sinkName: string) => {
    const sink = sinks.find(s => s.name === sinkName);
    if (sink) {
      fullMenuActions.showEqualizer(true, 'sinks', sink);
    }
  };

  /**
   * Function to open the equalizer for a route.
   */
  const handleOpenRouteEqualizer = (routeName: string) => {
    const route = routes.find(r => r.name === routeName);
    if (route) {
      fullMenuActions.showEqualizer(true, 'routes', route);
    }
  };

  /**
   * Function to open VNC for a source.
   */
  const handleOpenVnc = (sourceName: string) => {
    const source = sources.find(s => s.name === sourceName);
    if (source) {
      fullMenuActions.showVNC(true, source);
    }
  };
  
  /**
   * Function to control source playback (prev/play/next).
   */
  const handleControlSource = (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => {
    controlSource(sourceName, action);
  };

  /**
   * Boolean to determine if the current color mode is dark.
   */
  const isDarkMode = colorMode === 'dark';

  /**
   * Main render function for the FullMenu component.
   */
  return (
    <div className={`full-menu ${isDarkMode ? 'dark-mode' : ''}`}>
      {error && <div className="error-message">{error}</div>}
      
      <HeaderBar
        isDarkMode={isDarkMode}
        sources={sources}
        sinks={sinks}
        routes={routes}
        navigate={fullMenuActions.navigate}
        toggleSidebar={toggleSidebar}
        activeSource={contextActiveSource}
        controlSource={controlSource}
        updateVolume={fullMenuActions.updateVolume}
      />
      
      <div className="main-content">
        <Sidebar
          currentCategory={currentCategory}
          setCurrentCategory={setCurrentCategory}
          sources={sources}
          sinks={sinks}
          routes={routes}
          starredSources={starredSources}
          starredSinks={starredSinks}
          starredRoutes={starredRoutes}
          sidebarOpen={sidebarOpen}
          toggleSidebar={toggleSidebar}
          openDesktopMenu={openDesktopMenu}
          isDarkMode={isDarkMode}
        />
        
        <ContentPanel
          currentCategory={currentCategory}
          viewMode={viewMode}
          setViewMode={setViewMode}
          sortConfig={sortConfig}
          onSort={onSort}
          isDarkMode={isDarkMode}
          sources={sources}
          sinks={sinks}
          routes={routes}
          starredSources={starredSources}
          starredSinks={starredSinks}
          starredRoutes={starredRoutes}
          contextActiveSource={contextActiveSource}
          listeningToSink={listeningToSink}
          setCurrentCategory={setCurrentCategory}
          handleStar={handleStar}
          handleToggleSource={handleToggleSource}
          handleToggleSink={handleToggleSink}
          handleToggleRoute={handleToggleRoute}
          handleOpenSourceEqualizer={handleOpenSourceEqualizer}
          handleOpenSinkEqualizer={handleOpenSinkEqualizer}
          handleOpenRouteEqualizer={handleOpenRouteEqualizer}
          handleOpenVnc={handleOpenVnc}
          handleUpdateSourceVolume={handleUpdateSourceVolume}
          handleUpdateSinkVolume={handleUpdateSinkVolume}
          handleUpdateSourceTimeshift={handleUpdateSourceTimeshift}
          handleUpdateSinkTimeshift={handleUpdateSinkTimeshift}
          handleUpdateRouteVolume={handleUpdateRouteVolume}
          handleUpdateRouteTimeshift={handleUpdateRouteTimeshift}
          handleOpenVisualizer={handleOpenVisualizer}
          handleControlSource={handleControlSource}
          actions={fullMenuActions}
        />
      </div>
    </div>
  );
};

export default FullMenu;