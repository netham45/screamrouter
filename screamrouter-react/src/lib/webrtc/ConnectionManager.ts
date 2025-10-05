/**
 * @file ConnectionManager.ts
 * @description Manages WebRTC peer connections with automatic reconnection
 * Handles connection lifecycle, state monitoring, and recovery
 */

import { WHEPClient, WHEPSession } from './WHEPClient';
import { ICECandidateManager } from './ICECandidateManager';
import { HeartbeatManager } from './HeartbeatManager';

export type ConnectionState = 'disconnected' | 'connecting' | 'connected' | 'failed' | 'reconnecting';

export interface ConnectionConfig {
  iceServers?: RTCIceServer[];
  enableAutoReconnect?: boolean;
  reconnectDelay?: number;
  maxReconnectAttempts?: number;
  connectionTimeout?: number;
}

export interface ConnectionCallbacks {
  onStateChange?: (sinkId: string, state: ConnectionState) => void;
  onStream?: (sinkId: string, stream: MediaStream | null) => void;
  onError?: (sinkId: string, error: Error) => void;
  onStats?: (sinkId: string, stats: RTCStatsReport) => void;
}

interface ActiveConnection {
  sinkId: string;
  pc: RTCPeerConnection;
  session: WHEPSession | null;
  state: ConnectionState;
  stream: MediaStream | null;
  reconnectAttempts: number;
  reconnectTimeout?: NodeJS.Timeout;
  connectionTimeout?: NodeJS.Timeout;
}

export class ConnectionManager {
  private whepClient: WHEPClient;
  private candidateManager: ICECandidateManager;
  private heartbeatManager: HeartbeatManager;
  private connections: Map<string, ActiveConnection> = new Map();
  private callbacks: ConnectionCallbacks;
  
  // Configuration
  private readonly config: Required<ConnectionConfig> = {
    iceServers: [
      { urls: 'stun:stun.l.google.com:19302' },
      {
        urls: 'turn:192.168.3.201',
        username: 'screamrouter',
        credential: 'screamrouter',
      },
    ],
    enableAutoReconnect: true,
    reconnectDelay: 3000,
    maxReconnectAttempts: 5,
    connectionTimeout: 30000,
  };

  constructor(
    whepClient: WHEPClient,
    candidateManager: ICECandidateManager,
    heartbeatManager: HeartbeatManager,
    config?: ConnectionConfig,
    callbacks?: ConnectionCallbacks
  ) {
    this.whepClient = whepClient;
    this.candidateManager = candidateManager;
    this.heartbeatManager = heartbeatManager;
    this.callbacks = callbacks || {};
    
    if (config) {
      this.config = { ...this.config, ...config };
    }
  }

  /**
   * Connects to a sink
   */
  async connect(sinkId: string): Promise<MediaStream> {
    console.log(`[ConnectionManager] Connecting to sink ${sinkId}`);
    
    // Disconnect existing connection if any
    if (this.connections.has(sinkId)) {
      console.log(`[ConnectionManager] Disconnecting existing connection to ${sinkId}`);
      await this.disconnect(sinkId);
    }
    
    const connection: ActiveConnection = {
      sinkId,
      pc: new RTCPeerConnection({ iceServers: this.config.iceServers }),
      session: null,
      state: 'connecting',
      stream: null,
      reconnectAttempts: 0,
    };
    
    this.connections.set(sinkId, connection);
    this.updateConnectionState(sinkId, 'connecting');
    
    try {
      // Set up peer connection handlers
      this.setupPeerConnectionHandlers(connection);
      
      // Create offer
      connection.pc.addTransceiver('audio', { direction: 'recvonly' });
      const offer = await connection.pc.createOffer();
      
      // Enhance offer for stereo audio
      if (offer.sdp) {
        offer.sdp = offer.sdp.replace("useinbandfec=1", "useinbandfec=1;stereo=1;sprop-stereo=1");
        offer.sdp = offer.sdp.replace("useinbandfec=0", "useinbandfec=0;stereo=1;sprop-stereo=1");
      }
      
      await connection.pc.setLocalDescription(offer);
      
      // Create WHEP session
      const { session, answerSdp } = await this.whepClient.createSession(sinkId, offer.sdp!);
      connection.session = session;
      
      // Process queued candidates
      await this.candidateManager.processQueuedCandidates(session);
      
      // Set remote description
      await connection.pc.setRemoteDescription(new RTCSessionDescription({ type: 'answer', sdp: answerSdp }));
      
      // Start ICE candidate polling
      this.candidateManager.startPolling(session, connection.pc);
      
      // Start heartbeat
      this.heartbeatManager.startHeartbeat(session);
      
      // Set connection timeout
      this.setConnectionTimeout(connection);
      
      // Wait for stream
      return new Promise((resolve, reject) => {
        const checkInterval = setInterval(() => {
          const conn = this.connections.get(sinkId);
          if (!conn) {
            clearInterval(checkInterval);
            reject(new Error('Connection was closed'));
          } else if (conn.stream) {
            clearInterval(checkInterval);
            resolve(conn.stream);
          } else if (conn.state === 'failed') {
            clearInterval(checkInterval);
            reject(new Error('Connection failed'));
          }
        }, 100);
        
        // Timeout after 30 seconds
        setTimeout(() => {
          clearInterval(checkInterval);
          reject(new Error('Connection timeout'));
        }, 30000);
      });
    } catch (error) {
      console.error(`[ConnectionManager] Failed to connect to ${sinkId}:`, error);
      await this.handleConnectionFailure(connection, error as Error);
      throw error;
    }
  }

  /**
   * Disconnects from a sink
   */
  async disconnect(sinkId: string): Promise<void> {
    console.log(`[ConnectionManager] Disconnecting from sink ${sinkId}`);
    
    const connection = this.connections.get(sinkId);
    if (!connection) {
      console.log(`[ConnectionManager] No connection found for ${sinkId}`);
      return;
    }
    
    // Clear timeouts
    if (connection.reconnectTimeout) {
      clearTimeout(connection.reconnectTimeout);
    }
    if (connection.connectionTimeout) {
      clearTimeout(connection.connectionTimeout);
    }
    
    // Stop heartbeat
    if (connection.session) {
      this.heartbeatManager.stopHeartbeat(connection.session);
    }
    
    // Stop ICE candidate polling
    this.candidateManager.stopPolling();
    
    // Close peer connection
    if (connection.pc) {
      connection.pc.close();
    }
    
    // Delete WHEP session
    if (connection.session) {
      await this.whepClient.deleteSession(connection.session);
    }
    
    // Remove from connections map
    this.connections.delete(sinkId);
    
    // Notify callbacks
    this.updateConnectionState(sinkId, 'disconnected');
    if (this.callbacks.onStream) {
      this.callbacks.onStream(sinkId, null);
    }
  }

  /**
   * Disconnects all active connections
   */
  async disconnectAll(): Promise<void> {
    console.log(`[ConnectionManager] Disconnecting all connections (${this.connections.size} active)`);
    
    const disconnectPromises = Array.from(this.connections.keys()).map(sinkId => 
      this.disconnect(sinkId).catch(error => 
        console.error(`[ConnectionManager] Error disconnecting ${sinkId}:`, error)
      )
    );
    
    await Promise.all(disconnectPromises);
  }

  /**
   * Gets the connection state for a sink
   */
  getConnectionState(sinkId: string): ConnectionState {
    const connection = this.connections.get(sinkId);
    return connection ? connection.state : 'disconnected';
  }

  /**
   * Checks if connected to a sink
   */
  isConnected(sinkId: string): boolean {
    const connection = this.connections.get(sinkId);
    return connection ? connection.state === 'connected' : false;
  }

  /**
   * Gets the stream for a sink
   */
  getStream(sinkId: string): MediaStream | null {
    const connection = this.connections.get(sinkId);
    return connection ? connection.stream : null;
  }

  /**
   * Gets connection statistics
   */
  async getStats(sinkId: string): Promise<RTCStatsReport | null> {
    const connection = this.connections.get(sinkId);
    if (!connection || !connection.pc) {
      return null;
    }
    
    try {
      const stats = await connection.pc.getStats();
      if (this.callbacks.onStats) {
        this.callbacks.onStats(sinkId, stats);
      }
      return stats;
    } catch (error) {
      console.error(`[ConnectionManager] Failed to get stats for ${sinkId}:`, error);
      return null;
    }
  }

  /**
   * Sets up peer connection event handlers
   */
  private setupPeerConnectionHandlers(connection: ActiveConnection): void {
    const pc = connection.pc;
    const sinkId = connection.sinkId;
    
    // Handle ICE candidates
    pc.onicecandidate = (event) => {
      if (event.candidate && connection.session) {
        this.whepClient.sendCandidate(connection.session, event.candidate).catch(error =>
          console.error(`[ConnectionManager] Failed to send ICE candidate:`, error)
        );
      }
    };
    
    // Handle tracks
    pc.ontrack = (event) => {
      console.log(`[ConnectionManager] Received track for ${sinkId}`);
      connection.stream = event.streams[0];
      if (this.callbacks.onStream) {
        this.callbacks.onStream(sinkId, event.streams[0]);
      }
    };
    
    // Handle ICE connection state changes
    pc.oniceconnectionstatechange = () => {
      console.log(`[ConnectionManager] ICE state changed to ${pc.iceConnectionState} for ${sinkId}`);
      
      switch (pc.iceConnectionState) {
        case 'connected':
        case 'completed':
          this.handleConnectionSuccess(connection);
          break;
        case 'failed':
        case 'disconnected':
          this.handleConnectionFailure(connection, new Error(`ICE connection ${pc.iceConnectionState}`));
          break;
      }
    };
    
    // Handle connection state changes
    pc.onconnectionstatechange = () => {
      console.log(`[ConnectionManager] Connection state changed to ${pc.connectionState} for ${sinkId}`);
      
      if (pc.connectionState === 'failed') {
        this.handleConnectionFailure(connection, new Error('Peer connection failed'));
      }
    };
  }

  /**
   * Handles successful connection
   */
  private handleConnectionSuccess(connection: ActiveConnection): void {
    console.log(`[ConnectionManager] Connection successful for ${connection.sinkId}`);
    
    // Clear connection timeout
    if (connection.connectionTimeout) {
      clearTimeout(connection.connectionTimeout);
      connection.connectionTimeout = undefined;
    }
    
    // Reset reconnect attempts
    connection.reconnectAttempts = 0;
    
    // Update state
    this.updateConnectionState(connection.sinkId, 'connected');
  }

  /**
   * Handles connection failure
   */
  private async handleConnectionFailure(connection: ActiveConnection, error: Error): Promise<void> {
    console.error(`[ConnectionManager] Connection failed for ${connection.sinkId}:`, error);
    
    // Clear connection timeout
    if (connection.connectionTimeout) {
      clearTimeout(connection.connectionTimeout);
      connection.connectionTimeout = undefined;
    }
    
    // Notify error callback
    if (this.callbacks.onError) {
      this.callbacks.onError(connection.sinkId, error);
    }
    
    // Check if we should reconnect
    if (this.config.enableAutoReconnect && 
        connection.reconnectAttempts < this.config.maxReconnectAttempts) {
      await this.scheduleReconnect(connection);
    } else {
      this.updateConnectionState(connection.sinkId, 'failed');
      await this.disconnect(connection.sinkId);
    }
  }

  /**
   * Schedules a reconnection attempt
   */
  private async scheduleReconnect(connection: ActiveConnection): Promise<void> {
    connection.reconnectAttempts++;
    const delay = this.config.reconnectDelay * Math.pow(2, connection.reconnectAttempts - 1); // Exponential backoff
    
    console.log(`[ConnectionManager] Scheduling reconnect for ${connection.sinkId} in ${delay}ms (attempt ${connection.reconnectAttempts}/${this.config.maxReconnectAttempts})`);
    
    this.updateConnectionState(connection.sinkId, 'reconnecting');
    
    connection.reconnectTimeout = setTimeout(async () => {
      try {
        await this.connect(connection.sinkId);
      } catch (error) {
        console.error(`[ConnectionManager] Reconnect failed for ${connection.sinkId}:`, error);
      }
    }, delay);
  }

  /**
   * Sets a connection timeout
   */
  private setConnectionTimeout(connection: ActiveConnection): void {
    connection.connectionTimeout = setTimeout(() => {
      if (connection.state === 'connecting') {
        console.error(`[ConnectionManager] Connection timeout for ${connection.sinkId}`);
        this.handleConnectionFailure(connection, new Error('Connection timeout'));
      }
    }, this.config.connectionTimeout);
  }

  /**
   * Updates connection state and notifies callbacks
   */
  private updateConnectionState(sinkId: string, state: ConnectionState): void {
    const connection = this.connections.get(sinkId);
    if (connection) {
      connection.state = state;
    }
    
    if (this.callbacks.onStateChange) {
      this.callbacks.onStateChange(sinkId, state);
    }
  }

  /**
   * Gets the number of active connections
   */
  getActiveConnectionCount(): number {
    return this.connections.size;
  }

  /**
   * Cleans up all resources
   */
  async cleanup(): Promise<void> {
    console.log('[ConnectionManager] Cleaning up connection manager');
    await this.disconnectAll();
    this.candidateManager.cleanup();
    this.heartbeatManager.cleanup();
  }
}