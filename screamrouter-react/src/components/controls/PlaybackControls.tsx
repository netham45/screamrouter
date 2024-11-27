/**
 * React component for rendering playback controls including previous track, play/pause, and next track buttons.
 */
import React from 'react';
import ActionButton from './ActionButton';

/**
 * Interface defining the props for the PlaybackControls component.
 */
interface PlaybackControlsProps {
  /**
   * Callback function to handle playing the previous track.
   */
  onPrevTrack: () => void;
  /**
   * Callback function to handle playing or pausing the current track.
   */
  onPlay: () => void;
  /**
   * Callback function to handle playing the next track.
   */
  onNextTrack: () => void;
}

/**
 * React functional component for rendering playback controls.
 *
 * @param {PlaybackControlsProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const PlaybackControls: React.FC<PlaybackControlsProps> = ({ onPrevTrack, onPlay, onNextTrack }) => (
  <>
    <ActionButton onClick={onPrevTrack}>⏮</ActionButton>
    <ActionButton onClick={onPlay}>⏯</ActionButton>
    <ActionButton onClick={onNextTrack}>⏭</ActionButton>
  </>
);

export default PlaybackControls;
