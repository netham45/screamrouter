/**
 * React component for rendering a timeshift slider that allows users to adjust the timeshift value.
 * Uses Chakra UI components for consistent styling.
 */
import React, { useState, useCallback, useRef, useEffect } from 'react';
import {
  Box,
  Flex,
  Text,
  Slider,
  SliderTrack,
  SliderFilledTrack,
  SliderThumb,
  Tooltip,
  useColorModeValue
} from '@chakra-ui/react';

/**
 * Interface defining the props for the TimeshiftSlider component.
 */
interface TimeshiftSliderProps {
  /**
   * The current value of the timeshift slider.
   */
  value: number;
  /**
   * Callback function to handle changes in the timeshift value.
   */
  onChange: (value: number) => void;
}

/**
 * React functional component for rendering a timeshift slider using Chakra UI.
 *
 * @param {TimeshiftSliderProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const TimeshiftSlider: React.FC<TimeshiftSliderProps> = ({ value, onChange }) => {
  /**
   * State variable to store the local value of the timeshift slider.
   */
  const [localValue, setLocalValue] = useState(value);
  /**
   * State variable to control the visibility of the tooltip.
   */
  const [showTooltip, setShowTooltip] = useState(false);
  /**
   * Ref to store the timeout ID for debouncing the onChange callback.
   */
  const timeoutRef = useRef<NodeJS.Timeout | null>(null);
  
  // Color values for light/dark mode
  const trackBg = useColorModeValue('gray.100', 'gray.700');
  const filledTrackBg = useColorModeValue('blue.500', 'blue.300');
  const thumbBg = useColorModeValue('white', 'gray.200');
  const textColor = useColorModeValue('gray.700', 'gray.200');

  /**
   * Effect hook to update the local value when the external value changes.
   */
  useEffect(() => {
    setLocalValue(value);
  }, [value]);

  /**
   * Debounced version of the onChange callback to prevent frequent updates.
   *
   * @param {number} newValue - The new timeshift value.
   */
  const debouncedOnChange = useCallback(
    (newValue: number) => {
      if (timeoutRef.current) {
        clearTimeout(timeoutRef.current);
      }
      timeoutRef.current = setTimeout(() => {
        onChange(newValue);
      }, 300);
    },
    [onChange]
  );

  /**
   * Handler function for changes in the slider value.
   *
   * @param {number} val - The new slider value.
   */
  const handleChange = (val: number) => {
    const newValue = -val;
    setLocalValue(newValue);
    debouncedOnChange(newValue);
  };

  /**
   * Function to format the timeshift value into a human-readable string.
   *
   * @param {number} seconds - The timeshift value in seconds.
   * @returns {string} The formatted timeshift string.
   */
  const formatTimeshift = (seconds: number) => {
    const absSeconds = Math.abs(seconds);
    const minutes = Math.floor(absSeconds / 60);
    const remainingSeconds = Math.round(absSeconds % 60);
    return `${minutes}:${remainingSeconds.toString().padStart(2, '0')}`;
  };

  return (
    <Box width="100%" maxWidth="300px">
      <Flex direction="column" width="100%">
        <Slider
          id="timeshift-slider"
          aria-label="Timeshift slider"
          min={-300}
          max={0}
          step={0.1}
          value={-localValue}
          onChange={handleChange}
          onMouseEnter={() => setShowTooltip(true)}
          onMouseLeave={() => setShowTooltip(false)}
        >
          <SliderTrack bg={trackBg}>
            <SliderFilledTrack bg={filledTrackBg} />
          </SliderTrack>
          <Tooltip
            hasArrow
            bg="blue.500"
            color="white"
            placement="top"
            isOpen={showTooltip}
            label={`-${formatTimeshift(localValue)}`}
          >
            <SliderThumb bg={thumbBg} />
          </Tooltip>
        </Slider>
        <Text mt={2} fontSize="sm" color={textColor} alignSelf="flex-end">
          -{formatTimeshift(localValue)}
        </Text>
      </Flex>
    </Box>
  );
};

export default TimeshiftSlider;