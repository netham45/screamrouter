/**
 * React component for rendering a timeshift slider that allows users to adjust the timeshift value.
 */
import React, { useState, useCallback, useRef, useEffect } from 'react';

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
 * React functional component for rendering a timeshift slider.
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
   * Ref to store the timeout ID for debouncing the onChange callback.
   */
  const timeoutRef = useRef<NodeJS.Timeout | null>(null);

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
   * Handler function for changes in the input value.
   *
   * @param {React.ChangeEvent<HTMLInputElement>} e - The change event object.
   */
  const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const newValue = -parseFloat(e.target.value);
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
    const remainingSeconds = (absSeconds % 60).toFixed(1);
    return `${minutes}:${remainingSeconds.padStart(4, '0')}`;
  };

  return (
    <div className="timeshift-control">
      <label htmlFor="timeshift-slider"></label>
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
