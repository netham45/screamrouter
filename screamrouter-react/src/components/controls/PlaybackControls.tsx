import React from 'react';
import ActionButton from './ActionButton';

interface PlaybackControlsProps {
  onPrevTrack: () => void;
  onPlay: () => void;
  onNextTrack: () => void;
}

const PlaybackControls: React.FC<PlaybackControlsProps> = ({ onPrevTrack, onPlay, onNextTrack }) => (
  <>
    <ActionButton onClick={onPrevTrack}>⏮</ActionButton>
    <ActionButton onClick={onPlay}>⏯</ActionButton>
    <ActionButton onClick={onNextTrack}>⏭</ActionButton>
  </>
);

export default PlaybackControls;
