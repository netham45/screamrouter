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
  SliderThumb
} from '@chakra-ui/react';
import { useColorContext } from '../context/ColorContext';

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
  enabled?: boolean;
}

/**
 * A compact volume control component optimized for the DesktopMenu interface.
 */
const VolumeControl: React.FC<VolumeControlProps> = ({
  value,
  onChange,
  maxWidth="100px",
  enabled
}) => {
  // Get colors from context
  const { getDarkerColor, getLighterColor, } = useColorContext();
  
  // Generate dynamic colors - use base color directly for high visibility
  const trackBg = enabled ? getLighterColor(2) : getDarkerColor(.2);
  const filledTrackBg = enabled ? getLighterColor(5) : getDarkerColor(.4);
  const thumbBg = enabled ? "white" : getLighterColor(3);
  
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
        isDisabled={!enabled}
        style={{ width: "100%" }}
      >
        <SliderTrack bg={trackBg} height="6px" borderRadius="3px">
          <SliderFilledTrack 
            style={{ 
              backgroundColor: filledTrackBg,
              opacity: enabled ? 1 : 0.5,
              height: "6px"
            }} 
          />
        </SliderTrack>
        <SliderThumb 
          boxSize={4} 
          style={{ 
            backgroundColor: thumbBg,
            opacity: enabled ? 1 : 0.5,
            border: enabled ? `2px solid ${thumbBg}` : 'none'
          }}
        />
      </Slider>
    </Box>
  );
};

export default VolumeControl;
