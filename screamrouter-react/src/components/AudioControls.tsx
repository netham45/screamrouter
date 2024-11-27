/**
 * React component for controlling audio visualization.
 * This component renders a canvas element and sets up event listeners for window resizing,
 * canvas clicks, and keydown events. It also manages the start and stop of an audio visualizer
 * based on the provided sink IP address.
 *
 * @param {AudioControlsProps} props - The properties for the component.
 * @param {string | null} props.sinkIp - The IP address of the sink to visualize audio from.
 */
import React, { useEffect, useRef } from 'react';

interface AudioControlsProps {
  sinkIp: string | null;
}

const AudioControls: React.FC<AudioControlsProps> = ({ sinkIp }) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  /**
   * Sets up the canvas dimensions and event listeners when the component mounts.
   * Adjusts the canvas size on window resize, handles clicks on the canvas,
   * and listens for keydown events.
   */
  useEffect(() => {
    const canvas = canvasRef.current;
    if (canvas) {
      canvas.width = window.innerWidth;
      canvas.height = window.innerHeight;

      window.canvasClick = () => {
        // Handle canvas click
      };

      window.canvasOnKeyDown = () => {
        // Handle key down event
      };

      window.addEventListener('resize', () => {
        canvas.width = window.innerWidth;
        canvas.height = window.innerHeight;
      });

      canvas.addEventListener('click', window.canvasClick);
      document.addEventListener('keydown', window.canvasOnKeyDown);

      return () => {
        window.removeEventListener('resize', () => {
          canvas.width = window.innerWidth;
          canvas.height = window.innerHeight;
        });
        canvas.removeEventListener('click', window.canvasClick);
        document.removeEventListener('keydown', window.canvasOnKeyDown);
      };
    }
  }, []);

  /**
   * Starts or stops the audio visualizer based on whether a sink IP is provided.
   * Ensures that the visualizer is stopped when the component unmounts or when
   * the sink IP changes to null.
   */
  useEffect(() => {
    if (sinkIp) {
      window.startVisualizer(sinkIp);
    } else {
      window.stopVisualizer();
    }

    return () => {
      window.stopVisualizer();
    };
  }, [sinkIp]);

  return (
    <div className="audio-controls">
      <canvas ref={canvasRef} id="canvas"></canvas>
      {/* Add your audio control buttons here */}
    </div>
  );
};

export default AudioControls;
