import React, { useEffect, useRef } from 'react';

interface AudioVisualizerProps {
  visualizingSink: string | null;
  sinkIp: string | null;
}

const AudioVisualizer: React.FC<AudioVisualizerProps> = ({ visualizingSink, sinkIp }) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (canvas && visualizingSink) {
      canvas.width = window.innerWidth;
      canvas.height = window.innerHeight;

      window.canvasClick = () => {
        // Handle canvas click
      };

      window.canvasOnKeyDown = () => {
        // Handle key down event
      };

      const handleResize = () => {
        canvas.width = window.innerWidth;
        canvas.height = window.innerHeight;
      };

      window.addEventListener('resize', handleResize);
      canvas.addEventListener('click', window.canvasClick);
      document.addEventListener('keydown', window.canvasOnKeyDown);

      return () => {
        window.removeEventListener('resize', handleResize);
        canvas.removeEventListener('click', window.canvasClick);
        document.removeEventListener('keydown', window.canvasOnKeyDown);
      };
    }
  }, [visualizingSink]);

  useEffect(() => {
    if (sinkIp && visualizingSink) {
      window.startVisualizer(sinkIp);
    } else {
      window.stopVisualizer();
    }

    return () => {
      window.stopVisualizer();
    };
  }, [sinkIp, visualizingSink]);

  if (!visualizingSink) {
    return null;
  }

  return (
    <div className="audio-visualizer">
      <canvas ref={canvasRef} id="canvas"></canvas>
    </div>
  );
};

export default AudioVisualizer;
