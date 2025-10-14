/**
 * React context and provider for managing the application state.
 */
import React, { createContext, useContext, useState, useEffect, useRef } from 'react';
import { Sink, Source, Route, WebSocketUpdate } from '../api/api';
import ApiService from '../api/api';
const silenceMP3 = "/site/static/output_silence.mp3";

type ItemType = Source | Sink | Route;

interface AppContextType {
  /**
   * The currently Primary Source name.
   */
  activeSource: string | null;
  /**
   * The sink that is currently being listened to.
   */
  listeningStatus: Map<string, boolean>;
  /**
   * The audio streams from WebRTC listeners.
   */
  audioStreams: Map<string, MediaStream>;
  /**
   * The sink that is currently being visualized.
   */
  visualizingSink: Sink | null;
  /**
   * A map of sink IDs to playback errors.
   */
  playbackError: Map<string, Error>;
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
   * Toggles the Primary Source.
   * @param sourceName - The name of the source to toggle.
   */
  onToggleActiveSource: (sourceName: string) => void;
  /**
   * Sets the sink that is being transcribed.
   * @param sink - The sink.
   */
  onTranscribeSink: (ip: string) => void;
  /**
   * Sets the sink that is being listened to.
   * @param sinkId - The ID of the sink to listen to.
   */
  onListenToSink: (sinkId: string) => void;
  /**
   * Sets the entity (sink/source/route) that is being listened to.
   * @param entityType - The type of entity ('sink', 'source', or 'route').
   * @param entityId - The ID of the entity to listen to.
   */
  onListenToEntity: (entityType: 'sink' | 'source' | 'route', entityId: string) => void;
  /**
   * Sets the sink that is being visualized.
   * @param sink - The sink object or null to stop visualization.
   */
  onVisualizeSink: (sink: Sink | null) => void;
  /**
   * Callback for when audio playback fails.
   * @param sinkId - The ID of the sink where playback failed.
   * @param error - The error that occurred.
   */
  onPlaybackError: (sinkId: string, error: Error) => void;
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
   * @param type - The type of item ('sources', 'sinks', 'routes', 'group-sink' | 'group-source').
   * @param name - The name of the item to update.
   * @param timeshift - The new timeshift value in milliseconds.
   */
  updateTimeshift: (type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', name: string, timeshift: number) => Promise<void>;
  /**
   * Controls a source by sending actions like play, pause, next track, etc.
   * @param sourceName - The name of the source to control.
   * @param action - The action to perform ('prevtrack', 'play', 'nexttrack').
   */
  controlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'pause' | 'nexttrack') => Promise<void>;
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
  /**
   * Fetches all data (sources, sinks, routes) from the API and updates the state.
   * Optional, as it might not be used by all components.
   */
  refreshAppContext?: () => Promise<void>;
  silenceSessionAudioRef: React.RefObject<HTMLAudioElement>;
  isSilenceAudioPlaying: boolean;
  startedListeningSinks: Map<string, boolean>;
  setStartedListeningSinks: React.Dispatch<React.SetStateAction<Map<string, boolean>>>;
}

const AppContext = createContext<AppContextType | undefined>(undefined);

/**
 * React provider component for the application context.
 * @param children - The child components that will have access to the context.
 */
export const AppProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [activeSource, setActiveSource] = useState<string | null>(null);
  const [listeningStatus, setListeningStatus] = useState<Map<string, boolean>>(new Map());
  const webrtcListenersRef = useRef<Map<string, { pc: RTCPeerConnection, listenerId: string }>>(new Map());
  const heartbeatIntervalsRef = useRef<Map<string, NodeJS.Timeout>>(new Map());
  const [audioStreams, setAudioStreams] = useState<Map<string, MediaStream>>(new Map());
  const silenceSessionAudioRef = useRef<HTMLAudioElement>(null);
  // Removed audioContext and audioSourceNode - not needed for Chrome audio playback
  const [visualizingSink, setVisualizingSink] = useState<Sink | null>(null);
  const [playbackError, setPlaybackError] = useState<Map<string, Error>>(new Map());
  const [sources, setSources] = useState<Source[]>([]);
  const [sinks, setSinks] = useState<Sink[]>([]);
  const [routes, setRoutes] = useState<Route[]>([]);
  const [selectedItem, setSelectedItem] = useState<ItemType | null>(null);
  const [showEqualizerModal, setShowEqualizerModal] = useState(false);
  const [showEditModal, setShowEditModal] = useState(false);
  const [selectedItemType, setSelectedItemType] = useState<'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source' | null>(null);
  const [selectedEqualizerItem, setSelectedEqualizerItem] = useState<ItemType | null>(null);
  const [selectedEqualizerType, setSelectedEqualizerType] = useState<'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source' | null>(null);
  // Flag to track whether we need to refetch all data
  const [needsFullRefresh, setNeedsFullRefresh] = useState<boolean>(false);
  // Flag to track whether initial data has been loaded
  const [initialDataLoaded, setInitialDataLoaded] = useState<boolean>(false);
  const [manuallyPausedSinks, setManuallyPausedSinks] = useState<Set<string>>(new Set());
  const [isSilenceAudioPlaying, setIsSilenceAudioPlaying] = useState(true);
  const [startedListeningSinks, setStartedListeningSinks] = useState<Map<string, boolean>>(new Map());

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
    console.log("Fetching initial data...");
    try {
      const [sourcesResponse, sinksResponse, routesResponse] = await Promise.all([
        ApiService.getSources(),
        ApiService.getSinks(),
        ApiService.getRoutes()
      ]);
      // Set data directly, replacing any existing data
      setSources(Object.values(sourcesResponse.data));
      setSinks(Object.values(sinksResponse.data));
      setRoutes(Object.values(routesResponse.data));
      console.log("Initial data fetched successfully");
      setNeedsFullRefresh(false);
      setInitialDataLoaded(true);
    } catch (error) {
      console.error('Error fetching initial data:', error);
    }
  };

  // Handle WebSocket updates
  const handleWebSocketUpdate = (update: WebSocketUpdate) => {
    console.log("Processing WebSocket update:", update);
    
    // If this is the initial data load from api.ts setupWebSocket function, 
    // and we've already loaded data directly, skip this update to prevent duplicates
    if (!initialDataLoaded &&
        update.sources && Object.keys(update.sources).length > 0 &&
        update.sinks && Object.keys(update.sinks).length > 0 &&
        update.routes && Object.keys(update.routes).length > 0) {
      console.log("Initial data from WebSocket - using direct API data instead");
      setInitialDataLoaded(true);
      return;
    }
    
    // If we have an update from WebSocket before our direct API call completes,
    // mark initialDataLoaded to prevent the direct API call from overwriting WebSocket data
    if (!initialDataLoaded && 
        (update.sources || update.sinks || update.routes)) {
      setInitialDataLoaded(true);
    }
    
    // Handle source updates - fixed to properly merge by name
    if (update.sources && Object.keys(update.sources).length > 0) {
      setSources(prevSources => {
        // Create a map of all existing sources keyed by name
        const nameToSourceMap = new Map<string, Source>();
        
        // Add all existing sources to the map
        prevSources.forEach(source => {
          nameToSourceMap.set(source.name, source);
        });
        
        // Update/add sources from the update
        Object.entries(update.sources!).forEach(([name, source]) => {
          const isNew = !nameToSourceMap.has(name);
          nameToSourceMap.set(name, source);
          
          if (isNew) {
            console.log(`Added new source: ${name}`);
          } else {
            console.log(`Updated existing source: ${name}`);
          }
        });
        
        // Convert the map back to an array
        return Array.from(nameToSourceMap.values());
      });
    }
    
    // Handle sink updates - fixed to properly merge by name
    if (update.sinks && Object.keys(update.sinks).length > 0) {
      setSinks(prevSinks => {
        // Create a map of all existing sinks keyed by name
        const nameToSinkMap = new Map<string, Sink>();
        
        // Add all existing sinks to the map
        prevSinks.forEach(sink => {
          nameToSinkMap.set(sink.name, sink);
        });
        
        // Update/add sinks from the update
        Object.entries(update.sinks!).forEach(([name, sink]) => {
          const isNew = !nameToSinkMap.has(name);
          nameToSinkMap.set(name, sink);
          
          if (isNew) {
            console.log(`Added new sink: ${name}`);
          } else {
            console.log(`Updated existing sink: ${name}`);
          }
        });
        
        // Convert the map back to an array
        return Array.from(nameToSinkMap.values());
      });
    }
    
    // Handle route updates - fixed to properly merge by name
    if (update.routes && Object.keys(update.routes).length > 0) {
      setRoutes(prevRoutes => {
        // Create a map of all existing routes keyed by name
        const nameToRouteMap = new Map<string, Route>();
        
        // Add all existing routes to the map
        prevRoutes.forEach(route => {
          nameToRouteMap.set(route.name, route);
        });
        
        // Update/add routes from the update
        Object.entries(update.routes!).forEach(([name, route]) => {
          const isNew = !nameToRouteMap.has(name);
          nameToRouteMap.set(name, route);
          
          if (isNew) {
            console.log(`Added new route: ${name}`);
          } else {
            console.log(`Updated existing route: ${name}`);
          }
        });
        
        // Convert the map back to an array
        return Array.from(nameToRouteMap.values());
      });
    }
    
    // Handle removals if present in the update
    if (update.removals) {
      // Handle source removals
      if (update.removals.sources && update.removals.sources.length > 0) {
        setSources(prevSources => {
          const updatedSources = prevSources.filter(source => 
            !update.removals!.sources!.includes(source.name)
          );
          update.removals!.sources!.forEach((name: string) => {
            console.log(`Removed source: ${name}`);
          });
          return updatedSources;
        });
      }
      
      // Handle sink removals
      if (update.removals.sinks && update.removals.sinks.length > 0) {
        setSinks(prevSinks => {
          const updatedSinks = prevSinks.filter(sink => 
            !update.removals!.sinks!.includes(sink.name)
          );
          update.removals!.sinks!.forEach((name: string) => {
            console.log(`Removed sink: ${name}`);
          });
          return updatedSinks;
        });
      }
      
      // Handle route removals
      if (update.removals.routes && update.removals.routes.length > 0) {
        setRoutes(prevRoutes => {
          const updatedRoutes = prevRoutes.filter(route => 
            !update.removals!.routes!.includes(route.name)
          );
          update.removals!.routes!.forEach((name: string) => {
            console.log(`Removed route: ${name}`);
          });
          return updatedRoutes;
        });
      }
    }
  };

  // Set up WebSocket and initial data
  useEffect(() => {
    const storedActiveSource = localStorage.getItem('activeSource');
    if (storedActiveSource) {
      setActiveSource(storedActiveSource);
    }

    // Set up WebSocket handler before fetching initial data
    // This ensures any realtime updates that come in during/after initial load are processed
    ApiService.setWebSocketHandler((update: WebSocketUpdate) => {
      handleWebSocketUpdate(update);
    });

    // Now fetch initial data directly - this provides a clean initial state
    fetchInitialData();

    // Setup periodic check for full refresh
    const refreshInterval = setInterval(() => {
      if (needsFullRefresh) {
        console.log("Performing full data refresh");
        fetchInitialData();
      }
    }, 30000); // Check every 30 seconds

    // Cleanup on component unmount
    return () => {
      clearInterval(refreshInterval);
    };
  }, [needsFullRefresh]);

  // Removed duplicate useEffect hooks that were setting mediaSessionAudioRef to silence MP3
  // mediaSessionAudioRef should ONLY be used for WebRTC streams, not silence audio
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
   * Toggles the Primary Source.
   * @param sourceName - The name of the source to toggle.
   */
  const onToggleActiveSource = (sourceName: string) => {
    const newActiveSource = activeSource === sourceName ? null : sourceName;
    setActiveSource(newActiveSource);
    localStorage.setItem('activeSource', newActiveSource || '');
  };

  const onTranscribeSink = (ip: string) => {
    window.open(`/site/transcribe/${ip}/`, "Transcription");
  }

  /**
   * Sets the sink that is being listened to via WebRTC.
   * @param sinkId - The ID of the sink to start or stop listening to.
   */
  // Store temporary sink IDs for cleanup
  const temporarySinkIdsRef = useRef<Map<string, string>>(new Map());

  // --- Centralized Cleanup Function ---
  const cleanupConnection = async (entityId: string, entityType: 'sink' | 'source' | 'route', reason: string) => {
    console.log(`[WebRTC:${entityType}:${entityId}] Cleaning up connection. Reason: ${reason}`);
    const listener = webrtcListenersRef.current.get(entityId);

    // Stop heartbeat
    if (heartbeatIntervalsRef.current.has(entityId)) {
      clearInterval(heartbeatIntervalsRef.current.get(entityId)!);
      heartbeatIntervalsRef.current.delete(entityId);
      console.log(`[WebRTC:${entityType}:${entityId}] Stopped heartbeat.`);
    }

    if (listener) {
      listener.pc.close();
      // Send DELETE request to WHEP endpoint
      try {
        let webrtcId = entityId;
        let endpoint = '';
        
        // For sources and routes, use the temporary sink_id if available
        const temporarySinkId = temporarySinkIdsRef.current.get(entityId);
        
        if (entityType === 'sink') {
          const sink = sinks.find(s => s.name === entityId);
          webrtcId = sink?.config_id || entityId;
          endpoint = `/api/whep/${webrtcId}/${listener.listenerId}`;
        } else if (temporarySinkId) {
          // For sources and routes, use the temporary sink_id
          webrtcId = temporarySinkId;
          endpoint = `/api/whep/${webrtcId}/${listener.listenerId}`;
        }
        
        if (endpoint) {
          await fetch(endpoint, {
            method: 'DELETE',
          });
          console.log(`[WebRTC:${entityType}:${entityId}] Sent DELETE request to WHEP endpoint.`);
        }
      } catch (error) {
        console.error(`[WebRTC:${entityType}:${entityId}] Error sending DELETE request:`, error);
      }
    }

    // Clean up temporary entities for sources and routes
    if (entityType === 'source' || entityType === 'route') {
      const temporarySinkId = temporarySinkIdsRef.current.get(entityId);
      if (temporarySinkId) {
        try {
          console.log(`[WebRTC:${entityType}:${entityId}] Cleaning up temporary sink: ${temporarySinkId}`);
          await fetch(`/api/listen/temporary/${temporarySinkId}`, {
            method: 'DELETE',
          });
          console.log(`[WebRTC:${entityType}:${entityId}] Temporary sink cleaned up successfully.`);
          temporarySinkIdsRef.current.delete(entityId);
        } catch (error) {
          console.error(`[WebRTC:${entityType}:${entityId}] Error cleaning up temporary sink:`, error);
        }
      }
    }

    webrtcListenersRef.current.delete(entityId);
    setAudioStreams(prev => {
      const newMap = new Map(prev);
      newMap.delete(entityId);
      return newMap;
    });
    setListeningStatus(prev => new Map(prev).set(entityId, false));
    console.log(`[WebRTC:${entityType}:${entityId}] Connection cleanup complete.`);
  };

  // New unified function to listen to any entity type
  const onListenToEntity = async (entityType: 'sink' | 'source' | 'route', entityId: string) => {
    const isCurrentlyListening = listeningStatus.get(entityId) || webrtcListenersRef.current.has(entityId);

    // --- INTENT: STOP LISTENING ---
    // If the user clicks the entity that is already playing, we stop it.
    if (isCurrentlyListening) {
      console.log(`[WebRTC:${entityType}:${entityId}] User intent: STOP.`);
      await cleanupConnection(entityId, entityType, "User initiated stop");
      return; // Stop execution here
    }

    // --- INTENT: START LISTENING ---
    // If the user clicks a new entity, we enforce the "single stream" rule.
    console.log(`[WebRTC:${entityType}:${entityId}] User intent: START.`);

    // 1. Stop all other active listeners before starting a new one.
    const cleanupPromises: Promise<void>[] = [];
    webrtcListenersRef.current.forEach((listener, existingEntityId) => {
      if (existingEntityId !== entityId) {
        console.log(`[WebRTC] Found active listener on ${existingEntityId}. Cleaning up before starting new stream.`);
        // Determine the type based on what we have stored
        let existingType: 'sink' | 'source' | 'route' = 'sink';
        if (temporarySinkIdsRef.current.has(existingEntityId)) {
          // If we have a temporary sink ID, it's either a source or route
          // We can't determine exactly which, but cleanup will work the same
          existingType = 'source'; // Could be 'route' too, but cleanup is the same
        }
        cleanupPromises.push(cleanupConnection(existingEntityId, existingType, "New stream requested"));
      }
    });

    // Wait for all cleanup operations to complete.
    if (cleanupPromises.length > 0) {
      console.log(`[WebRTC] Waiting for ${cleanupPromises.length} existing connection(s) to close.`);
      await Promise.all(cleanupPromises);
      console.log(`[WebRTC] All existing connections closed.`);
    }

    // 2. For sources and routes, first setup temporary entities
    let webrtcSinkId = entityId;
    
    if (entityType === 'source' || entityType === 'route') {
      try {
        console.log(`[WebRTC:${entityType}:${entityId}] Setting up temporary entities...`);
        
        const setupEndpoint = entityType === 'source'
          ? `/api/listen/source/${entityId}/setup`
          : `/api/listen/route/${entityId}/setup`;
        
        const setupResponse = await fetch(setupEndpoint, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' }
        });
        
        if (!setupResponse.ok) {
          const errorText = await setupResponse.text();
          throw new Error(`Failed to setup ${entityType} listener: ${setupResponse.status} - ${errorText}`);
        }
        
        const setupData = await setupResponse.json();
        webrtcSinkId = setupData.sink_id;
        
        // Store the temporary sink ID for cleanup
        temporarySinkIdsRef.current.set(entityId, webrtcSinkId);
        
        console.log(`[WebRTC:${entityType}:${entityId}] Temporary sink created with ID: ${webrtcSinkId}`);
      } catch (error) {
        console.error(`[WebRTC:${entityType}:${entityId}] Failed to setup temporary entities:`, error);
        setPlaybackError(prev => new Map(prev).set(entityId, error as Error));
        return;
      }
    } else if (entityType === 'sink') {
      // For regular sinks, find the config_id
      const sink = sinks.find(s => s.name === entityId);
      webrtcSinkId = sink?.config_id || entityId;
    }

    // 3. Now, proceed with establishing the new connection using the sink ID.
    console.log(`[WebRTC:${entityType}:${entityId}] Proceeding to establish WHEP connection with sink_id: ${webrtcSinkId}`);
    const pc = new RTCPeerConnection({
      iceServers: [
        { urls: 'stun:stun.l.google.com:19302' },
        {
          urls: 'turn:192.168.3.201',
          username: 'screamrouter',
          credential: 'screamrouter',
        },
      ],
    });

    const iceCandidateQueue: RTCIceCandidate[] = [];
    let listenerId: string | null = null;

    pc.onicecandidate = (event) => {
      if (event.candidate) {
        const candidateJson = event.candidate.toJSON();
        const payload = {
          candidate: candidateJson.candidate,
          sdpMid: candidateJson.sdpMid
        };
        if (listenerId) {
          // Use the webrtcSinkId which is either the sink's config_id or the temporary sink_id
          const endpoint = `/api/whep/${webrtcSinkId}/${listenerId}`;
          
          fetch(endpoint, {
            method: 'PATCH',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
          }).catch(e => console.error(`[WebRTC:${entityType}:${entityId}] Error sending ICE candidate:`, e));
        } else {
          iceCandidateQueue.push(event.candidate);
        }
      }
    };

    pc.ontrack = (event) => {
      console.log(`[WebRTC:${entityType}:${entityId}] Received remote audio track.`);
      // Just store the stream - the WebRTCAudioPlayers component will handle playback
      setAudioStreams(prev => new Map(prev).set(entityId, event.streams[0]));
    };

    pc.oniceconnectionstatechange = async () => {
      console.log(`[WebRTC:${entityType}:${entityId}] ICE connection state changed to: ${pc.iceConnectionState}`);
      if (pc.iceConnectionState === 'connected') {
        setListeningStatus(prev => new Map(prev).set(entityId, true));
        const intervalId = setInterval(async () => {
          const listener = webrtcListenersRef.current.get(entityId);
          if (listener) {
            try {
              // Use the webrtcSinkId for heartbeat
              const endpoint = `/api/whep/${webrtcSinkId}/${listener.listenerId}`;
              
              const response = await fetch(endpoint, { method: 'POST' });
              if (response.status === 404) {
                console.error(`[WebRTC:${entityType}:${entityId}] Heartbeat failed: session not found. Cleaning up.`);
                await cleanupConnection(entityId, entityType, "Heartbeat failed");
              }
            } catch (error) {
              console.error(`[WebRTC:${entityType}:${entityId}] Heartbeat request error:`, error);
            }
          }
        }, 5000);
        heartbeatIntervalsRef.current.set(entityId, intervalId);
      } else if (['failed', 'disconnected', 'closed'].includes(pc.iceConnectionState)) {
        await cleanupConnection(entityId, entityType, `ICE state changed to ${pc.iceConnectionState}`);
      }
    };

    try {
      pc.addTransceiver('audio', { direction: 'recvonly' });
      const offer = await pc.createOffer();
      offer.sdp = offer.sdp?.replace("useinbandfec=1", "useinbandfec=1;stereo=1;sprop-stereo=1");
      offer.sdp = offer.sdp?.replace("useinbandfec=0", "useinbandfec=0;stereo=1;sprop-stereo=1");
      await pc.setLocalDescription(offer);

      // Use the webrtcSinkId for the WHEP endpoint
      const endpoint = `/api/whep/${webrtcSinkId}`;
      
      console.log(`[WebRTC:${entityType}:${entityId}] Using WebRTC sink ID: ${webrtcSinkId}`);

      const response = await fetch(endpoint, {
        method: 'POST',
        headers: { 'Content-Type': 'application/sdp' },
        body: offer.sdp
      });

      if (response.status !== 201) {
        throw new Error(`Failed to get answer from WHEP endpoint. Status: ${response.status}`);
      }

      const answerSdp = await response.text();
      const location = response.headers.get('Location');
      if (!location) {
        throw new Error('Location header not found in WHEP response');
      }

      listenerId = location.split('/').pop()!;
      console.log(`[WebRTC:${entityType}:${entityId}] Received listenerId: ${listenerId}.`);

      // Process any queued candidates now that we have the listenerId
      while (iceCandidateQueue.length > 0) {
        const candidate = iceCandidateQueue.shift();
        if (candidate) {
          const candidateJson = candidate.toJSON();
          const payload = {
            candidate: candidateJson.candidate,
            sdpMid: candidateJson.sdpMid
          };
          // Use the webrtcSinkId for the PATCH endpoint
          const patchEndpoint = `/api/whep/${webrtcSinkId}/${listenerId}`;
          
          fetch(patchEndpoint, {
            method: 'PATCH',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
          }).catch(e => console.error(`[WebRTC:${entityType}:${entityId}] Error sending queued ICE candidate:`, e));
        }
      }

      const answer = new RTCSessionDescription({ type: 'answer', sdp: answerSdp });
      await pc.setRemoteDescription(answer);

      webrtcListenersRef.current.set(entityId, { pc, listenerId: listenerId! });
      console.log(`[WebRTC:${entityType}:${entityId}] Started listening via WHEP`);

    } catch (error) {
      console.error(`[WebRTC:${entityType}:${entityId}] Error during WHEP negotiation:`, error);
      await cleanupConnection(entityId, entityType, "WHEP negotiation failed");
    }
  };

  // Keep the original onListenToSink for backward compatibility
  const onListenToSink = async (sinkId: string) => {
    await onListenToEntity('sink', sinkId);
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

  const onPlaybackError = (sinkId: string, error: Error) => {
    if (error.name === 'NotAllowedError') {
      console.error(`[PlaybackError:${sinkId}] Autoplay failed. Stopping WHEP connection.`, error);
      setPlaybackError(prev => new Map(prev).set(sinkId, error));
      // Also update listening status to false, since playback failed.
      setListeningStatus(prev => new Map(prev).set(sinkId, false));
      // THIS IS THE FIX: Trigger the cleanup logic in onListenToSink to send the WHEP stop signal.
      onListenToSink(sinkId);
      cleanupConnection(sinkId, 'sink', "Playback error");
    }
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

  useEffect(() => {
    const audioEl = silenceSessionAudioRef.current;
    if (!audioEl) {
      return;
    }
  
    const handlePause = () => {
      console.log("Silence audio paused, stopping all WebRTC sessions.");
      setIsSilenceAudioPlaying(false);
      const sinksToPause = new Set<string>();
      listeningStatus.forEach((isListening, sinkId) => {
        if (isListening) {
          sinksToPause.add(sinkId);
          onListenToSink(sinkId); // This will toggle it off
        }
      });
      setManuallyPausedSinks(sinksToPause);
      setStartedListeningSinks(prev => {
        const newMap = new Map(prev);
        sinksToPause.forEach(sinkId => newMap.set(sinkId, false));
        return newMap;
      });
    };
  
    const handlePlay = () => {
      console.log("Silence audio playing, restarting paused WebRTC sessions.");
      setIsSilenceAudioPlaying(true);
      manuallyPausedSinks.forEach(sinkId => {
        onListenToSink(sinkId); // This will toggle it on
      });
      setStartedListeningSinks(prev => {
        const newMap = new Map(prev);
        manuallyPausedSinks.forEach(sinkId => newMap.set(sinkId, true));
        return newMap;
      });
      setManuallyPausedSinks(new Set());
    };
  
    audioEl.addEventListener('pause', handlePause);
    audioEl.addEventListener('play', handlePlay);
  
    return () => {
      audioEl.removeEventListener('pause', handlePause);
      audioEl.removeEventListener('play', handlePlay);
    };
  }, [listeningStatus, onListenToSink, manuallyPausedSinks]);

  /**
   * Controls a source by sending actions like play, pause, next track, etc.
   * @param sourceName - The name of the source to control.
   * @param action - The action to perform ('prevtrack', 'play', 'nexttrack').
   */
  const controlSource = async (sourceName: string, action: 'prevtrack' | 'play' | 'pause' | 'nexttrack') => {
    try {
      await ApiService.controlSource(sourceName, action);
    } catch (error) {
      console.error('Error controlling source:', error);
    }
  };
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
      listeningStatus,
      audioStreams,
      visualizingSink,
      playbackError,
      sources,
      sinks,
      routes,
      onToggleActiveSource,
      onTranscribeSink,
      onListenToSink,
      onListenToEntity,
      onVisualizeSink,
      onPlaybackError,
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
      navigateToItem,
      refreshAppContext: fetchInitialData, // Provide fetchInitialData as refreshAppContext
      silenceSessionAudioRef,
      isSilenceAudioPlaying,
      startedListeningSinks,
      setStartedListeningSinks,
    }}>
      {children}
      <audio id={"silencesAudio"} ref={silenceSessionAudioRef} src={silenceMP3} style={{"display":"none"}} controls autoPlay loop />
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
