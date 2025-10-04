/**
 * @file WebRTCManager.ts
 * @description Manages WebRTC connections for audio streaming.
 * This class is responsible for establishing, maintaining, and closing a single WebRTC connection at a time.
 */

type StatusChangeCallback = (sinkId: string, status: boolean) => void;
type StreamCallback = (sinkId: string, stream: MediaStream | null) => void;
type PlaybackErrorCallback = (sinkId: string, error: Error) => void;

export class WebRTCManager {
  private pc: RTCPeerConnection | null = null;
  private listenerId: string | null = null;
  private activeSinkId: string | null = null;
  private heartbeatInterval: NodeJS.Timeout | null = null;
  private iceCandidateQueue: RTCIceCandidate[] = [];
  
  // Server ICE candidate polling
  private candidatePollingInterval: NodeJS.Timeout | null = null;
  private candidatePollingStartTime: number = 0;
  private readonly CANDIDATE_POLLING_INTERVAL_MS = 1000; // Poll every 1 second
  private readonly CANDIDATE_POLLING_MAX_DURATION_MS = 30000; // Stop after 30 seconds

  // Callbacks to update React state
  private onStatusChange: StatusChangeCallback;
  private onStream: StreamCallback;
  private onPlaybackError: PlaybackErrorCallback;

  constructor(
    onStatusChange: StatusChangeCallback,
    onStream: StreamCallback,
    onPlaybackError: PlaybackErrorCallback
  ) {
    this.onStatusChange = onStatusChange;
    this.onStream = onStream;
    this.onPlaybackError = onPlaybackError;
  }

  public isListeningTo(sinkId: string): boolean {
    return this.activeSinkId === sinkId && this.pc !== null;
  }

  public async toggleListening(sinkId: string): Promise<void> {
    if (this.isListeningTo(sinkId)) {
      console.log(`[WebRTC:${sinkId}] User intent: STOP.`);
      await this.stopListening();
    } else {
      console.log(`[WebRTC:${sinkId}] User intent: START.`);
      await this.startListening(sinkId);
    }
  }

  private async startListening(sinkId: string): Promise<void> {
    // Enforce single stream rule by stopping any active listener.
    if (this.activeSinkId) {
      console.log(`[WebRTC] Found active listener on ${this.activeSinkId}. Cleaning up before starting new stream.`);
      await this.stopListening();
    }

    console.log(`[WebRTC:${sinkId}] Proceeding to establish new WHEP connection.`);
    this.activeSinkId = sinkId;

    this.pc = new RTCPeerConnection({
      iceServers: [
        { urls: 'stun:stun.l.google.com:19302' },
        {
          urls: 'turn:192.168.3.201',
          username: 'screamrouter',
          credential: 'screamrouter',
        },
      ],
    });

    this.pc.onicecandidate = (event) => this.handleIceCandidate(event);
    this.pc.ontrack = (event) => this.handleTrack(event);
    this.pc.oniceconnectionstatechange = () => this.handleIceConnectionStateChange();

    try {
      this.pc.addTransceiver('audio', { direction: 'recvonly' });
      const offer = await this.pc.createOffer();
      offer.sdp = offer.sdp?.replace("useinbandfec=1", "useinbandfec=1;stereo=1;sprop-stereo=1");
      offer.sdp = offer.sdp?.replace("useinbandfec=0", "useinbandfec=0;stereo=1;sprop-stereo=1");
      await this.pc.setLocalDescription(offer);

      const response = await fetch(`/api/whep/${this.activeSinkId}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/sdp' },
        body: offer.sdp,
      });

      if (response.status !== 201) {
        throw new Error(`Failed to get answer from WHEP endpoint. Status: ${response.status}`);
      }

      const answerSdp = await response.text();
      const location = response.headers.get('Location');
      if (!location) {
        throw new Error('Location header not found in WHEP response');
      }

      this.listenerId = location.split('/').pop()!;
      console.log(`[WebRTC:${this.activeSinkId}] Received listenerId: ${this.listenerId}.`);

      // Process any queued candidates now that we have the listenerId
      this.processIceCandidateQueue();

      const answer = new RTCSessionDescription({ type: 'answer', sdp: answerSdp });
      await this.pc.setRemoteDescription(answer);

      // Start polling for server ICE candidates
      this.startServerCandidatePolling();

      console.log(`[WebRTC:${this.activeSinkId}] Started listening via WHEP`);
    } catch (error) {
      console.error(`[WebRTC:${this.activeSinkId}] Error during WHEP negotiation:`, error);
      await this.cleanupConnection("WHEP negotiation failed");
    }
  }

  public async stopListening(): Promise<void> {
    await this.cleanupConnection("User initiated stop");
  }

  private async cleanupConnection(reason: string): Promise<void> {
    if (!this.activeSinkId) return;

    const sinkId = this.activeSinkId;
    console.log(`[WebRTC:${sinkId}] Cleaning up connection. Reason: ${reason}`);

    // Stop server candidate polling
    if (this.candidatePollingInterval) {
      clearInterval(this.candidatePollingInterval);
      this.candidatePollingInterval = null;
      console.log(`[WebRTC:${sinkId}] Stopped server ICE candidate polling.`);
    }

    if (this.heartbeatInterval) {
      clearInterval(this.heartbeatInterval);
      this.heartbeatInterval = null;
      console.log(`[WebRTC:${sinkId}] Stopped heartbeat.`);
    }

    if (this.pc) {
      this.pc.close();
      this.pc = null;
    }

    if (this.listenerId) {
      try {
        await fetch(`/api/whep/${sinkId}/${this.listenerId}`, {
          method: 'DELETE',
        });
        console.log(`[WebRTC:${sinkId}] Sent DELETE request to WHEP endpoint.`);
      } catch (error) {
        console.error(`[WebRTC:${sinkId}] Error sending DELETE request:`, error);
      }
    }

    this.onStream(sinkId, null);
    this.onStatusChange(sinkId, false);

    this.activeSinkId = null;
    this.listenerId = null;
    this.iceCandidateQueue = [];

    console.log(`[WebRTC:${sinkId}] Connection cleanup complete.`);
  }

  private handleIceCandidate(event: RTCPeerConnectionIceEvent): void {
    if (event.candidate) {
      if (this.listenerId) {
        this.sendIceCandidate(event.candidate);
      } else {
        this.iceCandidateQueue.push(event.candidate);
      }
    }
  }

  private sendIceCandidate(candidate: RTCIceCandidate): void {
    const payload = {
      candidate: candidate.candidate,
      sdpMid: candidate.sdpMid,
    };
    fetch(`/api/whep/${this.activeSinkId}/${this.listenerId}`, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    }).catch(e => console.error(`[WebRTC:${this.activeSinkId}] Error sending ICE candidate:`, e));
  }

  private processIceCandidateQueue(): void {
    while (this.iceCandidateQueue.length > 0) {
      const candidate = this.iceCandidateQueue.shift();
      if (candidate) {
        this.sendIceCandidate(candidate);
      }
    }
  }

  private handleTrack(event: RTCTrackEvent): void {
    if (this.activeSinkId) {
      console.log(`[WebRTC:${this.activeSinkId}] Received remote audio track.`);
      this.onStream(this.activeSinkId, event.streams[0]);
    }
  }

  private async handleIceConnectionStateChange(): Promise<void> {
    if (!this.pc || !this.activeSinkId) return;

    const sinkId = this.activeSinkId;
    console.log(`[WebRTC:${sinkId}] ICE connection state changed to: ${this.pc.iceConnectionState}`);

    if (this.pc.iceConnectionState === 'connected') {
      this.onStatusChange(sinkId, true);
      this.startHeartbeat(sinkId);
    } else if (['failed', 'disconnected', 'closed'].includes(this.pc.iceConnectionState)) {
      await this.cleanupConnection(`ICE state changed to ${this.pc.iceConnectionState}`);
    }
  }

  private startHeartbeat(sinkId: string): void {
    if (this.heartbeatInterval) {
      clearInterval(this.heartbeatInterval);
    }
    this.heartbeatInterval = setInterval(async () => {
      if (this.pc && this.listenerId) {
        try {
          const response = await fetch(`/api/whep/${sinkId}/${this.listenerId}`, { method: 'POST' });
          if (response.status === 404) {
            console.error(`[WebRTC:${sinkId}] Heartbeat failed: session not found. Cleaning up.`);
            await this.cleanupConnection("Heartbeat failed");
          }
        } catch (error) {
          console.error(`[WebRTC:${sinkId}] Heartbeat request error:`, error);
        }
      }
    }, 5000);
  }

  /**
   * Starts polling for server ICE candidates.
   * This is critical for proper WebRTC connection establishment.
   * The server generates ICE candidates that need to be added to our peer connection.
   */
  private startServerCandidatePolling(): void {
    if (!this.activeSinkId || !this.listenerId || !this.pc) {
      console.warn(`[WebRTC] Cannot start candidate polling: missing required data`);
      return;
    }

    const sinkId = this.activeSinkId;
    const listenerId = this.listenerId;
    
    console.log(`[WebRTC:${sinkId}] Starting server ICE candidate polling for listener ${listenerId}`);
    
    // Record when we started polling
    this.candidatePollingStartTime = Date.now();
    
    // Clear any existing polling interval
    if (this.candidatePollingInterval) {
      clearInterval(this.candidatePollingInterval);
    }
    
    // Start polling interval
    this.candidatePollingInterval = setInterval(async () => {
      // Check if we should stop polling (timeout or connection established)
      const elapsedTime = Date.now() - this.candidatePollingStartTime;
      
      if (elapsedTime > this.CANDIDATE_POLLING_MAX_DURATION_MS) {
        console.log(`[WebRTC:${sinkId}] Stopping candidate polling after ${this.CANDIDATE_POLLING_MAX_DURATION_MS}ms timeout`);
        if (this.candidatePollingInterval) {
          clearInterval(this.candidatePollingInterval);
          this.candidatePollingInterval = null;
        }
        return;
      }
      
      // Check if connection is already established
      if (this.pc?.iceConnectionState === 'connected' || this.pc?.iceConnectionState === 'completed') {
        console.log(`[WebRTC:${sinkId}] ICE connection established, stopping candidate polling`);
        if (this.candidatePollingInterval) {
          clearInterval(this.candidatePollingInterval);
          this.candidatePollingInterval = null;
        }
        return;
      }
      
      // Poll for server candidates
      try {
        const response = await fetch(`/api/whep/${sinkId}/${listenerId}/candidates`, {
          method: 'GET',
          headers: { 'Accept': 'application/json' }
        });
        
        if (response.status === 200) {
          const candidates = await response.json();
          
          if (Array.isArray(candidates) && candidates.length > 0) {
            console.log(`[WebRTC:${sinkId}] Received ${candidates.length} server ICE candidates`);
            
            for (const candidateData of candidates) {
              if (candidateData.candidate && candidateData.sdpMid !== undefined) {
                try {
                  const iceCandidate = new RTCIceCandidate({
                    candidate: candidateData.candidate,
                    sdpMLineIndex: 0, // Audio is typically on line 0
                    sdpMid: candidateData.sdpMid
                  });
                  
                  if (this.pc) {
                    await this.pc.addIceCandidate(iceCandidate);
                    console.log(`[WebRTC:${sinkId}] Added server ICE candidate: ${candidateData.candidate}`);
                  }
                } catch (error) {
                  console.error(`[WebRTC:${sinkId}] Error adding server ICE candidate:`, error);
                }
              }
            }
          }
        } else if (response.status !== 404) {
          // 404 is expected when there are no candidates yet
          console.warn(`[WebRTC:${sinkId}] Unexpected status ${response.status} when polling for candidates`);
        }
      } catch (error) {
        console.error(`[WebRTC:${sinkId}] Error polling for server ICE candidates:`, error);
      }
    }, this.CANDIDATE_POLLING_INTERVAL_MS);
  }
}