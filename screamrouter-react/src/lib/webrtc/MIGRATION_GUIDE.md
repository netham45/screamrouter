# WebRTC Implementation Migration Guide

## Overview

This guide helps you migrate from the old WebRTC implementation to the new refactored architecture. The new implementation provides better separation of concerns, improved error handling, automatic reconnection, and real-time statistics.

## Key Changes

### 1. Architecture Changes

**Old Architecture:**
- WebRTC logic mixed in `AppContext.tsx`
- Single monolithic `WebRTCManager` class
- No server ICE candidate polling
- Limited error handling

**New Architecture:**
- Modular component design with clear responsibilities
- Separate classes for WHEP protocol, ICE management, heartbeats, and connections
- Full bidirectional ICE candidate exchange
- Comprehensive error handling and recovery

### 2. Component Structure

```
src/lib/webrtc/
├── WHEPClient.ts           # WHEP protocol implementation
├── ICECandidateManager.ts  # ICE candidate exchange
├── HeartbeatManager.ts     # Session heartbeat management
├── ConnectionManager.ts    # Connection lifecycle management
├── EnhancedWebRTCManager.ts # Main orchestrator
├── WebRTCManager.ts        # Legacy manager (with ICE fix)
└── index.ts                # Exports

src/context/
├── WebRTCContext.tsx        # Old context (deprecated)
└── EnhancedWebRTCContext.tsx # New context

src/components/webrtc/
├── AudioPlayer.tsx          # Old player (deprecated)
└── EnhancedAudioPlayer.tsx  # New player with stats
```

## Migration Steps

### Step 1: Update Imports

**Old:**
```typescript
import { useWebRTC } from '../../context/WebRTCContext';
import { WebRTCManager } from '../../lib/webrtc/WebRTCManager';
```

**New:**
```typescript
import { useEnhancedWebRTC } from '../../context/EnhancedWebRTCContext';
import { EnhancedWebRTCManager } from '../../lib/webrtc';
```

### Step 2: Update Context Provider

**Old (in App.tsx):**
```typescript
import { WebRTCProvider } from './context/WebRTCContext';

function App() {
  return (
    <WebRTCProvider>
      {/* Your app */}
    </WebRTCProvider>
  );
}
```

**New:**
```typescript
import { EnhancedWebRTCProvider } from './context/EnhancedWebRTCContext';

function App() {
  return (
    <EnhancedWebRTCProvider
      config={{
        enableAutoReconnect: true,
        reconnectDelay: 3000,
        maxReconnectAttempts: 5,
        enableStats: true,
      }}
      onError={(sinkId, error) => {
        console.error(`WebRTC error for ${sinkId}:`, error);
        // Show user notification
      }}
    >
      {/* Your app */}
    </EnhancedWebRTCProvider>
  );
}
```

### Step 3: Update Component Usage

**Old:**
```typescript
const MyComponent = () => {
  const { 
    listeningStatus, 
    audioStreams, 
    toggleListening 
  } = useWebRTC();
  
  const isListening = listeningStatus.get(sinkId);
  const stream = audioStreams.get(sinkId);
  
  return (
    <button onClick={() => toggleListening(sinkId)}>
      {isListening ? 'Stop' : 'Start'}
    </button>
  );
};
```

**New:**
```typescript
const MyComponent = () => {
  const { 
    state, 
    stream, 
    stats, 
    error,
    toggle 
  } = useWebRTCConnection(sinkId);
  
  return (
    <div>
      <button onClick={toggle}>
        {state === 'connected' ? 'Stop' : 'Start'}
      </button>
      {error && <div>Error: {error.message}</div>}
      {stats && <div>Packets: {stats.packetsReceived}</div>}
    </div>
  );
};
```

### Step 4: Update Audio Players

**Old:**
```typescript
import AudioPlayer from './components/webrtc/AudioPlayer';

<AudioPlayer 
  stream={stream} 
  sinkId={sinkId}
  onPlaybackError={handleError}
/>
```

**New:**
```typescript
import { EnhancedAudioPlayer } from './components/webrtc/EnhancedAudioPlayer';

<EnhancedAudioPlayer 
  sinkId={sinkId}
  sinkName="Living Room"
  showStats={true}
  showControls={true}
  autoPlay={true}
/>
```

### Step 5: Remove WebRTC from AppContext

Remove all WebRTC-related code from `AppContext.tsx`:

```typescript
// Remove these:
- webrtcListenersRef
- heartbeatIntervalsRef
- audioStreams
- onListenToSink
- cleanupConnection
- all WebRTC-related state and functions
```

## API Reference

### EnhancedWebRTCContext

```typescript
interface WebRTCContextValue {
  // State
  connectionStates: Map<string, ConnectionState>;
  audioStreams: Map<string, MediaStream>;
  stats: Map<string, WebRTCStats>;
  errors: Map<string, WebRTCError>;
  connecting: Set<string>;
  
  // Actions
  startListening(sinkId: string): Promise<void>;
  stopListening(sinkId: string): Promise<void>;
  toggleListening(sinkId: string): Promise<void>;
  stopAllListening(): Promise<void>;
  
  // Queries
  isListening(sinkId: string): boolean;
  isConnecting(sinkId: string): boolean;
  getConnectionState(sinkId: string): ConnectionState;
  getStream(sinkId: string): MediaStream | null;
  getStats(sinkId: string): WebRTCStats | null;
  getError(sinkId: string): WebRTCError | null;
  
  // Error handling
  clearError(sinkId: string): void;
  clearAllErrors(): void;
  
  // Configuration
  updateConfig(config: Partial<WebRTCManagerConfig>): void;
}
```

### Connection States

```typescript
type ConnectionState = 
  | 'disconnected'   // Not connected
  | 'connecting'     // Initial connection attempt
  | 'connected'      // Successfully connected
  | 'failed'         // Connection failed
  | 'reconnecting';  // Automatic reconnection attempt
```

### WebRTC Statistics

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

### Error Handling

```typescript
interface WebRTCError {
  type: 'network' | 'protocol' | 'server' | 'client';
  message: string;
  details?: any;
  recoverable: boolean;
  suggestedAction?: string;
}
```

## Configuration Options

```typescript
interface WebRTCManagerConfig {
  // WHEP endpoint
  baseUrl?: string;              // Default: '/api/whep'
  
  // ICE servers
  iceServers?: RTCIceServer[];   // STUN/TURN servers
  
  // Reconnection
  enableAutoReconnect?: boolean; // Default: true
  reconnectDelay?: number;       // Default: 3000ms
  maxReconnectAttempts?: number; // Default: 5
  
  // Timeouts
  connectionTimeout?: number;    // Default: 30000ms
  
  // Statistics
  statsInterval?: number;        // Default: 5000ms
  enableStats?: boolean;         // Default: true
}
```

## Feature Comparison

| Feature | Old Implementation | New Implementation |
|---------|-------------------|-------------------|
| Server ICE Candidates | ❌ Not polled | ✅ Automatic polling |
| Connection Recovery | ❌ Manual only | ✅ Automatic reconnection |
| Error Handling | ⚠️ Basic | ✅ Comprehensive |
| Statistics | ❌ None | ✅ Real-time stats |
| Heartbeat | ⚠️ Basic | ✅ Advanced with recovery |
| Code Organization | ❌ Mixed concerns | ✅ Clean separation |
| TypeScript Support | ⚠️ Partial | ✅ Full typing |
| Memory Management | ⚠️ Potential leaks | ✅ Proper cleanup |
| Testing | ❌ Difficult | ✅ Testable components |

## Rollback Plan

If you need to rollback to the old implementation:

1. The old `WebRTCManager` has been preserved with the ICE polling fix
2. The old `WebRTCContext` is still available
3. Use feature flags to switch between implementations:

```typescript
const USE_ENHANCED_WEBRTC = process.env.REACT_APP_USE_ENHANCED_WEBRTC === 'true';

const App = () => {
  if (USE_ENHANCED_WEBRTC) {
    return (
      <EnhancedWebRTCProvider>
        {/* New implementation */}
      </EnhancedWebRTCProvider>
    );
  } else {
    return (
      <WebRTCProvider>
        {/* Old implementation */}
      </WebRTCProvider>
    );
  }
};
```

## Troubleshooting

### Common Issues

1. **Connection fails immediately**
   - Check if the sink exists
   - Verify network connectivity
   - Check browser console for errors

2. **No audio after connection**
   - Check browser autoplay policies
   - Ensure audio element is not muted
   - Verify stream is attached to audio element

3. **Frequent disconnections**
   - Check network stability
   - Increase heartbeat interval if needed
   - Review server logs for errors

4. **High latency**
   - Check TURN server configuration
   - Monitor network conditions
   - Review statistics for packet loss

### Debug Mode

Enable debug logging:

```typescript
// In browser console
localStorage.setItem('webrtc_debug', 'true');
```

### Support

For issues or questions:
1. Check the browser console for detailed logs
2. Review the WebRTC statistics in the UI
3. Check server logs for WHEP endpoint errors
4. File an issue with reproduction steps

## Benefits of Migration

1. **Reliability**: Automatic reconnection and error recovery
2. **Performance**: Optimized ICE candidate exchange
3. **Monitoring**: Real-time statistics and connection state
4. **Maintainability**: Clean, modular architecture
5. **User Experience**: Better error messages and feedback
6. **Future-Proof**: Easier to extend and test

## Timeline

- **Phase 1** (Complete): Core refactor and ICE polling fix
- **Phase 2** (Complete): Enhanced components and context
- **Phase 3** (Current): Testing and migration
- **Phase 4** (Next): Deprecate old implementation

## Conclusion

The new WebRTC implementation provides a robust, maintainable solution for audio streaming. While migration requires some code changes, the benefits in reliability, performance, and user experience make it worthwhile.