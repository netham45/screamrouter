/**
 * React component for rendering a volume slider that allows users to adjust the volume level.
 */
import React from 'react';
import { VolumeSlider as CommonVolumeSlider } from '../../utils/commonUtils';

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
 * React functional component for rendering a volume slider.
 *
 * This component provides a user interface element that allows users to adjust
 * the volume level by sliding a control. The current volume is displayed as a percentage
 * next to the slider.
 *
 * @param {VolumeSliderProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const VolumeSlider: React.FC<VolumeSliderProps> = ({ value, onChange }) => (
  <div>
    <CommonVolumeSlider
      value={value}
      onChange={(newValue: number) => onChange(newValue)}
    />
    <span>{(value * 100).toFixed(0)}%</span>
  </div>
);

export default VolumeSlider;
