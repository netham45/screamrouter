import React, { useState, useCallback, useRef, useEffect } from 'react';

interface TimeshiftSliderProps {
  value: number;
  onChange: (value: number) => void;
}

const TimeshiftSlider: React.FC<TimeshiftSliderProps> = ({ value, onChange }) => {
  const [localValue, setLocalValue] = useState(value);
  const timeoutRef = useRef<NodeJS.Timeout | null>(null);

  useEffect(() => {
    setLocalValue(value);
  }, [value]);

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

  const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const newValue = -parseFloat(e.target.value);
    setLocalValue(newValue);
    debouncedOnChange(newValue);
  };

  const formatTimeshift = (seconds: number) => {
    const absSeconds = Math.abs(seconds);
    const minutes = Math.floor(absSeconds / 60);
    const remainingSeconds = (absSeconds % 60).toFixed(1);
    return `${minutes}:${remainingSeconds.padStart(4, '0')}`;
  };

  return (
    <div className="timeshift-control">
      <label htmlFor="timeshift-slider">Timeshift:</label>
      <input
        id="timeshift-slider"
        type="range"
        min="-300"
        max="0"
        step="0.1"
        value={-localValue}
        onChange={handleChange}
      />
      <span>-{formatTimeshift(localValue)}</span>
    </div>
  );
};

export default TimeshiftSlider;
