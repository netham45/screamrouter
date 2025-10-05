/**
 * @file WHEPClient.ts
 * @description WHEP (WebRTC-HTTP Egress Protocol) client implementation
 * Handles all HTTP communication with the WHEP endpoints
 */

export interface WHEPSession {
  sinkId: string;
  listenerId: string;
  location: string;
  createdAt: Date;
}

export interface ServerICECandidate {
  candidate: string;
  sdpMid: string;
}

export class WHEPClient {
  private readonly baseUrl: string;

  constructor(baseUrl: string = '/api/whep') {
    this.baseUrl = baseUrl;
  }

  /**
   * Creates a new WHEP session by sending an SDP offer
   */
  async createSession(sinkId: string, offerSdp: string): Promise<{ session: WHEPSession; answerSdp: string }> {
    const url = `${this.baseUrl}/${sinkId}`;
    
    try {
      const response = await fetch(url, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/sdp',
        },
        body: offerSdp,
      });

      if (response.status !== 201) {
        const errorText = await response.text().catch(() => 'Unknown error');
        throw new Error(`Failed to create WHEP session. Status: ${response.status}, Error: ${errorText}`);
      }

      const answerSdp = await response.text();
      const location = response.headers.get('Location');
      
      if (!location) {
        throw new Error('WHEP response missing Location header');
      }

      // Extract listener ID from location
      const listenerId = location.split('/').pop();
      if (!listenerId) {
        throw new Error('Could not extract listener ID from Location header');
      }

      const session: WHEPSession = {
        sinkId,
        listenerId,
        location,
        createdAt: new Date(),
      };

      console.log(`[WHEPClient] Session created for sink ${sinkId}, listener ${listenerId}`);
      return { session, answerSdp };
    } catch (error) {
      console.error(`[WHEPClient] Failed to create session for sink ${sinkId}:`, error);
      throw error;
    }
  }

  /**
   * Deletes an existing WHEP session
   */
  async deleteSession(session: WHEPSession): Promise<void> {
    const url = `${this.baseUrl}/${session.sinkId}/${session.listenerId}`;
    
    try {
      const response = await fetch(url, {
        method: 'DELETE',
      });

      if (response.status !== 204 && response.status !== 404) {
        console.warn(`[WHEPClient] Unexpected status ${response.status} when deleting session`);
      }

      console.log(`[WHEPClient] Session deleted for sink ${session.sinkId}, listener ${session.listenerId}`);
    } catch (error) {
      console.error(`[WHEPClient] Failed to delete session:`, error);
      // Don't throw - cleanup should be best effort
    }
  }

  /**
   * Sends a client ICE candidate to the server
   */
  async sendCandidate(session: WHEPSession, candidate: RTCIceCandidate): Promise<void> {
    const url = `${this.baseUrl}/${session.sinkId}/${session.listenerId}`;
    
    const payload = {
      candidate: candidate.candidate,
      sdpMid: candidate.sdpMid,
    };

    try {
      const response = await fetch(url, {
        method: 'PATCH',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(payload),
      });

      if (response.status !== 204) {
        console.warn(`[WHEPClient] Unexpected status ${response.status} when sending ICE candidate`);
      }
    } catch (error) {
      console.error(`[WHEPClient] Failed to send ICE candidate:`, error);
      throw error;
    }
  }

  /**
   * Polls for server ICE candidates
   */
  async pollServerCandidates(session: WHEPSession): Promise<ServerICECandidate[]> {
    const url = `${this.baseUrl}/${session.sinkId}/${session.listenerId}/candidates`;
    
    try {
      const response = await fetch(url, {
        method: 'GET',
        headers: {
          'Accept': 'application/json',
        },
      });

      if (response.status === 404) {
        // Session might have been deleted
        return [];
      }

      if (response.status !== 200) {
        console.warn(`[WHEPClient] Unexpected status ${response.status} when polling candidates`);
        return [];
      }

      const candidates = await response.json();
      
      if (!Array.isArray(candidates)) {
        console.warn(`[WHEPClient] Invalid response format for candidates`);
        return [];
      }

      return candidates.filter(c => c.candidate && c.sdpMid !== undefined);
    } catch (error) {
      console.error(`[WHEPClient] Failed to poll server candidates:`, error);
      return [];
    }
  }

  /**
   * Sends a heartbeat to keep the session alive
   */
  async sendHeartbeat(session: WHEPSession): Promise<boolean> {
    const url = `${this.baseUrl}/${session.sinkId}/${session.listenerId}`;
    
    try {
      const response = await fetch(url, {
        method: 'POST',
      });

      if (response.status === 404) {
        console.warn(`[WHEPClient] Session not found during heartbeat`);
        return false;
      }

      if (response.status !== 204) {
        console.warn(`[WHEPClient] Unexpected status ${response.status} during heartbeat`);
      }

      return response.status === 204;
    } catch (error) {
      console.error(`[WHEPClient] Heartbeat failed:`, error);
      return false;
    }
  }
}