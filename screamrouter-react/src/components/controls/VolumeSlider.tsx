import React from 'react';
import { VolumeSlider as CommonVolumeSlider } from '../../utils/commonUtils';

interface VolumeSliderProps {
  value: number;
  onChange: (value: number) => void;
}

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
