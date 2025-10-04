/**
 * @file EnhancedWebRTCContext.tsx
 * @description Enhanced WebRTC context using the refactored WebRTC components
 * Provides clean separation of concerns and improved error handling
 */

import React, { createContext, useContext, useState, useEffect, useCallback, useRef } from 'react';
import { EnhancedWebRTCManager, WebRTCStats, WebRTCError, WebRTCManagerConfig } from '../lib/webrtc/EnhancedWebRTCManager';
import { ConnectionState } from '../lib/webrtc/ConnectionManager';

export interface WebRTCContextState {
  // Connection states
  connectionStates: Map<string, ConnectionState>;
  audioStreams: Map<string, MediaStream>;
  
  // Statistics
  stats: Map<string, WebRTCStats>;
  
  // Errors
  errors: Map<string, WebRTCError>;
  
  // Loading states
  connecting: Set<string>;
}

export interface WebRTCContextActions {
  // Connection control
  startListening: (sinkId: string) => Promise<void>;
  stopListening: (sinkId: string) => Promise<void>;
  toggleListening: (sinkId: string) => Promise<void>;
  stopAllListening: () => Promise<void>;
  
  // State queries
  isListening: (sinkId: string) => boolean;
  isConnecting: (sinkId: string) => boolean;
  getConnectionState: (sinkId: string) => ConnectionState;
  getStream: (sinkId: string) => MediaStream | null;
  getStats: (sinkId: string) => WebRTCStats | null;
  getError: (sinkId: string) => WebRTCError | null;
  
  // Error handling
  clearError: (sinkId: string) => void;
  clearAllErrors: () => void;
  
  // Configuration
  updateConfig: (config: Partial<WebRTCManagerConfig>) => void;
}

export interface WebRTCContextValue extends WebRTCContextState, WebRTCContextActions {}

const WebRTCContext = createContext<WebRTCContextValue | undefined>(undefined);

export interface EnhancedWebRTCProviderProps {
  children: React.ReactNode;
  config?: WebRTCManagerConfig;
  onConnectionStateChange?: (sinkId: string, state: ConnectionState) => void;
  onError?: (sinkId: string, error: WebRTCError) => void;
  onStats?: (sinkId: string, stats: WebRTCStats) => void;
}

export const EnhancedWebRTCProvider: React.FC<EnhancedWebRTCProviderProps> = ({
  children,
  config,
  onConnectionStateChange,
  onError,
  onStats,
}) => {
  // State
  const [connectionStates, setConnectionStates] = useState<Map<string, ConnectionState>>(new Map());
  const [audioStreams, setAudioStreams] = useState<Map<string, MediaStream>>(new Map());
  const [stats, setStats] = useState<Map<string, WebRTCStats>>(new Map());
  const [errors, setErrors] = useState<Map<string, WebRTCError>>(new Map());
  const [connecting, setConnecting] = useState<Set<string>>(new Set());
  
  // WebRTC Manager reference
  const managerRef = useRef<EnhancedWebRTCManager | null>(null);
  
  // Initialize WebRTC Manager
  useEffect(() => {
    const manager = new EnhancedWebRTCManager(
      {
        onConnectionStateChange: (sinkId, state) => {
          console.log(`[WebRTCContext] Connection state changed: ${sinkId} -> ${state}`);
          
          setConnectionStates(prev => new Map(prev).set(sinkId, state));
          
          if (state === 'connecting') {
            setConnecting(prev => new Set(prev).add(sinkId));
          } else {
            setConnecting(prev => {
              const newSet = new Set(prev);
              newSet.delete(sinkId);
              return newSet;
            });
          }
          
          if (state === 'disconnected' || state === 'failed') {
            setAudioStreams(prev => {
              const newMap = new Map(prev);
              newMap.delete(sinkId);
              return newMap;
            });
          }
          
          // Call external handler if provided
          if (onConnectionStateChange) {
            onConnectionStateChange(sinkId, state);
          }
        },
        
        onStream: (sinkId, stream) => {
          console.log(`[WebRTCContext] Stream ${stream ? 'received' : 'removed'} for ${sinkId}`);
          
          setAudioStreams(prev => {
            const newMap = new Map(prev);
            if (stream) {
              newMap.set(sinkId, stream);
            } else {
              newMap.delete(sinkId);
            }
            return newMap;
          });
        },
        
        onError: (sinkId, error) => {
          console.error(`[WebRTCContext] Error for ${sinkId}:`, error);
          
          setErrors(prev => new Map(prev).set(sinkId, error));
          
          // Call external handler if provided
          if (onError) {
            onError(sinkId, error);
          }
        },
        
        onStats: (sinkId, statsData) => {
          setStats(prev => new Map(prev).set(sinkId, statsData));
          
          // Call external handler if provided
          if (onStats) {
            onStats(sinkId, statsData);
          }
        },
      },
      config
    );
    
    managerRef.current = manager;
    
    // Cleanup on unmount
    return () => {
      manager.cleanup();
    };
  }, []); // Only initialize once
  
  // Actions
  const startListening = useCallback(async (sinkId: string) => {
    if (!managerRef.current) {
      throw new Error('WebRTC Manager not initialized');
    }
    
    try {
      setConnecting(prev => new Set(prev).add(sinkId));
      await managerRef.current.startListening(sinkId);
    } catch (error) {
      console.error(`[WebRTCContext] Failed to start listening to ${sinkId}:`, error);
      throw error;
    } finally {
      setConnecting(prev => {
        const newSet = new Set(prev);
        newSet.delete(sinkId);
        return newSet;
      });
    }
  }, []);
  
  const stopListening = useCallback(async (sinkId: string) => {
    if (!managerRef.current) {
      throw new Error('WebRTC Manager not initialized');
    }
    
    try {
      await managerRef.current.stopListening(sinkId);
      
      // Clean up state
      setConnectionStates(prev => {
        const newMap = new Map(prev);
        newMap.delete(sinkId);
        return newMap;
      });
      
      setAudioStreams(prev => {
        const newMap = new Map(prev);
        newMap.delete(sinkId);
        return newMap;
      });
      
      setStats(prev => {
        const newMap = new Map(prev);
        newMap.delete(sinkId);
        return newMap;
      });
    } catch (error) {
      console.error(`[WebRTCContext] Failed to stop listening to ${sinkId}:`, error);
      throw error;
    }
  }, []);
  
  const toggleListening = useCallback(async (sinkId: string) => {
    if (!managerRef.current) {
      throw new Error('WebRTC Manager not initialized');
    }
    
    try {
      await managerRef.current.toggleListening(sinkId);
    } catch (error) {
      console.error(`[WebRTCContext] Failed to toggle listening for ${sinkId}:`, error);
      throw error;
    }
  }, []);
  
  const stopAllListening = useCallback(async () => {
    if (!managerRef.current) {
      throw new Error('WebRTC Manager not initialized');
    }
    
    try {
      await managerRef.current.stopAllListening();
      
      // Clear all state
      setConnectionStates(new Map());
      setAudioStreams(new Map());
      setStats(new Map());
      setConnecting(new Set());
    } catch (error) {
      console.error('[WebRTCContext] Failed to stop all listening:', error);
      throw error;
    }
  }, []);
  
  const isListening = useCallback((sinkId: string): boolean => {
    if (!managerRef.current) {
      return false;
    }
    return managerRef.current.isListening(sinkId);
  }, []);
  
  const isConnecting = useCallback((sinkId: string): boolean => {
    return connecting.has(sinkId);
  }, [connecting]);
  
  const getConnectionState = useCallback((sinkId: string): ConnectionState => {
    return connectionStates.get(sinkId) || 'disconnected';
  }, [connectionStates]);
  
  const getStream = useCallback((sinkId: string): MediaStream | null => {
    return audioStreams.get(sinkId) || null;
  }, [audioStreams]);
  
  const getStats = useCallback((sinkId: string): WebRTCStats | null => {
    return stats.get(sinkId) || null;
  }, [stats]);
  
  const getError = useCallback((sinkId: string): WebRTCError | null => {
    return errors.get(sinkId) || null;
  }, [errors]);
  
  const clearError = useCallback((sinkId: string) => {
    setErrors(prev => {
      const newMap = new Map(prev);
      newMap.delete(sinkId);
      return newMap;
    });
  }, []);
  
  const clearAllErrors = useCallback(() => {
    setErrors(new Map());
  }, []);
  
  const updateConfig = useCallback((newConfig: Partial<WebRTCManagerConfig>) => {
    if (!managerRef.current) {
      throw new Error('WebRTC Manager not initialized');
    }
    managerRef.current.updateConfig(newConfig);
  }, []);
  
  // Context value
  const value: WebRTCContextValue = {
    // State
    connectionStates,
    audioStreams,
    stats,
    errors,
    connecting,
    
    // Actions
    startListening,
    stopListening,
    toggleListening,
    stopAllListening,
    isListening,
    isConnecting,
    getConnectionState,
    getStream,
    getStats,
    getError,
    clearError,
    clearAllErrors,
    updateConfig,
  };
  
  return (
    <WebRTCContext.Provider value={value}>
      {children}
    </WebRTCContext.Provider>
  );
};

/**
 * Hook to use the WebRTC context
 */
export const useEnhancedWebRTC = (): WebRTCContextValue => {
  const context = useContext(WebRTCContext);
  if (!context) {
    throw new Error('useEnhancedWebRTC must be used within an EnhancedWebRTCProvider');
  }
  return context;
};

/**
 * Hook to get the connection state for a specific sink
 */
export const useWebRTCConnection = (sinkId: string) => {
  const { 
    getConnectionState, 
    getStream, 
    getStats, 
    getError,
    isConnecting,
    startListening,
    stopListening,
    toggleListening,
    clearError
  } = useEnhancedWebRTC();
  
  return {
    state: getConnectionState(sinkId),
    stream: getStream(sinkId),
    stats: getStats(sinkId),
    error: getError(sinkId),
    isConnecting: isConnecting(sinkId),
    isConnected: getConnectionState(sinkId) === 'connected',
    start: () => startListening(sinkId),
    stop: () => stopListening(sinkId),
    toggle: () => toggleListening(sinkId),
    clearError: () => clearError(sinkId),
  };
};

export default WebRTCContext;