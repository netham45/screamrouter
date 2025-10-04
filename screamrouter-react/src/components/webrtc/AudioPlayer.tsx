import React, { useEffect, useRef } from 'react';
import { useWebRTC } from '../../context/WebRTCContext';

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
        } catch (error) {
          // Autoplay can be blocked by the browser. This is the required exception handler.
          console.error("Audio play failed, triggering onPlaybackError:", error);
          onPlaybackError(sinkId, error as Error);
        }
      }
    };

    playAudio();
  }, [stream, sinkId, onPlaybackError]);

  // The audio element is rendered but not visible to the user.
  return <audio ref={audioRef} autoPlay={true} controls={true} muted={false} style={{"display":"none"}} />;
};

export const WebRTCAudioPlayers: React.FC = () => {
  // This is a placeholder, we need to get the streams from the context.
  // Let's assume AppContext provides a map of streams.
  const { audioStreams, playbackError } = useWebRTC();

  const handlePlaybackError = (sinkId: string, error: Error) => {
    // The playbackError is now handled in the WebRTCContext
    console.error(`Playback error for sink ${sinkId}:`, error);
  };

  return (
    <div>
      {Array.from(audioStreams.entries()).map(([sinkId, stream]) => (
        <AudioPlayer key={sinkId} sinkId={sinkId} stream={stream} onPlaybackError={handlePlaybackError} />
      ))}
    </div>
  );
};