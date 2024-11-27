/**
 * This file contains the API service for interacting with the backend.
 * It includes interfaces for Source, Sink, Route, and Equalizer objects,
 * as well as functions to manage WebSocket connections and perform HTTP requests.
 */

import axios from 'axios';
// Base URL for API requests
const BASE_URL = "/";

/**
 * Interface for Source object
 */
export interface Source {
  name: string;
  ip: string;
  enabled: boolean;
  is_group: boolean;
  group_members: string[];
  volume: number;
  equalizer: Equalizer;
  vnc_ip?: string;
  vnc_port?: number;
  delay: number;
  timeshift: number;
  favorite?: boolean;
  is_primary?: boolean;
}

/**
 * Interface for Sink object
 */
export interface Sink {
  name: string;
  ip: string;
  port: number;
  enabled: boolean;
  is_group: boolean;
  group_members: string[];
  volume: number;
  equalizer: Equalizer;
  bit_depth: number;
  sample_rate: number;
  channels: number;
  channel_layout: string;
  delay: number;
  timeshift: number;
  time_sync: boolean;
  time_sync_delay: number;
  favorite?: boolean;
}

/**
 * Interface for Route object
 */
export interface Route {
  name: string;
  source: string;
  sink: string;
  enabled: boolean;
  volume: number;
  equalizer: Equalizer;
  delay: number;
  timeshift: number;
  favorite?: boolean;
}

/**
 * Interface for Equalizer object
 */
export interface Equalizer {
  b1: number; b2: number; b3: number; b4: number; b5: number; b6: number;
  b7: number; b8: number; b9: number; b10: number; b11: number; b12: number;
  b13: number; b14: number; b15: number; b16: number; b17: number; b18: number;
}

/**
 * Interface for WebSocket update message
 */
export interface WebSocketUpdate {
  sources?: Record<string, Source>;
  sinks?: Record<string, Sink>;
  routes?: Record<string, Route>;
}

/**
 * Type for WebSocket message handler
 */
export type WebSocketMessageHandler = (update: WebSocketUpdate) => void;

let ws: WebSocket | null = null;
let messageHandler: WebSocketMessageHandler | null = null;

// Function to create WebSocket connection
const createWebSocket = () => {
  if (ws) return; // Don't create if already exists

  // Get the current URL's host and protocol
  const host = window.location.host;
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const wsUrl = `${protocol}//${host}:443/ws/config`;
  console.log("Creating WebSocket connection to:", wsUrl);

  ws = new WebSocket(wsUrl);

  ws.onmessage = (event) => {
    if (messageHandler) {
      try {
        const update = JSON.parse(event.data);
        messageHandler(update);
      } catch (error) {
        console.error('Error parsing WebSocket message:', error);
      }
    }
  };

  ws.onclose = () => {
    console.log("WebSocket closed, reloading page");
    window.location.reload();
  };

  ws.onerror = (error) => {
    console.error('WebSocket error:', error);
    ws?.close();
  };

  ws.onopen = () => {
    console.log("WebSocket connected successfully");
  };
};

// Function to fetch initial state and set up handler
const setupWebSocket = async (handler: WebSocketMessageHandler) => {
  messageHandler = handler;

  // Create WebSocket connection if it doesn't exist
  if (!ws) {
    createWebSocket();
  }

  // Fetch initial state
  try {
    const [sourcesRes, sinksRes, routesRes] = await Promise.all([
      axios.get<Record<string, Source>>('/sources'),
      axios.get<Record<string, Sink>>('/sinks'),
      axios.get<Record<string, Route>>('/routes')
    ]);

    // Send initial state through handler
    handler({
      sources: sourcesRes.data,
      sinks: sinksRes.data,
      routes: routesRes.data
    });
  } catch (error) {
    console.error('Error fetching initial state:', error);
  }
};

/**
 * ApiService object containing methods for interacting with the backend API
 */
const ApiService = {
  // WebSocket handler setter that also fetches initial state
  setWebSocketHandler: setupWebSocket,

  // GET requests
  getSources: () => axios.get<Record<string, Source>>('/sources'),
  getSinks: () => axios.get<Record<string, Sink>>('/sinks'),
  getRoutes: () => axios.get<Record<string, Route>>('/routes'),

  // POST requests for adding new items
  addSource: (data: Source) => axios.post<Source>('/sources', data),
  addSink: (data: Sink) => axios.post<Sink>('/sinks', data),
  addRoute: (data: Route) => axios.post<Route>('/routes', data),

  // PUT requests for updating existing items
  updateSource: (name: string, data: Partial<Source>) => axios.put<Source>( `/sources/${name}`, data),
  updateSink: (name: string, data: Partial<Sink>) => axios.put<Sink>( `/sinks/${name}`, data),
  updateRoute: (name: string, data: Partial<Route>) => axios.put<Route>( `/routes/${name}`, data),

  // DELETE requests
  deleteSource: (name: string) => axios.delete(`/sources/${name}`),
  deleteSink: (name: string) => axios.delete(`/sinks/${name}`),
  deleteRoute: (name: string) => axios.delete(`/routes/${name}`),

  // Enable/Disable requests
  enableSource: (name: string) => axios.get(`/sources/${name}/enable`),
  disableSource: (name: string) => axios.get(`/sources/${name}/disable`),
  enableSink: (name: string) => axios.get(`/sinks/${name}/enable`),
  disableSink: (name: string) => axios.get(`/sinks/${name}/disable`),
  enableRoute: (name: string) => axios.get(`/routes/${name}/enable`),
  disableRoute: (name: string) => axios.get(`/routes/${name}/disable`),

  // Volume update requests
  updateSourceVolume: (name: string, volume: number) => axios.get(`/sources/${name}/volume/${volume}`),
  updateSinkVolume: (name: string, volume: number) => axios.get(`/sinks/${name}/volume/${volume}`),
  updateRouteVolume: (name: string, volume: number) => axios.get(`/routes/${name}/volume/${volume}`),

  // Delay update requests
  updateSourceDelay: (name: string, delay: number) => axios.get(`/sources/${name}/delay/${delay}`),
  updateSinkDelay: (name: string, delay: number) => axios.get(`/sinks/${name}/delay/${delay}`),
  updateRouteDelay: (name: string, delay: number) => axios.get(`/routes/${name}/delay/${delay}`),

  // Timeshift update requests
  updateSourceTimeshift: (name: string, timeshift: number) => axios.get(`/sources/${name}/timeshift/${timeshift}`),
  updateSinkTimeshift: (name: string, timeshift: number) => axios.get(`/sinks/${name}/timeshift/${timeshift}`),
  updateRouteTimeshift: (name: string, timeshift: number) => axios.get(`/routes/${name}/timeshift/${timeshift}`),

  // Equalizer update requests
  updateSourceEqualizer: (name: string, equalizer: Equalizer) => axios.post(`/sources/${name}/equalizer`, equalizer),
  updateSinkEqualizer: (name: string, equalizer: Equalizer) => axios.post(`/sinks/${name}/equalizer`, equalizer),
  updateRouteEqualizer: (name: string, equalizer: Equalizer) => axios.post(`/routes/${name}/equalizer`, equalizer),

  // Reorder requests
  reorderSource: (name: string, newIndex: number) => axios.get(`/sources/${name}/reorder/${newIndex}`),
  reorderSink: (name: string, newIndex: number) => axios.get(`/sinks/${name}/reorder/${newIndex}`),
  reorderRoute: (name: string, newIndex: number) => axios.get(`/routes/${name}/reorder/${newIndex}`),

  // Control source playback
  controlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => 
    axios.get(`/sources/${sourceName}/${action}`),

  // Utility methods
  getSinkStreamUrl: (sinkIp: string) => `/stream/${sinkIp}/?random=${Math.random()}`,
  getVncUrl: (sourceName: string) => `/site/vnc/${sourceName}`,
};

export default ApiService;
