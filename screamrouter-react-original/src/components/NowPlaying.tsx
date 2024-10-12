import React from 'react';
import { Sink } from '../api/api';
import { ActionButton } from '../utils/commonUtils';

interface NowPlayingProps {
  listeningToSink: Sink | null;
  onStopListening: () => void;
}

const NowPlaying: React.FC<NowPlayingProps> = ({ listeningToSink, onStopListening }) => {
  if (!listeningToSink) {
    return (
      <div className="now-playing">
        <h3>Now Listening</h3>
        <p>Not listening to any sink</p>
      </div>
    );
  }

  return (
    <div className="now-playing">
      <h3>Now Listening</h3>
      <div className="now-playing-content">
        <span>{listeningToSink.name}</span>
        <ActionButton onClick={onStopListening}>
          Stop Listening
        </ActionButton>
      </div>
    </div>
  );
};

export default NowPlaying;