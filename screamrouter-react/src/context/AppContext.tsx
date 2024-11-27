/**
 * React context and provider for managing the application state.
 */
import React, { createContext, useContext, useState, useEffect, useRef } from 'react';
import { Sink, Source, Route, WebSocketUpdate } from '../api/api';
import ApiService from '../api/api';

type ItemType = Source | Sink | Route;

interface AppContextType {
  /**
   * The currently active source name.
   */
  activeSource: string | null;
  /**
   * The sink that is currently being listened to.
   */
  listeningToSink: Sink | null;
  /**
   * The sink that is currently being visualized.
   */
  visualizingSink: Sink | null;
  /**
   * List of all sources.
   */
  sources: Source[];
  /**
   * List of all sinks.
   */
  sinks: Sink[];
  /**
   * List of all routes.
   */
  routes: Route[];
  /**
   * Toggles the active source.
   * @param sourceName - The name of the source to toggle.
   */
  onToggleActiveSource: (sourceName: string) => void;
  /**
   * Sets the sink that is being listened to.
   * @param sink - The sink object or null to stop listening.
   */
  onListenToSink: (sink: Sink | null) => void;
  /**
   * Sets the sink that is being visualized.
   * @param sink - The sink object or null to stop visualization.
   */
  onVisualizeSink: (sink: Sink | null) => void;
  /**
   * Toggles the enabled status of a source, sink, route, group-sink, or group-source.
   * @param type - The type of item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   * @param name - The name of the item to toggle.
   * @param currentStatus - The current enabled status of the item.
   */
  toggleEnabled: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, currentStatus: boolean) => Promise<void>;
  /**
   * Updates the volume of a source, sink, route, group-sink, or group-source.
   * @param type - The type of item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   * @param name - The name of the item to update.
   * @param volume - The new volume level (0.0 to 1.0).
   */
  updateVolume: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, volume: number) => Promise<void>;
  /**
   * Updates the timeshift of a source, sink, route, group-sink, or group-source.
   * @param type - The type of item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   * @param name - The name of the item to update.
   * @param timeshift - The new timeshift value in milliseconds.
   */
  updateTimeshift: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, timeshift: number) => Promise<void>;
  /**
   * Controls a source by sending actions like play, pause, next track, etc.
   * @param sourceName - The name of the source to control.
   * @param action - The action to perform ('prevtrack', 'play', 'nexttrack').
   */
  controlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => Promise<void>;
  /**
   * Sets the currently selected item.
   * @param item - The item object or null to clear selection.
   */
  setSelectedItem: (item: ItemType | null) => void;
  /**
   * Shows or hides the equalizer modal.
   * @param show - Boolean indicating whether to show the modal.
   */
  setShowEqualizerModal: (show: boolean) => void;
  /**
   * Sets the type of the currently selected item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   * @param type - The type of the selected item or null to clear selection.
   */
  setSelectedItemType: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source' | null) => void;
  /**
   * Retrieves all routes for a given source.
   * @param sourceName - The name of the source.
   * @returns An array of routes associated with the source.
   */
  getRoutesForSource: (sourceName: string) => Route[];
  /**
   * Retrieves all routes for a given sink.
   * @param sinkName - The name of the sink.
   * @returns An array of routes associated with the sink.
   */
  getRoutesForSink: (sinkName: string) => Route[];
  /**
   * Fetches all sources from the API and updates the state.
   */
  fetchSources: () => Promise<void>;
  /**
   * Fetches all sinks from the API and updates the state.
   */
  fetchSinks: () => Promise<void>;
  /**
   * Fetches all routes from the API and updates the state.
   */
  fetchRoutes: () => Promise<void>;
  /**
   * Sets a source as the primary source.
   * @param source - The source object or null to clear the primary source.
   */
  setPrimarySource: (source: Source | null) => void;
  /**
   * Opens the VNC modal for a given source.
   * @param source - The source object with VNC IP and port information.
   */
  openVNCModal: (source: Source) => void;
  /**
   * Indicates whether the equalizer modal is currently shown.
   */
  showEqualizerModal: boolean;
  /**
   * The item that is currently selected for the equalizer modal.
   */
  selectedEqualizerItem: ItemType | null;
  /**
   * The type of the item that is currently selected for the equalizer modal ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   */
  selectedEqualizerType: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source' | null;
  /**
   * Opens the equalizer modal for a given item.
   * @param item - The item object to configure in the equalizer.
   * @param type - The type of the item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   */
  openEqualizerModal: (item: ItemType, type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source') => void;
  /**
   * Closes the equalizer modal.
   */
  closeEqualizerModal: () => void;
  /**
   * Indicates whether the edit modal is currently shown.
   */
  showEditModal: boolean;
  /**
   * Shows or hides the edit modal.
   * @param show - Boolean indicating whether to show the modal.
   */
  setShowEditModal: (show: boolean) => void;
  /**
   * Edits a given item by setting it as the selected item and showing the edit modal.
   * @param type - The type of the item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   * @param item - The item object to edit.
   */
  editItem: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', item: ItemType) => void;
  /**
   * The currently selected item.
   */
  selectedItem: ItemType | null;
  /**
   * The type of the currently selected item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   */
  selectedItemType: 'sources' | 'sinks' | 'routes' | 'group-sink' | "group-source" | null;
  /**
   * Navigates to a given item by setting it as the selected item.
   * @param type - The type of the item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   * @param name - The name of the item to navigate to.
   */
  navigateToItem: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string) => void;
}

const AppContext = createContext<AppContextType | undefined>(undefined);

/**
 * React provider component for the application context.
 * @param children - The child components that will have access to the context.
 */
export const AppProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [activeSource, setActiveSource] = useState<string | null>(null);
  const [listeningToSink, setListeningToSink] = useState<Sink | null>(null);
  const [visualizingSink, setVisualizingSink] = useState<Sink | null>(null);
  const [sources, setSources] = useState<Source[]>([]);
  const [sinks, setSinks] = useState<Sink[]>([]);
  const [routes, setRoutes] = useState<Route[]>([]);
  const [selectedItem, setSelectedItem] = useState<ItemType | null>(null);
  const [showEqualizerModal, setShowEqualizerModal] = useState(false);
  const [showEditModal, setShowEditModal] = useState(false);
  const [selectedItemType, setSelectedItemType] = useState<'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source' | null>(null);
  const [selectedEqualizerItem, setSelectedEqualizerItem] = useState<ItemType | null>(null);
  const [selectedEqualizerType, setSelectedEqualizerType] = useState<'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source' | null>(null);
  const audioRef = useRef<HTMLAudioElement | null>(null);

  /**
   * Opens a new window with the specified URL.
   * @param url - The URL to open.
   * @param width - The width of the window (default is 800).
   * @param height - The height of the window (default is 600).
   */
  const openInNewWindow = (url: string, width: number = 800, height: number = 600) => {
    const left = (window.screen.width - width) / 2;
    const top = (window.screen.height - height) / 2;
    window.open(url, '_blank', `width=${width},height=${height},left=${left},top=${top}`);
  };

  /**
   * Fetches initial data for sources, sinks, and routes from the API.
   */
  const fetchInitialData = async () => {
    try {
      const [sourcesResponse, sinksResponse, routesResponse] = await Promise.all([
        ApiService.getSources(),
        ApiService.getSinks(),
        ApiService.getRoutes()
      ]);
      setSources(Object.values(sourcesResponse.data));
      setSinks(Object.values(sinksResponse.data));
      setRoutes(Object.values(routesResponse.data));
    } catch (error) {
      console.error('Error fetching initial data:', error);
    }
  };

  useEffect(() => {
    const storedActiveSource = localStorage.getItem('activeSource');
    if (storedActiveSource) {
      setActiveSource(storedActiveSource);
    }

    audioRef.current = document.getElementById('audio') as HTMLAudioElement;

    // Fetch initial data
    fetchInitialData();

    // Set up WebSocket handler
    ApiService.setWebSocketHandler((update: WebSocketUpdate) => {
      if (update.sources) {
        setSources(prevSources => {
          const updatedSources = [...prevSources];
          Object.entries(update.sources || {}).forEach(([name, source]) => {
            const index = updatedSources.findIndex(s => s.name === name);
            if (index !== -1) {
              updatedSources[index] = source;
            }
          });
          return updatedSources;
        });
      }
      if (update.sinks) {
        setSinks(prevSinks => {
          const updatedSinks = [...prevSinks];
          Object.entries(update.sinks || {}).forEach(([name, sink]) => {
            const index = updatedSinks.findIndex(s => s.name === name);
            if (index !== -1) {
              updatedSinks[index] = sink;
            }
          });
          return updatedSinks;
        });
      }
      if (update.routes) {
        setRoutes(prevRoutes => {
          const updatedRoutes = [...prevRoutes];
          Object.entries(update.routes || {}).forEach(([name, route]) => {
            const index = updatedRoutes.findIndex(r => r.name === name);
            if (index !== -1) {
              updatedRoutes[index] = route;
            }
          });
          return updatedRoutes;
        });
      }
    });
  }, []);

  /**
   * Fetches all sources from the API and updates the state.
   */
  const fetchSources = async () => {
    try {
      const response = await ApiService.getSources();
      setSources(Object.values(response.data));
    } catch (error) {
      console.error('Error fetching sources:', error);
    }
  };

  /**
   * Fetches all sinks from the API and updates the state.
   */
  const fetchSinks = async () => {
    try {
      const response = await ApiService.getSinks();
      setSinks(Object.values(response.data));
    } catch (error) {
      console.error('Error fetching sinks:', error);
    }
  };

  /**
   * Fetches all routes from the API and updates the state.
   */
  const fetchRoutes = async () => {
    try {
      const response = await ApiService.getRoutes();
      setRoutes(Object.values(response.data));
    } catch (error) {
      console.error('Error fetching routes:', error);
    }
  };

  /**
   * Toggles the active source.
   * @param sourceName - The name of the source to toggle.
   */
  const onToggleActiveSource = (sourceName: string) => {
    const newActiveSource = activeSource === sourceName ? null : sourceName;
    setActiveSource(newActiveSource);
    localStorage.setItem('activeSource', newActiveSource || '');
  };

  /**
   * Sets the sink that is being listened to.
   * @param sink - The sink object or null to stop listening.
   */
  const onListenToSink = (sink: Sink | null) => {
    if (sink) {
      setListeningToSink(sink);
      if (audioRef.current) {
        audioRef.current.src = ApiService.getSinkStreamUrl(sink.ip);
        audioRef.current.play();
      }
    } else {
      setListeningToSink(null);
      if (audioRef.current) {
        audioRef.current.pause();
        audioRef.current.src = '';
      }
    }
  };

  /**
   * Sets the sink that is being visualized.
   * @param sink - The sink object or null to stop visualization.
   */
  const onVisualizeSink = (sink: Sink | null) => {
    if (sink) {
      openInNewWindow(`/site/visualizer?ip=${sink.ip}`, 800, 600);
    }
    setVisualizingSink(sink);
  };

  /**
   * Toggles the enabled status of a source, sink, route, group-sink, or group-source.
   * @param type - The type of item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   * @param name - The name of the item to toggle.
   * @param currentStatus - The current enabled status of the item.
   */
  const toggleEnabled = async (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, currentStatus: boolean) => {
    try {
      if (type === 'sources') {
        await ApiService.updateSource(name, { enabled: !currentStatus });
      } else if (type === 'sinks') {
        await ApiService.updateSink(name, { enabled: !currentStatus });
      } else if (type === 'routes') {
        await ApiService.updateRoute(name, { enabled: !currentStatus });
      }
    } catch (error) {
      console.error(`Error toggling ${type} enabled status:`, error);
    }
  };

  /**
   * Updates the volume of a source, sink, route, group-sink, or group-source.
   * @param type - The type of item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   * @param name - The name of the item to update.
   * @param volume - The new volume level (0.0 to 1.0).
   */
  const updateVolume = async (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, volume: number) => {
    try {
      if (type === 'sources') {
        await ApiService.updateSourceVolume(name, volume);
      } else if (type === 'sinks') {
        await ApiService.updateSinkVolume(name, volume);
      } else {
        await ApiService.updateRouteVolume(name, volume);
      }
    } catch (error) {
      console.error(`Error updating ${type} volume:`, error);
    }
  };

  /**
   * Updates the timeshift of a source, sink, route, group-sink, or group-source.
   * @param type - The type of item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   * @param name - The name of the item to update.
   * @param timeshift - The new timeshift value in milliseconds.
   */
  const updateTimeshift = async (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, timeshift: number) => {
    try {
      if (type === 'sources') {
        await ApiService.updateSourceTimeshift(name, timeshift);
      } else if (type === 'sinks') {
        await ApiService.updateSinkTimeshift(name, timeshift);
      } else {
        await ApiService.updateRouteTimeshift(name, timeshift);
      }
    } catch (error) {
      console.error(`Error updating ${type} timeshift:`, error);
    }
  };

  /**
   * Controls a source by sending actions like play, pause, next track, etc.
   * @param sourceName - The name of the source to control.
   * @param action - The action to perform ('prevtrack', 'play', 'nexttrack').
   */
  const controlSource = async (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => {
    try {
      await ApiService.controlSource(sourceName, action);
    } catch (error) {
      console.error('Error controlling source:', error);
    }
  };

  /**
   * Retrieves all routes for a given source.
   * @param sourceName - The name of the source.
   * @returns An array of routes associated with the source.
   */
  const getRoutesForSource = (sourceName: string) => {
    return routes.filter(route => route.source === sourceName);
  };

  /**
   * Retrieves all routes for a given sink.
   * @param sinkName - The name of the sink.
   * @returns An array of routes associated with the sink.
   */
  const getRoutesForSink = (sinkName: string) => {
    return routes.filter(route => route.sink === sinkName);
  };

  /**
   * Sets a source as the primary source.
   * @param source - The source object or null to clear the primary source.
   */
  const setPrimarySource = (source: Source | null) => {
    setSources(prevSources => prevSources.map(s => ({
      ...s,
      is_primary: s.name === source?.name
    })));
  };

  /**
   * Opens the VNC modal for a given source.
   * @param source - The source object with VNC IP and port information.
   */
  const openVNCModal = (source: Source) => {
    if (source.vnc_ip && source.vnc_port) {
      openInNewWindow(`/site/vnc?ip=${source.vnc_ip}&port=${source.vnc_port}`, 800, 600);
    }
  };

  /**
   * Opens the equalizer modal for a given item.
   * @param item - The item object to configure in the equalizer.
   * @param type - The type of the item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   */
  const openEqualizerModal = (item: ItemType, type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source') => {
    setSelectedEqualizerItem(item);
    setSelectedEqualizerType(type);
    setShowEqualizerModal(true);
  };

  /**
   * Closes the equalizer modal.
   */
  const closeEqualizerModal = () => {
    setSelectedEqualizerItem(null);
    setSelectedEqualizerType(null);
    setShowEqualizerModal(false);
  };

  /**
   * Edits a given item by setting it as the selected item and showing the edit modal.
   * @param type - The type of the item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   * @param item - The item object to edit.
   */
  const editItem = (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', item: ItemType) => {
    setSelectedItem(item);
    setSelectedItemType(type);
    setShowEditModal(true);
  };

  /**
   * Navigates to a given item by setting it as the selected item.
   * @param type - The type of the item ('sources', 'sinks', 'routes', 'group-sink', 'group-source').
   * @param name - The name of the item to navigate to.
   */
  const navigateToItem = (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string) => {
    setSelectedItemType(type);
    const items = type === 'sources' ? sources : type === 'sinks' ? sinks : routes;
    const item = items.find(i => i.name === name);
    if (item) {
      setSelectedItem(item);
    }
  };

  return (
    <AppContext.Provider value={{
      activeSource,
      listeningToSink,
      visualizingSink,
      sources,
      sinks,
      routes,
      onToggleActiveSource,
      onListenToSink,
      onVisualizeSink,
      toggleEnabled,
      updateVolume,
      updateTimeshift,
      controlSource,
      setSelectedItem,
      setShowEqualizerModal,
      setSelectedItemType,
      getRoutesForSource,
      getRoutesForSink,
      fetchSources,
      fetchSinks,
      fetchRoutes,
      setPrimarySource,
      openVNCModal,
      showEqualizerModal,
      selectedEqualizerItem,
      selectedEqualizerType,
      openEqualizerModal,
      closeEqualizerModal,
      showEditModal,
      setShowEditModal,
      editItem,
      selectedItem,
      selectedItemType,
      navigateToItem
    }}>
      {children}
    </AppContext.Provider>
  );
};

/**
 * Custom hook to use the AppContext.
 * @returns The context object containing all state and functions.
 */
export const useAppContext = () => {
  const context = useContext(AppContext);
  if (context === undefined) {
    throw new Error('useAppContext must be used within an AppProvider');
  }
  return context;
};

export default AppContext;
