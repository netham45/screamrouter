/**
 * @file EnhancedAudioPlayer.tsx
 * @description Enhanced audio player component with error handling and statistics display
 * Uses the refactored WebRTC context for improved reliability
 */

import React, { useEffect, useRef, useState } from 'react';
import { useWebRTCConnection } from '../../context/EnhancedWebRTCContext';

interface AudioPlayerProps {
  sinkId: string;
  sinkName?: string;
  showStats?: boolean;
  showControls?: boolean;
  autoPlay?: boolean;
  onPlaybackError?: (error: Error) => void;
}

export const EnhancedAudioPlayer: React.FC<AudioPlayerProps> = ({
  sinkId,
  sinkName,
  showStats = false,
  showControls = false,
  autoPlay = true,
  onPlaybackError,
}) => {
  const audioRef = useRef<HTMLAudioElement>(null);
  const [isPlaying, setIsPlaying] = useState(false);
  const [volume, setVolume] = useState(1.0);
  const [isMuted, setIsMuted] = useState(false);
  
  const {
    state,
    stream,
    stats,
    error,
    isConnecting,
    isConnected,
    toggle,
    clearError,
  } = useWebRTCConnection(sinkId);

  // Handle stream changes
  useEffect(() => {
    if (!audioRef.current) return;

    if (stream) {
      console.log(`[EnhancedAudioPlayer] Setting stream for ${sinkId}`);
      audioRef.current.srcObject = stream;
      
      if (autoPlay) {
        audioRef.current.play().then(() => {
          setIsPlaying(true);
        }).catch((error) => {
          console.error(`[EnhancedAudioPlayer] Playback failed for ${sinkId}:`, error);
          setIsPlaying(false);
          if (onPlaybackError) {
            onPlaybackError(error);
          }
        });
      }
    } else {
      console.log(`[EnhancedAudioPlayer] Clearing stream for ${sinkId}`);
      audioRef.current.srcObject = null;
      setIsPlaying(false);
    }
  }, [stream, sinkId, autoPlay, onPlaybackError]);

  // Handle volume changes
  useEffect(() => {
    if (audioRef.current) {
      audioRef.current.volume = volume;
      audioRef.current.muted = isMuted;
    }
  }, [volume, isMuted]);

  const handlePlayPause = () => {
    if (!audioRef.current) return;

    if (isPlaying) {
      audioRef.current.pause();
      setIsPlaying(false);
    } else {
      audioRef.current.play().then(() => {
        setIsPlaying(true);
      }).catch((error) => {
        console.error(`[EnhancedAudioPlayer] Failed to play:`, error);
        if (onPlaybackError) {
          onPlaybackError(error);
        }
      });
    }
  };

  const handleVolumeChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    setVolume(parseFloat(e.target.value));
  };

  const handleMuteToggle = () => {
    setIsMuted(!isMuted);
  };

  const handleConnect = () => {
    clearError();
    toggle();
  };

  const getStateColor = () => {
    switch (state) {
      case 'connected':
        return '#4caf50';
      case 'connecting':
      case 'reconnecting':
        return '#ff9800';
      case 'failed':
        return '#f44336';
      default:
        return '#9e9e9e';
    }
  };

  const getStateText = () => {
    if (isConnecting) return 'Connecting...';
    switch (state) {
      case 'connected':
        return 'Connected';
      case 'reconnecting':
        return 'Reconnecting...';
      case 'failed':
        return 'Failed';
      default:
        return 'Disconnected';
    }
  };

  return (
    <div style={styles.container}>
      <div style={styles.header}>
        <h3 style={styles.title}>{sinkName || sinkId}</h3>
        <div style={{ ...styles.statusIndicator, backgroundColor: getStateColor() }} />
      </div>

      <div style={styles.status}>
        <span>{getStateText()}</span>
        {error && (
          <div style={styles.error}>
            <span>Error: {error.message}</span>
            {error.suggestedAction && (
              <span style={styles.suggestion}> ({error.suggestedAction})</span>
            )}
          </div>
        )}
      </div>

      <div style={styles.controls}>
        <button
          onClick={handleConnect}
          disabled={isConnecting}
          style={{
            ...styles.button,
            ...(isConnected ? styles.disconnectButton : styles.connectButton),
          }}
        >
          {isConnecting ? 'Connecting...' : isConnected ? 'Disconnect' : 'Connect'}
        </button>

        {showControls && stream && (
          <>
            <button onClick={handlePlayPause} style={styles.button}>
              {isPlaying ? '⏸️' : '▶️'}
            </button>
            
            <button onClick={handleMuteToggle} style={styles.button}>
              {isMuted ? '🔇' : '🔊'}
            </button>
            
            <input
              type="range"
              min="0"
              max="1"
              step="0.01"
              value={volume}
              onChange={handleVolumeChange}
              style={styles.volumeSlider}
            />
            <span style={styles.volumeLabel}>{Math.round(volume * 100)}%</span>
          </>
        )}
      </div>

      {showStats && stats && (
        <div style={styles.stats}>
          <h4>Statistics</h4>
          <div style={styles.statGrid}>
            <div style={styles.statItem}>
              <span style={styles.statLabel}>Packets Received:</span>
              <span style={styles.statValue}>{stats.packetsReceived}</span>
            </div>
            <div style={styles.statItem}>
              <span style={styles.statLabel}>Packets Lost:</span>
              <span style={styles.statValue}>{stats.packetsLost}</span>
            </div>
            <div style={styles.statItem}>
              <span style={styles.statLabel}>Bytes Received:</span>
              <span style={styles.statValue}>{(stats.bytesReceived / 1024).toFixed(2)} KB</span>
            </div>
            <div style={styles.statItem}>
              <span style={styles.statLabel}>Jitter:</span>
              <span style={styles.statValue}>{stats.jitter.toFixed(3)} ms</span>
            </div>
            <div style={styles.statItem}>
              <span style={styles.statLabel}>RTT:</span>
              <span style={styles.statValue}>{(stats.roundTripTime * 1000).toFixed(1)} ms</span>
            </div>
            <div style={styles.statItem}>
              <span style={styles.statLabel}>Audio Level:</span>
              <span style={styles.statValue}>{(stats.audioLevel * 100).toFixed(1)}%</span>
            </div>
          </div>
        </div>
      )}

      <audio
        ref={audioRef}
        style={{ display: 'none' }}
        autoPlay={autoPlay}
      />
    </div>
  );
};

// Styles
const styles: { [key: string]: React.CSSProperties } = {
  container: {
    padding: '16px',
    border: '1px solid #e0e0e0',
    borderRadius: '8px',
    backgroundColor: '#fff',
    marginBottom: '16px',
    fontFamily: 'Arial, sans-serif',
  },
  header: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: '12px',
  },
  title: {
    margin: 0,
    fontSize: '18px',
    fontWeight: 'bold',
  },
  statusIndicator: {
    width: '12px',
    height: '12px',
    borderRadius: '50%',
  },
  status: {
    marginBottom: '12px',
    fontSize: '14px',
    color: '#666',
  },
  error: {
    marginTop: '8px',
    padding: '8px',
    backgroundColor: '#ffebee',
    borderRadius: '4px',
    color: '#c62828',
    fontSize: '12px',
  },
  suggestion: {
    fontStyle: 'italic',
  },
  controls: {
    display: 'flex',
    alignItems: 'center',
    gap: '8px',
    marginBottom: '12px',
  },
  button: {
    padding: '8px 16px',
    border: 'none',
    borderRadius: '4px',
    cursor: 'pointer',
    fontSize: '14px',
    fontWeight: 'bold',
    transition: 'background-color 0.3s',
  },
  connectButton: {
    backgroundColor: '#4caf50',
    color: 'white',
  },
  disconnectButton: {
    backgroundColor: '#f44336',
    color: 'white',
  },
  volumeSlider: {
    width: '100px',
  },
  volumeLabel: {
    fontSize: '12px',
    color: '#666',
  },
  stats: {
    marginTop: '16px',
    padding: '12px',
    backgroundColor: '#f5f5f5',
    borderRadius: '4px',
  },
  statGrid: {
    display: 'grid',
    gridTemplateColumns: 'repeat(2, 1fr)',
    gap: '8px',
    marginTop: '8px',
  },
  statItem: {
    display: 'flex',
    justifyContent: 'space-between',
    fontSize: '12px',
  },
  statLabel: {
    color: '#666',
  },
  statValue: {
    fontWeight: 'bold',
  },
};

/**
 * Component to render multiple audio players
 */
interface AudioPlayerListProps {
  sinkIds: string[];
  showStats?: boolean;
  showControls?: boolean;
}

export const EnhancedAudioPlayerList: React.FC<AudioPlayerListProps> = ({
  sinkIds,
  showStats = false,
  showControls = false,
}) => {
  return (
    <div>
      {sinkIds.map((sinkId) => (
        <EnhancedAudioPlayer
          key={sinkId}
          sinkId={sinkId}
          showStats={showStats}
          showControls={showControls}
        />
      ))}
    </div>
  );
};

export default EnhancedAudioPlayer;