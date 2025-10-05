/**
 * @file WebRTCContext.tsx
 * @description Provides a context for managing WebRTC state and interactions.
 */

import React, { createContext, useContext, useState, useMemo, useCallback } from 'react';
import { WebRTCManager } from '../lib/webrtc/WebRTCManager';

interface WebRTCContextType {
  listeningStatus: Map<string, boolean>;
  audioStreams: Map<string, MediaStream>;
  playbackError: Map<string, Error>;
  toggleListening: (sinkId: string) => void;
  isListeningTo: (sinkId: string) => boolean;
}

const WebRTCContext = createContext<WebRTCContextType | undefined>(undefined);

export const WebRTCProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [listeningStatus, setListeningStatus] = useState<Map<string, boolean>>(new Map());
  const [audioStreams, setAudioStreams] = useState<Map<string, MediaStream>>(new Map());
  const [playbackError, setPlaybackError] = useState<Map<string, Error>>(new Map());

  const handleStatusChange = useCallback((sinkId: string, status: boolean) => {
    setListeningStatus(prev => new Map(prev).set(sinkId, status));
  }, []);

  const handleStream = useCallback((sinkId: string, stream: MediaStream | null) => {
    setAudioStreams(prev => {
      const newMap = new Map(prev);
      if (stream) {
        newMap.set(sinkId, stream);
      } else {
        newMap.delete(sinkId);
      }
      return newMap;
    });
  }, []);

  const handlePlaybackError = useCallback((sinkId: string, error: Error) => {
    setPlaybackError(prev => new Map(prev).set(sinkId, error));
  }, []);

  const webRTCManager = useMemo(() => {
    return new WebRTCManager(handleStatusChange, handleStream, handlePlaybackError);
  }, [handleStatusChange, handleStream, handlePlaybackError]);

  const toggleListening = useCallback((sinkId: string) => {
    webRTCManager.toggleListening(sinkId);
  }, [webRTCManager]);

  const isListeningTo = useCallback((sinkId: string) => {
    return webRTCManager.isListeningTo(sinkId);
  }, [webRTCManager]);

  const value = {
    listeningStatus,
    audioStreams,
    playbackError,
    toggleListening,
    isListeningTo,
  };

  return (
    <WebRTCContext.Provider value={value}>
      {children}
    </WebRTCContext.Provider>
  );
};

export const useWebRTC = () => {
  const context = useContext(WebRTCContext);
  if (context === undefined) {
    throw new Error('useWebRTC must be used within a WebRTCProvider');
  }
  return context;
};