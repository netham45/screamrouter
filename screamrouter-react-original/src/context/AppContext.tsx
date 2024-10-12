import React, { createContext, useContext, useState, useEffect, useRef } from 'react';
import { Sink, Source, Route } from '../api/api';
import ApiService from '../api/api';

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
  toggleEnabled: (type: 'sources' | 'sinks', name: string, currentStatus: boolean) => Promise<void>;
  updateVolume: (type: 'sources' | 'sinks', name: string, volume: number) => Promise<void>;
  controlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => Promise<void>;
  setSelectedItem: (item: any) => void;
  setShowVNCModal: (show: boolean) => void;
  setShowEqualizerModal: (show: boolean) => void;
  setSelectedItemType: (type: 'sources' | 'sinks' | 'routes' | null) => void;
  getRoutesForSource: (sourceName: string) => Route[];
  getRoutesForSink: (sinkName: string) => Route[];
  fetchSources: () => Promise<void>;
  fetchRoutes: () => Promise<void>;
}

const AppContext = createContext<AppContextType | undefined>(undefined);

export const AppProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [activeSource, setActiveSource] = useState<string | null>(null);
  const [listeningToSink, setListeningToSink] = useState<Sink | null>(null);
  const [visualizingSink, setVisualizingSink] = useState<Sink | null>(null);
  const [sources, setSources] = useState<Source[]>([]);
  const [sinks, setSinks] = useState<Sink[]>([]);
  const [routes, setRoutes] = useState<Route[]>([]);
  const [selectedItem, setSelectedItem] = useState<any>(null);
  const [showVNCModal, setShowVNCModal] = useState(false);
  const [showEqualizerModal, setShowEqualizerModal] = useState(false);
  const [selectedItemType, setSelectedItemType] = useState<'sources' | 'sinks' | 'routes' | null>(null);
  const audioRef = useRef<HTMLAudioElement | null>(null);

  useEffect(() => {
    const storedActiveSource = localStorage.getItem('activeSource');
    if (storedActiveSource) {
      setActiveSource(storedActiveSource);
    }

    audioRef.current = document.getElementById('audio') as HTMLAudioElement;

    // Fetch initial data
    fetchSources();
    fetchSinks();
    fetchRoutes();
  }, []);

  const fetchSources = async () => {
    try {
      const response = await ApiService.getSources();
      setSources(response.data);
      // Check if the active source is still enabled
      if (activeSource && !response.data.some(source => source.name === activeSource && source.enabled)) {
        setActiveSource(null);
        localStorage.removeItem('activeSource');
      }
    } catch (error) {
      console.error('Error fetching sources:', error);
    }
  };

  const fetchSinks = async () => {
    try {
      const response = await ApiService.getSinks();
      setSinks(response.data);
    } catch (error) {
      console.error('Error fetching sinks:', error);
    }
  };

  const fetchRoutes = async () => {
    try {
      const response = await ApiService.getRoutes();
      setRoutes(response.data);
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
    setVisualizingSink(sink);
    if (sink) {
      window.startVisualizer(sink.ip);
    } else {
      window.stopVisualizer();
    }
  };

  const toggleEnabled = async (type: 'sources' | 'sinks', name: string, currentStatus: boolean) => {
    try {
      if (type === 'sources') {
        await ApiService.updateSource(name, { enabled: !currentStatus });
        if (activeSource === name && currentStatus) {
          // If we're disabling the active source, set it to null
          setActiveSource(null);
          localStorage.removeItem('activeSource');
        }
        await fetchSources();
      } else {
        await ApiService.updateSink(name, { enabled: !currentStatus });
        await fetchSinks();
      }
    } catch (error) {
      console.error(`Error toggling ${type} enabled status:`, error);
    }
  };

  const updateVolume = async (type: 'sources' | 'sinks', name: string, volume: number) => {
    try {
      if (type === 'sources') {
        await ApiService.updateSource(name, { volume });
        fetchSources();
      } else {
        await ApiService.updateSink(name, { volume });
        fetchSinks();
      }
    } catch (error) {
      console.error(`Error updating ${type} volume:`, error);
    }
  };

  const controlSource = async (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => {
    try {
      await ApiService.controlSource(sourceName, action);
      fetchSources();
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
      controlSource,
      setSelectedItem,
      setShowVNCModal,
      setShowEqualizerModal,
      setSelectedItemType,
      getRoutesForSource,
      getRoutesForSink,
      fetchSources,
      fetchRoutes
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