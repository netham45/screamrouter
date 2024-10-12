import React, { useEffect, useRef } from 'react';
import '../styles/AudioVisualizer.css';

interface AudioVisualizerProps {
  listeningToSink: string | null;
  visualizingSink: string | null;
  sinkIp: string | null;
}

const AudioVisualizer: React.FC<AudioVisualizerProps> = ({ listeningToSink, visualizingSink, sinkIp }) => {
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
    <div className="audio-visualizer">
      <canvas ref={canvasRef} id="canvas"></canvas>
    </div>
  );
};

export default AudioVisualizer;