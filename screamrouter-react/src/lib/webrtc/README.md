
# WebRTC Implementation for ScreamRouter

## Overview

This directory contains the refactored WebRTC implementation for ScreamRouter's browser-based audio streaming. The implementation uses the WHEP (WebRTC-HTTP Egress Protocol) for establishing WebRTC connections between the server and browser clients.

## Architecture

The WebRTC implementation follows a modular architecture with clear separation of concerns:

```
┌─────────────────────────────────────────────────────────────┐
│                    EnhancedWebRTCManager                     │
│                     (Main Orchestrator)                      │
└─────────────┬───────────────────────────────────────────────┘
              │
              ├──► WHEPClient         (WHEP Protocol)
              ├──► ICECandidateManager (ICE Exchange)
              ├──► HeartbeatManager    (Keep-Alive)
              └──► ConnectionManager   (Lifecycle)
```

## Components

### Core Components

#### 1. **WHEPClient** (`WHEPClient.ts`)
Handles all WHEP protocol communication with the server.

**Responsibilities:**
- Create WHEP sessions (SDP offer/answer exchange)
- Delete sessions on disconnect
- Send client ICE candidates to server
- Poll for server ICE candidates

**Key Methods:**
```typescript
createSession(sinkId: string, offerSdp: string): Promise<{session, answerSdp}>
deleteSession(session: WHEPSession): Promise<void>
sendCandidate(session: WHEPSession, candidate: RTCIceCandidate): Promise<void>
pollServerCandidates(session: WHEPSession): Promise<ServerICECandidate[]>
sendHeartbeat(session: WHEPSession): Promise<boolean>
```

#### 2. **ICECandidateManager** (`ICECandidateManager.ts`)
Manages ICE candidate exchange between client and server.

**Responsibilities:**
- Queue client candidates before session is ready
- Poll server for ICE candidates
- Add server candidates to peer connection
- Handle polling timeout and cleanup

**Features:**
- Configurable polling interval (default: 1s)
- Maximum polling duration (default: 30s)
- Automatic stop when connection established
- Queue management for client candidates

#### 3. **HeartbeatManager** (`HeartbeatManager.ts`)
Maintains WebRTC sessions with periodic heartbeats.

**Responsibilities:**
- Send periodic heartbeats to keep sessions alive
- Detect failed connections
- Trigger recovery on heartbeat failure
- Track missed heartbeats

**Configuration:**
- Heartbeat interval: 5 seconds
- Missed heartbeat threshold: 3
- Automatic recovery callbacks

#### 4. **ConnectionManager** (`ConnectionManager.ts`)
Manages WebRTC peer connection lifecycle.

**Responsibilities:**
- Create and configure RTCPeerConnection
- Handle connection state changes
- Automatic reconnection with exponential backoff
- Stream management
- Statistics collection

**Features:**
- Single connection enforcement
- Configurable ICE servers
- Connection timeout handling
- Reconnection attempts with backoff

#### 5. **EnhancedWebRTCManager** (`EnhancedWebRTCManager.ts`)
Main orchestrator that integrates all components.

**Responsibilities:**
- Coordinate all WebRTC components
- Provide unified API
- Handle errors and recovery
- Collect and report statistics

### React Integration

#### **EnhancedWebRTCContext** (`context/EnhancedWebRTCContext.tsx`)
React context providing WebRTC functionality to components.

**Features:**
- Global WebRTC state management
- Connection state tracking
- Stream management
- Error handling
- Statistics collection

**Hooks:**
```typescript
// Main context hook
useEnhancedWebRTC(): WebRTCContextValue

// Connection-specific hook
useWebRTCConnection(sinkId: string): {
  state: ConnectionState
  stream: MediaStream | null
  stats: WebRTCStats | null
  error: WebRTCError | null
  isConnecting: boolean
  isConnected: boolean
  start: () => Promise<void>
  stop: () => Promise<void>
  toggle: () => Promise<void>
  clearError: () => void
}
```

#### **EnhancedAudioPlayer** (`components/webrtc/EnhancedAudioPlayer.tsx`)
React component for audio playback with controls and statistics.

**Features:**
- Automatic stream management
- Playback controls
- Volume control
- Connection status indicator
- Real-time statistics display
- Error display with recovery suggestions

## Usage

### Basic Setup

```typescript
import { EnhancedWebRTCProvider } from './context/EnhancedWebRTCContext';

function App() {
  return (
    <EnhancedWebRTCProvider
      config={{
        enableAutoReconnect: true,
        reconnectDelay: 3000,
        maxReconnectAttempts: 5,
      }}
    >
      <YourComponents />
    </EnhancedWebRTCProvider>
  );
}
```

### Using in Components

```typescript
import { useWebRTCConnection } from '../../context/EnhancedWebRTCContext';

function AudioControls({ sinkId }) {
  const { 
    state, 
    stream, 
    stats, 
    toggle 
  } = useWebRTCConnection(sinkId);

  return (
    <div>
      <button onClick={toggle}>
        {state === 'connected' ? 'Disconnect' : 'Connect'}
      </button>
      {stats && (
        <div>
          Packets: {stats.packetsReceived}
          Lost: {stats.packetsLost}
        </div>
      )}
    </div>
  );
}
```

### Audio Player Component

```typescript
import { EnhancedAudioPlayer } from './components/webrtc/EnhancedAudioPlayer';

<EnhancedAudioPlayer
  sinkId="living-room"
  sinkName="Living Room Speakers"
  showStats={true}
  showControls={true}
  autoPlay={true}
/>
```

## Configuration

### ICE Servers

```typescript
const config = {
  iceServers: [
    { urls: 'stun:stun.l.google.com:19302' },
    {
      urls: 'turn:your-turn-server.com:3478',
      username: 'username',
      credential: 'password',
    },
  ],
};
```

### Reconnection Policy

```typescript
const config = {
  enableAutoReconnect: true,      // Enable automatic reconnection
  reconnectDelay: 3000,           // Initial delay (ms)
  maxReconnectAttempts: 5,        // Maximum attempts
  connectionTimeout: 30000,       // Connection timeout (ms)
};
```

### Statistics Collection

```typescript
const config = {
  enableStats: true,               // Enable statistics
  statsInterval: 5000,            // Collection interval (ms)
};
```

## API Endpoints

The implementation uses the following WHEP endpoints:

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/whep/{sink_id}` | Create WHEP session |
| PATCH | `/api/whep/{sink_id}/{listener_id}` | Send ICE candidate |
| GET | `/api/whep/{sink_id}/{listener_id}/candidates` | Poll server candidates |
| POST | `/api/whep/{sink_id}/{listener_id}` | Send heartbeat |
| DELETE | `/api/whep/{sink_id}/{listener_id}` | Delete session |

## Error Handling

The implementation provides comprehensive error handling:

```typescript
interface WebRTCError {
  type: 'network' | 'protocol' | 'server' | 'client';
  message: string;
  details?: any;
  recoverable: boolean;
  suggestedAction?: string;
}
```

### Error Types

- **Network Errors**: Connection failures, ICE failures
- **Protocol Errors**: WHEP negotiation failures, SDP errors
- **Server Errors**: Sink not found, server unavailable
- **Client Errors**: Browser compatibility, permissions

### Recovery Strategies

| Error Type | Recovery Strategy |
|------------|------------------|
| Network | Automatic retry with backoff |
| ICE Failure | Reconnect with new candidates |
| Heartbeat Timeout | Immediate reconnection |
| Server Error | User notification |

## Statistics

Real-time statistics are collected and available:

```typescript
interface WebRTCStats {
  connectionState: ConnectionState;
  packetsReceived: number;
  packetsLost: number;
  bytesReceived: number;
  jitter: number;
  roundTripTime: number;
  audioLevel: number;
  timestamp: Date;
}
```

## Debugging

### Enable Debug Logging

```javascript
// In browser console
localStorage.setItem('webrtc_debug', 'true');
```

### Chrome WebRTC Internals

Navigate to `chrome://webrtc-internals/` to see detailed WebRTC diagnostics.

### Common Issues

1. **No audio after connection**
   - Check browser autoplay policy
   - Ensure user interaction before playing audio
   - Verify audio element is not muted

2. **Connection fails**
   - Check network connectivity
   - Verify TURN server is accessible
   - Check firewall settings

3. **High latency**
   - Monitor packet loss in statistics
   - Check network bandwidth
   - Verify TURN server location

## Testing

### Unit Tests

```bash
npm test -- --coverage src/lib/webrtc
```

### Integration Tests

```bash
npm run test:integration
```

### Manual Testing

1. Open browser developer console
2. Enable debug logging
3. Connect to a sink
