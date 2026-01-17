/**
 * This file contains the API service for interacting with the backend.
 * It includes interfaces for Source, Sink, Route, and Equalizer objects,
 * as well as functions to manage WebSocket connections and perform HTTP requests.
 */

import axios, { AxiosRequestConfig, AxiosResponse } from 'axios';
import { Preferences, PreferencesUpdatePayload, UnifiedDiscoverySnapshot, DiscoveryInventoryResponse } from '../types/preferences';

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
  ip?: string | null;
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
  tag?: string | null;
  channels?: number | null; // Added for Source
  sample_rate?: number | null;
  bit_depth?: number | null;
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
  sap_target_sink?: string;
  sap_target_host?: string;
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
  remote_target?: RemoteRouteTarget;
}

export interface RemoteRouteTarget {
  router_uuid?: string;
  router_hostname?: string;
  router_address?: string;
  router_port?: number;
  router_scheme?: 'http' | 'https';
  sink_config_id?: string;
  sink_name?: string;
}

export interface SystemAudioDeviceInfo {
  tag: string;
  direction: 'capture' | 'playback';
  friendly_name: string;
  hw_id?: string;
  endpoint_id?: string;
  card_index: number;
  device_index: number;
  channels_supported: number[];
  sample_rates: number[];
  bit_depth?: number | null;
  bit_depths?: number[];
  present: boolean;
}

export interface RouterMdnsService {
  name: string;
  host: string;
  port: number;
  addresses: string[];
  properties: Record<string, string>;
  priority?: number;
  weight?: number;
}

export interface RouterServiceResponse {
  timeout: number;
  services: RouterMdnsService[];
}

export interface NeighborSinksRequest {
  hostname: string;
  address?: string;
  port: number;
  scheme?: 'http' | 'https';
  api_path?: string;
  verify_tls?: boolean;
  timeout?: number;
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

export interface BufferMetrics {
  size: number;
  high_watermark: number;
  depth_ms: number;
  fill_percent: number;
  push_rate_per_second: number;
  pop_rate_per_second: number;
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
  playback_rate: number;
  system_jitter_ms: number;
  last_system_delay_ms: number;
  timeshift_buffer: BufferMetrics;
}

export interface SourceStats {
  instance_id: string;
  source_tag: string;
  input_queue_size: number;
  output_queue_size: number;
  packets_processed_per_second: number;
  reconfigurations: number;
  playback_rate: number;
  input_samplerate: number;
  output_samplerate: number;
  resample_ratio: number;
  input_buffer: BufferMetrics;
  output_buffer: BufferMetrics;
  process_buffer: BufferMetrics;
  timeshift_buffer: BufferMetrics;
  last_packet_age_ms: number;
  last_origin_age_ms: number;
  chunks_pushed: number;
  discarded_packets: number;
  avg_processing_ms: number;
  peak_process_buffer_samples: number;
}

export interface WebRtcListenerStats {
  listener_id: string;
  connection_state: string;
  pcm_buffer_size: number;
  packets_sent_per_second: number;
}

export interface SinkInputLaneStats {
  instance_id: string;
  source_output_queue: BufferMetrics;
  ready_queue: BufferMetrics;
  last_chunk_dwell_ms: number;
  avg_chunk_dwell_ms: number;
  underrun_events: number;
  ready_total_received: number;
  ready_total_popped: number;
  ready_total_dropped: number;
}

export interface SinkStats {
  sink_id: string;
  active_input_streams: number;
  total_input_streams: number;
  packets_mixed_per_second: number;
  sink_buffer_underruns: number;
  sink_buffer_overflows: number;
  mp3_buffer_overflows: number;
  payload_buffer: BufferMetrics;
  mp3_output_buffer: BufferMetrics;
  mp3_pcm_buffer: BufferMetrics;
  last_chunk_dwell_ms: number;
  avg_chunk_dwell_ms: number;
  last_send_gap_ms: number;
  avg_send_gap_ms: number;
  inputs: SinkInputLaneStats[];
  webrtc_listeners: WebRtcListenerStats[];
}

export interface GlobalStats {
  timeshift_buffer_total_size: number;
  packets_added_to_timeshift_per_second: number;
  timeshift_inbound_buffer: BufferMetrics;
}

export interface AudioEngineStats {
  global_stats: GlobalStats;
  stream_stats: Record<string, StreamStats>;
  source_stats: SourceStats[];
  sink_stats: SinkStats[];
}

export interface SystemLoadAverage {
  one: number;
  five: number;
  fifteen: number;
}

export interface SystemMemoryStats {
  total_mb: number | null;
  available_mb: number | null;
  used_mb: number | null;
  used_percent: number | null;
}

export interface ProcessRuntimeStats {
  pid: number;
  name: string;
  status: string;
  cpu_percent: number | null;
  memory_percent: number | null;
  memory_rss_mb: number | null;
  num_threads: number | null;
  uptime_seconds: number | null;
  uptime_human: string | null;
  started_at: string | null;
}

export interface SystemInfo {
  hostname: string;
  fqdn: string;
  platform: {
    system: string;
    release: string;
    version: string;
    machine: string;
    python: string;
  };
  server_time: {
    local_iso: string;
    utc_iso: string;
  };
  uptime_seconds: number | null;
  uptime_human: string | null;
  cpu_count: number | null;
  load_average: SystemLoadAverage | null;
  memory: SystemMemoryStats;
  process: ProcessRuntimeStats;
}

// --- Audio Engine Settings Interfaces ---
export interface TimeshiftTuning {
  cleanup_interval_ms: number;
  late_packet_threshold_ms: number;
  target_buffer_level_ms: number;
  loop_max_sleep_ms: number;
  max_catchup_lag_ms: number;
  max_clock_pending_packets: number;
  rtp_continuity_slack_seconds: number;
  rtp_session_reset_threshold_seconds: number;
  playback_ratio_max_deviation_ppm: number;
  playback_ratio_slew_ppm_per_sec: number;
  playback_ratio_kp: number;
  playback_ratio_ki: number;
  playback_ratio_integral_limit_ppm: number;
  playback_ratio_smoothing: number;
  playback_ratio_inbound_rate_smoothing: number;
  playback_rate_adjustment_enabled: boolean;
}

export interface MixerTuning {
  mp3_bitrate_kbps: number;
  mp3_vbr_enabled: boolean;
  mp3_output_queue_max_size: number;
  underrun_hold_timeout_ms: number;
  max_input_queue_chunks: number;
  min_input_queue_chunks: number;
  max_ready_chunks_per_source: number;
  max_queued_chunks: number;
}

export interface SourceProcessorTuning {
  command_loop_sleep_ms: number;
  discontinuity_threshold_ms: number;
}

export interface ProcessorTuning {
  oversampling_factor: number;
  volume_smoothing_factor: number;
  dc_filter_cutoff_hz: number;
  normalization_target_rms: number;
  normalization_attack_smoothing: number;
  normalization_decay_smoothing: number;
  dither_noise_shaping_factor: number;
}

export interface SystemAudioTuning {
  alsa_target_latency_ms: number;
  alsa_periods_per_buffer: number;
  alsa_dynamic_latency_enabled: boolean;
  alsa_latency_min_ms: number;
  alsa_latency_max_ms: number;
  alsa_latency_low_water_ms: number;
  alsa_latency_high_water_ms: number;
  alsa_latency_integral_gain: number;
  alsa_latency_rate_limit_ms_per_sec: number;
  alsa_latency_idle_decay_ms_per_sec: number;
  alsa_latency_apply_hysteresis_ms: number;
  alsa_latency_reconfig_cooldown_ms: number;
  alsa_latency_xrun_boost_ms: number;
  alsa_latency_low_step_ms: number;
}

export interface RtpReceiverTuning {
  format_probe_duration_ms: number;
  format_probe_min_bytes: number;
}

export interface AudioEngineSettings {
  timeshift_tuning: TimeshiftTuning;
  mixer_tuning: MixerTuning;
  source_processor_tuning: SourceProcessorTuning;
  processor_tuning: ProcessorTuning;
  rtp_receiver_tuning: RtpReceiverTuning;
  system_audio_tuning: SystemAudioTuning;
}
// --- End Audio Engine Settings Interfaces ---


/**
 * Interface for WebSocket update message
 */
export interface WebSocketUpdate {
  sources?: Record<string, Source>;
  sinks?: Record<string, Sink>;
  routes?: Record<string, Route>;
  system_capture_devices?: Record<string, SystemAudioDeviceInfo> | SystemAudioDeviceInfo[];
  system_playback_devices?: Record<string, SystemAudioDeviceInfo> | SystemAudioDeviceInfo[];
  removals?: {
    sources?: string[];
    sinks?: string[];
    routes?: string[];
    system_capture_devices?: string[];
    system_playback_devices?: string[];
  };
}

/**
 * Type for WebSocket message handler
 */
export type WebSocketMessageHandler = (update: WebSocketUpdate) => void;

export type WebSocketConnectionState = 'connected' | 'disconnected';
export type WebSocketConnectionListener = (state: WebSocketConnectionState) => void;

type CachedResponse = {
  timestamp: number;
  promise: Promise<AxiosResponse<any>>;
};

const CACHE_TTL_MS = 5000;
const responseCache = new Map<string, CachedResponse>();

const buildCacheKey = (url: string, config?: AxiosRequestConfig) => {
  const paramsKey = config?.params ? JSON.stringify(config.params) : '';
  return `${url}::${paramsKey}`;
};

const cachedGet = <T>(url: string, config?: AxiosRequestConfig): Promise<AxiosResponse<T>> => {
  const key = buildCacheKey(url, config);
  const now = Date.now();
  const cached = responseCache.get(key);

  if (cached && now - cached.timestamp < CACHE_TTL_MS) {
    return cached.promise as Promise<AxiosResponse<T>>;
  }

  const requestPromise = axios.get<T>(url, config).then(response => {
    responseCache.set(key, {
      timestamp: Date.now(),
      promise: Promise.resolve(response)
    });
    return response;
  }).catch(error => {
    responseCache.delete(key);
    throw error;
  });

  responseCache.set(key, { timestamp: now, promise: requestPromise });
  return requestPromise;
};

const invalidateCacheByUrl = (url: string) => {
  const prefix = `${url}::`;
  responseCache.forEach((_entry, key) => {
    if (key === `${url}::` || key.startsWith(prefix)) {
      responseCache.delete(key);
    }
  });
};

const invalidateCaches = (...urls: string[]) => {
  urls.forEach(url => invalidateCacheByUrl(url));
};

const withCacheInvalidation = <T>(promise: Promise<T>, urlsToInvalidate: string[]): Promise<T> => {
  return promise.then(result => {
    invalidateCaches(...urlsToInvalidate);
    return result;
  });
};

let ws: WebSocket | null = null;
let messageHandler: WebSocketMessageHandler | null = null;
let reconnectTimeout: ReturnType<typeof setTimeout> | null = null;
let manualReconnectRequested = false;
const webSocketConnectionListeners = new Set<WebSocketConnectionListener>();

const notifyWebSocketState = (state: WebSocketConnectionState) => {
  webSocketConnectionListeners.forEach(listener => {
    try {
      listener(state);
    } catch (listenerError) {
      console.error('Error in WebSocket connection listener:', listenerError);
    }
  });
};

const scheduleWebSocketReconnect = (delayMs: number) => {
  if (reconnectTimeout) {
    clearTimeout(reconnectTimeout);
  }
  console.log(`Attempting to reconnect WebSocket in ${delayMs}ms...`);
  reconnectTimeout = setTimeout(() => {
    reconnectTimeout = null;
    createWebSocket();
  }, Math.max(delayMs, 0));
};

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
      notifyWebSocketState('disconnected');
      const reconnectDelay = manualReconnectRequested ? 0 : 5000;
      manualReconnectRequested = false;

      // Attempt to reconnect after 5 seconds
      scheduleWebSocketReconnect(reconnectDelay);
    };

    ws.onerror = (error) => {
      console.error('WebSocket error:', error);
      // Let onclose handle reconnection
    };

    ws.onopen = () => {
      console.log("WebSocket connected successfully");
      manualReconnectRequested = false;
      notifyWebSocketState('connected');
    };
  } catch (error) {
    console.error('Error creating WebSocket:', error);
    notifyWebSocketState('disconnected');
    // Attempt to reconnect after 5 seconds
    const reconnectDelay = manualReconnectRequested ? 0 : 5000;
    manualReconnectRequested = false;
    scheduleWebSocketReconnect(reconnectDelay);
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
      cachedGet<Record<string, Source>>('/sources'),
      cachedGet<Record<string, Sink>>('/sinks'),
      cachedGet<Record<string, Route>>('/routes')
    ]);

    let systemCaptureDevices: SystemAudioDeviceInfo[] = [];
    let systemPlaybackDevices: SystemAudioDeviceInfo[] = [];

    try {
      const systemDevicesRes = await cachedGet<{
        system_capture_devices?: SystemAudioDeviceInfo[] | Record<string, SystemAudioDeviceInfo>;
        system_playback_devices?: SystemAudioDeviceInfo[] | Record<string, SystemAudioDeviceInfo>;
      }>('/system_audio_devices');

      const capturePayload = systemDevicesRes.data.system_capture_devices;
      const playbackPayload = systemDevicesRes.data.system_playback_devices;

      if (Array.isArray(capturePayload)) {
        systemCaptureDevices = capturePayload;
      } else if (capturePayload) {
        systemCaptureDevices = Object.values(capturePayload);
      }

      if (Array.isArray(playbackPayload)) {
        systemPlaybackDevices = playbackPayload;
      } else if (playbackPayload) {
        systemPlaybackDevices = Object.values(playbackPayload);
      }
    } catch (error) {
      console.warn('Failed to fetch system audio devices:', error);
    }

    // Send initial state through handler
    handler({
      sources: sourcesRes.data,
      sinks: sinksRes.data,
      routes: routesRes.data,
      system_capture_devices: systemCaptureDevices,
      system_playback_devices: systemPlaybackDevices
    });
  } catch (error) {
    console.error('Error fetching initial state:', error);
  }
};

const addWebSocketConnectionListener = (listener: WebSocketConnectionListener) => {
  webSocketConnectionListeners.add(listener);
  return () => {
    webSocketConnectionListeners.delete(listener);
  };
};

const forceWebSocketReconnect = () => {
  manualReconnectRequested = true;
  if (ws) {
    try {
      ws.close();
      return;
    } catch (error) {
      console.error('Error closing WebSocket before reconnect:', error);
    }
  }
  createWebSocket();
};

/**
 * ApiService object containing methods for interacting with the backend API
 */
const ApiService = {
  // WebSocket handler setter that also fetches initial state
  setWebSocketHandler: setupWebSocket,
  onWebSocketStateChange: addWebSocketConnectionListener,
  forceWebSocketReconnect,

  // GET requests
  getSources: (options?: { includeTemporary?: boolean }) => cachedGet<Record<string, Source>>(
    '/sources',
    options?.includeTemporary ? { params: { include_temporary: true } } : undefined,
  ),
  getSinks: (options?: { includeTemporary?: boolean }) => cachedGet<Record<string, Sink>>(
    '/sinks',
    options?.includeTemporary ? { params: { include_temporary: true } } : undefined,
  ),
  getRoutes: () => cachedGet<Record<string, Route>>('/routes'),
  getSystemAudioDevices: () => cachedGet<{
    system_capture_devices?: SystemAudioDeviceInfo[] | Record<string, SystemAudioDeviceInfo>;
    system_playback_devices?: SystemAudioDeviceInfo[] | Record<string, SystemAudioDeviceInfo>;
  }>('/system_audio_devices'),

  // POST requests for adding new items
  addSource: (data: Source) => withCacheInvalidation(
    axios.post<Source>('/sources', data),
    ['/sources']
  ),
  addSink: (data: Sink) => withCacheInvalidation(
    axios.post<Sink>('/sinks', data),
    ['/sinks']
  ),
  addRoute: (data: Route) => withCacheInvalidation(
    axios.post<Route>('/routes', data),
    ['/routes']
  ),

  // PUT requests for updating existing items
  updateSource: (name: string, data: Partial<Source>) => withCacheInvalidation(
    axios.put<Source>(`/sources/${name}`, data),
    ['/sources']
  ),
  updateSink: (name: string, data: Partial<Sink>) => withCacheInvalidation(
    axios.put<Sink>(`/sinks/${name}`, data),
    ['/sinks']
  ),
  updateRoute: (name: string, data: Partial<Route>) => withCacheInvalidation(
    axios.put<Route>(`/routes/${name}`, data),
    ['/routes']
  ),

  // DELETE requests
  deleteSource: (name: string) => withCacheInvalidation(
    axios.delete(`/sources/${name}`),
    ['/sources']
  ),
  deleteSink: (name: string) => withCacheInvalidation(
    axios.delete(`/sinks/${name}`),
    ['/sinks']
  ),
  deleteRoute: (name: string) => withCacheInvalidation(
    axios.delete(`/routes/${name}`),
    ['/routes']
  ),

  // Enable/Disable requests
  enableSource: (name: string) => withCacheInvalidation(
    axios.get(`/sources/${name}/enable`),
    ['/sources']
  ),
  disableSource: (name: string) => withCacheInvalidation(
    axios.get(`/sources/${name}/disable`),
    ['/sources']
  ),
  enableSink: (name: string) => withCacheInvalidation(
    axios.get(`/sinks/${name}/enable`),
    ['/sinks']
  ),
  disableSink: (name: string) => withCacheInvalidation(
    axios.get(`/sinks/${name}/disable`),
    ['/sinks']
  ),
  enableRoute: (name: string) => withCacheInvalidation(
    axios.get(`/routes/${name}/enable`),
    ['/routes']
  ),
  disableRoute: (name: string) => withCacheInvalidation(
    axios.get(`/routes/${name}/disable`),
    ['/routes']
  ),

  // Volume update requests
  updateSourceVolume: (name: string, volume: number) => withCacheInvalidation(
    axios.get(`/sources/${name}/volume/${volume}`),
    ['/sources']
  ),
  updateSinkVolume: (name: string, volume: number) => withCacheInvalidation(
    axios.get(`/sinks/${name}/volume/${volume}`),
    ['/sinks']
  ),
  updateRouteVolume: (name: string, volume: number) => withCacheInvalidation(
    axios.get(`/routes/${name}/volume/${volume}`),
    ['/routes']
  ),

  // Delay update requests
  updateSourceDelay: (name: string, delay: number) => withCacheInvalidation(
    axios.get(`/sources/${name}/delay/${delay}`),
    ['/sources']
  ),
  updateSinkDelay: (name: string, delay: number) => withCacheInvalidation(
    axios.get(`/sinks/${name}/delay/${delay}`),
    ['/sinks']
  ),
  updateRouteDelay: (name: string, delay: number) => withCacheInvalidation(
    axios.get(`/routes/${name}/delay/${delay}`),
    ['/routes']
  ),

  // Timeshift update requests
  updateSourceTimeshift: (name: string, timeshift: number) => withCacheInvalidation(
    axios.get(`/sources/${name}/timeshift/${timeshift}`),
    ['/sources']
  ),
  updateSinkTimeshift: (name: string, timeshift: number) => withCacheInvalidation(
    axios.get(`/sinks/${name}/timeshift/${timeshift}`),
    ['/sinks']
  ),
  updateRouteTimeshift: (name: string, timeshift: number) => withCacheInvalidation(
    axios.get(`/routes/${name}/timeshift/${timeshift}`),
    ['/routes']
  ),

  // Equalizer update requests
  updateSourceEqualizer: (name: string, equalizer: Equalizer) => withCacheInvalidation(
    axios.post(`/sources/${name}/equalizer`, equalizer),
    ['/sources']
  ),
  updateSinkEqualizer: (name: string, equalizer: Equalizer) => withCacheInvalidation(
    axios.post(`/sinks/${name}/equalizer`, equalizer),
    ['/sinks']
  ),
  updateRouteEqualizer: (name: string, equalizer: Equalizer) => withCacheInvalidation(
    axios.post(`/routes/${name}/equalizer`, equalizer),
    ['/routes']
  ),


  // Reorder requests
  reorderSource: (name: string, newIndex: number) => withCacheInvalidation(
    axios.get(`/sources/${name}/reorder/${newIndex}`),
    ['/sources']
  ),
  reorderSink: (name: string, newIndex: number) => withCacheInvalidation(
    axios.get(`/sinks/${name}/reorder/${newIndex}`),
    ['/sinks']
  ),
  reorderRoute: (name: string, newIndex: number) => withCacheInvalidation(
    axios.get(`/routes/${name}/reorder/${newIndex}`),
    ['/routes']
  ),

  // Control source playback
  controlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'pause' | 'nexttrack') =>
    withCacheInvalidation(
      axios.get(`/sources/${sourceName}/${action}`),
      ['/sources']
    ),

  // Utility methods
  getSinkStreamUrl: (sinkIp: string) => `/stream/${sinkIp}/?random=${Math.random()}`,
  getVncUrl: (sourceName: string) => `/site/vnc/${sourceName}`,

  // Custom equalizer requests
  saveEqualizer: (name: string, equalizer: Equalizer) => {
    const new_eq = { ...equalizer };
    new_eq.name = name;
    return withCacheInvalidation(
      axios.post('/equalizers/', new_eq),
      ['/equalizers/']
    );
  },
  listEqualizers: () => cachedGet<{ equalizers: Equalizer[] }>('/equalizers/'),
  deleteEqualizer: (name: string) => withCacheInvalidation(
    axios.delete(`/equalizers/${name}`),
    ['/equalizers/']
  ),

  // New method to update equalizer based on type and name
  updateEqualizer: (type: 'sources' | 'sinks' | 'routes', name: string, equalizer: Equalizer) => {
    const targetUrl = `/equalizers/${type}/${name}`;
    const listUrl = `/equalizers/${type}/`;
    return withCacheInvalidation(
      axios.post(targetUrl, equalizer),
      ['/equalizers/', listUrl]
    );
  },

  // --- Preferences ---
  getPreferences: () => cachedGet<Preferences>('/preferences'),
  updatePreferences: (payload: PreferencesUpdatePayload) => withCacheInvalidation(
    axios.put<Preferences>('/preferences', payload),
    ['/preferences']
  ),

  // --- Discovery ---
  getDiscoverySnapshot: () => cachedGet<UnifiedDiscoverySnapshot>('/discovery/snapshot'),
  getUnmatchedDiscoveredDevices: () => cachedGet<DiscoveryInventoryResponse>('/discovery/unmatched'),
  addDiscoveredSource: (deviceKey: string) => withCacheInvalidation(
    axios.post('/sources/add-discovered', { device_key: deviceKey }),
    ['/sources', '/discovery/snapshot']
  ),
  addDiscoveredSink: (deviceKey: string) => withCacheInvalidation(
    axios.post('/sinks/add-discovered', { device_key: deviceKey }),
    ['/sinks', '/discovery/snapshot']
  ),
  getRouterServices: (timeout = 1.5) => cachedGet<RouterServiceResponse>('/mdns/router-services', {
    params: { timeout },
  }),
  getNeighborSinks: (payload: NeighborSinksRequest) =>
    axios.post<Record<string, Sink> | Sink[]>('/neighbors/sinks', payload),

  // --- Speaker Layout Update Methods ---
  updateSourceSpeakerLayout: (name: string, inputChannelKey: number, layout: SpeakerLayout) => {
    return withCacheInvalidation(
      axios.post(`/api/sources/${encodeURIComponent(name)}/speaker_layout/${inputChannelKey}`, layout),
      ['/sources']
    );
  },

  updateSinkSpeakerLayout: (name: string, inputChannelKey: number, layout: SpeakerLayout) => {
    return withCacheInvalidation(
      axios.post(`/api/sinks/${encodeURIComponent(name)}/speaker_layout/${inputChannelKey}`, layout),
      ['/sinks']
    );
  },

  updateRouteSpeakerLayout: (name: string, inputChannelKey: number, layout: SpeakerLayout) => {
    return withCacheInvalidation(
      axios.post(`/api/routes/${encodeURIComponent(name)}/speaker_layout/${inputChannelKey}`, layout),
      ['/routes']
    );
  },
  // --- End Speaker Layout Update Methods ---

  // --- System ---
  getSystemInfo: () => cachedGet<SystemInfo>('/api/system/info'),

  // --- Stats ---
  getStats: () => cachedGet<AudioEngineStats>('/api/stats'),

  // --- Settings ---
  getSettings: () => cachedGet<AudioEngineSettings>('/api/settings'),
  updateSettings: (settings: AudioEngineSettings) => withCacheInvalidation(
    axios.post('/api/settings', settings),
    ['/api/settings']
  ),
};

export default ApiService;
