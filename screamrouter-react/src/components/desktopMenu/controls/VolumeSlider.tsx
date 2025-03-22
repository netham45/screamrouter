/**
 * React component for rendering a compact volume slider for the DesktopMenu.
 * Optimized for limited space in the slide-out panel.
 */
import React, { useState } from 'react';
import {
  Box,
  Flex,
  Slider,
  SliderTrack,
  SliderFilledTrack,
  SliderThumb,
  Tooltip,
  useColorModeValue,
  Icon
} from '@chakra-ui/react';
import { ChevronUpIcon } from '@chakra-ui/icons';

/**
 * Interface defining the props for the VolumeSlider component.
 */
interface VolumeSliderProps {
  /**
   * The current value of the volume slider (0.0 to 1.0).
   */
  value: number;
  /**
   * Callback function to handle changes in the volume value.
   */
  onChange: (value: number) => void;
}

/**
 * A compact volume slider component optimized for the DesktopMenu interface.
 */
const VolumeSlider: React.FC<VolumeSliderProps> = ({ value, onChange }) => {
  const [showTooltip, setShowTooltip] = useState(false);
  
  // Color values for light/dark mode
  const trackBg = useColorModeValue('gray.100', 'gray.700');
  const filledTrackBg = useColorModeValue('green.500', 'green.300');
  const thumbBg = useColorModeValue('white', 'gray.200');
  
  return (
    <Box width="100%">
      <Flex direction="row" width="100%" align="center">
        <Slider
          id="volume-slider"
          aria-label="Volume slider"
          min={0}
          max={1}
          step={0.01}
          value={value}
          onChange={onChange}
          onMouseEnter={() => setShowTooltip(true)}
          onMouseLeave={() => setShowTooltip(false)}
          size="sm"
          flex="1"
        >
          <SliderTrack bg={trackBg} height="2px">
            <SliderFilledTrack bg={filledTrackBg} />
          </SliderTrack>
          <Tooltip
            hasArrow
            bg="green.500"
            color="white"
            placement="top"
            isOpen={showTooltip}
            label={`${Math.round(value * 100)}%`}
          >
            <SliderThumb boxSize={4} bg={thumbBg}>
              <Icon as={ChevronUpIcon} color="green.500" boxSize={2} />
            </SliderThumb>
          </Tooltip>
        </Slider>
      </Flex>
    </Box>
  );
};

export default VolumeSlider;
