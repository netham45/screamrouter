/**
 * @file EnhancedWebRTCManager.ts
 * @description Enhanced WebRTC manager that integrates all refactored components
 * Provides a clean API for WebRTC connection management with proper separation of concerns
 */

import { WHEPClient } from './WHEPClient';
import { ICECandidateManager } from './ICECandidateManager';
import { HeartbeatManager } from './HeartbeatManager';
import { ConnectionManager, ConnectionState, ConnectionConfig } from './ConnectionManager';

export interface WebRTCStats {
  connectionState: ConnectionState;
  iceConnectionState: RTCIceConnectionState | null;
  signalingState: RTCSignalingState | null;
  packetsReceived: number;
  packetsLost: number;
  bytesReceived: number;
  jitter: number;
  roundTripTime: number;
  audioLevel: number;
  timestamp: Date;
}

export interface WebRTCError {
  type: 'network' | 'protocol' | 'server' | 'client';
  message: string;
  details?: any;
  recoverable: boolean;
  suggestedAction?: string;
}

export interface WebRTCManagerCallbacks {
  onConnectionStateChange?: (sinkId: string, state: ConnectionState) => void;
  onStream?: (sinkId: string, stream: MediaStream | null) => void;
  onError?: (sinkId: string, error: WebRTCError) => void;
  onStats?: (sinkId: string, stats: WebRTCStats) => void;
}

export interface WebRTCManagerConfig extends ConnectionConfig {
  baseUrl?: string;
  statsInterval?: number;
  enableStats?: boolean;
}

export class EnhancedWebRTCManager {
  private whepClient: WHEPClient;
  private candidateManager: ICECandidateManager;
  private heartbeatManager: HeartbeatManager;
  private connectionManager: ConnectionManager;
  private callbacks: WebRTCManagerCallbacks;
  private statsIntervals: Map<string, NodeJS.Timeout> = new Map();
  private config: WebRTCManagerConfig;

  constructor(callbacks?: WebRTCManagerCallbacks, config?: WebRTCManagerConfig) {
    this.callbacks = callbacks || {};
    this.config = {
      baseUrl: '/api/whep',
      statsInterval: 5000,
      enableStats: true,
      ...config
    };

    // Initialize components
    this.whepClient = new WHEPClient(this.config.baseUrl);
    
    this.candidateManager = new ICECandidateManager(this.whepClient, {
      pollingInterval: 1000,
      maxPollingDuration: 30000,
    });
    
    this.heartbeatManager = new HeartbeatManager(
      this.whepClient,
      {
        heartbeatInterval: 5000,
        missedHeartbeatThreshold: 3,
      },
      {
        onHeartbeatFailed: (session) => {
          console.error(`[EnhancedWebRTCManager] Heartbeat failed for ${session.sinkId}`);
          this.handleHeartbeatFailure(session.sinkId);
        },
        onHeartbeatRecovered: (session) => {
          console.log(`[EnhancedWebRTCManager] Heartbeat recovered for ${session.sinkId}`);
        },
      }
    );
    
    this.connectionManager = new ConnectionManager(
      this.whepClient,
      this.candidateManager,
      this.heartbeatManager,
      {
        iceServers: this.config.iceServers,
        enableAutoReconnect: this.config.enableAutoReconnect,
        reconnectDelay: this.config.reconnectDelay,
        maxReconnectAttempts: this.config.maxReconnectAttempts,
        connectionTimeout: this.config.connectionTimeout,
      },
      {
        onStateChange: (sinkId, state) => {
          console.log(`[EnhancedWebRTCManager] Connection state changed: ${sinkId} -> ${state}`);
          
          if (state === 'connected' && this.config.enableStats) {
            this.startStatsCollection(sinkId);
          } else if (state === 'disconnected' || state === 'failed') {
            this.stopStatsCollection(sinkId);
          }
          
          if (this.callbacks.onConnectionStateChange) {
            this.callbacks.onConnectionStateChange(sinkId, state);
          }
        },
        onStream: (sinkId, stream) => {
          console.log(`[EnhancedWebRTCManager] Stream ${stream ? 'received' : 'removed'} for ${sinkId}`);
          if (this.callbacks.onStream) {
            this.callbacks.onStream(sinkId, stream);
          }
        },
        onError: (sinkId, error) => {
          console.error(`[EnhancedWebRTCManager] Connection error for ${sinkId}:`, error);
          const webrtcError = this.mapToWebRTCError(error);
          if (this.callbacks.onError) {
            this.callbacks.onError(sinkId, webrtcError);
          }
        },
      }
    );
  }

  /**
   * Starts listening to a sink
   */
  async startListening(sinkId: string): Promise<void> {
    console.log(`[EnhancedWebRTCManager] Starting to listen to sink ${sinkId}`);
    
    try {
      await this.connectionManager.connect(sinkId);
      console.log(`[EnhancedWebRTCManager] Successfully connected to sink ${sinkId}`);
    } catch (error) {
      console.error(`[EnhancedWebRTCManager] Failed to connect to sink ${sinkId}:`, error);
      throw error;
    }
  }

  /**
   * Stops listening to a sink
   */
  async stopListening(sinkId: string): Promise<void> {
    console.log(`[EnhancedWebRTCManager] Stopping listening to sink ${sinkId}`);
    
    this.stopStatsCollection(sinkId);
    await this.connectionManager.disconnect(sinkId);
    
    console.log(`[EnhancedWebRTCManager] Stopped listening to sink ${sinkId}`);
  }

  /**
   * Stops all active connections
   */
  async stopAllListening(): Promise<void> {
    console.log(`[EnhancedWebRTCManager] Stopping all connections`);
    
    // Stop all stats collection
    this.statsIntervals.forEach((interval, sinkId) => {
      this.stopStatsCollection(sinkId);
    });
    
    await this.connectionManager.disconnectAll();
    
    console.log(`[EnhancedWebRTCManager] All connections stopped`);
  }

  /**
   * Toggles listening for a sink
   */
  async toggleListening(sinkId: string): Promise<void> {
    if (this.isListening(sinkId)) {
      await this.stopListening(sinkId);
    } else {
      await this.startListening(sinkId);
    }
  }

  /**
   * Checks if currently listening to a sink
   */
  isListening(sinkId: string): boolean {
    return this.connectionManager.isConnected(sinkId);
  }

  /**
   * Gets the connection state for a sink
   */
  getConnectionState(sinkId: string): ConnectionState {
    return this.connectionManager.getConnectionState(sinkId);
  }

  /**
   * Gets the stream for a sink
   */
  getStream(sinkId: string): MediaStream | null {
    return this.connectionManager.getStream(sinkId);
  }

  /**
   * Gets statistics for a sink
   */
  async getStats(sinkId: string): Promise<WebRTCStats | null> {
    const statsReport = await this.connectionManager.getStats(sinkId);
    if (!statsReport) {
      return null;
    }
    
    return this.parseStatsReport(sinkId, statsReport);
  }

  /**
   * Starts collecting statistics for a sink
   */
  private startStatsCollection(sinkId: string): void {
    if (this.statsIntervals.has(sinkId)) {
      return;
    }
    
    console.log(`[EnhancedWebRTCManager] Starting stats collection for ${sinkId}`);
    
    const interval = setInterval(async () => {
      const stats = await this.getStats(sinkId);
      if (stats && this.callbacks.onStats) {
        this.callbacks.onStats(sinkId, stats);
      }
    }, this.config.statsInterval);
    
    this.statsIntervals.set(sinkId, interval);
  }

  /**
   * Stops collecting statistics for a sink
   */
  private stopStatsCollection(sinkId: string): void {
    const interval = this.statsIntervals.get(sinkId);
    if (interval) {
      clearInterval(interval);
      this.statsIntervals.delete(sinkId);
      console.log(`[EnhancedWebRTCManager] Stopped stats collection for ${sinkId}`);
    }
  }

  /**
   * Parses RTCStatsReport into WebRTCStats
   */
  private parseStatsReport(sinkId: string, report: RTCStatsReport): WebRTCStats {
    const stats: WebRTCStats = {
      connectionState: this.connectionManager.getConnectionState(sinkId),
      iceConnectionState: null,
      signalingState: null,
      packetsReceived: 0,
      packetsLost: 0,
      bytesReceived: 0,
      jitter: 0,
      roundTripTime: 0,
      audioLevel: 0,
      timestamp: new Date(),
    };
    
    report.forEach((stat) => {
      if (stat.type === 'inbound-rtp' && stat.kind === 'audio') {
        stats.packetsReceived = stat.packetsReceived || 0;
        stats.packetsLost = stat.packetsLost || 0;
        stats.bytesReceived = stat.bytesReceived || 0;
        stats.jitter = stat.jitter || 0;
      } else if (stat.type === 'candidate-pair' && stat.state === 'succeeded') {
        stats.roundTripTime = stat.currentRoundTripTime || 0;
      } else if (stat.type === 'track' && stat.kind === 'audio') {
        stats.audioLevel = stat.audioLevel || 0;
      }
    });
    
    return stats;
  }

  /**
   * Maps errors to WebRTCError format
   */
  private mapToWebRTCError(error: Error): WebRTCError {
    const message = error.message.toLowerCase();
    
    if (message.includes('network') || message.includes('ice')) {
      return {
        type: 'network',
        message: error.message,
        recoverable: true,
        suggestedAction: 'Check network connection',
      };
    } else if (message.includes('whep') || message.includes('sdp')) {
      return {
        type: 'protocol',
        message: error.message,
        recoverable: false,
        suggestedAction: 'Refresh the page',
      };
    } else if (message.includes('404') || message.includes('sink')) {
      return {
        type: 'server',
        message: error.message,
        recoverable: false,
        suggestedAction: 'Check if sink exists',
      };
    } else {
      return {
        type: 'client',
        message: error.message,
        recoverable: false,
        suggestedAction: 'Check browser compatibility',
      };
    }
  }

  /**
   * Handles heartbeat failure
   */
  private async handleHeartbeatFailure(sinkId: string): Promise<void> {
    console.error(`[EnhancedWebRTCManager] Handling heartbeat failure for ${sinkId}`);
    
    // Trigger reconnection
    try {
      await this.connectionManager.disconnect(sinkId);
      await this.connectionManager.connect(sinkId);
    } catch (error) {
      console.error(`[EnhancedWebRTCManager] Failed to recover from heartbeat failure:`, error);
    }
  }

  /**
   * Updates configuration
   */
  updateConfig(config: Partial<WebRTCManagerConfig>): void {
    this.config = { ...this.config, ...config };
    console.log('[EnhancedWebRTCManager] Configuration updated:', this.config);
  }

  /**
   * Gets the number of active connections
   */
  getActiveConnectionCount(): number {
    return this.connectionManager.getActiveConnectionCount();
  }

  /**
   * Cleans up all resources
   */
  async cleanup(): Promise<void> {
    console.log('[EnhancedWebRTCManager] Cleaning up WebRTC manager');
    
    // Stop all stats collection
    this.statsIntervals.forEach((interval) => {
      clearInterval(interval);
    });
    this.statsIntervals.clear();
    
    // Clean up all components
    await this.connectionManager.cleanup();
    
    console.log('[EnhancedWebRTCManager] Cleanup complete');
  }
}