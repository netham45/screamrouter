/**
 * Simplified timeshift control component for DesktopMenu.
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

interface TimeshiftControlProps {
  /**
   * Current timeshift value (in seconds)
   */
  value: number;
  
  /**
   * Function to call when timeshift changes
   */
  onChange: (value: number) => void;
  
  /**
   * Maximum timeshift value (in seconds)
   */
  max?: number;
}

/**
 * A compact timeshift control component optimized for the DesktopMenu interface.
 */
const TimeshiftControl: React.FC<TimeshiftControlProps> = ({
  value,
  onChange,
  max = 60
}) => {
  // Color values for light/dark mode
  const trackBg = useColorModeValue('gray.100', 'gray.700');
  const filledTrackBg = useColorModeValue('blue.500', 'blue.300');
  const thumbBg = useColorModeValue('white', 'gray.200');
  
  return (
    <Box width="100%" maxWidth="100px">
      <Slider
        aria-label="Timeshift control"
        min={0}
        max={max}
        step={1}
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

export default TimeshiftControl;