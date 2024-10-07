import React, { useState } from 'react';
import ApiService, { Equalizer as EqualizerType } from '../api/api';
import '../styles/Equalizer.css';

interface EqualizerProps {
  item: {
    name: string;
    equalizer: EqualizerType;
  };
  type: 'sources' | 'sinks' | 'routes';
  onClose: () => void;
}

const defaultEqualizer: EqualizerType = {
  b1: 1, b2: 1, b3: 1, b4: 1, b5: 1, b6: 1, b7: 1, b8: 1, b9: 1,
  b10: 1, b11: 1, b12: 1, b13: 1, b14: 1, b15: 1, b16: 1, b17: 1, b18: 1
};

const Equalizer: React.FC<EqualizerProps> = ({ item, type, onClose }) => {
  const [equalizer, setEqualizer] = useState<EqualizerType>(item.equalizer);

  const updateEqualizer = async () => {
    try {
      switch (type) {
        case 'sources':
          await ApiService.updateSourceEqualizer(item.name, equalizer);
          break;
        case 'sinks':
          await ApiService.updateSinkEqualizer(item.name, equalizer);
          break;
        case 'routes':
          await ApiService.updateRouteEqualizer(item.name, equalizer);
          break;
      }
    } catch (error) {
      console.error('Error updating equalizer:', error);
    }
  };

  const handleChange = (band: keyof EqualizerType, value: number) => {
    setEqualizer(prev => ({ ...prev, [band]: value }));
  };

  const resetToDefault = () => {
    setEqualizer(defaultEqualizer);
  };

  const sortedBands = Object.entries(equalizer).sort((a, b) => {
    const bandA = parseInt(a[0].slice(1));
    const bandB = parseInt(b[0].slice(1));
    return bandA - bandB;
  });

  const frequencies = [65, 92, 131, 185, 262, 370, 523, 740, 1047, 1480, 2093, 2960, 4186, 5920, 8372, 11840, 16744, 20000];

  return (
    <div className="equalizer-container">
      <h3 className="equalizer-title">Equalizer: {item.name}</h3>
      <div className="equalizer-sliders">
        {sortedBands.map(([band, value], index) => (
          <div key={band} className="equalizer-band">
            <input
              type="range"
              id={band}
              min="0"
              max="2"
              step="0.1"
              value={value}
              onChange={(e) => handleChange(band as keyof EqualizerType, parseFloat(e.target.value))}
              className="vertical-slider"
            />
            <div className="slider-info">
              <label htmlFor={band}>{frequencies[index]} Hz</label>
              <span className="slider-value">{value.toFixed(1)}</span>
            </div>
          </div>
        ))}
      </div>
      <div className="equalizer-buttons">
        <button className="apply-button" onClick={updateEqualizer}>Apply</button>
        <button className="default-button" onClick={resetToDefault}>Default</button>
        <button className="close-button" onClick={onClose}>Close</button>
      </div>
    </div>
  );
};

export default Equalizer;