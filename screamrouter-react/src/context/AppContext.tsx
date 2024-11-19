import React, { createContext, useContext, useState, useEffect, useRef } from 'react';
import { Sink, Source, Route, WebSocketUpdate } from '../api/api';
import ApiService from '../api/api';

type ItemType = Source | Sink | Route;

interface AppContextType {
  activeSource: string | null;
  listeningToSink: Sink | null;
  visualizingSink: Sink | null;
  sources: Source[];
  sinks: Sink[];
  routes: Route[];
  onToggleActiveSource: (sourceName: string) => void;
  onListenToSink: (sink: Sink | null) => void;
  onVisualizeSink: (sink: Sink | null) => void;
  toggleEnabled: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, currentStatus: boolean) => Promise<void>;
  updateVolume: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, volume: number) => Promise<void>;
  updateTimeshift: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, timeshift: number) => Promise<void>;
  controlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => Promise<void>;
  setSelectedItem: (item: ItemType | null) => void;
  setShowEqualizerModal: (show: boolean) => void;
  setSelectedItemType: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source' | null) => void;
  getRoutesForSource: (sourceName: string) => Route[];
  getRoutesForSink: (sinkName: string) => Route[];
  fetchSources: () => Promise<void>;
  fetchSinks: () => Promise<void>;
  fetchRoutes: () => Promise<void>;
  setPrimarySource: (source: Source | null) => void;
  openVNCModal: (source: Source) => void;
  showEqualizerModal: boolean;
  selectedEqualizerItem: ItemType | null;
  selectedEqualizerType: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source' | null;
  openEqualizerModal: (item: ItemType, type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source') => void;
  closeEqualizerModal: () => void;
  showEditModal: boolean;
  setShowEditModal: (show: boolean) => void;
  editItem: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', item: ItemType) => void;
  selectedItem: ItemType | null;
  selectedItemType: 'sources' | 'sinks' | 'routes' | 'group-sink' | "group-source" | null;
  navigateToItem: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string) => void;
}

const AppContext = createContext<AppContextType | undefined>(undefined);

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

  const openInNewWindow = (url: string, width: number = 800, height: number = 600) => {
    const left = (window.screen.width - width) / 2;
    const top = (window.screen.height - height) / 2;
    window.open(url, '_blank', `width=${width},height=${height},left=${left},top=${top}`);
  };

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

  const fetchSources = async () => {
    try {
      const response = await ApiService.getSources();
      setSources(Object.values(response.data));
    } catch (error) {
      console.error('Error fetching sources:', error);
    }
  };

  const fetchSinks = async () => {
    try {
      const response = await ApiService.getSinks();
      setSinks(Object.values(response.data));
    } catch (error) {
      console.error('Error fetching sinks:', error);
    }
  };

  const fetchRoutes = async () => {
    try {
      const response = await ApiService.getRoutes();
      setRoutes(Object.values(response.data));
    } catch (error) {
      console.error('Error fetching routes:', error);
    }
  };

  const onToggleActiveSource = (sourceName: string) => {
    const newActiveSource = activeSource === sourceName ? null : sourceName;
    setActiveSource(newActiveSource);
    localStorage.setItem('activeSource', newActiveSource || '');
  };

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

  const onVisualizeSink = (sink: Sink | null) => {
    if (sink) {
      openInNewWindow(`/site/visualizer?ip=${sink.ip}`, 800, 600);
    }
    setVisualizingSink(sink);
  };

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

  const controlSource = async (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => {
    try {
      await ApiService.controlSource(sourceName, action);
    } catch (error) {
      console.error('Error controlling source:', error);
    }
  };

  const getRoutesForSource = (sourceName: string) => {
    return routes.filter(route => route.source === sourceName);
  };

  const getRoutesForSink = (sinkName: string) => {
    return routes.filter(route => route.sink === sinkName);
  };

  const setPrimarySource = (source: Source | null) => {
    setSources(prevSources => prevSources.map(s => ({
      ...s,
      is_primary: s.name === source?.name
    })));
  };

  const openVNCModal = (source: Source) => {
    if (source.vnc_ip && source.vnc_port) {
      openInNewWindow(`/site/vnc?ip=${source.vnc_ip}&port=${source.vnc_port}`, 800, 600);
    }
  };

  const openEqualizerModal = (item: ItemType, type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source') => {
    setSelectedEqualizerItem(item);
    setSelectedEqualizerType(type);
    setShowEqualizerModal(true);
  };

  const closeEqualizerModal = () => {
    setSelectedEqualizerItem(null);
    setSelectedEqualizerType(null);
    setShowEqualizerModal(false);
  };

  const editItem = (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', item: ItemType) => {
    setSelectedItem(item);
    setSelectedItemType(type);
    setShowEditModal(true);
  };

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

export const useAppContext = () => {
  const context = useContext(AppContext);
  if (context === undefined) {
    throw new Error('useAppContext must be used within an AppProvider');
  }
  return context;
};

export default AppContext;
