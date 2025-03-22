/**
 * React component for rendering playback controls including previous track, play/pause, and next track buttons.
 * Uses Chakra UI components for consistent styling.
 */
import React from 'react';
import { ButtonGroup, Icon } from '@chakra-ui/react';
import { ChevronLeftIcon, ChevronRightIcon } from '@chakra-ui/icons';
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
  /**
   * Optional boolean indicating if playback is currently active.
   */
  isPlaying?: boolean;
}

/**
 * React functional component for rendering playback controls using Chakra UI.
 *
 * @param {PlaybackControlsProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const PlaybackControls: React.FC<PlaybackControlsProps> = ({
  onPrevTrack,
  onPlay,
  onNextTrack,
  isPlaying = false
}) => (
  <ButtonGroup spacing={2} isAttached variant="outline">
    <ActionButton
      onClick={onPrevTrack}
      aria-label="Previous track"
    >
      <Icon as={ChevronLeftIcon} boxSize={5} />
    </ActionButton>
    
    <ActionButton
      onClick={onPlay}
      aria-label={isPlaying ? "Pause" : "Play"}
      colorScheme={isPlaying ? "green" : "blue"}
    >
      {isPlaying ? "⏸" : "▶"}
    </ActionButton>
    
    <ActionButton
      onClick={onNextTrack}
      aria-label="Next track"
    >
      <Icon as={ChevronRightIcon} boxSize={5} />
    </ActionButton>
  </ButtonGroup>
);

export default PlaybackControls;
