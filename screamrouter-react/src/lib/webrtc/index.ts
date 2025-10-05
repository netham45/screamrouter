/**
 * @file index.ts
 * @description Export all WebRTC components and types
 */

// Core components
export { WHEPClient } from './WHEPClient';
export type { WHEPSession, ServerICECandidate } from './WHEPClient';

export { ICECandidateManager } from './ICECandidateManager';
export type { ICECandidateManagerConfig } from './ICECandidateManager';

export { HeartbeatManager } from './HeartbeatManager';
export type { HeartbeatManagerConfig, HeartbeatCallbacks } from './HeartbeatManager';

export { ConnectionManager } from './ConnectionManager';
export type { 
  ConnectionState, 
  ConnectionConfig, 
  ConnectionCallbacks 
} from './ConnectionManager';

export { EnhancedWebRTCManager } from './EnhancedWebRTCManager';
export type { 
  WebRTCStats, 
  WebRTCError, 
  WebRTCManagerCallbacks, 
  WebRTCManagerConfig 
} from './EnhancedWebRTCManager';

// Legacy manager (with ICE polling fix)
export { WebRTCManager } from './WebRTCManager';