import React, { useState, useEffect, useRef } from 'react';
import ApiService, { Equalizer as EqualizerType } from '../api/api';
import '../styles/Equalizer.css';

interface EqualizerProps {
  item: {
    name: string;
    equalizer: EqualizerType;
  };
  type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source';
  onClose: () => void;
  onDataChange: () => void;
}

const defaultEqualizer: EqualizerType = {
  b1: 1, b2: 1, b3: 1, b4: 1, b5: 1, b6: 1, b7: 1, b8: 1, b9: 1,
  b10: 1, b11: 1, b12: 1, b13: 1, b14: 1, b15: 1, b16: 1, b17: 1, b18: 1
};

const musicPresets: { [key: string]: EqualizerType } = {
  'Flat': defaultEqualizer,
  'Classical': {
    b1: 1.2, b2: 1.2, b3: 1, b4: 1, b5: 1, b6: 1, b7: 0.9, b8: 0.9, b9: 0.9,
    b10: 0.8, b11: 0.8, b12: 0.8, b13: 0.8, b14: 0.8, b15: 0.8, b16: 0.8, b17: 0.8, b18: 0.8
  },
  'Rock': {
    b1: 1.2, b2: 1.1, b3: 0.8, b4: 0.9, b5: 1, b6: 1.2, b7: 1.4, b8: 1.4, b9: 1.4,
    b10: 1.3, b11: 1.2, b12: 1.1, b13: 1, b14: 1, b15: 1, b16: 1.1, b17: 1.2, b18: 1.2
  },
  'Pop': {
    b1: 0.8, b2: 0.9, b3: 1, b4: 1.1, b5: 1.2, b6: 1.2, b7: 1.1, b8: 1, b9: 1,
    b10: 1, b11: 1, b12: 1.1, b13: 1.2, b14: 1.2, b15: 1.1, b16: 1, b17: 0.9, b18: 0.8
  },
  'Jazz': {
    b1: 1.1, b2: 1.1, b3: 1, b4: 1, b5: 1, b6: 1.1, b7: 1.2, b8: 1.2, b9: 1.2,
    b10: 1.1, b11: 1, b12: 0.9, b13: 0.9, b14: 0.9, b15: 1, b16: 1.1, b17: 1.1, b18: 1.1
  },
  'Electronic': {
    b1: 1.4, b2: 1.3, b3: 1.2, b4: 1, b5: 0.8, b6: 1, b7: 1.2, b8: 1.3, b9: 1.4,
    b10: 1.4, b11: 1.3, b12: 1.2, b13: 1.1, b14: 1, b15: 1.1, b16: 1.2, b17: 1.3, b18: 1.4
  }
};

class BiquadFilter {
    a0: number; a1: number; a2: number;
    b0: number; b1: number; b2: number;
  
    constructor() {
      this.a0 = this.b0 = 1.0;
      this.a1 = this.a2 = this.b1 = this.b2 = 0.0;
    }
  
    setParams(freq: number, Q: number, peakGain: number, sampleRate: number) {
      const V = Math.pow(10, Math.abs(peakGain) / 20);
      const K = Math.tan(Math.PI * freq / sampleRate);
      
      if (peakGain >= 0) {    // boost
        const norm = 1 / (1 + 1/Q * K + K * K);
        this.b0 = (1 + V/Q * K + K * K) * norm;
        this.b1 = 2 * (K * K - 1) * norm;
        this.b2 = (1 - V/Q * K + K * K) * norm;
        this.a1 = this.b1;
        this.a2 = (1 - 1/Q * K + K * K) * norm;
      } else {    // cut
        const norm = 1 / (1 + V/Q * K + K * K);
        this.b0 = (1 + 1/Q * K + K * K) * norm;
        this.b1 = 2 * (K * K - 1) * norm;
        this.b2 = (1 - 1/Q * K + K * K) * norm;
        this.a1 = this.b1;
        this.a2 = (1 - V/Q * K + K * K) * norm;
      }
      this.a0 = 1.0;
    }
}

const Equalizer: React.FC<EqualizerProps> = ({ item, type, onClose, onDataChange }) => {
  const [equalizer, setEqualizer] = useState<EqualizerType>(item.equalizer);
  const [error, setError] = useState<string | null>(null);
  const [preset, setPreset] = useState<string>('Custom');
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    // Check if the current equalizer matches any preset when component mounts
    checkAndSetPreset(item.equalizer);
  }, []);

  const checkAndSetPreset = (eq: EqualizerType) => {
    // eslint-disable-next-line @typescript-eslint/no-unused-vars
    const matchingPreset = Object.entries(musicPresets).find(([_, presetEq]) => 
      Object.entries(presetEq).every(([band, value]) => Math.abs(eq[band as keyof EqualizerType] - value) < 0.01)
    );
    
    if (matchingPreset) {
      setPreset(matchingPreset[0]);
    } else {
      setPreset('Custom');
    }
  };

  const updateEqualizer = async () => {
    try {
      setError(null);
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
      setError('Failed to update equalizer. Please try again.');
    }
  };

  const handleChange = (band: keyof EqualizerType, value: number) => {
    setEqualizer(prev => {
      const newEqualizer = { ...prev, [band]: value };
      checkAndSetPreset(newEqualizer);
      return newEqualizer;
    });
  };

  const resetToDefault = () => {
    setEqualizer(defaultEqualizer);
    setPreset('Flat');
  };

  const handlePresetChange = (event: React.ChangeEvent<HTMLSelectElement>) => {
    const selectedPreset = event.target.value;
    setPreset(selectedPreset);
    if (selectedPreset !== 'Custom') {
      setEqualizer(musicPresets[selectedPreset]);
    }
  };

  const handleClose = () => {
    onClose();
    onDataChange(); // Trigger data reload when the equalizer window is closed
  };

  const sortedBands = Object.entries(equalizer).sort((a, b) => {
    const bandA = parseInt(a[0].slice(1));
    const bandB = parseInt(b[0].slice(1));
    return bandA - bandB;
  });

  const frequencies = [65.406392, 92.498606, 130.81278, 184.99721, 261.62557, 369.99442, 523.25113, 739.9884,
                       1046.5023, 1479.9768, 2093.0045, 2959.9536, 4186.0091, 5919.9072, 8372.0181, 11839.814,
                       16744.036, 20000.0];

  const calculateMagnitudeResponse = (freq: number, sampleRate: number): number => {
    let totalGain = 1;
    // eslint-disable-next-line @typescript-eslint/no-unused-vars
    sortedBands.forEach(([_, gain], index) => {
      const filter = new BiquadFilter();
      filter.setParams(frequencies[index], 1.41, 20 * Math.log10(Math.max(gain, 0.001)), sampleRate);

      const w = 2 * Math.PI * freq / sampleRate;
      const phi = Math.pow(Math.sin(w/2), 2);
      const b0 = filter.b0, b1 = filter.b1, b2 = filter.b2, a0 = filter.a0, a1 = filter.a1, a2 = filter.a2;

      const magnitude = Math.sqrt(
        Math.pow(b0 + b1 + b2, 2) - 4*(b0*b1 + 4*b0*b2 + b1*b2)*phi + 16*b0*b2*phi*phi
      ) / Math.sqrt(
        Math.pow(a0 + a1 + a2, 2) - 4*(a0*a1 + 4*a0*a2 + a1*a2)*phi + 16*a0*a2*phi*phi
      );

      totalGain *= magnitude;
    });

    return 20 * Math.log10(totalGain);
  };

  useEffect(() => {
    const canvas = canvasRef.current;
    if (canvas) {
      const ctx = canvas.getContext('2d');
      if (ctx) { 
        ctx.clearRect(0, 0, canvas.width, canvas.height);

        // Set canvas background to dark
        ctx.fillStyle = '#1e1e1e';
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        // Draw background grid
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
        ctx.lineWidth = 1;

        // Vertical grid lines (frequency)
        const freqLabels = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 24000];
        freqLabels.forEach(freq => {
          const x = (Math.log2(freq / 20)) / (Math.log2(24000 / 20)) * canvas.width;
          ctx.beginPath();
          ctx.moveTo(x, 0);
          ctx.lineTo(x, canvas.height - 20);
          ctx.stroke();

          // Frequency labels
          ctx.fillStyle = 'rgba(255, 255, 255, 0.7)';
          ctx.font = '10px Arial';
          ctx.textAlign = 'center';
          ctx.fillText(freq >= 1000 ? `${freq/1000}k` : freq.toString(), x, canvas.height - 5);
        });

        // Horizontal grid lines (dB)
        const dbLabels = [-12, -9, -6, -3, 0, 3, 6, 9, 12];
        const maxDb = 12;
        dbLabels.forEach(db => {
          const y = canvas.height / 2 - 10 - (db / maxDb) * ((canvas.height - 20) / 2);
          ctx.beginPath();
          ctx.moveTo(0, y);
          ctx.lineTo(canvas.width, y);
          ctx.stroke();

          // dB labels
          ctx.fillStyle = 'rgba(255, 255, 255, 0.7)';
          ctx.font = '10px Arial';
          ctx.textAlign = 'right';
          ctx.fillText(`${db} dB`, canvas.width - 5, y + 3);
        });

        // Draw equalizer response
        ctx.strokeStyle = '#2ecc71';
        ctx.lineWidth = 2;
        ctx.beginPath();

        const sampleRate = 48000;
        const points = 200;

        for (let i = 0; i < points; i++) {
          const freq = Math.exp(Math.log(20) + (Math.log(24000) - Math.log(20)) * (i / (points - 1)));
          const magnitude = calculateMagnitudeResponse(freq, sampleRate);
          
          const x = (Math.log2(freq / 20)) / (Math.log2(24000 / 20)) * canvas.width;
          const y = canvas.height / 2 - 10 - (magnitude / maxDb) * ((canvas.height - 20) / 2);

          if (i === 0) {
            ctx.moveTo(x, y);
          } else {
            ctx.lineTo(x, y);
          }
        }
        ctx.stroke();

        // X-axis label
        ctx.fillStyle = 'rgba(255, 255, 255, 0.9)';
        ctx.font = '12px Arial';
        ctx.textAlign = 'center';
        ctx.fillText('Frequency (Hz)', canvas.width / 2, canvas.height - 20);

        // Y-axis label
        ctx.save();
        ctx.translate(10, canvas.height / 2);
        ctx.rotate(-Math.PI / 2);
        ctx.textAlign = 'center';
        ctx.fillText('Gain (dB)', 0, 0);
        ctx.restore();
      }
    }
  }, [equalizer]);

  return (
    <div className="equalizer-container">
      <h3 className="equalizer-title">
        <span className="music-note">♪</span> Equalizer: {item.name} <span className="music-note">♪</span>
      </h3>
      {error && <div className="error-message">{error}</div>}
      <div className="equalizer-graph">
        <canvas ref={canvasRef} width="600" height="310" style={{border: '1px solid #444'}}></canvas>
      </div>
      <div className="equalizer-controls">
        <div className="preset-selector">
          <label htmlFor="preset-select">Preset: </label>
          <select id="preset-select" value={preset} onChange={handlePresetChange} className="preset-select">
            <option value="Custom">Custom</option>
            {Object.keys(musicPresets).map(presetName => (
              <option key={presetName} value={presetName}>{presetName}</option>
            ))}
          </select>
        </div>
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
                <label htmlFor={band}>{frequencies[index].toFixed(0)}</label>
              </div>
            </div>
          ))}
        </div>
        <div className="hz-label">Hz</div>
      </div>
      <div className="equalizer-buttons">
        <button className="apply-button" onClick={updateEqualizer}>Apply</button>
        <button className="default-button" onClick={resetToDefault}>Default</button>
        <button className="close-button" onClick={handleClose}>Close</button>
      </div>
    </div>
  );
};

export default Equalizer;
