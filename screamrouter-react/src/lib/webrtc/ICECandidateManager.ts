/**
 * @file ICECandidateManager.ts
 * @description Manages ICE candidate exchange between client and server
 * Handles queuing, polling, and applying candidates to peer connections
 */

import { WHEPClient, WHEPSession, ServerICECandidate } from './WHEPClient';

export interface ICECandidateManagerConfig {
  pollingInterval?: number;
  maxPollingDuration?: number;
  maxCandidateQueueSize?: number;
}

export class ICECandidateManager {
  private whepClient: WHEPClient;
  private clientCandidateQueue: RTCIceCandidate[] = [];
  private pollingInterval: NodeJS.Timeout | null = null;
  private pollingStartTime: number = 0;
  
  // Configuration
  private readonly config: Required<ICECandidateManagerConfig> = {
    pollingInterval: 1000,
    maxPollingDuration: 30000,
    maxCandidateQueueSize: 50,
  };

  constructor(whepClient: WHEPClient, config?: ICECandidateManagerConfig) {
    this.whepClient = whepClient;
    if (config) {
      this.config = { ...this.config, ...config };
    }
  }

  /**
   * Queues a client ICE candidate for sending
   */
  queueClientCandidate(candidate: RTCIceCandidate): void {
    if (this.clientCandidateQueue.length >= this.config.maxCandidateQueueSize) {
      console.warn('[ICECandidateManager] Client candidate queue full, dropping oldest candidate');
      this.clientCandidateQueue.shift();
    }
    
    this.clientCandidateQueue.push(candidate);
    console.log(`[ICECandidateManager] Queued client ICE candidate (queue size: ${this.clientCandidateQueue.length})`);
  }

  /**
   * Processes all queued client candidates
   */
  async processQueuedCandidates(session: WHEPSession): Promise<void> {
    console.log(`[ICECandidateManager] Processing ${this.clientCandidateQueue.length} queued candidates`);
    
    while (this.clientCandidateQueue.length > 0) {
      const candidate = this.clientCandidateQueue.shift();
      if (candidate) {
        try {
          await this.whepClient.sendCandidate(session, candidate);
          console.log(`[ICECandidateManager] Sent queued client ICE candidate`);
        } catch (error) {
          console.error('[ICECandidateManager] Failed to send queued candidate:', error);
          // Continue processing other candidates
        }
      }
    }
  }

  /**
   * Starts polling for server ICE candidates
   */
  startPolling(session: WHEPSession, pc: RTCPeerConnection): void {
    if (this.pollingInterval) {
      console.warn('[ICECandidateManager] Polling already active, stopping existing interval');
      this.stopPolling();
    }

    console.log(`[ICECandidateManager] Starting server ICE candidate polling`);
    this.pollingStartTime = Date.now();

    this.pollingInterval = setInterval(async () => {
      // Check timeout
      const elapsed = Date.now() - this.pollingStartTime;
      if (elapsed > this.config.maxPollingDuration) {
        console.log(`[ICECandidateManager] Polling timeout reached (${this.config.maxPollingDuration}ms)`);
        this.stopPolling();
        return;
      }

      // Check connection state
      const iceState = pc.iceConnectionState;
      if (iceState === 'connected' || iceState === 'completed') {
        console.log(`[ICECandidateManager] ICE connection established (${iceState}), stopping polling`);
        this.stopPolling();
        return;
      }

      // Check if peer connection is closed
      if (pc.connectionState === 'closed' || pc.connectionState === 'failed') {
        console.log(`[ICECandidateManager] Peer connection ${pc.connectionState}, stopping polling`);
        this.stopPolling();
        return;
      }

      // Poll for candidates
      try {
        const candidates = await this.whepClient.pollServerCandidates(session);
        
        if (candidates.length > 0) {
          console.log(`[ICECandidateManager] Received ${candidates.length} server ICE candidates`);
          
          for (const candidateData of candidates) {
            try {
              const iceCandidate = new RTCIceCandidate({
                candidate: candidateData.candidate,
                sdpMLineIndex: 0, // Audio track is typically on line 0
                sdpMid: candidateData.sdpMid,
              });
              
              await pc.addIceCandidate(iceCandidate);
              console.log(`[ICECandidateManager] Added server ICE candidate: ${candidateData.candidate.substring(0, 50)}...`);
            } catch (error) {
              console.error('[ICECandidateManager] Failed to add server ICE candidate:', error);
            }
          }
        }
      } catch (error) {
        console.error('[ICECandidateManager] Error polling for server candidates:', error);
      }
    }, this.config.pollingInterval);
  }

  /**
   * Stops polling for server ICE candidates
   */
  stopPolling(): void {
    if (this.pollingInterval) {
      clearInterval(this.pollingInterval);
      this.pollingInterval = null;
      console.log('[ICECandidateManager] Stopped server ICE candidate polling');
    }
  }

  /**
   * Clears all queued candidates and stops polling
   */
  cleanup(): void {
    this.clientCandidateQueue = [];
    this.stopPolling();
    console.log('[ICECandidateManager] Cleaned up ICE candidate manager');
  }

  /**
   * Gets the current queue size
   */
  getQueueSize(): number {
    return this.clientCandidateQueue.length;
  }

  /**
   * Checks if polling is currently active
   */
  isPolling(): boolean {
    return this.pollingInterval !== null;
  }
}