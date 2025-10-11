import React, { useEffect, useRef } from 'react';
import { useAppContext } from '../../context/AppContext';

const AudioPlayer: React.FC<{
  stream: MediaStream,
  sinkId: string,
  onPlaybackError: (sinkId: string, error: Error) => void
}> = ({ stream, sinkId, onPlaybackError }) => {
  const audioRef = useRef<HTMLAudioElement>(null);

  useEffect(() => {
    const playAudio = async () => {
      if (audioRef.current && stream) {
        if (audioRef.current.srcObject !== stream) {
          audioRef.current.srcObject = stream;
        }
        try {
          await audioRef.current.play();
          console.log(`[AudioPlayer] Successfully playing audio for ${sinkId}`);
        } catch (error) {
          // Autoplay can be blocked by the browser. This is the required exception handler.
          console.error(`[AudioPlayer] Audio play failed for ${sinkId}:`, error);
          onPlaybackError(sinkId, error as Error);
        }
      }
    };

    playAudio();
  }, [stream, sinkId, onPlaybackError]);

  // The audio element is rendered but not visible to the user.
  return <audio ref={audioRef} autoPlay={true} controls={true} playsInline style={{"display":"none"}} />;
};

export const WebRTCAudioPlayers: React.FC = () => {
  // FIX: Use useAppContext instead of useWebRTC to get the actual streams
  const { audioStreams, onPlaybackError } = useAppContext();

  const handlePlaybackError = (sinkId: string, error: Error) => {
    console.error(`[WebRTCAudioPlayers] Playback error for sink ${sinkId}:`, error);
    onPlaybackError(sinkId, error);
  };

  return (
    <div>
      {Array.from(audioStreams.entries()).map(([sinkId, stream]) => (
        <AudioPlayer key={sinkId} sinkId={sinkId} stream={stream} onPlaybackError={handlePlaybackError} />
      ))}
    </div>
  );
};