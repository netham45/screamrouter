/**
 * @file HeartbeatManager.ts
 * @description Manages WebRTC session heartbeats to keep connections alive
 * Monitors heartbeat failures and triggers recovery actions
 */

import { WHEPClient, WHEPSession } from './WHEPClient';

export interface HeartbeatManagerConfig {
  heartbeatInterval?: number;
  missedHeartbeatThreshold?: number;
  enabled?: boolean;
}

export interface HeartbeatCallbacks {
  onHeartbeatFailed?: (session: WHEPSession) => void;
  onHeartbeatRecovered?: (session: WHEPSession) => void;
}

export class HeartbeatManager {
  private whepClient: WHEPClient;
  private heartbeatIntervals: Map<string, NodeJS.Timeout> = new Map();
  private missedHeartbeats: Map<string, number> = new Map();
  private callbacks: HeartbeatCallbacks;
  
  // Configuration
  private readonly config: Required<HeartbeatManagerConfig> = {
    heartbeatInterval: 5000,
    missedHeartbeatThreshold: 3,
    enabled: true,
  };

  constructor(whepClient: WHEPClient, config?: HeartbeatManagerConfig, callbacks?: HeartbeatCallbacks) {
    this.whepClient = whepClient;
    this.callbacks = callbacks || {};
    
    if (config) {
      this.config = { ...this.config, ...config };
    }
  }

  /**
   * Starts heartbeat for a session
   */
  startHeartbeat(session: WHEPSession): void {
    if (!this.config.enabled) {
      console.log('[HeartbeatManager] Heartbeats disabled, skipping');
      return;
    }

    const sessionKey = this.getSessionKey(session);
    
    // Clear any existing heartbeat for this session
    this.stopHeartbeat(session);
    
    console.log(`[HeartbeatManager] Starting heartbeat for session ${sessionKey} (interval: ${this.config.heartbeatInterval}ms)`);
    
    // Reset missed heartbeat counter
    this.missedHeartbeats.set(sessionKey, 0);
    
    const interval = setInterval(async () => {
      try {
        const success = await this.whepClient.sendHeartbeat(session);
        
        if (success) {
          // Reset missed counter on successful heartbeat
          const previousMissed = this.missedHeartbeats.get(sessionKey) || 0;
          if (previousMissed > 0) {
            console.log(`[HeartbeatManager] Heartbeat recovered for session ${sessionKey}`);
            this.missedHeartbeats.set(sessionKey, 0);
            
            if (this.callbacks.onHeartbeatRecovered) {
              this.callbacks.onHeartbeatRecovered(session);
            }
          }
        } else {
          this.handleMissedHeartbeat(session);
        }
      } catch (error) {
        console.error(`[HeartbeatManager] Heartbeat error for session ${sessionKey}:`, error);
        this.handleMissedHeartbeat(session);
      }
    }, this.config.heartbeatInterval);
    
    this.heartbeatIntervals.set(sessionKey, interval);
  }

  /**
   * Stops heartbeat for a session
   */
  stopHeartbeat(session: WHEPSession): void {
    const sessionKey = this.getSessionKey(session);
    const interval = this.heartbeatIntervals.get(sessionKey);
    
    if (interval) {
      clearInterval(interval);
      this.heartbeatIntervals.delete(sessionKey);
      this.missedHeartbeats.delete(sessionKey);
      console.log(`[HeartbeatManager] Stopped heartbeat for session ${sessionKey}`);
    }
  }

  /**
   * Stops all active heartbeats
   */
  stopAll(): void {
    console.log(`[HeartbeatManager] Stopping all heartbeats (${this.heartbeatIntervals.size} active)`);
    
    this.heartbeatIntervals.forEach((interval, sessionKey) => {
      clearInterval(interval);
      console.log(`[HeartbeatManager] Stopped heartbeat for session ${sessionKey}`);
    });
    
    this.heartbeatIntervals.clear();
    this.missedHeartbeats.clear();
  }

  /**
   * Handles a missed heartbeat
   */
  private handleMissedHeartbeat(session: WHEPSession): void {
    const sessionKey = this.getSessionKey(session);
    const currentMissed = (this.missedHeartbeats.get(sessionKey) || 0) + 1;
    this.missedHeartbeats.set(sessionKey, currentMissed);
    
    console.warn(`[HeartbeatManager] Missed heartbeat for session ${sessionKey} (${currentMissed}/${this.config.missedHeartbeatThreshold})`);
    
    if (currentMissed >= this.config.missedHeartbeatThreshold) {
      console.error(`[HeartbeatManager] Heartbeat threshold exceeded for session ${sessionKey}`);
      this.stopHeartbeat(session);
      
      if (this.callbacks.onHeartbeatFailed) {
        this.callbacks.onHeartbeatFailed(session);
      }
    }
  }

  /**
   * Gets a unique key for a session
   */
  private getSessionKey(session: WHEPSession): string {
    return `${session.sinkId}:${session.listenerId}`;
  }

  /**
   * Gets the number of active heartbeats
   */
  getActiveHeartbeatCount(): number {
    return this.heartbeatIntervals.size;
  }

  /**
   * Gets the missed heartbeat count for a session
   */
  getMissedHeartbeatCount(session: WHEPSession): number {
    const sessionKey = this.getSessionKey(session);
    return this.missedHeartbeats.get(sessionKey) || 0;
  }

  /**
   * Checks if a session has an active heartbeat
   */
  hasActiveHeartbeat(session: WHEPSession): boolean {
    const sessionKey = this.getSessionKey(session);
    return this.heartbeatIntervals.has(sessionKey);
  }

  /**
   * Updates configuration
   */
  updateConfig(config: Partial<HeartbeatManagerConfig>): void {
    Object.assign(this.config, config);
    console.log('[HeartbeatManager] Configuration updated:', this.config);
  }

  /**
   * Cleans up all resources
   */
  cleanup(): void {
    this.stopAll();
    console.log('[HeartbeatManager] Cleaned up heartbeat manager');
  }
}