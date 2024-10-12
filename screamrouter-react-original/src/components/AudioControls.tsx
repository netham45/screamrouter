import React, { useEffect, useRef } from 'react';
import '../styles/AudioVisualizer.css';

interface AudioControlsProps {
  listeningToSink: string | null;
  visualizingSink: string | null;
  sinkIp: string | null;
}

const AudioControls: React.FC<AudioControlsProps> = ({ listeningToSink, visualizingSink, sinkIp }) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (canvas) {
      canvas.width = window.innerWidth;
      canvas.height = window.innerHeight;

      window.canvasClick = () => {
        // Handle canvas click
      };

      window.canvasOnKeyDown = (event: KeyboardEvent) => {
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