/**
 * Simplified volume control component for DesktopMenu.
 * Optimized for compact display in the slide-out panel.
 */
import React from 'react';
import {
  Box,
  Slider,
  SliderTrack,
  SliderFilledTrack,
  SliderThumb,
  useColorModeValue
} from '@chakra-ui/react';

interface VolumeControlProps {
  /**
   * Current volume value (0-1)
   */
  value: number;
  
  /**
   * Function to call when volume changes
   */
  onChange: (value: number) => void;
  maxWidth?: string;
}

/**
 * A compact volume control component optimized for the DesktopMenu interface.
 */
const VolumeControl: React.FC<VolumeControlProps> = ({
  value,
  onChange,
  maxWidth="100px"
}) => {
  // Color values for light/dark mode
  const trackBg = useColorModeValue('gray.100', 'gray.700');
  const filledTrackBg = useColorModeValue('green.500', 'green.300');
  const thumbBg = useColorModeValue('white', 'gray.200');
  
  return (
    <Box width="100%" maxWidth={maxWidth}>
      <Slider
        aria-label="Volume control"
        min={0}
        max={1}
        step={0.01}
        value={value}
        onChange={onChange}
        size="sm"
      >
        <SliderTrack bg={trackBg} height="4px">
          <SliderFilledTrack bg={filledTrackBg} />
        </SliderTrack>
        <SliderThumb boxSize={3} bg={thumbBg} />
      </Slider>
    </Box>
  );
};

export default VolumeControl;