/**
 * This file contains the API service for interacting with the backend.
 * It includes interfaces for Source, Sink, Route, and Equalizer objects,
 * as well as functions to manage WebSocket connections and perform HTTP requests.
 */

import axios from 'axios';

/**
 * Interface for Source object
 */

// --- New SpeakerLayout Interface ---
export interface SpeakerLayout {
  auto_mode: boolean;
  matrix: number[][]; // Expects an 8x8 matrix
}
// --- End New SpeakerLayout Interface ---

export interface SourceMetadata {
  title?: string;
  artist?: string;
  album?: string;
  artUrl?: string;
}

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
  is_process?: boolean;
  tag?: string;
  channels?: number; // Added for Source
  speaker_layouts?: { [key: number]: SpeakerLayout }; // New dictionary
  metadata?: SourceMetadata;
}

/**
 * Interface for Sink object
 */
export interface Sink {
  name: string;
  config_id?: string;  // Unique ID for C++ engine
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
  speaker_layouts?: { [key: number]: SpeakerLayout }; // New dictionary
  protocol?: string;
  volume_normalization?: boolean;
  multi_device_mode?: boolean;  // For RTP multi-device configuration
  rtp_receiver_mappings?: Array<{  // RTP receiver channel mappings
    receiver_sink_name: string;  // Changed to match backend field name
    left_channel: number;
    right_channel: number;
  }>;
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
  speaker_layouts?: { [key: number]: SpeakerLayout }; // New dictionary
}

/**
 * Interface for Equalizer object
 */
export interface Equalizer {
  b1: number; b2: number; b3: number; b4: number; b5: number; b6: number;
  b7: number; b8: number; b9: number; b10: number; b11: number; b12: number;
  b13: number; b14: number; b15: number; b16: number; b17: number; b18: number;
  name?: string;
  normalization_enabled?: boolean;
}

export interface StreamStats {
  jitter_estimate_ms: number;
  packets_per_second: number;
  timeshift_buffer_size: number;
  timeshift_buffer_late_packets: number;
  timeshift_buffer_lagging_events: number;
  last_arrival_time_error_ms: number;
  total_anchor_adjustment_ms: number;
  total_packets_in_stream: number;
  tm_buffer_underruns: number;
  tm_packets_discarded: number;
  target_buffer_level_ms: number;
  buffer_target_fill_percentage: number;
}

export interface SourceStats {
  instance_id: string;
  source_tag: string;
  input_queue_size: number;
  output_queue_size: number;
  packets_processed_per_second: number;
  reconfigurations: number;
}

export interface WebRtcListenerStats {
  listener_id: string;
  connection_state: string;
  pcm_buffer_size: number;
  packets_sent_per_second: number;
}

export interface SinkStats {
  sink_id: string;
  active_input_streams: number;
  total_input_streams: number;
  packets_mixed_per_second: number;
  sink_buffer_underruns: number;
  sink_buffer_overflows: number;
  mp3_buffer_overflows: number;
  webrtc_listeners: WebRtcListenerStats[];
}

export interface GlobalStats {
  timeshift_buffer_total_size: number;
  packets_added_to_timeshift_per_second: number;
}

export interface AudioEngineStats {
  global_stats: GlobalStats;
  stream_stats: Record<string, StreamStats>;
  source_stats: SourceStats[];
  sink_stats: SinkStats[];
}

// --- Audio Engine Settings Interfaces ---
export interface TimeshiftTuning {
  cleanup_interval_ms: number;
  reanchor_interval_sec: number;
  jitter_smoothing_factor: number;
  jitter_safety_margin_multiplier: number;
  late_packet_threshold_ms: number;
  target_buffer_level_ms: number;
  proportional_gain_kp: number;
  min_playback_rate: number;
  max_playback_rate: number;
  loop_max_sleep_ms: number;
}

export interface MixerTuning {
  grace_period_timeout_ms: number;
  grace_period_poll_interval_ms: number;
  mp3_bitrate_kbps: number;
  mp3_vbr_enabled: boolean;
  mp3_output_queue_max_size: number;
}

export interface SourceProcessorTuning {
  command_loop_sleep_ms: number;
}

export interface ProcessorTuning {
  oversampling_factor: number;
  volume_smoothing_factor: number;
  dc_filter_cutoff_hz: number;
  soft_clip_threshold: number;
  soft_clip_knee: number;
  normalization_target_rms: number;
  normalization_attack_smoothing: number;
  normalization_decay_smoothing: number;
  dither_noise_shaping_factor: number;
}

export interface AudioEngineSettings {
  timeshift_tuning: TimeshiftTuning;
  mixer_tuning: MixerTuning;
  source_processor_tuning: SourceProcessorTuning;
  processor_tuning: ProcessorTuning;
}
// --- End Audio Engine Settings Interfaces ---


/**
 * Interface for WebSocket update message
 */
export interface WebSocketUpdate {
  sources?: Record<string, Source>;
  sinks?: Record<string, Sink>;
  routes?: Record<string, Route>;
  removals?: {
    sources?: string[];
    sinks?: string[];
    routes?: string[];
  };
}

/**
 * Type for WebSocket message handler
 */
export type WebSocketMessageHandler = (update: WebSocketUpdate) => void;

let ws: WebSocket | null = null;
let messageHandler: WebSocketMessageHandler | null = null;
let reconnectTimeout: ReturnType<typeof setTimeout> | null = null;

// Function to create WebSocket connection
const createWebSocket = () => {
  if (ws && ws.readyState === WebSocket.OPEN) return; // Don't create if already exists and open

  // Clear any existing reconnect timeout
  if (reconnectTimeout) {
    clearTimeout(reconnectTimeout);
    reconnectTimeout = null;
  }

  // Get the current URL's host and protocol
  const host = window.location.host;
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  
  // Use the same host without hardcoded port
  const wsUrl = `${protocol}//${host}/ws/config`;
  console.log("Creating WebSocket connection to:", wsUrl);

  try {
    ws = new WebSocket(wsUrl);

    ws.onmessage = (event) => {
      if (messageHandler) {
        try {
          const update = JSON.parse(event.data);
          console.log("WebSocket update received:", update);
          messageHandler(update);
        } catch (error) {
          console.error('Error parsing WebSocket message:', error);
        }
      }
    };

    ws.onclose = (event) => {
      console.log(`WebSocket closed (code: ${event.code}, reason: ${event.reason})`);
      ws = null;
      
      // Attempt to reconnect after 5 seconds
      reconnectTimeout = setTimeout(() => {
        console.log("Attempting to reconnect WebSocket...");
        createWebSocket();
      }, 5000);
    };

    ws.onerror = (error) => {
      console.error('WebSocket error:', error);
      // Let onclose handle reconnection
    };

    ws.onopen = () => {
      console.log("WebSocket connected successfully");
    };
  } catch (error) {
    console.error('Error creating WebSocket:', error);
    // Attempt to reconnect after 5 seconds
    reconnectTimeout = setTimeout(() => {
      console.log("Attempting to reconnect WebSocket...");
      createWebSocket();
    }, 5000);
  }
};

// Function to fetch initial state and set up handler
const setupWebSocket = async (handler: WebSocketMessageHandler) => {
  messageHandler = handler;

  // Create WebSocket connection if it doesn't exist
  createWebSocket();

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
  controlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'pause' | 'nexttrack') =>
    axios.get(`/sources/${sourceName}/${action}`),

  // Utility methods
  getSinkStreamUrl: (sinkIp: string) => `/stream/${sinkIp}/?random=${Math.random()}`,
  getVncUrl: (sourceName: string) => `/site/vnc/${sourceName}`,

  // Custom equalizer requests
  saveEqualizer: (name: string, equalizer: Equalizer) => { 
    const new_eq = {... equalizer};
    new_eq.name = name;
    return axios.post('/equalizers/', new_eq); } ,
  listEqualizers: () => axios.get<{ equalizers: Equalizer[] }>('/equalizers/'),
  deleteEqualizer: (name: string) => axios.delete(`/equalizers/${name}`),

  // New method to update equalizer based on type and name
  updateEqualizer: (type: 'sources' | 'sinks' | 'routes', name: string, equalizer: Equalizer) => {
    return axios.post(`/equalizers/${type}/${name}`, equalizer);
  },

  // --- Speaker Layout Update Methods ---
  updateSourceSpeakerLayout: (name: string, inputChannelKey: number, layout: SpeakerLayout) => {
      return axios.post(`/api/sources/${encodeURIComponent(name)}/speaker_layout/${inputChannelKey}`, layout);
  },

  updateSinkSpeakerLayout: (name: string, inputChannelKey: number, layout: SpeakerLayout) => {
      return axios.post(`/api/sinks/${encodeURIComponent(name)}/speaker_layout/${inputChannelKey}`, layout);
  },

  updateRouteSpeakerLayout: (name: string, inputChannelKey: number, layout: SpeakerLayout) => {
      return axios.post(`/api/routes/${encodeURIComponent(name)}/speaker_layout/${inputChannelKey}`, layout);
  },
  // --- End Speaker Layout Update Methods ---

  // --- Stats ---
  getStats: () => axios.get<AudioEngineStats>('/api/stats'),

  // --- Settings ---
  getSettings: () => axios.get<AudioEngineSettings>('/api/settings'),
  updateSettings: (settings: AudioEngineSettings) => axios.post('/api/settings', settings),
};

export default ApiService;
