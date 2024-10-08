import axios from 'axios';

// Base URL for API requests, fallback to '/' if not set in environment
const BASE_URL = process.env.REACT_APP_API_URL || '/';

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
  time_sync: boolean;
  time_sync_delay: number;
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
 * ApiService object containing methods for interacting with the backend API
 */
const ApiService = {
  // GET requests
  getSources: () => axios.get<Source[]>(`${BASE_URL}sources`),
  getSinks: () => axios.get<Sink[]>(`${BASE_URL}sinks`),
  getRoutes: () => axios.get<Route[]>(`${BASE_URL}routes`),

  // POST requests for adding new items
  addSource: (data: Source) => axios.post<Source>(`${BASE_URL}sources`, data),
  addSink: (data: Sink) => axios.post<Sink>(`${BASE_URL}sinks`, data),
  addRoute: (data: Route) => axios.post<Route>(`${BASE_URL}routes`, data),

  // PUT requests for updating existing items
  updateSource: (name: string, data: Partial<Source>) => axios.put<Source>(`${BASE_URL}sources/${name}`, data),
  updateSink: (name: string, data: Partial<Sink>) => axios.put<Sink>(`${BASE_URL}sinks/${name}`, data),
  updateRoute: (name: string, data: Partial<Route>) => axios.put<Route>(`${BASE_URL}routes/${name}`, data),

  // DELETE requests
  deleteSource: (name: string) => axios.delete(`${BASE_URL}sources/${name}`),
  deleteSink: (name: string) => axios.delete(`${BASE_URL}sinks/${name}`),
  deleteRoute: (name: string) => axios.delete(`${BASE_URL}routes/${name}`),

  // Enable/Disable requests
  enableSource: (name: string) => axios.get(`${BASE_URL}sources/${name}/enable`),
  disableSource: (name: string) => axios.get(`${BASE_URL}sources/${name}/disable`),
  enableSink: (name: string) => axios.get(`${BASE_URL}sinks/${name}/enable`),
  disableSink: (name: string) => axios.get(`${BASE_URL}sinks/${name}/disable`),
  enableRoute: (name: string) => axios.get(`${BASE_URL}routes/${name}/enable`),
  disableRoute: (name: string) => axios.get(`${BASE_URL}routes/${name}/disable`),

  // Volume update requests
  updateSourceVolume: (name: string, volume: number) => axios.get(`${BASE_URL}sources/${name}/volume/${volume}`),
  updateSinkVolume: (name: string, volume: number) => axios.get(`${BASE_URL}sinks/${name}/volume/${volume}`),
  updateRouteVolume: (name: string, volume: number) => axios.get(`${BASE_URL}routes/${name}/volume/${volume}`),

  // Delay update requests
  updateSourceDelay: (name: string, delay: number) => axios.get(`${BASE_URL}sources/${name}/delay/${delay}`),
  updateSinkDelay: (name: string, delay: number) => axios.get(`${BASE_URL}sinks/${name}/delay/${delay}`),
  updateRouteDelay: (name: string, delay: number) => axios.get(`${BASE_URL}routes/${name}/delay/${delay}`),

  // Equalizer update requests
  updateSourceEqualizer: (name: string, equalizer: Equalizer) => axios.post(`${BASE_URL}sources/${name}/equalizer`, equalizer),
  updateSinkEqualizer: (name: string, equalizer: Equalizer) => axios.post(`${BASE_URL}sinks/${name}/equalizer`, equalizer),
  updateRouteEqualizer: (name: string, equalizer: Equalizer) => axios.post(`${BASE_URL}routes/${name}/equalizer`, equalizer),

  // Reorder requests
  reorderSource: (name: string, newIndex: number) => axios.get(`${BASE_URL}sources/${name}/reorder/${newIndex}`),
  reorderSink: (name: string, newIndex: number) => axios.get(`${BASE_URL}sinks/${name}/reorder/${newIndex}`),
  reorderRoute: (name: string, newIndex: number) => axios.get(`${BASE_URL}routes/${name}/reorder/${newIndex}`),

  // Control source playback
  controlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => 
    axios.get(`${BASE_URL}sources/${sourceName}/${action}`),

  // Utility methods
  getSinkStreamUrl: (sinkIp: string) => `${BASE_URL}stream/${sinkIp}/?random=${Math.random()}`,
  getVncUrl: (sourceName: string) => `${BASE_URL}site/vnc/${sourceName}`,
};

export default ApiService;