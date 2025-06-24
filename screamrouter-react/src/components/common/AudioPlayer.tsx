import React, { useEffect, useRef } from 'react';

interface AudioPlayerProps {
  stream: MediaStream;
}

const AudioPlayer: React.FC<AudioPlayerProps> = ({ stream }) => {
  const audioRef = useRef<HTMLAudioElement>(null);

  const audioContextRef = useRef<AudioContext | null>(null);
  const sourceNodeRef = useRef<MediaStreamAudioSourceNode | null>(null);

  useEffect(() => {
    if (!stream || !audioRef.current) {
      return;
    }

    if (!audioContextRef.current) {
      try {
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        audioContextRef.current = new (window.AudioContext || (window as any).webkitAudioContext)();
      } catch (e) {
        console.error("Web Audio API is not supported in this browser", e);
        // Fallback to original behavior
        audioRef.current.srcObject = stream;
        return;
      }
    }
    const audioContext = audioContextRef.current;

    if (sourceNodeRef.current) {
      sourceNodeRef.current.disconnect();
    }

    const destination = audioContext.createMediaStreamDestination();
    const source = audioContext.createMediaStreamSource(stream);
    source.connect(destination);
    sourceNodeRef.current = source;

    audioRef.current.srcObject = destination.stream;
    audioRef.current.play().catch(e => console.error("AudioPlayer play failed", e));

    return () => {
      if (sourceNodeRef.current) {
        sourceNodeRef.current.disconnect();
        sourceNodeRef.current = null;
      }
    };
  }, [stream]);

  return <audio ref={audioRef} autoPlay={true} controls={true} style={{"display":"none"}}/>;
};

export default AudioPlayer;