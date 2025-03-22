/**
 * React component for rendering a volume slider that allows users to adjust the volume level.
 * Uses Chakra UI components for consistent styling.
 */
import React, { useState } from 'react';
import {
  Box,
  Flex,
  Text,
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
 * React functional component for rendering a volume slider using Chakra UI.
 *
 * This component provides a user interface element that allows users to adjust
 * the volume level by sliding a control. The current volume is displayed as a percentage
 * next to the slider.
 *
 * @param {VolumeSliderProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const VolumeSlider: React.FC<VolumeSliderProps> = ({ value, onChange }) => {
  const [showTooltip, setShowTooltip] = useState(false);
  
  // Color values for light/dark mode
  const trackBg = useColorModeValue('gray.100', 'gray.700');
  const filledTrackBg = useColorModeValue('green.500', 'green.300');
  const thumbBg = useColorModeValue('white', 'gray.200');
  const textColor = useColorModeValue('gray.700', 'gray.200');
  
  return (
    <Box width="100%" maxWidth="300px">
      <Flex direction="column" width="100%">
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
          mr={3}
          flex="1"
        >
          <SliderTrack bg={trackBg}>
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
            <SliderThumb boxSize={6} bg={thumbBg}>
              <Icon as={ChevronUpIcon} color="green.500" />
            </SliderThumb>
          </Tooltip>
        </Slider>
        <Text fontSize="sm" width="40px" textAlign="right" color={textColor}>
          {Math.round(value * 100)}%
        </Text>
      </Flex>
    </Box>
  );
};

export default VolumeSlider;
